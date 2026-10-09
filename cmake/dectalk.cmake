# DECtalk, built from the third_party/dectalk submodule without modifying it
# (src/modules/dectalk/docs/Speech.md §4.2).
#
# Three targets:
#   dectalk       the engine, as a DLL, or elsewhere a shared library,
#                 exporting what src/dapi/src/dectalk.def lists
#   dectalk_dic   the host tool that compiles the pronunciation dictionary
#   dectalk_data  runs it, leaving dtalk_us.dic in the runtime output folder
#                 beside the engine, where dectalk_engine.cpp looks
#
# On Windows, based on upstream's cmake branch, with the source list checked
# against the develop branch's "DECtalk API.vcxproj"; elsewhere, on the
# develop branch's Linux makefiles (configure.ac, dapi/src/Makefile.in). It
# is 1990s C: it is built with its own flags and no warnings, and never with
# ours (cmake/warnings.cmake).

set(DECTALK_ROOT "${CMAKE_SOURCE_DIR}/third_party/dectalk/src")
set(DECTALK_DAPI "${DECTALK_ROOT}/dapi/src")

if(NOT EXISTS "${DECTALK_DAPI}/api/ttsapi.c")
    message(FATAL_ERROR "third_party/dectalk is empty. Run: git submodule update --init third_party/dectalk")
endif()

# What both builds compile.
set(DECTALK_SOURCES
    api/crypt2.c        api/decstd97.c      api/ttsapi.c

    cmd/cmd_init.c      cmd/cmd_wav.c       cmd/cm_char.c
    cmd/cm_cmd.c        cmd/cm_copt.c       cmd/cm_main.c
    cmd/cm_pars.c       cmd/cm_phon.c       cmd/cm_text.c
    cmd/cm_util.c       cmd/par_ambi.c      cmd/par_char.c
    cmd/par_dict.c      cmd/par_pars.c      cmd/par_rule.c

    hlsyn/acxf1c.c      hlsyn/brent.c       hlsyn/circuit.c
    hlsyn/hlframe.c     hlsyn/inithl.c      hlsyn/log10table.c
    hlsyn/nasalf1x.c    hlsyn/sqrttable.c

    kernel/services.c   kernel/usa_init.c

    lts/loaddict.c      lts/lsa_adju.c      lts/lsa_coni.c
    lts/lsa_fr.c        lts/lsa_gr.c        lts/lsa_ir.c
    lts/lsa_it.c        lts/lsa_ja.c        lts/lsa_rtbi.c
    lts/lsa_rule.c      lts/lsa_sl.c        lts/lsa_sp.c
    lts/lsa_task.c      lts/lsa_us.c        lts/lsa_util.c
    lts/lsw_main.c      lts/ls_chari.c      lts/ls_dict.c
    lts/ls_homo.c       lts/ls_math.c       lts/ls_proc.c
    lts/ls_spel.c       lts/ls_speli.c      lts/ls_suff.c
    lts/ls_suffi.c

    nt/mmalloc.c        nt/opthread.c       nt/pipe.c
    nt/playaud.c        nt/spc.c

    ph/phinit.c         ph/phlog.c          ph/phprint.c
    ph/ph_aloph.c       ph/ph_claus.c       ph/ph_draw.c
    ph/ph_drwt0.c       ph/ph_inton.c       ph/ph_main.c
    ph/ph_romi.c        ph/ph_setar.c       ph/ph_sort.c
    ph/ph_syl.c         ph/ph_syntx.c       ph/ph_task.c
    ph/ph_timng.c       ph/ph_vdefi.c       ph/ph_vset.c

    vtm/playtone.c      vtm/sync.c          vtm/vtm.c
    vtm/vtmiont.c
)

# dectalk_zeroed_heap.h makes every allocation zeroed. DECtalk reads heap
# memory it never wrote, so with the release CRT the same request spoke
# differently from run to run (src/modules/dectalk/docs/Speech.md §4.2).
set(DECTALK_ZEROED_HEAP "${CMAKE_CURRENT_LIST_DIR}/dectalk_zeroed_heap.h")

if(WIN32)
    list(APPEND DECTALK_SOURCES
        hlsyn/frame.c   hlsyn/llinit.c  hlsyn/reson.c
        hlsyn/sample.c  hlsyn/voice.c
        nt/dbgwins.c
        dectalk.def
    )
    set(DECTALK_INCLUDES api cmd hlsyn include lts nt ph protos vtm ../..)

    # The Release|x64 defines from the vcxproj. OS_SIXTY_FOUR_BIT, which only
    # its Debug|x64 configuration lists, comes from _WIN64 in nt/opmmsys.h
    # anyway.
    set(DECTALK_DEFINES
        WIN32 _WINDOWS _USRDLL DECTALKAPI_EXPORTS USE_CORE_DLL BLD_DECTALK_DLL
        ACNA ENGLISH_US ENGLISH AMD64
        $<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>
        _CRT_SECURE_NO_WARNINGS
    )
    set(DECTALK_DIC_DEFINES WIN32 _CONSOLE ENGLISH_US ENGLISH WINDIC _CRT_SECURE_NO_WARNINGS)
    # /W0: thousands of warnings in code we do not maintain. No /utf-8
    # either: some of the sources are Latin-1.
    set(DECTALK_OPTIONS /W0 "/FI${DECTALK_ZEROED_HEAP}")
    set(DECTALK_DIC_OPTIONS /W0)
    set(DECTALK_DIC_ARGUMENTS "")
else()
    # The Linux makefiles' DECTALK_TTS_OBJS: osf/ in place of the Windows
    # wave and window code, and nt/linux_audio.c with its audio off, since
    # the bot only ever speaks into memory.
    list(APPEND DECTALK_SOURCES
        api/init.c
        nt/linux_audio.c
        osf/dtmmio.c    osf/loadable.c
    )
    set(DECTALK_INCLUDES api cmd dic hlsyn include kernel lts nt osf ph protos vtm ../..)

    # configure.ac's DEFINES for *-*-linux-*, the US English build's
    # LANGUAGE, and api/Makefile.in's -DDEC. The install prefix is only where
    # DECtalk looks for a dictionary when it is given none, which it always
    # is. Never _DEBUG, which upstream's Linux build never defines either:
    # include/kernel.h's debug-only OutputDebugString does not compile there.
    set(DECTALK_DEFINES
        _REENTRANT NOMME LTSSIM TTSSIM ANSI BLD_DECTALK_DLL ACCESS32 TYPING_MODE
        ENGLISH ENGLISH_US ACNA DEC DISABLE_AUDIO
        DECTALK_INSTALL_PREFIX="/usr/local"
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    )
    set(DECTALK_DIC_DEFINES
        _REENTRANT NOMME LTSSIM TTSSIM ANSI BLD_DECTALK_DLL ACCESS32 TYPING_MODE
        ENGLISH ENGLISH_US ACNA
    )
    # -w: no warnings, as /W0. -fcommon and -fgnu89-inline are the C of the
    # compilers it was written for: tentative definitions shared between
    # files, and __inline functions that are defined once for every caller
    # (cmd/cm_prot.h's par_* helpers). Newer compilers make some of what this
    # code does an error by default; the -Wno-error ones put it back to a
    # warning, which -w then silences, as GCC 13 and older did.
    set(DECTALK_OPTIONS
        -w -fPIC -fno-strict-aliasing -fcommon -fgnu89-inline
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion>
        -include "${DECTALK_ZEROED_HEAP}"
    )
    set(DECTALK_DIC_OPTIONS -w -fcommon)
    # The makefile's own argument: the dictionary in the byte order and
    # layout the engine reads on every little-endian system.
    set(DECTALK_DIC_ARGUMENTS "/t:win32")
endif()
list(TRANSFORM DECTALK_SOURCES PREPEND "${DECTALK_DAPI}/")
list(TRANSFORM DECTALK_INCLUDES PREPEND "${DECTALK_DAPI}/")

add_library(dectalk SHARED ${DECTALK_SOURCES})
target_include_directories(dectalk PRIVATE ${DECTALK_INCLUDES})
target_compile_definitions(dectalk PRIVATE ${DECTALK_DEFINES})
target_compile_options(dectalk PRIVATE ${DECTALK_OPTIONS})
if(WIN32)
    target_link_libraries(dectalk PRIVATE winmm)
else()
    # Only what dectalk.def lists is exported, as from the DLL: the rest of
    # its thousands of globals stay its own, and -Bsymbolic, as upstream
    # links it, keeps its calls to them its own too.
    set(dectalk_exports "${CMAKE_CURRENT_BINARY_DIR}/dectalk.map")
    file(STRINGS "${DECTALK_DAPI}/dectalk.def" dectalk_def_lines)
    set(dectalk_listed FALSE)
    set(dectalk_map "{\n  global:\n")
    foreach(line IN LISTS dectalk_def_lines)
        if(line MATCHES "^EXPORTS")
            set(dectalk_listed TRUE)
        elseif(dectalk_listed AND line MATCHES "^[ \t]+([A-Za-z_][A-Za-z0-9_]*)")
            string(APPEND dectalk_map "    ${CMAKE_MATCH_1};\n")
        endif()
    endforeach()
    string(APPEND dectalk_map "  local: *;\n};\n")
    file(CONFIGURE OUTPUT "${dectalk_exports}" CONTENT "${dectalk_map}")
    target_link_options(dectalk PRIVATE "LINKER:--version-script=${dectalk_exports}" "LINKER:-Bsymbolic")
    set_property(TARGET dectalk APPEND PROPERTY LINK_DEPENDS "${dectalk_exports}")
    # What configure would write to config.h. Only lts/lsw_main.c includes
    # it, and glibc has iconv.
    set(dectalk_config "${CMAKE_CURRENT_BINARY_DIR}/dectalk_config")
    file(CONFIGURE OUTPUT "${dectalk_config}/config.h"
         CONTENT "/* Written by cmake/dectalk.cmake, as configure would. */\n#define HAVE_ICONV 1\n")
    target_include_directories(dectalk PRIVATE "${dectalk_config}")
    find_package(Threads REQUIRED)
    target_link_libraries(dectalk PRIVATE Threads::Threads m)
    # Beside the executables, as the DLL is, which find it by their $ORIGIN
    # run path (CMakeLists.txt).
    set_target_properties(dectalk PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endif()
# Only ttsapi.h is meant for callers, and off Windows the osf/ header of
# Windows types it includes. Both are consumed as system headers so our
# warnings do not apply to them.
target_include_directories(dectalk SYSTEM INTERFACE "${DECTALK_DAPI}/api" $<$<NOT:$<PLATFORM_ID:Windows>>:${DECTALK_DAPI}/osf>)

# The dictionary compiler, run on the build machine.
add_executable(dectalk_dic "${DECTALK_DAPI}/dic/dic.c")
target_include_directories(dectalk_dic PRIVATE ${DECTALK_INCLUDES})
if(WIN32)
    target_compile_definitions(dectalk_dic PRIVATE ${DECTALK_DIC_DEFINES} $<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>)
else()
    target_compile_definitions(dectalk_dic PRIVATE ${DECTALK_DIC_DEFINES} $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
endif()
target_compile_options(dectalk_dic PRIVATE ${DECTALK_DIC_OPTIONS})
# Kept out of bin/, which is what ships.
set_target_properties(dectalk_dic PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

# The compiled dictionary goes in the same folder as the engine, and so
# beside every executable that loads it: CMAKE_RUNTIME_OUTPUT_DIRECTORY is one
# folder for the bot and the tests alike.
get_property(dectalk_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(dectalk_multi_config)
    set(DECTALK_DICTIONARY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/dtalk_us.dic")
else()
    set(DECTALK_DICTIONARY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dtalk_us.dic")
endif()
# The compiler takes any argument starting with / for an option, so off
# Windows it is run in the folder and given the file's name alone, as
# upstream's makefile does.
get_filename_component(dectalk_dictionary_folder "${DECTALK_DICTIONARY}" DIRECTORY)
if(WIN32)
    set(dectalk_dic_command dectalk_dic "${DECTALK_DAPI}/dic/Dic_us.txt" "${DECTALK_DICTIONARY}")
else()
    set(dectalk_dic_command
        ${CMAKE_COMMAND} -E make_directory "${dectalk_dictionary_folder}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${dectalk_dictionary_folder}"
            $<TARGET_FILE:dectalk_dic> "${DECTALK_DAPI}/dic/Dic_us.txt" dtalk_us.dic ${DECTALK_DIC_ARGUMENTS}
    )
endif()
add_custom_command(
    OUTPUT "${DECTALK_DICTIONARY}"
    COMMAND ${dectalk_dic_command}
    DEPENDS dectalk_dic "${DECTALK_DAPI}/dic/Dic_us.txt"
    COMMENT "Compiling the DECtalk dictionary"
    VERBATIM
)
# Off Windows, DECtalk looks for a DECtalk.conf beside the program before it
# takes the dictionary it is given, and says so on stderr at every utterance
# when there is none. This is the one upstream installs, naming the same
# dictionary.
if(NOT WIN32)
    set(DECTALK_CONF "${dectalk_dictionary_folder}/DECtalk.conf")
    file(GENERATE OUTPUT "${DECTALK_CONF}" CONTENT "US_dict:dtalk_us.dic\n")
endif()
add_custom_target(dectalk_data DEPENDS "${DECTALK_DICTIONARY}")
add_dependencies(dectalk dectalk_data)
