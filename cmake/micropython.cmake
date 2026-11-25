# MicroPython for KoraOS (project_doc/koracli-plan.md, step 0a): the core from
# third_party/micropython (a pinned subset, see its PROVENANCE.md) and the port
# in user/micropython, built into the static library koraos_micropython, which
# the `micropython` program links (cmake/userfs.cmake).
#
# The sources are MicroPython's own list (py/py.cmake). The generated headers
# follow py/mkrules.cmake's steps (qstrs, module definitions, root pointers),
# written out here for two reasons: the version comes from the pinned tag
# (makeversionhdr.py would otherwise run git describe in KoraOS's repository
# and report its commit), and the preprocessor runs with the user programs'
# cross flags, which are compile options rather than include directories.
include_guard(GLOBAL)

# The pinned release: its tag, commit and date (2026-08-24, the date the
# banner shows, so every build of it is the same).
set(MICROPY_TAG "v1.29.0")
set(MICROPY_HASH "0fd6c573e")
set(MICROPY_DATE_EPOCH 1787581954)
# The main thread's stack, declared at link time (-z stack-size) and given to
# MicroPython's stack check (user/micropython/main.c).
set(MICROPY_KORA_STACK_SIZE 65536)

# Adds the library from its own directory (user/micropython/CMakeLists.txt),
# where the per-configuration compiler flags are dropped, as for the libc: the
# program is then the same in every build type.
function(koraos_add_micropython SOURCE_ROOT)
    add_subdirectory("${SOURCE_ROOT}/user/micropython" "${CMAKE_BINARY_DIR}/micropython")
endfunction()

# The library and its generated headers; called from user/micropython.
function(koraos_micropython_library SOURCE_ROOT)
    set(MICROPY_DIR "${SOURCE_ROOT}/third_party/micropython")
    set(MICROPY_PORT_DIR "${SOURCE_ROOT}/user/micropython")
    include("${MICROPY_DIR}/py/py.cmake")

    set(gen "${CMAKE_CURRENT_BINARY_DIR}/genhdr")
    file(MAKE_DIRECTORY "${gen}")

    set(sources ${MICROPY_SOURCE_PY}
        "${MICROPY_DIR}/shared/readline/readline.c"
        "${MICROPY_DIR}/shared/runtime/pyexec.c"
        "${MICROPY_DIR}/shared/runtime/gchelper_generic.c"
        "${MICROPY_DIR}/extmod/modtime.c" "${SOURCE_ROOT}/user/libconsole/keys.c"
        "${MICROPY_PORT_DIR}/main.c" "${MICROPY_PORT_DIR}/hal.c" "${MICROPY_PORT_DIR}/file.c"
        "${MICROPY_PORT_DIR}/modos.c" "${MICROPY_PORT_DIR}/modkora.c")

    # The flags every file is compiled and preprocessed with: the user
    # programs' (USER_COMPILE_FLAGS), MicroPython's include directories and
    # the port's shims. Upstream code is left as it is, so its warnings are
    # not errors; MicroPython's own frames stay well under the 2 KB the user
    # programs are held to.
    set(flags ${USER_COMPILE_FLAGS} -std=gnu11 -DNDEBUG
        -DMICROPY_KORA_STACK_SIZE=${MICROPY_KORA_STACK_SIZE}
        "-I${MICROPY_DIR}" "-I${MICROPY_PORT_DIR}" "-I${MICROPY_PORT_DIR}/shims"
        "-I${CMAKE_CURRENT_BINARY_DIR}")

    set(py "${MICROPY_DIR}/py")
    set(version "${gen}/mpversion.h")
    add_custom_command(
        OUTPUT "${version}"
        COMMAND ${CMAKE_COMMAND} -E env MICROPY_GIT_TAG=${MICROPY_TAG}
                MICROPY_GIT_HASH=${MICROPY_HASH} SOURCE_DATE_EPOCH=${MICROPY_DATE_EPOCH}
                ${Python3_EXECUTABLE} "${py}/makeversionhdr.py" "${version}"
        DEPENDS "${py}/makeversionhdr.py" "${py}/mpconfig.h"
        COMMENT "MicroPython: version header"
        VERBATIM)

    # Every MP_QSTR_, module and root pointer the sources mention.
    set(last "${gen}/qstr.i.last")
    add_custom_command(
        OUTPUT "${last}"
        COMMAND ${Python3_EXECUTABLE} "${py}/makeqstrdefs.py" pp ${CMAKE_C_COMPILER} -E
                output "${last}" cflags ${flags} -DNO_QSTR cxxflags ${flags} -DNO_QSTR
                sources ${sources}
        DEPENDS "${version}" ${sources} "${MICROPY_PORT_DIR}/mpconfigport.h"
                "${MICROPY_PORT_DIR}/mphalport.h"
        COMMENT "MicroPython: preprocessing for qstrs"
        VERBATIM COMMAND_EXPAND_LISTS)
    foreach(kind qstr module root_pointer)
        add_custom_command(
            OUTPUT "${gen}/${kind}.collected"
            COMMAND ${Python3_EXECUTABLE} "${py}/makeqstrdefs.py" split ${kind} "${last}"
                    "${gen}/${kind}" _
            COMMAND ${Python3_EXECUTABLE} "${py}/makeqstrdefs.py" cat ${kind} _ "${gen}/${kind}"
                    "${gen}/${kind}.collected"
            DEPENDS "${last}"
            VERBATIM)
    endforeach()

    # qstrdefs.h and those collected, through the preprocessor with each Q()
    # kept as written, then hashed into the qstr table.
    set(preprocessed "${gen}/qstrdefs.preprocessed.h")
    add_custom_command(
        OUTPUT "${preprocessed}"
        COMMAND cat "${py}/qstrdefs.h" "${gen}/qstr.collected"
                | sed "s/^Q(.*)/\"&\"/"
                | ${CMAKE_C_COMPILER} -E ${flags} -
                | sed "s/^\\\"\\(Q(.*)\\)\\\"/\\1/" > "${preprocessed}"
        DEPENDS "${py}/qstrdefs.h" "${gen}/qstr.collected"
        VERBATIM COMMAND_EXPAND_LISTS)
    set(qstrs "${gen}/qstrdefs.generated.h")
    add_custom_command(
        OUTPUT "${qstrs}"
        COMMAND ${Python3_EXECUTABLE} "${py}/makeqstrdata.py" "${preprocessed}" > "${qstrs}"
        DEPENDS "${preprocessed}" "${py}/makeqstrdata.py"
        VERBATIM)
    set(moduledefs "${gen}/moduledefs.h")
    add_custom_command(
        OUTPUT "${moduledefs}"
        COMMAND ${Python3_EXECUTABLE} "${py}/makemoduledefs.py" "${gen}/module.collected"
                > "${moduledefs}"
        DEPENDS "${gen}/module.collected" "${py}/makemoduledefs.py"
        VERBATIM)
    set(roots "${gen}/root_pointers.h")
    add_custom_command(
        OUTPUT "${roots}"
        COMMAND ${Python3_EXECUTABLE} "${py}/make_root_pointers.py"
                "${gen}/root_pointer.collected" > "${roots}"
        DEPENDS "${gen}/root_pointer.collected" "${py}/make_root_pointers.py"
        VERBATIM)
    add_custom_target(micropython_headers DEPENDS "${version}" "${qstrs}" "${moduledefs}" "${roots}")

    add_library(koraos_micropython STATIC EXCLUDE_FROM_ALL ${sources})
    add_dependencies(koraos_micropython micropython_headers)
    target_compile_options(koraos_micropython PRIVATE ${flags})
endfunction()
