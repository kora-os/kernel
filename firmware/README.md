# Raspberry Pi boot firmware

These are the closed-source Raspberry Pi GPU boot firmware blobs and device trees
needed to boot KoraOS on real hardware. They are loaded by the SoC before our
kernel and are **not** linked into it (mere aggregation), so their license does
not affect KoraOS's GPL-3.0-or-later licensing.

- **License:** Raspberry Pi / Broadcom firmware license, see `LICENCE.broadcom`
  (redistribution permitted on Raspberry Pi hardware).
- **Upstream:** https://github.com/raspberrypi/firmware (`boot/` directory)
- **Pinned commit:** `12eeaa12865869b07db760f4bbb7507ec6f1976c`

## Files

| File | Board | Purpose |
|------|-------|---------|
| `bootcode.bin` | Pi 3 | Second-stage bootloader (Pi 4 uses its SPI EEPROM instead) |
| `start.elf`, `fixup.dat` | Pi 3 | GPU firmware / SDRAM setup |
| `start4.elf`, `fixup4.dat` | Pi 4 | GPU firmware / SDRAM setup |
| `bcm2710-rpi-3-b.dtb` | Pi 3 | Device tree |
| `bcm2711-rpi-4-b.dtb` | Pi 4 | Device tree |

The build copies these, plus `config.txt` and the kernel image(s), to the boot
partition. To update, re-download the same files from a newer upstream commit and
update the pin above.
