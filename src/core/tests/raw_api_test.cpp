#include "core/discord/raw_api.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using latibot::discord::build_endpoint;

TEST_CASE("a bare path gets the API version prefix", "[discord]") {
    CHECK(build_endpoint("/channels/1/messages") == "/api/v10/channels/1/messages");
}

TEST_CASE("a missing leading slash is added", "[discord]") {
    CHECK(build_endpoint("channels/1/messages") == "/api/v10/channels/1/messages");
}

TEST_CASE("a path that already names the API version is left alone", "[discord]") {
    CHECK(build_endpoint("/api/v10/channels/1") == "/api/v10/channels/1");
}

TEST_CASE("trailing slashes are trimmed", "[discord]") {
    // DPP only appends "/" plus parameters when there are parameters, so a
    // trailing slash here would reach Discord as part of the route.
    CHECK(build_endpoint("/channels/1/") == "/api/v10/channels/1");
    CHECK(build_endpoint("/channels/1///") == "/api/v10/channels/1");
}

TEST_CASE("an empty path becomes the API root", "[discord]") {
    CHECK(build_endpoint("") == "/api/v10");
}
