#include "core/db/backup.hpp"

#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "support/temp_directory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using latibot::db::database;
using latibot::testing::temp_directory;

namespace {

constexpr std::chrono::system_clock::time_point stamp(int minutes_past_epoch) {
    return std::chrono::system_clock::time_point{std::chrono::minutes{minutes_past_epoch}};
}

void seed(database& db, int rows) {
    latibot::db::migrate(db);
    for (int i = 0; i < rows; ++i) {
        db.prepare("INSERT INTO guild_settings (guild_id, key, value) VALUES (?, ?, ?)",
                   1234567890123456789ULL, "key-" + std::to_string(i), std::to_string(i))
            .run();
    }
}

int row_count(const std::filesystem::path& file) {
    database copy{file};
    auto query = copy.prepare("SELECT COUNT(*) FROM guild_settings");
    return query.step() ? query.get<int>(0) : -1;
}

bool passes_integrity_check(const std::filesystem::path& file) {
    database copy{file};
    auto query = copy.prepare("PRAGMA integrity_check");
    return query.step() && query.get<std::string>(0) == "ok";
}

} // namespace

TEST_CASE("a backup is a complete, valid copy", "[db]") {
    const temp_directory temp;

    database db{temp.file("bot.db")};
    seed(db, 25);

    const std::filesystem::path destination = temp.file("copy.db");
    latibot::db::backup_to_file(db, destination);

    REQUIRE(std::filesystem::exists(destination));
    CHECK(passes_integrity_check(destination));
    CHECK(row_count(destination) == 25);
}

TEST_CASE("backing up an in-memory database writes it to disk", "[db]") {
    const temp_directory temp;

    database db{std::filesystem::path(database::in_memory)};
    seed(db, 3);

    const std::filesystem::path destination = temp.file("from-memory.db");
    latibot::db::backup_to_file(db, destination);

    CHECK(row_count(destination) == 3);
}

TEST_CASE("an existing backup file is replaced", "[db]") {
    const temp_directory temp;

    const std::filesystem::path destination = temp.file("copy.db");
    {
        std::ofstream junk(destination, std::ios::binary);
        junk << "not a database";
    }

    database db{std::filesystem::path(database::in_memory)};
    seed(db, 2);

    latibot::db::backup_to_file(db, destination);

    CHECK(passes_integrity_check(destination));
    CHECK(row_count(destination) == 2);
}

TEST_CASE("a backup taken while other threads write is consistent", "[db]") {
    // The single-connection lock (plan v4 §5.2) is what makes this safe: the
    // backup holds the connection, so no write lands mid-copy.
    const temp_directory temp;

    database db{temp.file("bot.db")};
    seed(db, 0);

    std::atomic<bool> stop{false};
    std::atomic<int> written{0};
    std::thread writer([&db, &stop, &written] {
        while (!stop.load()) {
            const int next = written.fetch_add(1);
            db.prepare("INSERT INTO guild_settings (guild_id, key, value) VALUES (?, ?, ?)", 1,
                       "concurrent-" + std::to_string(next), "v")
                .run();
        }
    });

    const std::filesystem::path destination = temp.file("copy.db");
    latibot::db::backup_to_file(db, destination);

    stop.store(true);
    writer.join();

    CHECK(passes_integrity_check(destination));
    // The copy holds some prefix of the writes, and is never corrupt.
    const int copied = row_count(destination);
    CHECK(copied >= 0);
    CHECK(copied <= written.load());
}

TEST_CASE("rotation keeps the newest backups", "[db]") {
    const temp_directory temp;

    database db{std::filesystem::path(database::in_memory)};
    seed(db, 1);

    std::vector<std::filesystem::path> created;
    for (int minute = 1; minute <= 5; ++minute) {
        created.push_back(
            latibot::db::create_backup(db, temp.path(), "bot", /*keep=*/3, stamp(minute)));
    }

    const auto remaining = latibot::db::list_backups(temp.path(), "bot");
    REQUIRE(remaining.size() == 3);

    // The three newest survive, in chronological order.
    CHECK(remaining[0] == created[2]);
    CHECK(remaining[1] == created[3]);
    CHECK(remaining[2] == created[4]);
    CHECK_FALSE(std::filesystem::exists(created[0]));
    CHECK_FALSE(std::filesystem::exists(created[1]));
}

TEST_CASE("rotation ignores unrelated files", "[db]") {
    const temp_directory temp;

    {
        std::ofstream other(temp.file("notes.txt"));
        other << "keep me";
    }
    {
        std::ofstream other(temp.file("other-20240101-000000.db"));
        other << "different prefix";
    }

    database db{std::filesystem::path(database::in_memory)};
    seed(db, 1);
    latibot::db::create_backup(db, temp.path(), "bot", /*keep=*/1, stamp(1));
    latibot::db::create_backup(db, temp.path(), "bot", /*keep=*/1, stamp(2));

    CHECK(latibot::db::list_backups(temp.path(), "bot").size() == 1);
    CHECK(std::filesystem::exists(temp.file("notes.txt")));
    CHECK(std::filesystem::exists(temp.file("other-20240101-000000.db")));
}

TEST_CASE("backup file names carry a sortable UTC timestamp", "[db]") {
    const temp_directory temp;

    database db{std::filesystem::path(database::in_memory)};
    seed(db, 1);

    const auto path = latibot::db::create_backup(db, temp.path(), "bot", /*keep=*/5, stamp(90));

    // 90 minutes after the epoch: 1970-01-01 01:30:00 UTC.
    CHECK(path.filename().string() == "bot-19700101-013000.db");
}
