#pragma once
#include "engine/animation_library.hpp"
namespace engine {
class AnimationLibraryEditor {
public:
    explicit AnimationLibraryEditor(std::shared_ptr<const AnimationLibrary>,std::size_t historyLimit=32);
    const std::shared_ptr<const AnimationLibrary>& library()const{return history_[cursor_];}
    void edit(std::shared_ptr<const AnimationLibrary>);
    void open(std::span<const std::byte>);
    bool undo();bool redo();
    std::size_t undoCount()const{return cursor_;}
    std::size_t redoCount()const{return history_.size()-cursor_-1;}
    bool dirty()const{return library()!=saved_;}
    void markSaved(){saved_=library();}
private:
    std::vector<std::shared_ptr<const AnimationLibrary>> history_;
    std::shared_ptr<const AnimationLibrary> saved_;
    std::size_t cursor_=0,limit_;
};
}
