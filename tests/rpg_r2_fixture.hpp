#pragma once
// Test-only canonical text, deliberately not a save format or runtime serializer.
inline constexpr const char* rpgR2Fixture =
"state 1 3 2\n"
"producer test.input 3\n"
"character 10:1 example.scholar xp=35 level=2\n"
"attribute example.insight 30 30\n"
"resource example.reserve 20 20 12\n"
"unlock example.first\n"
"unlock example.second\n"
"state 1 2 2\n"
"producer test.input 2\n"
"character 20:1 example.operator xp=11 level=2\n"
"attribute example.precision 600 600\n"
"resource example.charge 2500 2500 1950\n"
"unlock example.first\n"
"unlock example.second\n";
