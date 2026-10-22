#include "counter/counter.hpp"
#include <iostream>
int main() {
    counter_example::Demo d;d.add(0);d.add(0);d.add(1);
    if(d.first->counter.value!=2||d.second->counter.value!=1||!d.rejectsBrokenReplacement())return 1;
    d.reset(0);if(d.first->counter.value||d.second->counter.value!=1)return 2;
    auto survivor=std::move(d.second);d.modules.replace({});
    survivor->counter.add();if(survivor->counter.value!=2)return 3;
    std::cout<<"External module consumer: independent sessions, retained provider, failed replacement PASS\n";
}
