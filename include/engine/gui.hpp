#pragma once
#include "engine/math.hpp"
#include "engine/texture.hpp"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine {
struct GuiRect {
    float x=0,y=0,width=0,height=0;
    bool contains(Vec2 p) const;
    bool empty() const { return width<=0 || height<=0; }
};
GuiRect intersect(GuiRect a,GuiRect b);
struct GuiColor { float r=1,g=1,b=1,a=1; };
struct GuiStyle {
    GuiColor background{0.055f,0.08f,0.12f,0.96f};
    GuiColor foreground{0.9f,0.94f,1,1};
    GuiColor border{0.18f,0.25f,0.34f,1};
    GuiColor accent{0.05f,0.5f,0.65f,1};
};
struct VerticalLayout { float gap=8; };
struct HorizontalLayout { float gap=8; };
struct WrapLayout { float gap=8,rowGap=8; };
enum class GuiLayout { Absolute, Vertical, Horizontal, Wrap };
enum class GuiAnchor { TopLeft, TopRight, BottomLeft, BottomRight, Center };
struct GuiVertex { Vec2 position,uv; GuiColor color; };
struct GuiDrawCommand {
    std::shared_ptr<const Texture> texture;
    GuiRect clip;
    std::uint32_t firstIndex=0,indexCount=0;
};
struct GuiDrawList {
    std::vector<GuiVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<GuiDrawCommand> commands;
    void clear();
    void quad(GuiRect bounds,GuiRect clip,GuiColor color,
        std::shared_ptr<const Texture> texture={},GuiRect uv={0,0,1,1});
};
// Fonts are trusted application assets, not user uploads. UTF-8 strings; glyphs
// are rasterized lazily into immutable atlas snapshots shared by draw commands.
class GuiFont {
public:
    struct Glyph { float advance=0; GuiRect bounds,uv; std::size_t page=0; };
    static std::shared_ptr<GuiFont> loadTrustedTrueType(std::span<const std::byte> bytes,float pixelHeight=20);
    ~GuiFont();
    GuiFont(const GuiFont&)=delete;
    GuiFont& operator=(const GuiFont&)=delete;
    const Glyph& glyph(std::uint32_t codepoint);
    float measure(std::string_view text);
    float lineHeight() const;
    float ascent() const;
    std::size_t byteAt(std::string_view text,float x);
    std::shared_ptr<const Texture> atlas(std::size_t page);
private:
    GuiFont();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class GuiSystem;
class Widget {
public:
    virtual ~Widget()=default;
    Widget(const Widget&)=delete;
    Widget& operator=(const Widget&)=delete;
    template<class T,class... Args> T& add(Args&&... args) {
        auto child=std::make_unique<T>(std::forward<Args>(args)...);
        child->parent_=this; auto& result=*child;
        children_.push_back(std::move(child)); return result;
    }
    void setPosition(Vec2 value);
    void setSize(Vec2 value);
    void setPadding(float value);
    void setAnchor(GuiAnchor value) { anchor_=value; }
    void setLayout(VerticalLayout value);
    void setLayout(HorizontalLayout value);
    void setLayout(WrapLayout value);
    void setStretch(bool width,bool height=false) { stretchWidth_=width;stretchHeight_=height; }
    void setAccessibleName(std::string value);
    void setAccessibleDescription(std::string value);
    void setAccessibilityHidden(bool value) { accessibilityHidden_=value; }
    void setLiveRegion(bool value) { liveRegion_=value; }
    std::uint64_t id() const { return id_; }
    void setAbsoluteLayout() { layout_=GuiLayout::Absolute; }
    void setVisible(bool value) { visible_=value; }
    void setEnabled(bool value) { enabled_=value; }
    void setClipChildren(bool value) { clipChildren_=value; }
    bool visible() const { return visible_; }
    bool enabled() const { return enabled_; }
    bool active() const;
    const GuiRect& bounds() const { return bounds_; }
    const GuiRect& clip() const { return clip_; }
    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }
    GuiStyle style;
protected:
    Widget();
    virtual bool interactive() const { return false; }
    virtual bool focusable() const { return false; }
    virtual std::string_view displayedText() const { return {}; }
    friend class GuiSystem;
    Widget* parent_=nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    Vec2 position_{},size_{180,32};
    float padding_=0,gap_=0,rowGap_=0;
    std::uint64_t id_=0;
    std::string accessibleName_,accessibleDescription_;
    bool stretchWidth_=false,stretchHeight_=false,accessibilityHidden_=false,liveRegion_=false;
    GuiAnchor anchor_=GuiAnchor::TopLeft;
    GuiLayout layout_=GuiLayout::Absolute;
    bool visible_=true,enabled_=true,clipChildren_=true;
    GuiRect bounds_,clip_;
};
class Panel : public Widget {
public: Panel() { setSize({300,180}); }
protected: bool interactive() const override { return true; }
};
// Vertical scrolling in design units. Child clipping is always enforced.
class ScrollPanel : public Panel {
public:
    void setScrollOffset(float value);
    float scrollOffset() const { return offset_; }
    float maxScrollOffset() const { return maximum_; }
protected: bool focusable() const override { return true; }
private:
    float offset_=0,maximum_=0;
    friend class GuiSystem;
};
class TextLabel : public Widget {
public:
    explicit TextLabel(std::string text={}) { setText(std::move(text)); }
    void setText(std::string value);
    const std::string& text() const { return text_; }
    void setWrap(bool value,bool autoHeight=true) { wrap_=value;autoHeight_=autoHeight; }
    std::size_t lineCount() const { return lines_.size(); }
protected: std::string_view displayedText() const override { return text_; }
private:
    std::string text_;
    bool wrap_=false,autoHeight_=true;
    std::vector<std::string> lines_;
    friend class GuiSystem;
};
class Image : public Widget {
public:
    explicit Image(std::shared_ptr<const Texture> texture={}) : texture_(std::move(texture)) { style.foreground={1,1,1,1}; }
    void setTexture(std::shared_ptr<const Texture> value) { texture_=std::move(value); }
    const std::shared_ptr<const Texture>& texture() const { return texture_; }
private: std::shared_ptr<const Texture> texture_;
};
class Button : public Widget {
public:
    explicit Button(std::string text={}) { setText(std::move(text));setSize({180,38}); }
    void setText(std::string value);
    const std::string& text() const { return text_; }
    void onClick(std::function<void()> callback) { click_=std::move(callback); }
protected:
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    std::string_view displayedText() const override { return text_; }
private:
    std::string text_; std::function<void()> click_;
    friend class GuiSystem;
};
class TextInput : public Widget {
public:
    TextInput() { setSize({240,38}); }
    void setText(std::string value);
    const std::string& text() const { return text_; }
    void setPlaceholder(std::string value);
    const std::string& placeholder() const { return placeholder_; }
    void setSelection(std::size_t start,std::size_t end,bool backward=false);
    std::size_t selectionStart() const { return selectionStart_; }
    std::size_t selectionEnd() const { return selectionEnd_; }
    bool selectionBackward() const { return selectionBackward_; }
    std::uint32_t textRevision() const { return textRevision_; }
    bool composing() const { return composing_; }
    void onChange(std::function<void(const std::string&)> callback) { change_=std::move(callback); }
    void onSubmit(std::function<void()> callback) { submit_=std::move(callback); }
protected:
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    std::string_view displayedText() const override { return text_.empty() ? placeholder_ : text_; }
private:
    std::string text_,placeholder_;
    std::size_t selectionStart_=0,selectionEnd_=0;
    float scroll_=0;
    bool composing_=false;
    bool selectionBackward_=false;
    std::uint32_t textRevision_=0;
    std::function<void(const std::string&)> change_;
    std::function<void()> submit_;
    friend class GuiSystem;
};
enum class GuiPointer { Move, Down, Up, Cancel };
enum class GuiKey { Tab, Enter, Space, Escape, Up, Down, PageUp, PageDown, Home, End };
enum class GuiRole { Group, Text, Image, Button, Textbox, ScrollRegion };
struct GuiAccessibleNode {
    std::uint64_t id=0;
    GuiRole role=GuiRole::Group;
    std::string name,description,value;
    GuiRect bounds;
    bool disabled=false,focused=false,live=false;
};
class GuiSystem {
public:
    Widget& root() { return root_; }
    void setFont(std::shared_ptr<GuiFont> font) { font_=std::move(font); }
    const std::shared_ptr<GuiFont>& font() const { return font_; }
    void layout(Vec2 logicalSize);
    void setScale(float scale);
    float scale() const { return scale_; }
    const GuiDrawList& draw(double seconds);
    bool pointer(GuiPointer action,Vec2 position,bool extendSelection=false);
    bool wheel(Vec2 position) const;
    bool wheel(Vec2 position,Vec2 delta);
    std::vector<GuiAccessibleNode> accessibilityNodes() const;
    bool focusWidget(std::uint64_t id); // IDs are never reused; inactive/stale IDs fail.
    bool activateWidget(std::uint64_t id);
    bool key(GuiKey key,bool shift=false,bool repeat=false);
    void editText(std::string value,std::size_t start,std::size_t end,bool composing=false,bool backward=false);
    void blur();
    Widget* focused() const { return focus_ && focus_->active() ? focus_ : nullptr; }
    TextInput* focusedInput() const;
    bool capturesPointer() const { return (captured_ && captured_->active()) || (scrollCapture_ && scrollCapture_->active()); }
private:
    Widget root_;
    std::shared_ptr<GuiFont> font_;
    GuiDrawList draw_;
    Widget* hover_=nullptr;
    Widget* focus_=nullptr;
    Widget* captured_=nullptr;
    std::size_t selectionAnchor_=0;
    double caretEpoch_=0,lastTime_=0;
    float scale_=1;
    Vec2 logicalSize_{},pointerStart_{},pointerLast_{};
    ScrollPanel* scrollCapture_=nullptr;
    bool scrollDragging_=false;
    void arrange(Widget& widget,GuiRect bounds,GuiRect clip,bool active);
    Widget* hit(Widget& widget,Vec2 point) const;
    void paint(Widget& widget,double seconds);
    void text(std::string_view value,Vec2 position,GuiRect clip,GuiColor color);
    void activate(Button& button);
    bool reachable(const Widget& widget) const;
    void reveal(Widget& widget);
    void resetCaret() { caretEpoch_=lastTime_; }
};
} // namespace engine
