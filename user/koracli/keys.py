# SPDX-License-Identifier: GPL-3.0-or-later
#
# Key positions (USB HID usages) and modifier bits, as the console's
# key-event mode delivers them (kora.key(), <libk/koraos.h> KEY_* and
# INPUT_MOD_*).

ENTER = 0x28
ESCAPE = 0x29
BACKSPACE = 0x2A
TAB = 0x2B
F1 = 0x3A
F2 = 0x3B
F3 = 0x3C
F4 = 0x3D
F5 = 0x3E
F6 = 0x3F
F7 = 0x40
F8 = 0x41
HOME = 0x4A
PAGE_UP = 0x4B
DELETE = 0x4C
END = 0x4D
PAGE_DOWN = 0x4E
RIGHT = 0x4F
LEFT = 0x50
DOWN = 0x51
UP = 0x52
KP_ENTER = 0x58

CTRL = 0x0011   # left or right
SHIFT = 0x0022
ALT = 0x0004    # left Alt; right Alt is AltGr where the layout has one
META = 0x0088   # Command, or the Windows key
