# Vendored: Spleen fonts

The console fonts in `sys:fonts` come from [Spleen](https://github.com/fcambus/spleen),
Frederic Cambus's monospaced bitmap fonts (see the project [CREDITS](../../CREDITS.md)).

- **License:** BSD 2-Clause (see `LICENSE`), compatible with KoraOS's
  GPL-3.0-or-later. Each BDF keeps its copyright comment.
- **Upstream:** https://github.com/fcambus/spleen
- **Release:** 2.3.0, tag `2.3.0`, commit `b2d04a6180d44f47396dc530f3b2ca46b87e618b`

## What was vendored

The BDF sources of five sizes, unchanged: `spleen-5x8.bdf`, `spleen-6x12.bdf`,
`spleen-8x16.bdf`, `spleen-12x24.bdf` and `spleen-16x32.bdf`. The 32x64 size
is left out (too large for a console at today's resolutions).

The build converts them to PSF2 (`scripts/bdf2psf.py`, run by
`cmake/userfs.cmake`) and installs them in `/fonts` on the userfs image.

## Updating

Replace the BDFs with those of a newer release and update the tag and commit
above; `tests/run-font-tests.py` checks every glyph's conversion.
