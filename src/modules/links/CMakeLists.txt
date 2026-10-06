# The links module (README.md). url_rules.hpp and replacements.hpp are public:
# linkstats counts reactions on replacements and finds old ones by their rules.
latibot_module(links
    SOURCES
        src/module.cpp
        src/url_rules.cpp
        src/replacements.cpp
        src/url_replacer.cpp
        src/embed_watch.cpp
        src/links_command.cpp
    TESTS
        tests/url_rules_test.cpp
        tests/links_command_test.cpp
        tests/embed_watch_test.cpp
        tests/url_rule_store_test.cpp
        tests/replacement_store_test.cpp
        tests/links_panel_test.cpp
        tests/links_module_test.cpp
)
