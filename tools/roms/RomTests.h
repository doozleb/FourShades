#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace roms {

enum class Method { Blargg, Mooneye, Screenshot };

struct RomTest {
    std::string name;
    std::string rom;   // path relative to the Shootout's testroms/
    std::string group;
    Method method = Method::Screenshot;
    double runtime = 0.0;
    double limitSeconds = 0.0;
    std::vector<std::string> references;
};

struct TestList {
    std::string shootoutCommit;
    std::vector<RomTest> tests;
};

// Parses tools/roms/tests.json. Throws std::runtime_error on anything missing
// or unknown.
TestList parseTestList(std::string_view jsonText);

} // namespace roms
