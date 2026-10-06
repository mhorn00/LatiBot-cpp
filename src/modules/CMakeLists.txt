# Every feature module, in dependency order: each after the modules it
# requires (docs/modules/Module_Plan_Final.md §3), which is also the order the
# bot builds them in. Each is on unless its switch is off; one whose
# requirement is off stops the configure, naming both (cmake/modules.cmake).

option(LATIBOT_WITH_LLM "Build the language model module: /llm, /memory and its answers" ON)
if(LATIBOT_WITH_LLM)
    add_subdirectory(llm)
endif()

option(LATIBOT_WITH_TRIGGERS "Build the triggers module: /trigger and its replies" ON)
if(LATIBOT_WITH_TRIGGERS)
    add_subdirectory(triggers)
endif()

option(LATIBOT_WITH_NICKNAMES "Build the nicknames module: /nickname, /nicknames and the history" ON)
if(LATIBOT_WITH_NICKNAMES)
    add_subdirectory(nicknames)
endif()

option(LATIBOT_WITH_MIDNIGHT "Build the midnight module: /midnight and the daily message" ON)
if(LATIBOT_WITH_MIDNIGHT)
    add_subdirectory(midnight)
endif()

option(LATIBOT_WITH_LINKS "Build the links module: /links, /urltoggle and link replacement" ON)
if(LATIBOT_WITH_LINKS)
    add_subdirectory(links)
endif()

option(LATIBOT_WITH_LINKSTATS "Build the linkstats module: /linkstats and reaction counts; requires links" ON)
if(LATIBOT_WITH_LINKSTATS)
    add_subdirectory(linkstats)
endif()
