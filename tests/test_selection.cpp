#include <doctest/doctest.h>

#include "sst/Selection.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {
const std::vector<std::string> kAvailable{"00", "01", "40", "41", "42", "cb 00", "cb 01"};
}

TEST_CASE("ranges expand to the files that exist, in order") {
    CHECK(sst::expandSelection("40-42", kAvailable) == std::vector<std::string>{"40", "41", "42"});
    CHECK(sst::expandSelection("00-41", kAvailable) == std::vector<std::string>{"00", "01", "40", "41"});
    CHECK(sst::expandSelection("cb00-cb01", kAvailable) == std::vector<std::string>{"cb 00", "cb 01"});
}

TEST_CASE("single items accept 'cbXX' and 'cb XX', and duplicates are dropped") {
    CHECK(sst::expandSelection("00, cb 01,cb01,00", kAvailable) == std::vector<std::string>{"00", "cb 01"});
    CHECK(sst::expandSelection("40,41", kAvailable) == std::vector<std::string>{"40", "41"});
}

TEST_CASE("bad selections throw") {
    CHECK_THROWS_AS(sst::expandSelection("d3", kAvailable), std::runtime_error);     // no such file
    CHECK_THROWS_AS(sst::expandSelection("40-cb01", kAvailable), std::runtime_error); // mixed range
    CHECK_THROWS_AS(sst::expandSelection("42-40", kAvailable), std::runtime_error);   // backwards
    CHECK_THROWS_AS(sst::expandSelection("4", kAvailable), std::runtime_error);       // not two hex digits
}
