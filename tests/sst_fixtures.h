#pragma once

#include <doctest/doctest.h>

#include <string>
#include <string_view>

namespace sst_fixtures {

// One real-format test: NOP at 0x0100.
inline const std::string kNop =
    R"([{"name":"00 0000",)"
    R"("initial":{"pc":256,"sp":65534,"a":1,"b":2,"c":3,"d":4,"e":5,"f":176,"h":6,"l":7,"ime":1,"ie":0,"ram":[[256,0]]},)"
    R"("final":{"pc":257,"sp":65534,"a":1,"b":2,"c":3,"d":4,"e":5,"f":176,"h":6,"l":7,"ime":1,"ram":[[256,0]]},)"
    R"("cycles":[[256,0,"r-m"]]}])";

// `text` with the first occurrence of `from` replaced by `to`. Each test
// breaks the expected result in exactly one place this way.
inline std::string with(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

} // namespace sst_fixtures
