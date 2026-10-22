#include "engine/gui.hpp"
#include <iostream>
#include <stdexcept>
using namespace engine;
int main() {
    try {
        GuiSystem gui;gui.setScale(1.5f);
        auto& old=gui.root().add<Button>("Old");old.setSize({100,40});
        const auto id=old.id();int clicks=0;old.onClick([&]{++clicks;});
        gui.layout({300,200});gui.focusWidget(id);gui.pointer(GuiPointer::Down,{10,10});
        if(!gui.capturesPointer())throw std::runtime_error("Missing test capture");
        gui.clear();
        if(gui.focused()||gui.capturesPointer()||!gui.root().children().empty()||gui.scale()!=1.5f)
            throw std::runtime_error("Clear retained widget state or lost host scaling");
        if(gui.activateWidget(id)||gui.focusWidget(id))throw std::runtime_error("Stale widget survived");
        gui.pointer(GuiPointer::Up,{10,10});
        if(clicks)throw std::runtime_error("Clear activated old callback");
        auto& next=gui.root().add<Button>("Next");next.setSize({100,40});next.onClick([&]{++clicks;});
        gui.layout({300,200});gui.pointer(GuiPointer::Down,{10,10});gui.pointer(GuiPointer::Up,{10,10});
        if(clicks!=1)throw std::runtime_error("Replacement widget failed");
        gui.clear();gui.clear();std::cout<<"GUI clear capture, focus, stale ID and rebuild checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
