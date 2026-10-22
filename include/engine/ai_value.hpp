#pragma once
#include "engine/math.hpp"
#include <string>
#include <variant>
#include <vector>
#include <iosfwd>
namespace engine {
enum class AiParameterType { Boolean,Number,Vector,Actor };
using AiValue=std::variant<bool,double,Vec3,std::string>;
struct AiParameter {std::string key;AiParameterType type;AiValue initial;double minimum=-10000,maximum=10000;};
// Shared graph/controller data validation; actor values use stable saved IDs.
void validateAiValue(const AiParameter&,const AiValue&);
void validateAiSchema(const std::vector<AiParameter>&,std::size_t limit=16);
void writeAiValue(std::ostream&,const AiValue&);
AiValue readAiValue(std::istream&);
}
