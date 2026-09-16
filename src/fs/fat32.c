#include "fs/fat32.h"
#include "fs/blkdev.h"
#include "lib/string.h"

// Read-only FAT32. The single-threaded, cooperative kernel makes static scratch
// buffers safe: FS calls never overlap. Two buffers are kept distinct so a FAT
// lookup (fat_buf) does not clobber directory/file data mid-read (data_buf).

struct fat32_volume {
    uint32_t sec_per_clus;      // sectors per cluster
    uint32_t fat_start;         // LBA of the first FAT (== reserved sector count)
    uint32_t first_data_sector; // LBA of cluster 2
    uint32_t root_cluster;      // first cluster of the root directory
    bool mounted;
};

static struct fat32_volume vol;

static uint8_t fat_buf[BLK_SECTOR_SIZE];
static uint32_t fat_cached_sec = 0xFFFFFFFFu;  // LBA currently in fat_buf
static uint8_t data_buf[BLK_SECTOR_SIZE];

// --- small local helpers (freestanding: no libc) ---------------------------

static uint32_t rd16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int read_sector(uint32_t lba, uint8_t *buf) {
    return blk_read(lba, 1, buf);
}

// A data cluster number is valid if it names a real cluster and is not an
// end-of-chain / bad marker.
static bool cluster_valid(uint32_t c) {
    return c >= 2 && c < 0x0FFFFFF8u;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return vol.first_data_sector + (cluster - 2) * vol.sec_per_clus;
}

// Follow the FAT to the next cluster in a chain. Returns an end-of-chain marker
// (>= 0x0FFFFFF8) on the last cluster, or 0 on an I/O error (treated as end).
static uint32_t fat_next(uint32_t cluster) {
    uint32_t fat_off = cluster * 4;
    uint32_t sec = vol.fat_start + fat_off / BLK_SECTOR_SIZE;
    uint32_t off = fat_off % BLK_SECTOR_SIZE;
    if (fat_cached_sec != sec) {
        if (read_sector(sec, fat_buf) != 0) {
            fat_cached_sec = 0xFFFFFFFFu;
            return 0;
        }
        fat_cached_sec = sec;
    }
    return rd32(fat_buf + off) & 0x0FFFFFFFu;
}

// Read up to `len` bytes at byte offset `pos` along the cluster chain starting
// at `first_cluster`. Returns bytes read -- fewer than `len` only at end of the
// chain -- or a negative FS_ERR_* code.
static long chain_read(uint32_t first_cluster, uint32_t pos, void *buf,
                       uint32_t len) {
    if (len == 0) {
        return 0;
    }
    uint32_t cluster_bytes = vol.sec_per_clus * BLK_SECTOR_SIZE;
    uint32_t cluster = first_cluster;

    for (uint32_t skip = pos / cluster_bytes; skip > 0; skip--) {
        cluster = fat_next(cluster);
        if (!cluster_valid(cluster)) {
            return 0;  // pos is past the end of the chain
        }
    }
    uint32_t off = pos % cluster_bytes;

    uint8_t *out = buf;
    uint32_t done = 0;
    while (done < len) {
        if (!cluster_valid(cluster)) {
            break;  // reached end of chain
        }
        uint32_t lba = cluster_to_lba(cluster);
        while (off < cluster_bytes && done < len) {
            uint32_t si = off / BLK_SECTOR_SIZE;
            uint32_t so = off % BLK_SECTOR_SIZE;
            if (read_sector(lba + si, data_buf) != 0) {
                return FS_ERR_IO;
            }
            uint32_t chunk = BLK_SECTOR_SIZE - so;
            if (chunk > len - done) {
                chunk = len - done;
            }
            memcpy(out + done, data_buf + so, chunk);
            done += chunk;
            off += chunk;
        }
        if (off >= cluster_bytes) {
            cluster = fat_next(cluster);
            off = 0;
        }
    }
    return (long)done;
}

// --- long filename (VFAT) decoding -----------------------------------------

// An LFN holds at most 20 entries of 13 UTF-16 code units.
#define FAT32_LFN_MAX_UNITS 260

// Byte offsets of the 13 UTF-16 code units within a 32-byte LFN entry.
static const uint8_t lfn_off[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

// Copy one LFN entry's 13 UTF-16 code units into `units` at their sequenced
// position. Entries appear in reverse order (highest ordinal first); the entry
// carrying the 0x40 bit is physically first and its ordinal is the total entry
// count, so it fixes how many code units the name occupies. Returns that count
// (unchanged for the non-terminal entries). Checksums are not verified.
static uint32_t lfn_collect(const uint8_t *ent, uint16_t *units, uint32_t nunits) {
    uint32_t ord = ent[0] & 0x1F;
    if (ord == 0 || ord * 13 > FAT32_LFN_MAX_UNITS) {
        return nunits;  // malformed ordinal; ignore this entry
    }
    if (ent[0] & 0x40) {
        nunits = ord * 13;
    }
    uint32_t base = (ord - 1) * 13;
    for (uint32_t k = 0; k < 13; k++) {
        units[base + k] = (uint16_t)rd16(ent + lfn_off[k]);
    }
    return nunits;
}

// Decode `n` little-endian UTF-16 code units (possibly with surrogate pairs) to
// a NUL-terminated UTF-8 string in `out` (capacity `cap`, including the NUL).
// Stops at a 0x0000 unit (the LFN terminator) or when the buffer is full. Lone
// surrogates decode to U+FFFD.
static void utf16_to_utf8(const uint16_t *units, uint32_t n, char *out,
                          uint32_t cap) {
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t cp = units[i];
        if (cp == 0x0000) {
            break;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF) {  // high surrogate
            uint32_t lo = (i + 1 < n) ? units[i + 1] : 0;
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            } else {
                cp = 0xFFFD;  // unpaired high surrogate
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;  // unpaired low surrogate
        }

        uint32_t need = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (o + need + 1 > cap) {
            break;  // no room for this char plus the NUL
        }
        if (cp < 0x80) {
            out[o++] = (char)cp;
        } else if (cp < 0x800) {
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[o++] = (char)(0xF0 | (cp >> 18));
            out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[o] = 0;
}

// Map one 8.3 short-name byte: lower-case ASCII, and replace any non-ASCII byte
// with '?'. Short names are stored in an OEM code page, not Unicode; decoding
// that is out of scope, and a raw >= 0x80 byte would be invalid UTF-8. Names
// that actually need non-ASCII characters carry an LFN, which we decode fully.
static char short_byte(uint8_t b) {
    if (b >= 0x80) {
        return '?';
    }
    return to_lower((char)b);
}

// Build a name from an 8.3 short-directory entry: "NAME    EXT" -> "name.ext",
// spaces trimmed, lower-cased.
static void name_from_83(const uint8_t *ent, char *out) {
    int o = 0;
    for (int i = 0; i < 8; i++) {
        if (ent[i] == ' ') {
            break;
        }
        out[o++] = short_byte(ent[i]);
    }
    if (ent[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11; i++) {
            if (ent[i] == ' ') {
                break;
            }
            out[o++] = short_byte(ent[i]);
        }
    }
    out[o] = 0;
}

// --- public API -------------------------------------------------------------

int fat32_mount(void) {
    uint8_t sec[BLK_SECTOR_SIZE];
    if (read_sector(0, sec) != 0) {
        return FS_ERR_IO;
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        return FS_ERR_NOFS;
    }
    uint32_t bytes_per_sec = rd16(sec + 11);
    uint32_t spc = sec[13];
    uint32_t rsvd = rd16(sec + 14);
    uint32_t nfats = sec[16];
    uint32_t fatsz = rd32(sec + 36);   // FATSz32
    uint32_t root_clus = rd32(sec + 44);  // BPB_RootClus

    if (bytes_per_sec != BLK_SECTOR_SIZE || spc == 0 || rsvd == 0 ||
        nfats == 0 || fatsz == 0 || root_clus < 2) {
        return FS_ERR_NOFS;
    }

    vol.sec_per_clus = spc;
    vol.fat_start = rsvd;
    vol.first_data_sector = rsvd + nfats * fatsz;
    vol.root_cluster = root_clus;
    vol.mounted = true;
    fat_cached_sec = 0xFFFFFFFFu;
    return 0;
}

int fat32_readdir(fat32_file_t *dir, fat32_dirent_t *out) {
    if (!dir->is_dir) {
        return FS_ERR_NOTDIR;
    }
    uint8_t ent[32];
    uint16_t units[FAT32_LFN_MAX_UNITS];
    uint32_t nunits = 0;
    bool have_lfn = false;

    for (;;) {
        long n = chain_read(dir->first_cluster, dir->pos, ent, sizeof(ent));
        if (n < 0) {
            return (int)n;
        }
        if (n < (long)sizeof(ent)) {
            return 0;  // ran off the end of the directory's cluster chain
        }
        dir->pos += sizeof(ent);

        uint8_t first = ent[0];
        if (first == 0x00) {
            return 0;  // no more entries
        }
        if (first == 0xE5) {
            have_lfn = false;  // deleted
            nunits = 0;
            continue;
        }
        uint8_t attr = ent[11];
        if ((attr & 0x0F) == 0x0F) {
            nunits = lfn_collect(ent, units, nunits);
            have_lfn = true;
            continue;
        }
        if (attr & 0x08) {
            have_lfn = false;  // volume label
            nunits = 0;
            continue;
        }
        if (first == '.') {
            have_lfn = false;  // "." or ".."
            nunits = 0;
            continue;
        }

        if (have_lfn) {
            utf16_to_utf8(units, nunits, out->name, FAT32_NAME_MAX + 1);
        } else {
            name_from_83(ent, out->name);
        }
        out->first_cluster = (rd16(ent + 20) << 16) | rd16(ent + 26);
        out->size = rd32(ent + 28);
        out->is_dir = (attr & 0x10) != 0;
        return 1;
    }
}

// Case-insensitive compare of a NUL-terminated name against a length-delimited
// component.
static bool name_eq(const char *name, const char *comp, size_t comp_len) {
    size_t i = 0;
    for (; i < comp_len; i++) {
        if (name[i] == 0 || to_lower(name[i]) != to_lower(comp[i])) {
            return false;
        }
    }
    return name[i] == 0;
}

static int find_in_dir(uint32_t dir_cluster, const char *comp, size_t comp_len,
                       fat32_dirent_t *out) {
    fat32_file_t dir = {.first_cluster = dir_cluster, .size = 0, .pos = 0,
                        .is_dir = true};
    fat32_dirent_t de;
    int r;
    while ((r = fat32_readdir(&dir, &de)) == 1) {
        if (name_eq(de.name, comp, comp_len)) {
            memcpy(out, &de, sizeof(*out));
            return 0;
        }
    }
    return r < 0 ? r : FS_ERR_NOTFOUND;
}

// Resolve an absolute path to its directory entry. "/" resolves to a synthetic
// root-directory entry.
static int resolve(const char *path, fat32_dirent_t *out) {
    if (!vol.mounted) {
        return FS_ERR_NOFS;
    }
    if (path == NULL || path[0] != '/') {
        return FS_ERR_INVAL;
    }
    fat32_dirent_t cur = {.name = "/", .size = 0,
                          .first_cluster = vol.root_cluster, .is_dir = true};
    const char *p = path + 1;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len > 0) {
            if (!cur.is_dir) {
                return FS_ERR_NOTDIR;
            }
            fat32_dirent_t de;
            int r = find_in_dir(cur.first_cluster, start, len, &de);
            if (r != 0) {
                return r;
            }
            memcpy(&cur, &de, sizeof(cur));
        }
        if (*p == '/') {
            p++;
        }
    }
    memcpy(out, &cur, sizeof(*out));
    return 0;
}

int fat32_open(const char *path, fat32_file_t *out) {
    fat32_dirent_t de;
    int r = resolve(path, &de);
    if (r != 0) {
        return r;
    }
    if (de.is_dir) {
        return FS_ERR_ISDIR;
    }
    out->first_cluster = de.first_cluster;
    out->size = de.size;
    out->pos = 0;
    out->is_dir = false;
    return 0;
}

int fat32_opendir(const char *path, fat32_file_t *out) {
    fat32_dirent_t de;
    int r = resolve(path, &de);
    if (r != 0) {
        return r;
    }
    if (!de.is_dir) {
        return FS_ERR_NOTFILE;
    }
    out->first_cluster = de.first_cluster;
    out->size = 0;
    out->pos = 0;
    out->is_dir = true;
    return 0;
}

long fat32_read(fat32_file_t *f, void *buf, uint32_t len) {
    if (f->is_dir) {
        return FS_ERR_ISDIR;
    }
    if (f->pos >= f->size) {
        return 0;
    }
    uint32_t remaining = f->size - f->pos;
    if (len > remaining) {
        len = remaining;
    }
    long n = chain_read(f->first_cluster, f->pos, buf, len);
    if (n < 0) {
        return n;
    }
    f->pos += (uint32_t)n;
    return n;
}

int fat32_stat(const char *path, fat32_stat_t *out) {
    fat32_dirent_t de;
    int r = resolve(path, &de);
    if (r != 0) {
        return r;
    }
    out->size = de.size;
    out->is_dir = de.is_dir;
    return 0;
}
