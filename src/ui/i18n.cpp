#include "ui/i18n.hpp"

#include <yyjson.h>

#include <fstream>
#include <sstream>

namespace playfin::ui {

namespace {

std::map<std::string, std::string, std::less<>> parseTable(std::string_view json) {
    std::map<std::string, std::string, std::less<>> table;
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (doc == nullptr) {
        return table;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        yyjson_obj_iter iter = yyjson_obj_iter_with(root);
        yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            if (yyjson_is_str(value)) {
                table.emplace(std::string(yyjson_get_str(key), yyjson_get_len(key)),
                              std::string(yyjson_get_str(value), yyjson_get_len(value)));
            }
        }
    }
    yyjson_doc_free(doc);
    return table;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

std::string format(std::string text, std::initializer_list<std::string_view> args) {
    std::size_t index = 0;
    for (std::string_view arg : args) {
        const std::string placeholder = "{" + std::to_string(index++) + "}";
        std::size_t at = 0;
        while ((at = text.find(placeholder, at)) != std::string::npos) {
            text.replace(at, placeholder.size(), arg);
            at += arg.size();
        }
    }
    return text;
}

std::string baseLanguage(std::string_view tag) { return std::string(tag.substr(0, tag.find('-'))); }

}  // namespace

const char* pluralCategory(std::string_view language, long long n) {
    const std::string base = baseLanguage(language);
    const long long abs = n < 0 ? -n : n;
    if (base == "ja" || base == "ko" || base == "zh" || base == "vi") {
        return "other";
    }
    if (base == "ru" || base == "uk") {
        const long long mod10 = abs % 10;
        const long long mod100 = abs % 100;
        if (mod10 == 1 && mod100 != 11) {
            return "one";
        }
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) {
            return "few";
        }
        return "many";
    }
    if (base == "cs") {
        if (abs == 1) {
            return "one";
        }
        return (abs >= 2 && abs <= 4) ? "few" : "other";
    }
    if (base == "fr" || base == "pt") {
        return abs <= 1 ? "one" : "other";
    }
    return abs == 1 ? "one" : "other";
}

bool Translator::load(const std::string& dir, const std::string& language) {
    std::string english;
    if (!readFile(dir + "/en.json", english)) {
        return false;
    }
    std::string local;
    std::string resolved;
    for (const std::string& candidate : {language, baseLanguage(language)}) {
        if (!candidate.empty() && candidate != "en" && readFile(dir + "/" + candidate + ".json", local)) {
            resolved = candidate;
            break;
        }
    }
    loadStrings(english, resolved.empty() ? std::string_view("{}") : std::string_view(local), resolved);
    return true;
}

void Translator::loadStrings(std::string_view englishJson, std::string_view localJson, const std::string& language) {
    english_ = parseTable(englishJson);
    local_ = parseTable(localJson);
    language_ = language;
}

const std::string* Translator::find(std::string_view key) const {
    if (auto it = local_.find(key); it != local_.end()) {
        return &it->second;
    }
    if (auto it = english_.find(key); it != english_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string Translator::tr(std::string_view key, std::initializer_list<std::string_view> args) const {
    const std::string* text = find(key);
    return format(text != nullptr ? *text : std::string(key), args);
}

std::string Translator::plural(std::string_view key, long long count,
                               std::initializer_list<std::string_view> args) const {
    const std::string category = pluralCategory(language_.empty() ? "en" : language_, count);
    const std::string base(key);
    const std::string* text = find(base + "." + category);
    if (text == nullptr && category != "other") {
        text = find(base + ".other");
    }
    const std::string number = std::to_string(count);
    std::string out = text != nullptr ? *text : base;
    out = format(out, {number});
    std::size_t index = 1;
    for (std::string_view arg : args) {
        const std::string placeholder = "{" + std::to_string(index++) + "}";
        std::size_t at = 0;
        while ((at = out.find(placeholder, at)) != std::string::npos) {
            out.replace(at, placeholder.size(), arg);
            at += arg.size();
        }
    }
    return out;
}

std::vector<std::string> Translator::missingKeys() const {
    std::vector<std::string> missing;
    for (const auto& entry : english_) {
        if (local_.find(entry.first) == local_.end()) {
            missing.push_back(entry.first);
        }
    }
    return missing;
}

Translator& translator() {
    static Translator instance;
    return instance;
}

std::string tr(std::string_view key, std::initializer_list<std::string_view> args) { return translator().tr(key, args); }

}  // namespace playfin::ui
