# The linkstats module (README.md).
latibot_module(linkstats CONFIG
    REQUIRES links
    SOURCES
        src/module.cpp
        src/reactions.cpp
        src/emote_reactions.cpp
        src/media_posts.cpp
        src/backfill.cpp
        src/emoji_copies.cpp
        src/legacy_replacements.cpp
        src/linkstats_command.cpp
    # SHA-256 of emoji images, so one image is copied once (emoji_copies.cpp).
    # DPP links OpenSSL already; this says so.
    LINKS OpenSSL::Crypto
    TESTS
        tests/linkstats_command_test.cpp
        tests/legacy_replacements_test.cpp
        tests/reactions_test.cpp
        tests/backfill_test.cpp
        tests/emoji_copies_test.cpp
        tests/emote_reactions_test.cpp
        tests/media_posts_test.cpp
        tests/reaction_store_test.cpp
        tests/linkstats_module_test.cpp
)
