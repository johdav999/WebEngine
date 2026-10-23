#include "engine/gui.hpp"
#include "gui_utf8.hpp"
#include <atomic>
#include <limits>
#include <map>
#include <tuple>

namespace engine {
bool GuiRect::contains(Vec2 p) const { return p.x>=x && p.y>=y && p.x<x+width && p.y<y+height; }
GuiRect intersect(GuiRect a,GuiRect b) {
    const float x=std::max(a.x,b.x),y=std::max(a.y,b.y);
    return {x,y,std::max(0.f,std::min(a.x+a.width,b.x+b.width)-x),std::max(0.f,std::min(a.y+a.height,b.y+b.height)-y)};
}
namespace {
void valid(Vec2 value,bool size) {
    if(!std::isfinite(value.x)||!std::isfinite(value.y)||(size&&(value.x<0||value.y<0))) throw std::invalid_argument("Invalid GUI geometry");
}
void validSpacing(float value) { if(!std::isfinite(value)||value<0) throw std::invalid_argument("Invalid GUI spacing"); }
GuiRect inset(GuiRect r,float padding) {
    return {r.x+padding,r.y+padding,std::max(0.f,r.width-2*padding),std::max(0.f,r.height-2*padding)};
}
bool same(GuiRect a,GuiRect b) { return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height; }
void validColor(GuiColor c) {
    for(float v:{c.r,c.g,c.b,c.a}) if(!std::isfinite(v)||v<0||v>1) throw std::invalid_argument("Invalid GUI color");
}
void validStyle(const GuiStyle& s) {
    for(auto c:{s.background,s.foreground,s.border,s.accent,s.focusRing,s.selection,s.placeholder}) validColor(c);
    if(!std::isfinite(s.opacity)||s.opacity<0||s.opacity>1) throw std::invalid_argument("Invalid GUI opacity");
}
void validStates(const GuiStateStyles& states) {
    for(const auto* s:{&states.checked,&states.hovered,&states.pressed,&states.focused,&states.disabled}) s->validate();
}
std::vector<std::string> wrapText(std::string_view value,GuiFont& font,float width,bool wrap) {
    std::vector<std::string> lines;
    std::size_t start=0,offset=0,space=std::string_view::npos;
    float advance=0;
    while(offset<value.size()) {
        const auto before=offset;const auto cp=gui_detail::next(value,offset);
        if(cp=='\n'||cp=='\r') {
            lines.emplace_back(value.substr(start,before-start));
            if(cp=='\r'&&offset<value.size()&&value[offset]=='\n') ++offset;
            start=offset;space=std::string_view::npos;advance=0;continue;
        }
        const float next=font.glyph(cp).advance;
        if(wrap&&advance+next>width&&before>start) {
            const auto end=space!=std::string_view::npos ? space : before;
            lines.emplace_back(value.substr(start,end-start));
            start=space!=std::string_view::npos ? space+1 : before;
            offset=start;advance=0;space=std::string_view::npos;continue;
        }
        if(cp==' ') space=before;
        advance+=next;
    }
    lines.emplace_back(value.substr(start));return lines;
}
void preflight(Widget& widget,GuiFont& font) {
    if(!widget.visible()) return;
    if(const auto* label=dynamic_cast<TextLabel*>(&widget)) font.measure(label->text());
    if(const auto* button=dynamic_cast<Button*>(&widget)) font.measure(button->text());
    if(const auto* input=dynamic_cast<TextInput*>(&widget)) { font.measure(input->text());font.measure(input->placeholder()); }
    for(const auto& child:widget.children()) preflight(*child,font);
}
}
GuiColorOverride& GuiColorOverride::operator=(GuiColor value) { validColor(value);value_=value;return *this; }
GuiStyleOverrides& GuiStyleOverrides::operator=(const GuiStyle& value) {
    validStyle(value);
    background=value.background;foreground=value.foreground;border=value.border;accent=value.accent;
    focusRing=value.focusRing;selection=value.selection;placeholder=value.placeholder;opacity=value.opacity;return *this;
}
void GuiStyleOverrides::validate() const {
    for(const auto* c:{&background,&foreground,&border,&accent,&focusRing,&selection,&placeholder}) if(c->hasValue()) validColor(*c);
    if(opacity&&(!std::isfinite(*opacity)||*opacity<0||*opacity>1)) throw std::invalid_argument("Invalid GUI opacity");
}
void GuiStyleOverrides::apply(GuiStyle& value) const {
    validate();
    if(background.hasValue()) value.background=background;
    if(foreground.hasValue()) value.foreground=foreground;
    if(border.hasValue()) value.border=border;
    if(accent.hasValue()) value.accent=accent;
    if(focusRing.hasValue()) value.focusRing=focusRing;
    if(selection.hasValue()) value.selection=selection;
    if(placeholder.hasValue()) value.placeholder=placeholder;
    if(opacity) value.opacity=*opacity;
}
GuiTheme::GuiTheme() {
    common.states.disabled.opacity=0.4f;
    forKind(GuiStyleKind::Image).normal.foreground={1,1,1,1};
    auto& button=forKind(GuiStyleKind::Button).states;
    button.hovered.background={0.05f,0.5f,0.65f,1};
    button.focused.background={0.05f,0.5f,0.65f,1};
    button.pressed.background={0.035f,0.35f,0.455f,1};
}
GuiTheme GuiTheme::light() {
    GuiTheme theme;
    theme.defaults.background={0.95f,0.96f,0.98f,0.98f};theme.defaults.foreground={0.08f,0.12f,0.18f,1};
    theme.defaults.border={0.55f,0.62f,0.7f,1};theme.defaults.accent={0.02f,0.36f,0.5f,1};
    theme.defaults.focusRing={0.02f,0.36f,0.5f,1};theme.defaults.selection={0.3f,0.65f,0.85f,0.6f};
    theme.defaults.placeholder={0.38f,0.43f,0.5f,1};
    auto& button=theme.forKind(GuiStyleKind::Button).states;
    button.hovered.background={0.77f,0.88f,0.94f,1};button.focused.background={0.77f,0.88f,0.94f,1};
    button.pressed.background={0.54f,0.75f,0.86f,1};return theme;
}
GuiStyleDefaults& GuiTheme::forKind(GuiStyleKind kind) {
    const auto index=static_cast<std::size_t>(kind);
    if(index>=kinds_.size()) throw std::invalid_argument("Invalid GUI style kind");
    return kinds_[index];
}
const GuiStyleDefaults& GuiTheme::forKind(GuiStyleKind kind) const {
    const auto index=static_cast<std::size_t>(kind);
    if(index>=kinds_.size()) throw std::invalid_argument("Invalid GUI style kind");
    return kinds_[index];
}
void GuiTheme::validate() const {
    validStyle(defaults);common.normal.validate();validStates(common.states);
    for(const auto& kind:kinds_) { kind.normal.validate();validStates(kind.states); }
}
void GuiSystem::setTheme(const GuiTheme& theme) { theme.validate();theme_=theme; }
GuiVisualState GuiSystem::visualState(const Widget& widget) const {
    return {widget.checkedStyle(),widget.active() && &widget==hover_,widget.active() && &widget==captured_,
        &widget==focused(),!widget.active()};
}
GuiStyle GuiSystem::resolvedStyle(const Widget& widget) const {
    auto style=theme_.defaults;
    const auto& kind=theme_.forKind(widget.styleKind());
    theme_.common.normal.apply(style);kind.normal.apply(style);widget.style.apply(style);
    const auto state=visualState(widget);
    // Layer each state across common, component and inline overrides before
    // moving to the next state, so disabled wins and pressed remains visible
    // even when the same widget also has keyboard focus.
    const auto apply=[&](auto member,bool active) {
        if(active) { (theme_.common.states.*member).apply(style);(kind.states.*member).apply(style);(widget.stateStyles.*member).apply(style); }
    };
    apply(&GuiStateStyles::checked,state.checked);
    apply(&GuiStateStyles::hovered,state.hovered);
    apply(&GuiStateStyles::focused,state.focused);
    apply(&GuiStateStyles::pressed,state.pressed);
    apply(&GuiStateStyles::disabled,state.disabled);
    validStyle(style);return style;
}
void GuiDrawList::clear() { vertices.clear();indices.clear();commands.clear(); }
void GuiDrawList::quad(GuiRect r,GuiRect clip,GuiColor color,std::shared_ptr<const Texture> texture,GuiRect uv) {
    if(r.empty()||intersect(r,clip).empty()||color.a<=0) return;
    if(!texture) texture=Texture::white();
    const auto base=static_cast<std::uint32_t>(vertices.size());
    const auto first=static_cast<std::uint32_t>(indices.size());
    vertices.push_back({{r.x,r.y},{uv.x,uv.y},color});
    vertices.push_back({{r.x+r.width,r.y},{uv.x+uv.width,uv.y},color});
    vertices.push_back({{r.x+r.width,r.y+r.height},{uv.x+uv.width,uv.y+uv.height},color});
    vertices.push_back({{r.x,r.y+r.height},{uv.x,uv.y+uv.height},color});
    indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});
    if(!commands.empty()&&commands.back().texture==texture&&same(commands.back().clip,clip)) commands.back().indexCount+=6;
    else commands.push_back({std::move(texture),clip,first,6});
}
float GuiPainter::measureText(std::string_view value) const {
    gui_detail::validate(value);return font_ ? font_->measure(value) : 0;
}
float GuiPainter::lineHeight() const { return font_ ? font_->lineHeight() : 20; }
void GuiPainter::fill(GuiRect r,GuiColor color) { image(r,{},color); }
void GuiPainter::image(GuiRect r,std::shared_ptr<const Texture> texture,GuiColor color) {
    valid({r.x,r.y},false);valid({r.width,r.height},true);
    r={bounds_.x+r.x*scale_,bounds_.y+r.y*scale_,r.width*scale_,r.height*scale_};
    color.a*=style_.opacity;
    list_.quad(r,clip_,color,std::move(texture));
}
void GuiPainter::text(std::string_view value,Vec2 position,GuiColor color) {
    valid(position,false);measureText(value); // Populate all glyphs before borrowing atlas snapshots.
    if(!font_) return;
    color.a*=style_.opacity;
    for(std::size_t offset=0;offset<value.size();) {
        const auto& glyph=font_->glyph(gui_detail::next(value,offset));
        auto r=glyph.bounds;r.x=bounds_.x+(r.x+position.x)*scale_;
        r.y=bounds_.y+(r.y+position.y+font_->ascent())*scale_;r.width*=scale_;r.height*=scale_;
        list_.quad(r,clip_,color,font_->atlas(glyph.page),glyph.uv);position.x+=glyph.advance;
    }
}
Checkbox::Checkbox(std::string text,bool checked) : checked_(checked) { setText(std::move(text));setSize({240,38}); }
void Checkbox::setText(std::string value) { gui_detail::validate(value);text_=std::move(value); }
void Checkbox::setChecked(bool value) {
    if(value==checked_) return;
    checked_=value;if(change_) { auto callback=change_;callback(value); }
}
bool Checkbox::onActivate() { setChecked(!checked_);return true; }
GuiPointerResult Checkbox::onPointer(const GuiPointerEvent& event) {
    if(event.action==GuiPointer::Up&&event.inside) onActivate();
    return GuiPointerResult::Handled;
}
void Checkbox::describeAccessibility(GuiAccessibleNode& node) const { node.role=GuiRole::Checkbox;node.checked=checked_; }
void Checkbox::onPaint(GuiPainter& painter) {
    const auto& style=painter.style();
    const auto size=painter.size();const float edge=std::min(22.f,std::min(size.x,size.y)),y=(size.y-edge)/2;
    painter.fill({0,y,edge,edge},painter.focused() ? style.focusRing : style.border);
    painter.fill({2,y+2,std::max(0.f,edge-4),std::max(0.f,edge-4)},checked_ ? style.accent : style.background);
    if(checked_) {
        painter.fill({edge*.22f,y+edge*.45f,edge*.22f,edge*.3f},style.foreground);
        painter.fill({edge*.44f,y+edge*.25f,edge*.3f,edge*.5f},style.foreground);
    }
    if(painter.hovered()||painter.pressed()) painter.fill({0,y+edge-2,edge,2},style.accent);
    painter.text(text_,{edge+10,(size.y-painter.lineHeight())/2},style.foreground);
}
Slider::Slider() { setSize({240,38}); }
void Slider::setRange(float minimum,float maximum,float step) {
    const double span=double(maximum)-minimum;
    if(!std::isfinite(minimum)||!std::isfinite(maximum)||!std::isfinite(step)||
       span<=0||span>std::numeric_limits<float>::max()||step<=0||step>span)
        throw std::invalid_argument("Invalid GUI slider range");
    minimum_=minimum;maximum_=maximum;step_=step;setValue(value_);
}
void Slider::setValue(float value) {
    if(!std::isfinite(value)) throw std::invalid_argument("Invalid GUI slider value");
    const double clamped=std::clamp(value,minimum_,maximum_);
    const float next=clamped==maximum_ ? maximum_ : float(std::clamp(double(minimum_)+
        std::round((clamped-minimum_)/step_)*step_,double(minimum_),double(maximum_)));
    if(next==value_) return;
    value_=next;if(change_) { auto callback=change_;callback(next); }
}
GuiPointerResult Slider::onPointer(const GuiPointerEvent& event) {
    if(event.action==GuiPointer::Cancel) { dragging_=false;return GuiPointerResult::Handled; }
    if(event.action==GuiPointer::Down) dragging_=true;
    if(dragging_) {
        const float length=std::max(0.f,event.size.x-24);
        if(length>0) setValue(float(double(minimum_)+(double(maximum_)-minimum_)*std::clamp((event.position.x-12)/length,0.f,1.f)));
    }
    if(event.action==GuiPointer::Up) dragging_=false;
    return GuiPointerResult::Capture;
}
bool Slider::onKey(const GuiKeyEvent& event) {
    const auto key=event.key;
    if(key==GuiKey::Home) setValue(minimum_);
    else if(key==GuiKey::End) setValue(maximum_);
    else if(key==GuiKey::Left||key==GuiKey::Down||key==GuiKey::Right||key==GuiKey::Up||key==GuiKey::PageUp||key==GuiKey::PageDown) {
        const bool increase=key==GuiKey::Right||key==GuiKey::Up||key==GuiKey::PageUp;
        const double delta=double(step_)*((key==GuiKey::PageUp||key==GuiKey::PageDown) ? 10 : 1);
        setValue(float(std::clamp(double(value_)+(increase ? delta : -delta),double(minimum_),double(maximum_))));
    } else return false;
    return true;
}
void Slider::describeAccessibility(GuiAccessibleNode& node) const {
    node.role=GuiRole::Slider;node.minimum=minimum_;node.maximum=maximum_;node.number=value_;node.step=step_;node.value=std::to_string(value_);
}
void Slider::onPaint(GuiPainter& painter) {
    const auto& style=painter.style();
    const auto size=painter.size();const float pad=std::min(12.f,size.x/2),length=size.x-2*pad;
    const float x=pad+length*float((double(value_)-minimum_)/(double(maximum_)-minimum_));
    painter.fill({pad,std::max(0.f,(size.y-6)/2),length,std::min(6.f,size.y)},style.border);
    painter.fill({pad,std::max(0.f,(size.y-6)/2),x-pad,std::min(6.f,size.y)},style.accent);
    const float knob=std::min(18.f,std::min(size.x,size.y));
    painter.fill({x-knob/2,(size.y-knob)/2,knob,knob},painter.focused()||painter.hovered()||painter.pressed() ? style.accent : style.foreground);
    if(painter.focused()) painter.fill({0,size.y-2,size.x,2},style.focusRing);
}
Widget::Widget() { static std::atomic<std::uint64_t> next{1};id_=next.fetch_add(1); }
void Widget::setAccessibleName(std::string value) { gui_detail::validate(value);accessibleName_=std::move(value); }
void Widget::setAccessibleDescription(std::string value) { gui_detail::validate(value);accessibleDescription_=std::move(value); }
void ScrollPanel::setScrollOffset(float value) { validSpacing(value);offset_=value; }
void GuiSystem::setScale(float value) {
    if(!std::isfinite(value)||value<0.25f||value>4) throw std::invalid_argument("Invalid GUI scale");
    scale_=value;
}
void Widget::setPosition(Vec2 value) { valid(value,false);position_=value; }
void Widget::setSize(Vec2 value) { valid(value,true);size_=value; }
void Widget::setSizeLimits(Vec2 minimum,Vec2 maximum) {
    valid(minimum,true);valid(maximum,true);
    if(minimum.x>maximum.x||minimum.y>maximum.y) throw std::invalid_argument("Invalid GUI size limits");
    minimumSize_=minimum;maximumSize_=maximum;
}
namespace {
void validAlign(GuiAlign value) { if(value<GuiAlign::Start||value>GuiAlign::End) throw std::invalid_argument("Invalid GUI alignment"); }
float aligned(GuiAlign value,float free) { return value==GuiAlign::Center ? free/2 : value==GuiAlign::End ? free : 0; }
float layoutNumber(double value) {
    if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max()) throw std::invalid_argument("GUI layout overflow");
    return float(value);
}
}
void Widget::setAlignment(GuiAlign horizontal,GuiAlign vertical) {
    validAlign(horizontal);validAlign(vertical);horizontalAlign_=horizontal;verticalAlign_=vertical;hasAlignment_=true;
}
void Widget::setContentAlignment(GuiAlign horizontal,GuiAlign vertical) {
    validAlign(horizontal);validAlign(vertical);contentHorizontalAlign_=horizontal;contentVerticalAlign_=vertical;
}
void Widget::setPadding(float value) { validSpacing(value);padding_=value; }
void Widget::setLayout(VerticalLayout value) { validSpacing(value.gap);gap_=value.gap;layout_=GuiLayout::Vertical; }
void Widget::setLayout(HorizontalLayout value) { validSpacing(value.gap);gap_=value.gap;layout_=GuiLayout::Horizontal; }
void Widget::setLayout(WrapLayout value) { validSpacing(value.gap);validSpacing(value.rowGap);gap_=value.gap;rowGap_=value.rowGap;layout_=GuiLayout::Wrap; }
void Widget::setLayout(GridLayout value) {
    validSpacing(value.columnGap);validSpacing(value.rowGap);
    if(!value.columns||value.columns>64) throw std::invalid_argument("Invalid GUI grid columns");
    columns_=value.columns;gap_=value.columnGap;rowGap_=value.rowGap;layout_=GuiLayout::Grid;
}
bool Widget::active() const { return visible_&&enabled_&&(!parent_||parent_->active()); }
void TextLabel::setText(std::string value) { gui_detail::validate(value);text_=std::move(value); }
void Button::setText(std::string value) { gui_detail::validate(value);text_=std::move(value); }
void TextInput::setPlaceholder(std::string value) { gui_detail::validate(value);placeholder_=std::move(value); }
void TextInput::setText(std::string value) {
    gui_detail::validate(value);
    // The first controls are single-line. Browsers may paste line breaks.
    for(char& c:value) if(c=='\r'||c=='\n'||c=='\t') c=' ';
    const bool changed=value!=text_;text_=std::move(value);if(changed) ++textRevision_;
    setSelection(selectionStart_,selectionEnd_,selectionBackward_);
    if(changed&&change_) { auto callback=change_;callback(text_); }
}
void TextInput::setSelection(std::size_t start,std::size_t end,bool backward) {
    const auto low=std::min(start,end),high=std::max(start,end);
    start=gui_detail::boundary(text_,low);
    end=gui_detail::boundary(text_,high);
    backward=backward&&start!=end;
    if(start!=selectionStart_||end!=selectionEnd_||backward!=selectionBackward_) ++textRevision_;
    selectionStart_=start;selectionEnd_=end;selectionBackward_=backward;
}
struct GuiSystem::LayoutPass {
    using Key=std::tuple<Widget*,float,float,bool,bool>;
    std::map<Key,Vec2> measurements;
    unsigned work=0;
    struct Item { Widget* widget;GuiRect bounds,clip;std::vector<std::string> lines;float maximum=0,offset=0; };
    std::vector<Item> items;
};
Vec2 GuiSystem::measure(Widget& widget,Vec2 available,bool stretchWidth,bool stretchHeight,LayoutPass& pass,unsigned depth) {
    if(depth>128||++pass.work>65536) throw std::invalid_argument("GUI layout measurement budget exceeded");
    const LayoutPass::Key key{&widget,available.x,available.y,stretchWidth,stretchHeight};
    if(const auto found=pass.measurements.find(key);found!=pass.measurements.end()) return found->second;
    const auto clamp=[&](Vec2 size) {
        valid(size,true);return Vec2{std::clamp(size.x,widget.minimumSize_.x,widget.maximumSize_.x),std::clamp(size.y,widget.minimumSize_.y,widget.maximumSize_.y)};
    };
    Vec2 result=widget.size_,natural=widget.size_;
    auto* label=dynamic_cast<TextLabel*>(&widget);
    auto* image=dynamic_cast<Image*>(&widget);
    const bool container=dynamic_cast<Panel*>(&widget)||!widget.children_.empty();
    if(font_) {
        if(label) {
            const auto lines=wrapText(label->text_,*font_,0,false);natural={0,layoutNumber(double(lines.size())*font_->lineHeight())};
            for(const auto& line:lines) natural.x=std::max(natural.x,font_->measure(line));
        } else if(auto* button=dynamic_cast<Button*>(&widget)) natural={layoutNumber(double(font_->measure(button->text()))+16),font_->lineHeight()+12};
        else if(auto* input=dynamic_cast<TextInput*>(&widget)) natural={layoutNumber(double(font_->measure(input->text().empty() ? input->placeholder() : input->text()))+18),font_->lineHeight()+16};
        else if(auto* check=dynamic_cast<Checkbox*>(&widget)) natural={layoutNumber(double(font_->measure(check->text()))+32),std::max(22.f,font_->lineHeight())+8};
    }
    if(image&&image->texture()&&image->texture()->width&&image->texture()->height) {
        natural={float(image->texture()->width),float(image->texture()->height)};
        if(widget.contentWidth_&&!widget.contentHeight_) {
            const float height=clamp({result.x,stretchHeight&&widget.stretchHeight_ ? available.y : result.y}).y;
            natural.x=layoutNumber(double(height)*image->texture()->width/image->texture()->height);
        }
    }
    if(widget.contentWidth_&&container) {
        double width=0,widest=0,cursor=0;std::size_t count=0;
        const Vec2 inner{std::max(0.f,available.x-2*widget.padding_),std::max(0.f,available.y-2*widget.padding_)};
        for(const auto& child:widget.children_) if(child->visible_) {
            const auto size=measure(*child,inner,false,false,pass,depth+1);++count;
            widest=std::max(widest,double(size.x)+std::max(0.f,child->position_.x));
            if(widget.layout_==GuiLayout::Horizontal||widget.layout_==GuiLayout::Wrap) {
                if(count>1) cursor+=widget.gap_;
                width=std::max(width,cursor+std::max(0.f,child->position_.x)+size.x);cursor+=size.x;
            }
        }
        if(widget.layout_==GuiLayout::Grid) width=count ? widest*widget.columns_+double(widget.columns_-1)*widget.gap_ : 0;
        else if(widget.layout_!=GuiLayout::Horizontal&&widget.layout_!=GuiLayout::Wrap) width=widest;
        natural.x=layoutNumber(width+2*double(widget.padding_));
    }
    if(widget.contentWidth_) result.x=natural.x;
    if(stretchWidth&&widget.stretchWidth_) result.x=available.x;
    result=clamp(result);
    if(image&&image->texture()&&image->texture()->width&&widget.contentHeight_&&!widget.contentWidth_)
        natural.y=layoutNumber(double(result.x)*image->texture()->height/image->texture()->width);
    if(widget.contentHeight_) result.y=natural.y;
    if(label&&font_&&((label->wrap_&&label->autoHeight_)||widget.contentHeight_)) {
        const auto lines=wrapText(label->text_,*font_,result.x,label->wrap_);
        result.y=layoutNumber(double(lines.size())*font_->lineHeight());
    }
    if(widget.contentHeight_&&container) {
        const auto rectangles=childRects(widget,{result.x,result.y},true,pass,depth);
        double bottom=widget.padding_;
        for(std::size_t i=0;i<rectangles.size();++i) if(widget.children_[i]->visible_)
            bottom=std::max(bottom,double(rectangles[i].y)+rectangles[i].height);
        result.y=layoutNumber(bottom+widget.padding_);
    }
    if(stretchHeight&&widget.stretchHeight_) result.y=available.y;
    result=clamp(result);pass.measurements.emplace(key,result);return result;
}
std::vector<GuiRect> GuiSystem::childRects(Widget& widget,Vec2 size,bool measuringHeight,LayoutPass& pass,unsigned depth) {
    const float pad=widget.padding_,width=std::max(0.f,size.x-2*pad),height=std::max(0.f,size.y-2*pad);
    const auto count=widget.children_.size();std::vector<GuiRect> rectangles(count);
    std::vector<std::size_t> visible;visible.reserve(count);
    for(std::size_t i=0;i<count;++i) if(widget.children_[i]->visible_) visible.push_back(i);
    const auto hAlign=[&](const Widget& c){return c.hasAlignment_ ? c.horizontalAlign_ : widget.contentHorizontalAlign_;};
    const auto vAlign=[&](const Widget& c){return c.hasAlignment_ ? c.verticalAlign_ : widget.contentVerticalAlign_;};
    if(widget.layout_==GuiLayout::Grid) {
        const float cell=std::max(0.f,layoutNumber((double(width)-double(widget.columns_-1)*widget.gap_)/widget.columns_));
        const std::size_t rows=(visible.size()+widget.columns_-1)/widget.columns_;
        std::vector<float> heights(rows,0);
        for(std::size_t n=0;n<visible.size();++n) {
            const auto i=visible[n];const auto measured=measure(*widget.children_[i],{cell,height},true,false,pass,depth+1);
            rectangles[i]={0,0,measured.x,measured.y};heights[n/widget.columns_]=std::max(heights[n/widget.columns_],measured.y);
        }
        double total=rows ? double(rows-1)*widget.rowGap_ : 0;for(auto h:heights) total+=h;
        float y=pad+(measuringHeight ? 0 : aligned(widget.contentVerticalAlign_,std::max(0.f,layoutNumber(double(height)-total))));
        for(std::size_t n=0;n<visible.size();++n) {
            const auto i=visible[n],row=n/widget.columns_;auto& c=*widget.children_[i];
            const auto measured=measure(c,{cell,heights[row]},true,!measuringHeight,pass,depth+1);
            rectangles[i]={layoutNumber(double(pad)+(n%widget.columns_)*(double(cell)+widget.gap_)+aligned(hAlign(c),cell-measured.x)+c.position_.x),
                layoutNumber(double(y)+(measuringHeight ? 0 : aligned(vAlign(c),heights[row]-measured.y))+c.position_.y),measured.x,measured.y};
            if(n%widget.columns_==widget.columns_-1) y=layoutNumber(double(y)+heights[row]+widget.rowGap_);
        }
        return rectangles;
    }
    for(auto i:visible) {
        auto& c=*widget.children_[i];const auto measured=measure(c,{width,height},true,!measuringHeight,pass,depth+1);
        rectangles[i]={layoutNumber(double(pad)+c.position_.x),layoutNumber(double(pad)+c.position_.y),measured.x,measured.y};
    }
    if(widget.layout_==GuiLayout::Vertical||widget.layout_==GuiLayout::Horizontal) {
        const bool vertical=widget.layout_==GuiLayout::Vertical;
        double total=visible.empty() ? 0 : double(visible.size()-1)*widget.gap_;
        for(auto i:visible) total+=vertical ? rectangles[i].height : rectangles[i].width;
        float cursor=vertical&&measuringHeight ? 0 : aligned(vertical ? widget.contentVerticalAlign_ : widget.contentHorizontalAlign_,
            std::max(0.f,layoutNumber(double(vertical ? height : width)-total)));
        for(auto i:visible) {
            auto& c=*widget.children_[i];auto& r=rectangles[i];
            if(vertical) { r.x=layoutNumber(double(r.x)+aligned(hAlign(c),width-r.width));r.y=layoutNumber(double(r.y)+cursor);cursor=layoutNumber(double(cursor)+r.height+widget.gap_); }
            else { r.x=layoutNumber(double(r.x)+cursor);r.y=layoutNumber(double(r.y)+(measuringHeight ? 0 : aligned(vAlign(c),height-r.height)));cursor=layoutNumber(double(cursor)+r.width+widget.gap_); }
        }
    } else if(widget.layout_==GuiLayout::Wrap) {
        struct Row { std::vector<std::size_t> indices;float width=0,height=0; };
        std::vector<Row> rows;
        for(auto i:visible) {
            const auto& r=rectangles[i];
            if(rows.empty()||(!rows.back().indices.empty()&&double(rows.back().width)+widget.gap_+r.width>width)) rows.emplace_back();
            auto& row=rows.back();if(!row.indices.empty()) row.width=layoutNumber(double(row.width)+widget.gap_);
            row.width=layoutNumber(double(row.width)+r.width);row.height=std::max(row.height,r.height);row.indices.push_back(i);
        }
        double total=rows.empty()?0:double(rows.size()-1)*widget.rowGap_;for(const auto& row:rows) total+=row.height;
        float y=measuringHeight ? 0 : aligned(widget.contentVerticalAlign_,std::max(0.f,layoutNumber(double(height)-total)));
        for(const auto& row:rows) {
            float x=aligned(widget.contentHorizontalAlign_,std::max(0.f,width-row.width));
            for(auto i:row.indices) { auto& r=rectangles[i];r.x=layoutNumber(double(r.x)+x);r.y=layoutNumber(double(r.y)+y+(measuringHeight ? 0 : aligned(vAlign(*widget.children_[i]),row.height-r.height)));x=layoutNumber(double(x)+r.width+widget.gap_); }
            y=layoutNumber(double(y)+row.height+widget.rowGap_);
        }
    } else for(auto i:visible) {
        auto& c=*widget.children_[i];auto& r=rectangles[i];
        if(c.hasAlignment_) { r.x=layoutNumber(double(r.x)+aligned(c.horizontalAlign_,width-r.width));if(!measuringHeight) r.y=layoutNumber(double(r.y)+aligned(c.verticalAlign_,height-r.height)); }
        else switch(c.anchor_) {
            case GuiAnchor::TopRight:r.x=layoutNumber(double(pad)+width-r.width-c.position_.x);break;
            case GuiAnchor::BottomLeft:
                if(!measuringHeight) { r.y=layoutNumber(double(pad)+height-r.height-c.position_.y); }
                break;
            case GuiAnchor::BottomRight:r.x=layoutNumber(double(pad)+width-r.width-c.position_.x);if(!measuringHeight)r.y=layoutNumber(double(pad)+height-r.height-c.position_.y);break;
            case GuiAnchor::Center:r.x=layoutNumber(double(pad)+(width-r.width)/2+c.position_.x);if(!measuringHeight)r.y=layoutNumber(double(pad)+(height-r.height)/2+c.position_.y);break;
            default:break;
        }
    }
    return rectangles;
}
void GuiSystem::layout(Vec2 logicalSize) {
    valid(logicalSize,true);LayoutPass pass;
    arrange(root_,{0,0,logicalSize.x,logicalSize.y},{0,0,logicalSize.x,logicalSize.y},root_.visible_,pass,0);
    // Publish only a complete layout, retaining previous geometry/focus on failure.
    logicalSize_=logicalSize;root_.size_=logicalSize;
    for(auto& item:pass.items) {
        item.widget->bounds_=item.bounds;item.widget->clip_=item.clip;
        if(auto* label=dynamic_cast<TextLabel*>(item.widget)) label->lines_=std::move(item.lines);
        if(auto* scroll=dynamic_cast<ScrollPanel*>(item.widget)) { scroll->maximum_=item.maximum;scroll->offset_=item.offset; }
    }
    if(focus_&&(!focus_->active()||focus_->clip_.empty()||focus_->accessibilityHidden())) blur();
    if(captured_&&(!captured_->active()||captured_->clip_.empty())) {
        cancelPointer();scrollCapture_=nullptr;scrollDragging_=false;
    }
    if(scrollCapture_&&!scrollCapture_->active()) { scrollCapture_=nullptr;scrollDragging_=false; }
    if(hover_&&!hover_->active()) hover_=nullptr;
}
void GuiSystem::arrange(Widget& widget,GuiRect bounds,GuiRect clip,bool shown,LayoutPass& pass,unsigned depth) {
    if(depth>128) throw std::invalid_argument("GUI layout depth exceeded");
    valid({bounds.x,bounds.y},false);valid({bounds.width,bounds.height},true);
    layoutNumber(double(bounds.x)+bounds.width);layoutNumber(double(bounds.y)+bounds.height);
    shown=shown&&widget.visible_;
    const float padding=layoutNumber(double(widget.padding_)*scale_);
    const GuiRect content{layoutNumber(double(bounds.x)+padding),layoutNumber(double(bounds.y)+padding),
        layoutNumber(std::max(0.0,double(bounds.width)-2*padding)),layoutNumber(std::max(0.0,double(bounds.height)-2*padding))};
    auto* scroll=dynamic_cast<ScrollPanel*>(&widget);
    const auto childClip=(widget.clipChildren_||scroll) ? intersect(content,clip) : clip;
    const auto rectangles=childRects(widget,{bounds.width/scale_,bounds.height/scale_},false,pass,depth);
    double extent=0;
    for(std::size_t i=0;i<rectangles.size();++i) if(widget.children_[i]->visible_)
        extent=std::max(extent,double(rectangles[i].y)+rectangles[i].height-widget.padding_);
    const float maximum=scroll ? std::max(0.f,layoutNumber(extent-content.height/scale_)) : 0;
    const float offset=scroll ? std::min(scroll->offset_,maximum) : 0;
    LayoutPass::Item item{&widget,bounds,shown ? intersect(bounds,clip) : GuiRect{}, {},maximum,offset};
    if(const auto* label=dynamic_cast<TextLabel*>(&widget);label&&font_)
        item.lines=wrapText(label->text_,*font_,bounds.width/scale_,label->wrap_);
    pass.items.push_back(std::move(item));
    for(std::size_t i=0;i<widget.children_.size();++i) {
        const auto r=rectangles[i];
        GuiRect placed{layoutNumber(double(bounds.x)+r.x*scale_),layoutNumber(double(bounds.y)+(r.y-offset)*scale_),
            layoutNumber(double(r.width)*scale_),layoutNumber(double(r.height)*scale_)};
        arrange(*widget.children_[i],placed,childClip,shown,pass,depth+1);
    }
}
Widget* GuiSystem::hit(Widget& widget,Vec2 point) const {
    if(!widget.active()) return nullptr;
    // A parent may opt out of clipping so children can extend beyond it.
    if((widget.clipChildren_||dynamic_cast<ScrollPanel*>(&widget))&&!widget.clip_.contains(point)) return nullptr;
    for(auto it=widget.children_.rbegin();it!=widget.children_.rend();++it) if(auto* found=hit(**it,point)) return found;
    return widget.interactive()&&widget.clip_.contains(point) ? &widget : nullptr;
}
void GuiSystem::blur() {
    if(auto* input=dynamic_cast<TextInput*>(focus_)) input->composing_=false;
    focus_=nullptr;
}
TextInput* GuiSystem::focusedInput() const { return dynamic_cast<TextInput*>(focused()); }
void GuiSystem::activate(Button& button) { if(button.active()&&button.click_) { auto callback=button.click_;callback(); } }
GuiPointerResult GuiSystem::sendPointer(Widget& widget,GuiPointer action,Vec2 position,bool extend) {
    return widget.onPointer({action,{(position.x-widget.bounds_.x)/scale_,(position.y-widget.bounds_.y)/scale_},
        {widget.bounds_.width/scale_,widget.bounds_.height/scale_},widget.clip_.contains(position),extend});
}
void GuiSystem::cancelPointer() {
    auto* previous=captured_;captured_=nullptr;
    if(previous) sendPointer(*previous,GuiPointer::Cancel,pointerLast_);
}
bool GuiSystem::pointer(GuiPointer action,Vec2 position,bool extend) {
    valid(position,false);
    auto* target=hit(root_,position);hover_=target;
    if(captured_&&!captured_->active()) { cancelPointer();scrollCapture_=nullptr;scrollDragging_=false; }
    if(action==GuiPointer::Cancel) { const bool consumed=captured_||scrollCapture_;cancelPointer();scrollCapture_=nullptr;scrollDragging_=false;hover_=nullptr;return consumed; }
    if(action==GuiPointer::Down) {
        cancelPointer();
        captured_=target;
        pointerStart_=pointerLast_=position;scrollCapture_=nullptr;scrollDragging_=false;
        if(!dynamic_cast<TextInput*>(target)) for(auto* w=target;w;w=w->parent_) if(auto* s=dynamic_cast<ScrollPanel*>(w)) { scrollCapture_=s;break; }
        blur();
        if(target&&target->focusable()) focus_=target;
        if(auto* input=focusedInput();input&&font_) {
            const auto offset=font_->byteAt(input->text_,(position.x-input->bounds_.x)/scale_-9+input->scroll_);
            selectionAnchor_=extend ? (input->selectionBackward_ ? input->selectionEnd_ : input->selectionStart_) : offset;
            input->setSelection(selectionAnchor_,offset,offset<selectionAnchor_);resetCaret();
        }
        if(target&&sendPointer(*target,action,position,extend)==GuiPointerResult::Capture) scrollCapture_=nullptr;
    } else if(action==GuiPointer::Move&&(captured_||scrollCapture_)) {
        if(scrollCapture_) {
            if(std::hypot(position.x-pointerStart_.x,position.y-pointerStart_.y)>6*scale_) scrollDragging_=true;
            if(scrollDragging_) {
                cancelPointer();
                scrollCapture_->offset_=std::clamp(scrollCapture_->offset_+(pointerLast_.y-position.y)/scale_,0.f,scrollCapture_->maximum_);
                layout(logicalSize_);
            }
            pointerLast_=position;
        }
        if(auto* input=dynamic_cast<TextInput*>(captured_);input&&font_) {
            const auto offset=font_->byteAt(input->text_,(position.x-input->bounds_.x)/scale_-9+input->scroll_);
            input->setSelection(selectionAnchor_,offset,offset<selectionAnchor_);resetCaret();
        }
        if(captured_) sendPointer(*captured_,action,position,extend);
    } else if(action==GuiPointer::Up) {
        if(scrollDragging_) { scrollCapture_=nullptr;scrollDragging_=false;captured_=nullptr;return true; }
        scrollCapture_=nullptr;
        auto* previous=captured_;captured_=nullptr;
        if(previous) {
            const auto reply=sendPointer(*previous,action,position,extend);
            if(reply==GuiPointerResult::Ignored&&previous==target) if(auto* button=dynamic_cast<Button*>(previous)) activate(*button);
        }
        return previous||target;
    }
    else if(action==GuiPointer::Move&&target) sendPointer(*target,action,position,extend);
    return captured_||scrollCapture_||target;
}
bool GuiSystem::wheel(Vec2 position) const { return captured_||hit(const_cast<Widget&>(root_),position); }
bool GuiSystem::wheel(Vec2 position,Vec2 delta) {
    valid(position,false);valid(delta,false);
    auto* target=hit(root_,position);
    for(auto* w=target;w;w=w->parent_) if(auto* scroll=dynamic_cast<ScrollPanel*>(w)) {
        const auto next=std::clamp(scroll->offset_+delta.y/scale_,0.f,scroll->maximum_);
        if(next!=scroll->offset_) { scroll->offset_=next;layout(logicalSize_);return true; }
    }
    return captured_||target;
}
bool GuiSystem::reachable(const Widget& widget) const {
    if(!widget.active()) return false;
    if(!widget.clip_.empty()) return true;
    // Only scroll clipping is revealable; ordinary hidden/off-canvas geometry
    // must not enter keyboard navigation.
    GuiRect candidate=widget.bounds_;
    if(candidate.empty()) return false;
    for(auto* p=widget.parent_;p;p=p->parent_) {
        if(dynamic_cast<const ScrollPanel*>(p)) {
            const auto content=inset(p->bounds_,p->padding_*scale_);
            if(candidate.x+candidate.width<=content.x||candidate.x>=content.x+content.width) return false;
            candidate.y=content.y;candidate.height=std::min(candidate.height,content.height);
        }
        if(p->clipChildren_&&intersect(candidate,inset(p->bounds_,p->padding_*scale_)).empty()) return false;
    }
    return !intersect(candidate,{0,0,logicalSize_.x,logicalSize_.y}).empty();
}
void GuiSystem::reveal(Widget& widget) {
    for(auto* p=widget.parent_;p;p=p->parent_) if(auto* scroll=dynamic_cast<ScrollPanel*>(p)) {
        const auto content=inset(p->bounds_,p->padding_*scale_);
        float shift=0;
        if(widget.bounds_.y<content.y) shift=widget.bounds_.y-content.y;
        else if(widget.bounds_.y+widget.bounds_.height>content.y+content.height) shift=widget.bounds_.y+widget.bounds_.height-content.y-content.height;
        scroll->offset_=std::clamp(scroll->offset_+shift/scale_,0.f,scroll->maximum_);
        layout(logicalSize_);
    }
}
bool GuiSystem::focusWidget(std::uint64_t id) {
    Widget* target=nullptr;
    const auto visit=[&](auto&& self,Widget& w)->void { if(w.id_==id) target=&w;for(const auto& child:w.children_) self(self,*child); };
    visit(visit,root_);
    if(!target||!target->focusable()||!reachable(*target)) return false;
    if(target->accessibilityHidden()) return false;
    blur();reveal(*target);focus_=target;resetCaret();return true;
}
bool GuiSystem::activateWidget(std::uint64_t id) {
    if(!focusWidget(id)) return false;
    if(auto* button=dynamic_cast<Button*>(focused())) { activate(*button);return true; }
    return focused()->onActivate();
}
bool GuiSystem::setAccessibleValue(std::uint64_t id,float value) {
    if(!std::isfinite(value)||!focusWidget(id)) return false;
    return focused()->onAccessibleValue(value);
}
std::vector<GuiAccessibleNode> GuiSystem::accessibilityNodes() const {
    std::vector<GuiAccessibleNode> nodes;
    const auto visit=[&](auto&& self,const Widget& w,bool enabled)->void {
        if(!w.visible_||w.accessibilityHidden_) return;
        enabled=enabled&&w.enabled_;
        GuiAccessibleNode n;n.id=w.id_;n.bounds=w.bounds_;n.disabled=!enabled;n.focused=&w==focused();n.live=w.liveRegion_;
        n.description=w.accessibleDescription_;n.name=w.accessibleName_;
        if(dynamic_cast<const TextLabel*>(&w)) n.role=GuiRole::Text;
        else if(dynamic_cast<const Button*>(&w)) n.role=GuiRole::Button;
        else if(const auto* input=dynamic_cast<const TextInput*>(&w)) { n.role=GuiRole::Textbox;n.value=input->text();if(n.name.empty()) n.name=input->placeholder(); }
        else if(dynamic_cast<const Image*>(&w)) n.role=GuiRole::Image;
        else if(dynamic_cast<const ScrollPanel*>(&w)) n.role=GuiRole::ScrollRegion;
        w.describeAccessibility(n);
        // Hook semantics cannot replace identity, layout or system-owned state.
        n.id=w.id_;n.bounds=w.bounds_;n.disabled=!enabled;n.focused=&w==focused();n.live=w.liveRegion_;
        n.clippedBounds=intersect(w.bounds_,w.clip_);n.keyboardFocusable=w.focusable()&&enabled&&reachable(w);
        if(n.name.empty()&&n.role!=GuiRole::Textbox) n.name=w.displayedText();
        if((n.role!=GuiRole::Group||!n.name.empty())&&(!w.clip_.empty()||reachable(w))) nodes.push_back(std::move(n));
        for(const auto& child:w.children_) self(self,*child,enabled);
    };visit(visit,root_,true);return nodes;
}
bool GuiSystem::key(GuiKey key,bool shift,bool repeat) {
    if(key==GuiKey::Tab) {
        std::vector<Widget*> widgets;
        const auto collect=[&](auto&& self,Widget& w)->void {
            if(!w.active()||w.accessibilityHidden_) return;
            if(w.focusable()&&reachable(w)) widgets.push_back(&w);
            for(const auto& child:w.children_) self(self,*child);
        };
        collect(collect,root_);if(widgets.empty()) return false;
        const auto it=std::find(widgets.begin(),widgets.end(),focused());
        const auto next=it==widgets.end() ? (shift ? widgets.size()-1 : 0) : (std::size_t(it-widgets.begin())+widgets.size()+(shift ? -1 : 1))%widgets.size();
        return focusWidget(widgets[next]->id_);
    }
    if(!focused()) return false;
    if(key==GuiKey::Escape) { cancelPointer();scrollCapture_=nullptr;scrollDragging_=false;blur();return true; }
    if(focused()->onKey({key,shift,repeat})) return true;
    if(key>=GuiKey::Up) {
        // Single-line text navigation remains with the native text service.
        if(focusedInput()) return false;
        if(key==GuiKey::Left||key==GuiKey::Right) return false;
        for(auto* p=focused();p;p=p->parent_) if(auto* s=dynamic_cast<ScrollPanel*>(p)) {
            float amount=0;
            if(key==GuiKey::Up||key==GuiKey::Down) amount=(key==GuiKey::Up ? -32.f : 32.f);
            if(key==GuiKey::PageUp||key==GuiKey::PageDown) amount=(key==GuiKey::PageUp ? -1.f : 1.f)*s->bounds_.height/scale_;
            s->offset_=key==GuiKey::Home ? 0 : key==GuiKey::End ? s->maximum_ : std::clamp(s->offset_+amount,0.f,s->maximum_);
            layout(logicalSize_);return true;
        }
        return false;
    }
    if(key==GuiKey::Enter||key==GuiKey::Space) {
        if(repeat) return true;
        if(focused()->onActivate()) return true;
        if(auto* button=dynamic_cast<Button*>(focused())) { if(!repeat) activate(*button);return true; }
        if(key==GuiKey::Enter) if(auto* input=focusedInput()) { if(!repeat&&!input->composing_&&input->submit_) { auto callback=input->submit_;callback(); }return true; }
    }
    return false;
}
void GuiSystem::editText(std::string value,std::size_t start,std::size_t end,bool composing,bool backward) {
    if(auto* input=focusedInput()) {
        gui_detail::validate(value);
        input->composing_=composing;input->setText(std::move(value));input->setSelection(start,end,backward);resetCaret();
    }
}
void GuiSystem::text(std::string_view value,Vec2 position,GuiRect clip,GuiColor color) {
    if(!font_) return;
    for(std::size_t offset=0;offset<value.size();) {
        const auto& glyph=font_->glyph(gui_detail::next(value,offset));
        auto r=glyph.bounds;r.x=r.x*scale_+position.x;r.y=r.y*scale_+position.y+font_->ascent()*scale_;r.width*=scale_;r.height*=scale_;
        if(!r.empty()) draw_.quad(r,clip,color,font_->atlas(glyph.page),glyph.uv);
        position.x+=glyph.advance*scale_;
    }
}
const GuiDrawList& GuiSystem::draw(double seconds) {
    if(!std::isfinite(seconds)) throw std::invalid_argument("Invalid GUI time");
    const auto validate=[&](auto&& self,const Widget& widget)->void {
        if(!widget.visible()) return;
        widget.style.validate();validStates(widget.stateStyles);resolvedStyle(widget);
        for(const auto& child:widget.children()) self(self,*child);
    };
    validate(validate,root_); // Failed style edits retain the last complete draw list.
    lastTime_=seconds;draw_.clear();
    if(font_) preflight(root_,*font_); // Finish atlas changes before commands reference snapshots.
    paint(root_,seconds);return draw_;
}
void GuiSystem::paint(Widget& widget,double seconds) {
    if(!widget.visible_) return;
    const auto resolved=resolvedStyle(widget);
    auto style=resolved;
    for(auto* c:{&style.background,&style.foreground,&style.border,&style.accent,&style.focusRing,&style.selection,&style.placeholder}) c->a*=style.opacity;
    if(!widget.clip_.empty()) {
        auto foreground=style.foreground;auto background=style.background;
        const bool focused=&widget==this->focused();
        const auto box=[&](GuiColor fill) {
            draw_.quad(widget.bounds_,widget.clip_,focused ? style.focusRing : style.border);
            draw_.quad(inset(widget.bounds_,scale_),widget.clip_,fill);
        };
        const float line=(font_ ? font_->lineHeight() : 20)*scale_;
        if(dynamic_cast<Panel*>(&widget)) box(background);
        else if(auto* label=dynamic_cast<TextLabel*>(&widget)) {
            float y=widget.bounds_.y+(widget.bounds_.height-label->lines_.size()*line)/2;
            for(const auto& value:label->lines_) { text(value,{widget.bounds_.x,y},widget.clip_,foreground);y+=line; }
        }
        else if(auto* image=dynamic_cast<Image*>(&widget)) { if(image->texture()) draw_.quad(widget.bounds_,widget.clip_,foreground,image->texture()); }
        else if(auto* button=dynamic_cast<Button*>(&widget)) {
            box(background);
            const float width=font_ ? font_->measure(button->text())*scale_ : 0;
            text(button->text(),{widget.bounds_.x+(widget.bounds_.width-width)/2,widget.bounds_.y+(widget.bounds_.height-line)/2},intersect(inset(widget.bounds_,4*scale_),widget.clip_),foreground);
        } else if(auto* input=dynamic_cast<TextInput*>(&widget)) {
            box(background);const auto content=intersect(inset(widget.bounds_,8*scale_),widget.clip_);
            const float width=std::max(0.f,widget.bounds_.width/scale_-18);
            const float end=font_ ? font_->measure(std::string_view(input->text_).substr(0,input->selectionEnd_)) : 0;
            const float start=font_ ? font_->measure(std::string_view(input->text_).substr(0,input->selectionStart_)) : 0;
            const float total=font_ ? font_->measure(input->text_) : 0;
            const float caret=input->selectionBackward_ ? start : end;
            if(focused) {
                if(caret-input->scroll_>width) input->scroll_=caret-width;
                if(caret<input->scroll_) input->scroll_=caret;
            }
            input->scroll_=std::clamp(input->scroll_,0.f,std::max(0.f,total-width));
            const float x=widget.bounds_.x+(9-input->scroll_)*scale_,y=widget.bounds_.y+(widget.bounds_.height-line)/2;
            if(focused&&start!=end) draw_.quad({x+start*scale_,y,(end-start)*scale_,line},content,style.selection);
            if(input->text_.empty()) text(input->placeholder_,{widget.bounds_.x+9*scale_,y},content,style.placeholder);
            else text(input->text_,{x,y},content,foreground);
            if(focused&&(input->composing_||std::fmod(std::max(0.0,seconds-caretEpoch_),1.0)<0.55)) draw_.quad({x+caret*scale_,y,scale_,line},content,foreground);
            if(focused&&input->composing_) draw_.quad({x+start*scale_,y+line-scale_,std::max(2.f,end-start)*scale_,scale_},content,foreground);
        }
    }
    if(!widget.clip_.empty()) {
        GuiPainter painter(draw_,font_.get(),widget.bounds_,widget.clip_,scale_,&widget==hover_,&widget==captured_,
            &widget==focused(),widget.active(),seconds,resolved);
        widget.onPaint(painter);
    }
    for(const auto& child:widget.children_) paint(*child,seconds);
    if(const auto* scroll=dynamic_cast<const ScrollPanel*>(&widget);scroll&&scroll->maximum_>0&&!widget.clip_.empty()) {
        const auto content=inset(widget.bounds_,widget.padding_*scale_);
        const float height=content.height*content.height/(content.height+scroll->maximum_*scale_);
        const float y=content.y+(content.height-height)*scroll->offset_/scroll->maximum_;
        draw_.quad({widget.bounds_.x+widget.bounds_.width-4*scale_,y,3*scale_,height},widget.clip_,style.accent);
    }
}
}

void engine::GuiSystem::clear() {
    cancelPointer(); blur(); hover_=nullptr; scrollCapture_=nullptr;
    scrollDragging_=false; selectionAnchor_=0;
    root_.children_.clear(); draw_={};
}
