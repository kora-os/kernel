# SPDX-License-Identifier: GPL-3.0-or-later
#
# MicroPython's core and KoraOS's port as the static library
# koraos_micropython (cmake/micropython.cmake), added by cmake/userfs.cmake.
# Its flags are fixed there, as the user programs' are, so the per-configuration
# defaults are dropped here: the library, and the program linking it, is the
# same in every build type.
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_C_FLAGS_${config} "")
endforeach()
koraos_micropython_library("${SOURCE_ROOT}")
