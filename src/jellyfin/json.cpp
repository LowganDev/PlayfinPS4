#include "jellyfin/json.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace playfin::jellyfin::json {

std::optional<Document> Document::parse(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
    if (doc == nullptr) {
        return std::nullopt;
    }
    return Document(doc);
}

yyjson_val* field(yyjson_val* object, const char* key) {
    return (object != nullptr && yyjson_is_obj(object)) ? yyjson_obj_get(object, key) : nullptr;
}

std::string textOf(yyjson_val* value) {
    if (!yyjson_is_str(value)) {
        return {};
    }
    const char* text = yyjson_get_str(value);
    std::size_t length = yyjson_get_len(value);
    if (length > kMaxString) {
        length = kMaxString;
        // do not cut a UTF-8 sequence: back up to the start of the character at the limit
        while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
            --length;
        }
    }
    return std::string(text, length);
}

std::string string(yyjson_val* object, const char* key) { return textOf(field(object, key)); }

std::int64_t integer(yyjson_val* object, const char* key, std::int64_t fallback) {
    yyjson_val* value = field(object, key);
    if (yyjson_is_sint(value)) {
        return yyjson_get_sint(value);
    }
    if (yyjson_is_uint(value)) {
        const std::uint64_t u = yyjson_get_uint(value);
        return u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ? fallback
                                                                                         : static_cast<std::int64_t>(u);
    }
    if (yyjson_is_real(value)) {
        const double d = yyjson_get_real(value);
        if (std::isfinite(d) && d > -9.2e18 && d < 9.2e18) {
            return static_cast<std::int64_t>(d);
        }
    }
    return fallback;
}

double number(yyjson_val* object, const char* key, double fallback) {
    yyjson_val* value = field(object, key);
    if (!yyjson_is_num(value)) {
        return fallback;
    }
    const double d = yyjson_get_num(value);
    return std::isfinite(d) ? d : fallback;
}

bool boolean(yyjson_val* object, const char* key, bool fallback) {
    yyjson_val* value = field(object, key);
    return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}

yyjson_val* object(yyjson_val* parent, const char* key) {
    yyjson_val* value = field(parent, key);
    return yyjson_is_obj(value) ? value : nullptr;
}

yyjson_val* array(yyjson_val* parent, const char* key) {
    yyjson_val* value = field(parent, key);
    return yyjson_is_arr(value) ? value : nullptr;
}

std::vector<std::string> strings(yyjson_val* parent, const char* key) {
    std::vector<std::string> out;
    yyjson_val* list = array(parent, key);
    if (list == nullptr) {
        return out;
    }
    yyjson_arr_iter iter = yyjson_arr_iter_with(list);
    yyjson_val* entry = nullptr;
    while ((entry = yyjson_arr_iter_next(&iter)) != nullptr && out.size() < kMaxArray) {
        if (yyjson_is_str(entry)) {
            out.push_back(textOf(entry));
        }
    }
    return out;
}

std::string writeObject(std::initializer_list<std::pair<const char*, std::string>> fields) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        return "{}";
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    for (const auto& entry : fields) {
        yyjson_mut_obj_add_strncpy(doc, root, entry.first, entry.second.data(), entry.second.size());
    }
    std::size_t length = 0;
    char* text = yyjson_mut_write(doc, 0, &length);
    std::string out = text != nullptr ? std::string(text, length) : "{}";
    std::free(text);
    yyjson_mut_doc_free(doc);
    return out;
}

Writer::Writer() : doc_(yyjson_mut_doc_new(nullptr)) {}

Writer::~Writer() {
    if (doc_ != nullptr) {
        yyjson_mut_doc_free(doc_);
    }
}

void Writer::add(yyjson_mut_val* value) {
    if (doc_ == nullptr || value == nullptr) {
        return;
    }
    if (open_.empty()) {
        yyjson_mut_doc_set_root(doc_, value);
    } else if (yyjson_mut_is_arr(open_.back())) {
        yyjson_mut_arr_append(open_.back(), value);
    } else {
        yyjson_mut_obj_add(open_.back(), yyjson_mut_strncpy(doc_, key_.data(), key_.size()), value);
    }
    key_.clear();
}

Writer& Writer::object() {
    yyjson_mut_val* value = doc_ != nullptr ? yyjson_mut_obj(doc_) : nullptr;
    add(value);
    if (value != nullptr) {
        open_.push_back(value);
    }
    return *this;
}

Writer& Writer::array() {
    yyjson_mut_val* value = doc_ != nullptr ? yyjson_mut_arr(doc_) : nullptr;
    add(value);
    if (value != nullptr) {
        open_.push_back(value);
    }
    return *this;
}

Writer& Writer::end() {
    if (!open_.empty()) {
        open_.pop_back();
    }
    return *this;
}

Writer& Writer::key(const char* name) {
    key_ = name;
    return *this;
}

Writer& Writer::value(const std::string& text) {
    add(doc_ != nullptr ? yyjson_mut_strncpy(doc_, text.data(), text.size()) : nullptr);
    return *this;
}

Writer& Writer::value(std::int64_t number) {
    add(doc_ != nullptr ? yyjson_mut_sint(doc_, number) : nullptr);
    return *this;
}

Writer& Writer::value(bool flag) {
    add(doc_ != nullptr ? yyjson_mut_bool(doc_, flag) : nullptr);
    return *this;
}

Writer& Writer::raw(const std::string& json) {
    add(doc_ != nullptr ? yyjson_mut_rawncpy(doc_, json.data(), json.size()) : nullptr);
    return *this;
}

std::string Writer::str() const {
    if (doc_ == nullptr) {
        return "{}";
    }
    std::size_t length = 0;
    char* text = yyjson_mut_write(doc_, 0, &length);
    std::string out = text != nullptr ? std::string(text, length) : "{}";
    std::free(text);
    return out;
}

}  // namespace playfin::jellyfin::json
