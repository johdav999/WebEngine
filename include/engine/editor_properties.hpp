#pragma once
#include "engine/node.hpp"
#include <variant>
#include <limits>

namespace engine {
using EditorValue=std::variant<double,bool,std::string>;
enum class EditorPropertyType { Number, Boolean, Text, Choice };
struct EditorPropertyInfo {
    std::string key,label,group;
    EditorPropertyType type=EditorPropertyType::Number;
    double minimum=-1e9,maximum=1e9,step=0.1;
    std::vector<std::string> choices;
    EditorValue value=0.0;
};
// Application registrations are trusted code. Captured dependencies must outlive
// the registry and sessions using it. Getters must not mutate scene state.
struct EditorPropertyDefinition {
    EditorPropertyInfo info;
    std::function<bool(const Node&)> accepts;
    std::function<EditorValue(const Node&)> get;
    std::function<void(Node&,const EditorValue&)> set;
};
class EditorPropertyRegistry {
public:
    void add(EditorPropertyDefinition definition);
    const EditorPropertyDefinition* find(std::string_view key,const Node& node) const;
    std::vector<EditorPropertyInfo> describe(const Node& node) const;
    static void validate(const EditorPropertyInfo& info,const EditorValue& value);
    static std::shared_ptr<const EditorPropertyRegistry> builtins();
private:
    std::vector<EditorPropertyDefinition> entries_;
};
} // namespace engine
