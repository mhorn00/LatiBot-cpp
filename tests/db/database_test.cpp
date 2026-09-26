#include "core/db/database.hpp"

#include "core/db/error.hpp"

#include "support/capture_log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

using latibot::db::database;
using latibot::db::db_error;
using latibot::db::transaction;

namespace {

/// `database` owns a mutex and is deliberately neither copyable nor movable,
/// so tests build it in place.
struct test_database {
    database db{std::filesystem::path(database::in_memory)};

    test_database() {
        db.execute(R"sql(
            CREATE TABLE things (
                id     INTEGER PRIMARY KEY,
                name   TEXT    NOT NULL UNIQUE,
                weight REAL,
                raw    BLOB,
                note   TEXT
            );
        )sql");
    }
};

} // namespace

TEST_CASE("opening an unwritable path reports the SQLite error", "[db]") {
    const std::filesystem::path missing = "no-such-directory-here/bot.db";
    REQUIRE_THROWS_AS(database{missing}, db_error);
}

TEST_CASE("values survive a bind and get round trip", "[db]") {
    test_database fixture;
    database& db = fixture.db;

    const std::vector<std::byte> raw{std::byte{0x00}, std::byte{0xFF}, std::byte{0x10}};

    {
        auto insert = db.prepare(
            "INSERT INTO things (id, name, weight, raw, note) "
            "VALUES (?, ?, ?, ?, ?)");
        insert.bind_all(std::uint64_t{1234567890123456789ULL}, "first", 2.5, std::span(raw), std::optional<std::string>{});
        insert.run();
    }

    auto query = db.prepare("SELECT id, name, weight, raw, note FROM things");
    REQUIRE(query.step());

    CHECK(query.get<std::uint64_t>(0) == 1234567890123456789ULL);
    CHECK(query.get<std::string>(1) == "first");
    CHECK(query.get<double>(2) == 2.5);
    CHECK(query.get<std::vector<std::byte>>(3) == raw);
    CHECK(query.is_null(4));
    CHECK(query.get<std::optional<std::string>>(4) == std::nullopt);

    CHECK_FALSE(query.step());
}

TEST_CASE("optional values bind as NULL or as the value", "[db]") {
    test_database fixture;
    database& db = fixture.db;

    auto insert = db.prepare("INSERT INTO things (name, note) VALUES (?, ?)");
    insert.bind_all("with-note", std::optional<std::string>{"hello"});
    insert.run();

    auto query = db.prepare("SELECT note FROM things WHERE name = ?", "with-note");
    REQUIRE(query.step());
    CHECK(query.get<std::optional<std::string>>(0) == std::optional<std::string>{"hello"});
}

TEST_CASE("a constraint violation throws with the SQLite code", "[db]") {
    test_database fixture;
    database& db = fixture.db;

    db.prepare("INSERT INTO things (name) VALUES (?)", "unique-name").run();

    auto duplicate = db.prepare("INSERT INTO things (name) VALUES (?)", "unique-name");
    try {
        duplicate.run();
        FAIL("expected a constraint violation");
    } catch (const db_error& error) {
        // SQLITE_CONSTRAINT is 19; extended codes add a suffix byte.
        CHECK((error.code() & 0xFF) == 19);
    }
}

TEST_CASE("malformed SQL is reported, not executed", "[db]") {
    test_database fixture;
    database& db = fixture.db;
    REQUIRE_THROWS_AS(db.prepare("SELECT FROM nowhere"), db_error);
    REQUIRE_THROWS_AS(db.execute("NOT SQL AT ALL"), db_error);
}

TEST_CASE("a transaction commits or rolls back", "[db]") {
    test_database fixture;
    database& db = fixture.db;

    SECTION("committed work is visible") {
        {
            transaction tx(db);
            db.prepare("INSERT INTO things (name) VALUES (?)", "kept").run();
            tx.commit();
        }
        auto count = db.prepare("SELECT COUNT(*) FROM things");
        REQUIRE(count.step());
        CHECK(count.get<int>(0) == 1);
    }

    SECTION("leaving the scope without committing rolls back") {
        {
            transaction tx(db);
            db.prepare("INSERT INTO things (name) VALUES (?)", "dropped").run();
        }
        auto count = db.prepare("SELECT COUNT(*) FROM things");
        REQUIRE(count.step());
        CHECK(count.get<int>(0) == 0);
    }

    SECTION("an explicit rollback discards the work") {
        {
            transaction tx(db);
            db.prepare("INSERT INTO things (name) VALUES (?)", "dropped").run();
            tx.rollback();
        }
        auto count = db.prepare("SELECT COUNT(*) FROM things");
        REQUIRE(count.step());
        CHECK(count.get<int>(0) == 0);
    }
}

TEST_CASE("snowflakes and times are stored as the integers they always were", "[db]") {
    // The stores used to cast by hand at every bind; the same integers must
    // still reach the table, or every existing database would read wrong.
    test_database fixture;
    database& db = fixture.db;
    db.execute("CREATE TABLE ids (id INTEGER, maybe INTEGER, at INTEGER)");

    const dpp::snowflake id{1234567890123456789ULL};
    const std::chrono::sys_seconds at{std::chrono::seconds{1'790'000'000}};
    db.prepare("INSERT INTO ids VALUES (?, ?, ?)", id, std::optional<dpp::snowflake>{}, at).run();

    auto raw = db.prepare("SELECT id, maybe, at FROM ids");
    REQUIRE(raw.step());
    CHECK(raw.get<std::int64_t>(0) == 1234567890123456789LL);
    CHECK(raw.is_null(1));
    CHECK(raw.get<std::int64_t>(2) == 1'790'000'000);
    CHECK(raw.get<dpp::snowflake>(0) == id);
    CHECK_FALSE(raw.get<std::optional<dpp::snowflake>>(1).has_value());
    CHECK(raw.get<std::chrono::sys_seconds>(2) == at);

    SECTION("a finer time is stored to the second") {
        db.execute("DELETE FROM ids");
        db.prepare("INSERT INTO ids (at) VALUES (?)", at + std::chrono::milliseconds{999}).run();
        auto rounded = db.prepare("SELECT at FROM ids");
        REQUIRE(rounded.step());
        CHECK(rounded.get<std::chrono::sys_seconds>(0) == at);
    }
}

TEST_CASE("a rollback that fails is logged rather than thrown", "[db]") {
    // Otherwise the first sign of it is every later write failing to start a
    // transaction, with nothing saying why.
    test_database fixture;
    const latibot::testing::capture_log log;
    {
        transaction tx(fixture.db);
        // Ended behind the transaction's back, so its own rollback fails.
        fixture.db.execute("COMMIT");
    }
    CHECK(log.contains(latibot::util::log_level::error, "could not roll back a transaction"));
}

TEST_CASE("last_insert_rowid and changes report the previous statement", "[db]") {
    test_database fixture;
    database& db = fixture.db;

    db.prepare("INSERT INTO things (name) VALUES (?)", "one").run();
    const std::int64_t first = db.last_insert_rowid();

    db.prepare("INSERT INTO things (name) VALUES (?)", "two").run();
    CHECK(db.last_insert_rowid() == first + 1);

    db.prepare("UPDATE things SET weight = ?", 1.0).run();
    CHECK(db.changes() == 2);
}

TEST_CASE("concurrent writers are serialized by the connection lock", "[db][threads]") {
    // The plan's concurrency model is one connection behind a mutex
    // (plan v4 §5.2). If that holds, parallel writers cannot corrupt or lose
    // rows, and none of them throws SQLITE_BUSY.
    test_database fixture;
    database& db = fixture.db;

    constexpr int thread_count = 4;
    constexpr int per_thread = 50;

    std::vector<std::thread> writers;
    writers.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        writers.emplace_back([&db, t] {
            for (int i = 0; i < per_thread; ++i) {
                db.prepare("INSERT INTO things (name) VALUES (?)", "t" + std::to_string(t) + "-" + std::to_string(i)).run();
            }
        });
    }
    for (std::thread& writer : writers) {
        writer.join();
    }

    auto count = db.prepare("SELECT COUNT(*) FROM things");
    REQUIRE(count.step());
    CHECK(count.get<int>(0) == thread_count * per_thread);
}
