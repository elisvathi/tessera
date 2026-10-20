#pragma once

#include <jansson.h>

#include <cstdint>
#include <string>

/// A thin layer over jansson: typed reads with fallbacks, so loading code never has to check every field.
/// jansson is the JSON library Rack itself uses; it has no Rack dependency, so the model stays testable.
namespace tessera::json {

/// Owns one reference to a json_t and releases it when it goes out of scope.
class Ref {
  public:
    explicit Ref(json_t* j = nullptr) : j(j) {}
    ~Ref() { json_decref(j); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;

    json_t* get() const { return j; }
    json_t* release() {
        json_t* out = j;
        j = nullptr;
        return out;
    }

  private:
    json_t* j;
};

inline json_t* mut(const json_t* j) { return const_cast<json_t*>(j); }

inline const json_t* get(const json_t* obj, const char* key) { return json_object_get(obj, key); }

inline double asNumber(const json_t* j, double fallback) { return json_is_number(j) ? json_number_value(j) : fallback; }
inline int64_t asInt(const json_t* j, int64_t fallback) {
    if (json_is_integer(j))
        return json_integer_value(j);
    return json_is_real(j) ? int64_t(json_real_value(j)) : fallback;
}
inline bool asBool(const json_t* j, bool fallback) { return json_is_boolean(j) ? json_is_true(j) : fallback; }
inline std::string asString(const json_t* j, const std::string& fallback) {
    return json_is_string(j) ? std::string(json_string_value(j)) : fallback;
}

inline double getNumber(const json_t* obj, const char* key, double fallback) {
    return asNumber(get(obj, key), fallback);
}
inline float getFloat(const json_t* obj, const char* key, float fallback) {
    return float(asNumber(get(obj, key), fallback));
}
inline int64_t getInt(const json_t* obj, const char* key, int64_t fallback) { return asInt(get(obj, key), fallback); }
inline bool getBool(const json_t* obj, const char* key, bool fallback) { return asBool(get(obj, key), fallback); }
inline std::string getString(const json_t* obj, const char* key, const std::string& fallback) {
    return asString(get(obj, key), fallback);
}

inline double numberAt(const json_t* arr, size_t i, double fallback) {
    return asNumber(json_array_get(arr, i), fallback);
}
inline int64_t intAt(const json_t* arr, size_t i, int64_t fallback) { return asInt(json_array_get(arr, i), fallback); }

/// Sets obj[key] = value, taking ownership of value.
inline void set(json_t* obj, const char* key, json_t* value) { json_object_set_new(obj, key, value); }
inline void setInt(json_t* obj, const char* key, int64_t v) { set(obj, key, json_integer(v)); }
inline void setNumber(json_t* obj, const char* key, double v) { set(obj, key, json_real(v)); }
inline void setBool(json_t* obj, const char* key, bool v) { set(obj, key, json_boolean(v)); }
inline void setString(json_t* obj, const char* key, const std::string& v) { set(obj, key, json_string(v.c_str())); }

/// Iterates a JSON array, skipping it entirely when `arr` is not an array.
template <typename F>
void forEach(const json_t* arr, F f) {
    if (!json_is_array(arr))
        return;
    for (size_t i = 0; i < json_array_size(arr); i++)
        f(json_array_get(arr, i));
}

} // namespace tessera::json
