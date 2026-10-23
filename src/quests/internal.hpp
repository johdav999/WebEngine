#pragma once
#include "engine/quests/runtime.hpp"
#include <algorithm>
#include <limits>

namespace engine::quests::detail {
[[noreturn]] inline void fail(ErrorCode c, const std::string &m) { throw Error(c, m); }
inline void require(bool ok, ErrorCode c, const std::string &m) {
    if (!ok)
        fail(c, m);
}
inline void bound(std::size_t n, std::size_t max, const char *what) {
    require(n <= max, ErrorCode::Limit, what);
}
inline void key(const std::string &s) {
    require(!s.empty() && s.size() <= Limits::keyBytes, ErrorCode::InvalidContent,
            "Invalid quest key length");
    for (unsigned char c : s)
        require((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-',
                ErrorCode::InvalidContent, "Invalid quest key: " + s);
}
inline void value(const Value &v) {
    if (auto s = std::get_if<std::string>(&v))
        bound(s->size(), Limits::stringBytes, "String value too long");
}
inline void fields(const Fields &f, std::size_t max = Limits::fields) {
    bound(f.size(), max, "Too many fields");
    for (const auto &[k, v] : f) {
        key(k);
        value(v);
    }
}
inline void typed(const Fields &values, const Fields &schema, bool partial) {
    if (!partial)
        require(values.size() == schema.size(), ErrorCode::TypeMismatch, "Field count mismatch");
    for (const auto &[k, v] : values) {
        auto i = schema.find(k);
        require(i != schema.end() && i->second.index() == v.index(), ErrorCode::TypeMismatch,
                "Unknown/wrong field type: " + k);
        value(v);
    }
}
inline std::uint64_t increment(std::uint64_t n) {
    require(n < std::numeric_limits<std::uint64_t>::max() - 2, ErrorCode::Limit,
            "Sequence/revision exhausted");
    return n + 1;
}
inline bool unlocked(const Snapshot &s, const QuestDefinition &q) {
    return std::all_of(q.prerequisites.begin(), q.prerequisites.end(),
                       [&](const auto &p) { return s.quests.at(p).earned; });
}
inline std::string receipt(const QuestDefinition &q, const EffectDefinition &e) { return q.id + "/" + e.key; }
void validateDefinition(const CatalogDefinition &, const Bindings *);
void validateCall(const BindingCall &, const BindingSchema &, const CatalogDefinition &);
std::string fingerprint(const CatalogDefinition &);
} // namespace engine::quests::detail
