#include "roms/RomTests.h"

#include <nlohmann/json.hpp>

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
        list.tests.push_back(std::move(t));
    }
    return list;
}

} // namespace roms
