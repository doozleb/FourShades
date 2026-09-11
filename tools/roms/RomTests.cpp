#include "roms/RomTests.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace roms {

namespace {
using nlohmann::json;

const json& need(const json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) {
        throw std::runtime_error(std::string("tests.json: missing '") + key + "'");
    }
    return object.at(key);
}

Method methodFrom(const std::string& text) {
    if (text == "blargg") return Method::Blargg;
    if (text == "mooneye") return Method::Mooneye;
    if (text == "screenshot") return Method::Screenshot;
    throw std::runtime_error("tests.json: unknown method '" + text + "'");
}

bool startsWith(const std::string& text, std::string_view prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

// The method is fixed by where the ROM lives, as make_test_list.py assigns it.
Method methodForPath(const std::string& rom) {
    if (startsWith(rom, "blargg/")) return Method::Blargg;
    if (startsWith(rom, "mooneye/") && !startsWith(rom, "mooneye/manual-only/")) return Method::Mooneye;
    return Method::Screenshot;
}

// A limit that doesn't come from the formula is a hand-raised limit.
void checkEntry(const RomTest& t) {
    const double expected = std::max(2.0 * t.runtime, t.runtime + 5.0);
    if (std::abs(t.limitSeconds - expected) > 1e-9) {
        throw std::runtime_error("tests.json: " + t.name + ": limit_seconds is not max(2 x runtime, runtime + 5)");
    }
    if (t.method != methodForPath(t.rom)) {
        throw std::runtime_error("tests.json: " + t.name + ": method doesn't follow from the ROM's path");
    }
}
} // namespace

TestList parseTestList(std::string_view jsonText) {
    const json doc = json::parse(jsonText);
    TestList list;
    list.shootoutCommit = need(doc, "shootout_commit").get<std::string>();
    for (const json& item : need(doc, "tests")) {
        RomTest t;
        t.name = need(item, "name").get<std::string>();
        t.rom = need(item, "rom").get<std::string>();
        t.group = need(item, "group").get<std::string>();
        t.method = methodFrom(need(item, "method").get<std::string>());
        const json& informational = need(item, "informational");
        if (!informational.is_boolean()) {
            throw std::runtime_error("tests.json: 'informational' must be true or false for " + t.name);
        }
        t.informational = informational.get<bool>();
        t.runtime = need(item, "runtime").get<double>();
        t.limitSeconds = need(item, "limit_seconds").get<double>();
        t.references = need(item, "references").get<std::vector<std::string>>();
        checkEntry(t);
        list.tests.push_back(std::move(t));
    }
    return list;
}

} // namespace roms
