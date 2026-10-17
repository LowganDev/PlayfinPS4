#pragma once

#include <yyjson.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Safe access to server JSON: a missing or mistyped field gives its default, never a crash.
namespace playfin::jellyfin::json {

constexpr std::size_t kMaxString = 64 * 1024;
constexpr std::size_t kMaxArray = 10000;

class Document {
public:
    static std::optional<Document> parse(std::string_view text);

    Document(Document&& other) noexcept : doc_(std::exchange(other.doc_, nullptr)) {}
    Document& operator=(Document&& other) noexcept {
        if (this != &other) {
            reset();
            doc_ = std::exchange(other.doc_, nullptr);
        }
        return *this;
    }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    ~Document() { reset(); }

    yyjson_val* root() const { return yyjson_doc_get_root(doc_); }

private:
    explicit Document(yyjson_doc* doc) : doc_(doc) {}
    void reset() {
        if (doc_ != nullptr) {
            yyjson_doc_free(doc_);
            doc_ = nullptr;
        }
    }

    yyjson_doc* doc_ = nullptr;
};

yyjson_val* field(yyjson_val* object, const char* key);  // nullptr unless `object` is an object with that key
std::string textOf(yyjson_val* value);                   // "" unless a string; capped at kMaxString
std::string string(yyjson_val* object, const char* key);
std::int64_t integer(yyjson_val* object, const char* key, std::int64_t fallback = 0);  // int, uint or real
double number(yyjson_val* object, const char* key, double fallback = 0);
bool boolean(yyjson_val* object, const char* key, bool fallback = false);
yyjson_val* object(yyjson_val* parent, const char* key);  // nullptr unless an object
yyjson_val* array(yyjson_val* parent, const char* key);   // nullptr unless an array
std::vector<std::string> strings(yyjson_val* parent, const char* key);  // string entries only, at most kMaxArray

// {"key":"value",...} with proper escaping
std::string writeObject(std::initializer_list<std::pair<const char*, std::string>> fields);

// Nested JSON built in order: object()/array() open a container (the root, an array element, or the value of the
// last key), end() closes it. Strings are escaped; raw() inserts already-serialised JSON.
class Writer {
public:
    Writer();
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    Writer& object();
    Writer& array();
    Writer& end();
    Writer& key(const char* name);
    Writer& value(const std::string& text);
    Writer& value(const char* text) { return value(std::string(text)); }
    Writer& value(std::int64_t number);
    Writer& value(int number) { return value(static_cast<std::int64_t>(number)); }
    Writer& value(bool flag);
    Writer& raw(const std::string& json);
    std::string str() const;

private:
    void add(yyjson_mut_val* value);

    yyjson_mut_doc* doc_;
    std::vector<yyjson_mut_val*> open_;
    std::string key_;
};

}  // namespace playfin::jellyfin::json
