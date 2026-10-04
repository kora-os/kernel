#include "fs/fat32.h"
#include "fs/blkdev.h"
#include "lib/string.h"
#include "mm/kmalloc.h"

// Each mounted view owns its geometry and scratch caches. Calls are serialized
// by the kernel; no task state or additional filesystem locks are required.
struct fat32_volume {
    blkdev_t *dev;
    uint32_t sec_per_clus;
    uint32_t fat_start;
    uint32_t first_data_sector;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint32_t fat_cached_sec;
    char label[12];
    uint8_t fat_buf[BLK_SECTOR_SIZE];
    uint8_t data_buf[BLK_SECTOR_SIZE];
};

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

static int read_sector(fat32_volume_t *vol, uint32_t lba, uint8_t *buf) {
    return blkdev_read(vol->dev, lba, 1, buf);
}

// A data cluster number is valid if it names a real cluster and is not an
// end-of-chain / bad marker.
static bool cluster_valid(fat32_volume_t *vol, uint32_t c) {
    return c >= 2 && c < vol->cluster_count + 2;
}

static uint32_t cluster_to_lba(fat32_volume_t *vol, uint32_t cluster) {
    return vol->first_data_sector + (cluster - 2) * vol->sec_per_clus;
}

// Follow one FAT link. Preserve I/O failure separately from EOC markers.
static int fat_next(fat32_volume_t *vol, uint32_t cluster, uint32_t *next) {
    uint32_t fat_off = cluster * 4;
    uint32_t sec = vol->fat_start + fat_off / BLK_SECTOR_SIZE;
    uint32_t off = fat_off % BLK_SECTOR_SIZE;
    if (vol->fat_cached_sec != sec) {
        if (read_sector(vol, sec, vol->fat_buf) != 0) {
            vol->fat_cached_sec = 0xFFFFFFFFu;
            return FS_ERR_IO;
        }
        vol->fat_cached_sec = sec;
    }
    *next = rd32(vol->fat_buf + off) & 0x0FFFFFFFu;
    return 0;
}

// Floyd's detector bounds corrupt cyclic chains promptly, including self loops.
// Its lookahead reads FAT entries only and preserves transport error codes.
static int chain_advance(fat32_volume_t *vol, uint32_t *cluster,
                         uint32_t *slow, uint32_t *fast) {
    int rc = fat_next(vol, *cluster, cluster);
    if (rc != 0) {
        return rc;
    }
    if (*cluster >= 0x0ffffff8u) {
        return 0;
    }
    if (!cluster_valid(vol, *cluster)) {
        return FS_ERR_CORRUPT;
    }
    if (cluster_valid(vol, *slow)) {
        rc = fat_next(vol, *slow, slow);
        if (rc != 0) {
            return rc;
        }
    }
    for (unsigned i = 0; i < 2 && cluster_valid(vol, *fast); i++) {
        rc = fat_next(vol, *fast, fast);
        if (rc != 0) {
            return rc;
        }
    }
    if (cluster_valid(vol, *fast) && *slow == *fast) {
        return FS_ERR_CORRUPT;
    }
    return 0;
}

// Read a byte range from a bounded chain, preserving corruption and I/O errors.
static long chain_read(fat32_volume_t *vol, uint32_t first_cluster, uint32_t pos,
                       void *buf, uint32_t len) {
    if (len == 0) {
        return 0;
    }
    uint32_t cluster_bytes = vol->sec_per_clus * BLK_SECTOR_SIZE;
    uint32_t cluster = first_cluster;
    uint32_t slow = cluster, fast = cluster;
    uint32_t traversed = 0;
    uint32_t skip = pos / cluster_bytes;
    if (skip >= vol->cluster_count) {
        return FS_ERR_CORRUPT;
    }
    while (skip--) {
        if (!cluster_valid(vol, cluster) || ++traversed > vol->cluster_count) {
            return FS_ERR_CORRUPT;
        }
        int rc = chain_advance(vol, &cluster, &slow, &fast);
        if (rc != 0) {
            return rc;
        }
        if (cluster >= 0x0ffffff8u) {
            return 0;
        }
    }
    uint32_t off = pos % cluster_bytes;
    uint8_t *out = buf;
    uint32_t done = 0;
    while (done < len) {
        if (!cluster_valid(vol, cluster) || ++traversed > vol->cluster_count) {
            return FS_ERR_CORRUPT;
        }
        uint32_t lba = cluster_to_lba(vol, cluster);
        while (off < cluster_bytes && done < len) {
            uint32_t si = off / BLK_SECTOR_SIZE;
            uint32_t so = off % BLK_SECTOR_SIZE;
            if (read_sector(vol, lba + si, vol->data_buf) != 0) {
                return FS_ERR_IO;
            }
            uint32_t chunk = BLK_SECTOR_SIZE - so;
            if (chunk > len - done) {
                chunk = len - done;
            }
            memcpy(out + done, vol->data_buf + so, chunk);
            done += chunk;
            off += chunk;
        }
        if (done == len) {
            break;
        }
        int rc = chain_advance(vol, &cluster, &slow, &fast);
        if (rc != 0) {
            return rc;
        }
        if (cluster >= 0x0ffffff8u) {
            break;
        }
        off = 0;
    }
    return (long)done;
}

// --- long filename (VFAT) decoding -----------------------------------------

// An LFN holds at most 20 entries of 13 UTF-16 code units.
#define FAT32_LFN_MAX_UNITS 260

// Byte offsets of the 13 UTF-16 code units within a 32-byte LFN entry.
static const uint8_t lfn_off[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

static uint8_t short_checksum(const uint8_t *ent) {
    uint8_t sum = 0;
    for (unsigned i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + ent[i]);
    }
    return sum;
}

static bool lfn_complete(const uint16_t *units, uint32_t n) {
    bool terminated = false;
    for (uint32_t i = 0; i < n; i++) {
        if (terminated) {
            if (units[i] != 0xffffu) {
                return false;
            }
        } else if (units[i] == 0) {
            terminated = true;
        } else if (i >= 255 || units[i] == 0xffffu) {
            return false;
        }
    }
    return n > 0 && units[0] != 0;
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

static void copy_label(char *out, const uint8_t *label) {
    unsigned end = 11;
    while (end && label[end - 1] == ' ') {
        end--;
    }
    for (unsigned i = 0; i < end; i++) {
        out[i] = short_byte(label[i]);
    }
    out[end] = 0;
    if (strcmp(out, "no name") == 0) {
        out[0] = 0;
    }
}

int fat32_mount(blkdev_t *dev, fat32_volume_t **out) {
    if (out == NULL) {
        return FS_ERR_INVAL;
    }
    *out = NULL;
    if (dev == NULL) {
        return FS_ERR_INVAL;
    }
    uint8_t sec[BLK_SECTOR_SIZE];
    if (blkdev_read(dev, 0, 1, sec) != 0) {
        return FS_ERR_IO;
    }
    uint32_t spc = sec[13], rsvd = rd16(sec + 14), nfats = sec[16];
    uint32_t fatsz = rd32(sec + 36), root = rd32(sec + 44);
    uint32_t total = rd16(sec + 19);
    if (total == 0) {
        total = rd32(sec + 32);
    }
    uint64_t data = (uint64_t)rsvd + (uint64_t)nfats * fatsz;
    uint32_t flags = rd16(sec + 40);
    uint32_t active = (flags & 0x80) ? flags & 0x0f : 0;
    if (sec[510] != 0x55 || sec[511] != 0xaa || rd16(sec + 11) != 512 ||
        !spc || (spc & (spc - 1)) || spc > 128 || !rsvd || !nfats ||
        !fatsz || rd16(sec + 17) || rd16(sec + 22) || rd16(sec + 42) ||
        active >= nfats || total > dev->sector_count || data >= total) {
        return FS_ERR_NOFS;
    }
    uint32_t clusters = (total - (uint32_t)data) / spc;
    if (!clusters || clusters > 0x0ffffff5u ||
        (uint64_t)fatsz * 128 < (uint64_t)clusters + 2 ||
        root < 2 || root >= clusters + 2) {
        return FS_ERR_NOFS;
    }
    fat32_volume_t *vol = kmalloc(sizeof(*vol));
    if (vol == NULL) {
        return FS_ERR_IO;
    }
    vol->dev = dev;
    vol->sec_per_clus = spc;
    vol->fat_start = rsvd + active * fatsz;
    vol->first_data_sector = (uint32_t)data;
    vol->root_cluster = root;
    vol->cluster_count = clusters;
    vol->fat_cached_sec = 0xffffffffu;
    copy_label(vol->label, sec + 71);
    // Root-directory label is authoritative; the BPB copy can be stale.
    for (uint32_t pos = 0; ; pos += 32) {
        uint8_t ent[32];
        long n = chain_read(vol, root, pos, ent, sizeof(ent));
        if (n < 0) {
            kfree(vol);
            return (int)n;
        }
        if (n == 0 || ent[0] == 0) {
            break;
        }
        if (ent[0] != 0xe5 && ent[11] == 0x08) {
            copy_label(vol->label, ent);
            break;
        }
    }
    *out = vol;
    return 0;
}

void fat32_unmount(fat32_volume_t *volume) {
    kfree(volume);
}

const char *fat32_label(const fat32_volume_t *volume) {
    return volume->label;
}

bool fat32_is_read_only(const fat32_volume_t *volume) {
    return volume->dev->read_only;
}

int fat32_readdir(fat32_file_t *dir, fat32_dirent_t *out) {
    if (dir == NULL || out == NULL || dir->volume == NULL) {
        return FS_ERR_INVAL;
    }
    if (!dir->is_dir) {
        return FS_ERR_NOTDIR;
    }
    uint8_t ent[32];
    uint16_t units[FAT32_LFN_MAX_UNITS] = {0};
    uint32_t nunits = 0;
    uint32_t expected = 0;
    uint8_t checksum = 0;
    bool have_lfn = false;

    for (;;) {
        long n = chain_read(dir->volume, dir->first_cluster, dir->pos, ent, sizeof(ent));
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
        if (attr == 0x0f) {
            unsigned ord = ent[0] & 0x1f;
            if (ent[0] & 0x40) {
                memset(units, 0, sizeof(units));
                expected = ord;
                nunits = ord * 13;
                checksum = ent[13];
                have_lfn = ord > 0 && ord <= 20;
            }
            if (!have_lfn || ord == 0 || ord > 20 || ord != expected || ent[13] != checksum ||
                (ent[0] & 0xa0) || ent[12] || rd16(ent + 26)) {
                have_lfn = false;
            } else {
                for (unsigned k = 0; k < 13; k++) {
                    units[(ord - 1) * 13 + k] = (uint16_t)rd16(ent + lfn_off[k]);
                }
                expected--;
            }
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

        if (have_lfn && expected == 0 && short_checksum(ent) == checksum &&
            lfn_complete(units, nunits)) {
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

static int find_in_dir(fat32_volume_t *vol, uint32_t dir_cluster, const char *comp, size_t comp_len,
                       fat32_dirent_t *out) {
    fat32_file_t dir = {.volume = vol, .first_cluster = dir_cluster, .size = 0, .pos = 0,
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
int fat32_lookup(fat32_volume_t *vol, const char *path, fat32_dirent_t *out) {
    if (vol == NULL) {
        return FS_ERR_NOFS;
    }
    if (path == NULL || out == NULL || path[0] != '/') {
        return FS_ERR_INVAL;
    }
    fat32_dirent_t cur = {.name = "/", .size = 0,
                          .first_cluster = vol->root_cluster, .is_dir = true};
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
            int r = find_in_dir(vol, cur.first_cluster, start, len, &de);
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

int fat32_open(fat32_volume_t *vol, const char *path, fat32_file_t *out) {
    if (out == NULL) {
        return FS_ERR_INVAL;
    }
    fat32_dirent_t de;
    int r = fat32_lookup(vol, path, &de);
    if (r != 0) {
        return r;
    }
    if (de.is_dir) {
        return FS_ERR_ISDIR;
    }
    out->volume = vol;
    out->first_cluster = de.first_cluster;
    out->size = de.size;
    out->pos = 0;
    out->is_dir = false;
    return 0;
}

int fat32_opendir(fat32_volume_t *vol, const char *path, fat32_file_t *out) {
    if (out == NULL) {
        return FS_ERR_INVAL;
    }
    fat32_dirent_t de;
    int r = fat32_lookup(vol, path, &de);
    if (r != 0) {
        return r;
    }
    if (!de.is_dir) {
        return FS_ERR_NOTFILE;
    }
    out->volume = vol;
    out->first_cluster = de.first_cluster;
    out->size = 0;
    out->pos = 0;
    out->is_dir = true;
    return 0;
}

long fat32_read(fat32_file_t *f, void *buf, uint32_t len) {
    if (f == NULL || f->volume == NULL || (len && buf == NULL)) {
        return FS_ERR_INVAL;
    }
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
    long n = chain_read(f->volume, f->first_cluster, f->pos, buf, len);
    if (n < 0) {
        return n;
    }
    if ((uint32_t)n != len) {
        return FS_ERR_CORRUPT;
    }
    f->pos += (uint32_t)n;
    return n;
}

int fat32_stat(fat32_volume_t *vol, const char *path, fat32_stat_t *out) {
    if (out == NULL) {
        return FS_ERR_INVAL;
    }
    fat32_dirent_t de;
    int r = fat32_lookup(vol, path, &de);
    if (r != 0) {
        return r;
    }
    out->size = de.size;
    out->is_dir = de.is_dir;
    return 0;
}
