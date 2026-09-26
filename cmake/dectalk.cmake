# DECtalk, built from the third_party/dectalk submodule without modifying it
# (plan v4 §12.1).
#
# Three targets:
#   dectalk       the engine, as a DLL exporting what src/dapi/src/dectalk.def
#                 lists
#   dectalk_dic   the host tool that compiles the pronunciation dictionary
#   dectalk_data  runs it, leaving dtalk_us.dic in the runtime output folder
#                 beside dectalk.dll, where core/audio/dectalk_engine.cpp looks
#
# Based on upstream's cmake branch, with the source list checked against the
# develop branch's "DECtalk API.vcxproj". It is 1990s C: it is built with its
# own flags and no warnings, and never with ours (cmake/warnings.cmake).

set(DECTALK_ROOT "${CMAKE_SOURCE_DIR}/third_party/dectalk/src")
set(DECTALK_DAPI "${DECTALK_ROOT}/dapi/src")

if(NOT EXISTS "${DECTALK_DAPI}/api/ttsapi.c")
    message(FATAL_ERROR "third_party/dectalk is empty. Run: git submodule update --init third_party/dectalk")
endif()

set(DECTALK_SOURCES
    api/crypt2.c        api/decstd97.c      api/ttsapi.c

    cmd/cmd_init.c      cmd/cmd_wav.c       cmd/cm_char.c
    cmd/cm_cmd.c        cmd/cm_copt.c       cmd/cm_main.c
    cmd/cm_pars.c       cmd/cm_phon.c       cmd/cm_text.c
    cmd/cm_util.c       cmd/par_ambi.c      cmd/par_char.c
    cmd/par_dict.c      cmd/par_pars.c      cmd/par_rule.c

    hlsyn/acxf1c.c      hlsyn/brent.c       hlsyn/circuit.c
    hlsyn/frame.c       hlsyn/hlframe.c     hlsyn/inithl.c
    hlsyn/llinit.c      hlsyn/log10table.c  hlsyn/nasalf1x.c
    hlsyn/reson.c       hlsyn/sample.c      hlsyn/sqrttable.c
    hlsyn/voice.c

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

    nt/dbgwins.c        nt/mmalloc.c        nt/opthread.c
    nt/pipe.c           nt/playaud.c        nt/spc.c

    ph/phinit.c         ph/phlog.c          ph/phprint.c
    ph/ph_aloph.c       ph/ph_claus.c       ph/ph_draw.c
    ph/ph_drwt0.c       ph/ph_inton.c       ph/ph_main.c
    ph/ph_romi.c        ph/ph_setar.c       ph/ph_sort.c
    ph/ph_syl.c         ph/ph_syntx.c       ph/ph_task.c
    ph/ph_timng.c       ph/ph_vdefi.c       ph/ph_vset.c

    vtm/playtone.c      vtm/sync.c          vtm/vtm.c
    vtm/vtmiont.c

    dectalk.def
)
list(TRANSFORM DECTALK_SOURCES PREPEND "${DECTALK_DAPI}/")

set(DECTALK_INCLUDES api cmd hlsyn include lts nt ph protos vtm ../..)
list(TRANSFORM DECTALK_INCLUDES PREPEND "${DECTALK_DAPI}/")

# The Release|x64 defines from the vcxproj. OS_SIXTY_FOUR_BIT, which only its
# Debug|x64 configuration lists, comes from _WIN64 in nt/opmmsys.h anyway.
set(DECTALK_DEFINES
    WIN32 _WINDOWS _USRDLL DECTALKAPI_EXPORTS USE_CORE_DLL BLD_DECTALK_DLL
    ACNA ENGLISH_US ENGLISH AMD64
    $<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>
    _CRT_SECURE_NO_WARNINGS
)

add_library(dectalk SHARED ${DECTALK_SOURCES})
target_include_directories(dectalk PRIVATE ${DECTALK_INCLUDES})
target_compile_definitions(dectalk PRIVATE ${DECTALK_DEFINES})
target_link_libraries(dectalk PRIVATE winmm)
# /W0: thousands of warnings in code we do not maintain. No /utf-8 either:
# some of the sources are Latin-1.
target_compile_options(dectalk PRIVATE /W0)
# Only ttsapi.h is meant for callers. It is consumed as a system header so
# our /W4 /WX does not apply to it.
target_include_directories(dectalk SYSTEM INTERFACE "${DECTALK_DAPI}/api")

# The dictionary compiler, run on the build machine.
add_executable(dectalk_dic "${DECTALK_DAPI}/dic/dic.c")
target_include_directories(dectalk_dic PRIVATE ${DECTALK_INCLUDES})
target_compile_definitions(dectalk_dic PRIVATE
    WIN32 _CONSOLE ENGLISH_US ENGLISH WINDIC _CRT_SECURE_NO_WARNINGS
    $<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>
)
target_compile_options(dectalk_dic PRIVATE /W0)
# Kept out of bin/, which is what ships.
set_target_properties(dectalk_dic PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

# The compiled dictionary goes in the same folder as dectalk.dll, and so
# beside every executable that loads it: CMAKE_RUNTIME_OUTPUT_DIRECTORY is one
# folder for the bot and the tests alike.
get_property(dectalk_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(dectalk_multi_config)
    set(DECTALK_DICTIONARY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/dtalk_us.dic")
else()
    set(DECTALK_DICTIONARY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dtalk_us.dic")
endif()
add_custom_command(
    OUTPUT "${DECTALK_DICTIONARY}"
    COMMAND dectalk_dic "${DECTALK_DAPI}/dic/Dic_us.txt" "${DECTALK_DICTIONARY}"
    DEPENDS dectalk_dic "${DECTALK_DAPI}/dic/Dic_us.txt"
    COMMENT "Compiling the DECtalk dictionary"
    VERBATIM
)
add_custom_target(dectalk_data DEPENDS "${DECTALK_DICTIONARY}")
add_dependencies(dectalk dectalk_data)
