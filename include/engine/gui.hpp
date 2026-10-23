#pragma once
#include "engine/math.hpp"
#include "engine/texture.hpp"
#include <functional>
#include <array>
#include <optional>
#include <limits>
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
    GuiColor focusRing{0.05f,0.5f,0.65f,1};
    GuiColor selection{0.08f,0.38f,0.6f,0.85f};
    GuiColor placeholder{0.495f,0.517f,0.55f,0.55f};
    float opacity=1;
};
// Assignment keeps the familiar widget.style.background={r,g,b,a} spelling.
// Unset fields inherit. Read effective colors from GuiPainter::style instead.
class GuiColorOverride {
public:
    GuiColorOverride()=default;
    explicit GuiColorOverride(GuiColor fallback) : fallback_(fallback) {}
    GuiColorOverride& operator=(GuiColor value);
    operator GuiColor() const { return value_.value_or(fallback_); }
    bool hasValue() const { return value_.has_value(); }
    void reset() { value_.reset(); }
private:
    GuiColor fallback_{};
    std::optional<GuiColor> value_;
};
struct GuiStyleOverrides {
    GuiColorOverride background{GuiStyle{}.background},foreground{GuiStyle{}.foreground},
        border{GuiStyle{}.border},accent{GuiStyle{}.accent},focusRing{GuiStyle{}.focusRing},
        selection{GuiStyle{}.selection},placeholder{GuiStyle{}.placeholder};
    std::optional<float> opacity;
    GuiStyleOverrides& operator=(const GuiStyle& value);
    void validate() const;
    void apply(GuiStyle& value) const;
};
struct GuiStateStyles {
    GuiStyleOverrides checked,hovered,pressed,focused,disabled;
};
enum class GuiStyleKind { Widget, Panel, ScrollPanel, TextLabel, Image, Button, TextInput, Checkbox, Slider, Count };
struct GuiStyleDefaults {
    GuiStyleOverrides normal;
    GuiStateStyles states;
};
struct GuiVisualState { bool checked=false,hovered=false,pressed=false,focused=false,disabled=false; };
class GuiTheme {
public:
    GuiTheme(); // Dark defaults.
    static GuiTheme light();
    GuiStyle defaults;
    GuiStyleDefaults common;
    GuiStyleDefaults& forKind(GuiStyleKind kind);
    const GuiStyleDefaults& forKind(GuiStyleKind kind) const;
    void validate() const;
private:
    std::array<GuiStyleDefaults,static_cast<std::size_t>(GuiStyleKind::Count)> kinds_;
};
struct VerticalLayout { float gap=8; };
struct HorizontalLayout { float gap=8; };
struct WrapLayout { float gap=8,rowGap=8; };
struct GridLayout { std::uint32_t columns=2; float columnGap=8,rowGap=8; };
enum class GuiLayout { Absolute, Vertical, Horizontal, Wrap, Grid };
enum class GuiAlign { Start, Center, End };
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
enum class GuiPointer { Move, Down, Up, Cancel };
enum class GuiKey { Tab, Enter, Space, Escape, Up, Down, PageUp, PageDown, Home, End, Left, Right };
enum class GuiRole { Group, Text, Image, Button, Textbox, ScrollRegion, Checkbox, Slider };
struct GuiAccessibleNode {
    std::uint64_t id=0;
    GuiRole role=GuiRole::Group;
    std::string name,description,value;
    GuiRect bounds;
    bool disabled=false,focused=false,live=false,checked=false;
    float minimum=0,maximum=1,number=0,step=0.01f;
    GuiRect clippedBounds; // Visible logical bounds; empty for offscreen controls.
    bool keyboardFocusable=false;
};
// Hook positions are widget-local design units. Capture reserves the entire
// drag for this widget; Handled permits an ancestor ScrollPanel to take over.
enum class GuiPointerResult { Ignored, Handled, Capture };
struct GuiPointerEvent {
    GuiPointer action;
    Vec2 position,size;
    bool inside=false,extendSelection=false;
};
struct GuiKeyEvent { GuiKey key; bool shift=false,repeat=false; };
class GuiSystem;
// Borrow only during onPaint. Primitives are local design units, automatically
// scaled, clipped and dimmed. The widget cannot replace the system's draw list.
class GuiPainter {
public:
    Vec2 size() const { return {bounds_.width/scale_,bounds_.height/scale_}; }
    bool hovered() const { return hovered_; }
    bool pressed() const { return pressed_; }
    bool focused() const { return focused_; }
    bool enabled() const { return enabled_; }
    double seconds() const { return seconds_; }
    const GuiStyle& style() const { return style_; } // Resolved colors; opacity applied by primitives.
    float measureText(std::string_view text) const;
    float lineHeight() const;
    void fill(GuiRect bounds,GuiColor color);
    void image(GuiRect bounds,std::shared_ptr<const Texture> texture,GuiColor tint={1,1,1,1});
    void text(std::string_view text,Vec2 position,GuiColor color);
private:
    friend class GuiSystem;
    GuiPainter(GuiDrawList& list,GuiFont* font,GuiRect bounds,GuiRect clip,float scale,
        bool hovered,bool pressed,bool focused,bool enabled,double seconds,GuiStyle style)
        : list_(list),font_(font),bounds_(bounds),clip_(clip),scale_(scale),hovered_(hovered),
          pressed_(pressed),focused_(focused),enabled_(enabled),seconds_(seconds),style_(style) {}
    GuiDrawList& list_;
    GuiFont* font_;
    GuiRect bounds_,clip_;
    float scale_;
    bool hovered_,pressed_,focused_,enabled_;
    double seconds_;
    GuiStyle style_;
};
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
    void setSizeLimits(Vec2 minimum,Vec2 maximum);
    void setMinSize(Vec2 value) { setSizeLimits(value,maximumSize_); }
    void setMaxSize(Vec2 value) { setSizeLimits(minimumSize_,value); }
    Vec2 minSize() const { return minimumSize_; }
    Vec2 maxSize() const { return maximumSize_; }
    void setContentSize(bool width=true,bool height=true) { contentWidth_=width;contentHeight_=height; }
    void setAlignment(GuiAlign horizontal,GuiAlign vertical);
    void clearAlignment() { hasAlignment_=false; }
    void setContentAlignment(GuiAlign horizontal,GuiAlign vertical);
    void setPadding(float value);
    void setAnchor(GuiAnchor value) { anchor_=value; }
    void setLayout(VerticalLayout value);
    void setLayout(HorizontalLayout value);
    void setLayout(WrapLayout value);
    void setLayout(GridLayout value);
    GuiLayout layoutType() const { return layout_; }
    std::uint32_t gridColumns() const { return layout_==GuiLayout::Grid ? columns_ : 0; }
    void setStretch(bool width,bool height=false) { stretchWidth_=width;stretchHeight_=height; }
    void setAccessibleName(std::string value);
    const std::string& accessibleName() const { return accessibleName_; }
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
    bool accessibilityHidden() const { return accessibilityHidden_ || (parent_ && parent_->accessibilityHidden()); }
    bool active() const;
    const GuiRect& bounds() const { return bounds_; }
    const GuiRect& clip() const { return clip_; }
    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }
    GuiStyleOverrides style;
    GuiStateStyles stateStyles;
    virtual GuiStyleKind styleKind() const { return GuiStyleKind::Widget; }
protected:
    Widget();
    virtual bool interactive() const { return false; }
    virtual bool focusable() const { return false; }
    virtual std::string_view displayedText() const { return {}; }
    virtual void onPaint(GuiPainter&) {}
    virtual GuiPointerResult onPointer(const GuiPointerEvent&) { return GuiPointerResult::Ignored; }
    virtual bool onKey(const GuiKeyEvent&) { return false; }
    virtual bool onActivate() { return false; }
    virtual bool onAccessibleValue(float) { return false; }
    virtual void describeAccessibility(GuiAccessibleNode&) const {}
    virtual bool checkedStyle() const { return false; }
    friend class GuiSystem;
    Widget* parent_=nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    Vec2 position_{},size_{180,32};
    Vec2 minimumSize_{},maximumSize_{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
    bool contentWidth_=false,contentHeight_=false,hasAlignment_=false;
    GuiAlign horizontalAlign_=GuiAlign::Start,verticalAlign_=GuiAlign::Start;
    GuiAlign contentHorizontalAlign_=GuiAlign::Start,contentVerticalAlign_=GuiAlign::Start;
    std::uint32_t columns_=2;
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
    GuiStyleKind styleKind() const override { return GuiStyleKind::Panel; }
protected: bool interactive() const override { return true; }
};
// Vertical scrolling in design units. Child clipping is always enforced.
class ScrollPanel : public Panel {
public:
    GuiStyleKind styleKind() const override { return GuiStyleKind::ScrollPanel; }
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
    GuiStyleKind styleKind() const override { return GuiStyleKind::TextLabel; }
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
    GuiStyleKind styleKind() const override { return GuiStyleKind::Image; }
    explicit Image(std::shared_ptr<const Texture> texture={}) : texture_(std::move(texture)) {}
    void setTexture(std::shared_ptr<const Texture> value) { texture_=std::move(value); }
    const std::shared_ptr<const Texture>& texture() const { return texture_; }
private: std::shared_ptr<const Texture> texture_;
};
class Button : public Widget {
public:
    GuiStyleKind styleKind() const override { return GuiStyleKind::Button; }
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
    GuiStyleKind styleKind() const override { return GuiStyleKind::TextInput; }
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
class Checkbox : public Widget {
public:
    GuiStyleKind styleKind() const override { return GuiStyleKind::Checkbox; }
    explicit Checkbox(std::string text={},bool checked=false);
    void setText(std::string text);
    const std::string& text() const { return text_; }
    void setChecked(bool checked); // Notifies once on a changed value, including programmatic changes.
    bool checked() const { return checked_; }
    void onChange(std::function<void(bool)> callback) { change_=std::move(callback); }
protected:
    bool checkedStyle() const override { return checked_; }
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    std::string_view displayedText() const override { return text_; }
    void onPaint(GuiPainter&) override;
    GuiPointerResult onPointer(const GuiPointerEvent&) override;
    bool onActivate() override;
    void describeAccessibility(GuiAccessibleNode&) const override;
private:
    std::string text_;
    bool checked_=false;
    std::function<void(bool)> change_;
};
class Slider : public Widget {
public:
    GuiStyleKind styleKind() const override { return GuiStyleKind::Slider; }
    Slider(); // Horizontal; range 0..1, step .01, value .5.
    void setRange(float minimum,float maximum,float step=0.01f);
    void setValue(float value); // Finite values clamp and snap; changed values notify once.
    float value() const { return value_; }
    float minimum() const { return minimum_; }
    float maximum() const { return maximum_; }
    float step() const { return step_; }
    void onChange(std::function<void(float)> callback) { change_=std::move(callback); }
protected:
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onPaint(GuiPainter&) override;
    GuiPointerResult onPointer(const GuiPointerEvent&) override;
    bool onKey(const GuiKeyEvent&) override;
    bool onAccessibleValue(float value) override { setValue(value);return true; }
    void describeAccessibility(GuiAccessibleNode&) const override;
private:
    float minimum_=0,maximum_=1,step_=0.01f,value_=0.5f;
    bool dragging_=false;
    std::function<void(float)> change_;
};
class GuiSystem {
public:
    // Idle only: cancels capture/focus before destroying widgets. Keeps font/theme/scale.
    void clear();
    Widget& root() { return root_; }
    void setFont(std::shared_ptr<GuiFont> font) { font_=std::move(font); }
    const std::shared_ptr<GuiFont>& font() const { return font_; }
    void setTheme(const GuiTheme& theme); // Validates the complete value before replacement; no retained references.
    const GuiTheme& theme() const { return theme_; }
    GuiVisualState visualState(const Widget& widget) const;
    GuiStyle resolvedStyle(const Widget& widget) const;
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
    bool setAccessibleValue(std::uint64_t id,float value); // Slider semantic action; validates ID/value.
    bool key(GuiKey key,bool shift=false,bool repeat=false);
    void editText(std::string value,std::size_t start,std::size_t end,bool composing=false,bool backward=false);
    void blur();
    Widget* focused() const { return focus_ && focus_->active() ? focus_ : nullptr; }
    TextInput* focusedInput() const;
    bool capturesPointer() const { return (captured_ && captured_->active()) || (scrollCapture_ && scrollCapture_->active()); }
private:
    Widget root_;
    GuiTheme theme_;
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
    struct LayoutPass;
    Vec2 measure(Widget& widget,Vec2 available,bool stretchWidth,bool stretchHeight,LayoutPass& pass,unsigned depth);
    std::vector<GuiRect> childRects(Widget& widget,Vec2 size,bool measuringHeight,LayoutPass& pass,unsigned depth);
    void arrange(Widget& widget,GuiRect bounds,GuiRect clip,bool active,LayoutPass& pass,unsigned depth);
    Widget* hit(Widget& widget,Vec2 point) const;
    void paint(Widget& widget,double seconds);
    void text(std::string_view value,Vec2 position,GuiRect clip,GuiColor color);
    void activate(Button& button);
    bool reachable(const Widget& widget) const;
    void reveal(Widget& widget);
    void resetCaret() { caretEpoch_=lastTime_; }
    GuiPointerResult sendPointer(Widget& widget,GuiPointer action,Vec2 position,bool extend=false);
    void cancelPointer();
};
} // namespace engine
