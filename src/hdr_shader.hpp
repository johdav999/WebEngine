#pragma once
#include <string>
namespace engine {
inline std::string hdrShader(const char* source,bool enabled) {
    return std::string(enabled?"const hdrOutput=true;\n":"const hdrOutput=false;\n")+
        "fn hdrEncode(c:vec3f)->vec3f {if(hdrOutput){return c;}return pow(max(c,vec3f(0)),vec3f(1.0/2.2));}\n"+source;
}
}
