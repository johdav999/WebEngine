#include "engine/gui.hpp"
#include "gui_utf8.hpp"
#include <atomic>

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
void Widget::setPadding(float value) { validSpacing(value);padding_=value; }
void Widget::setLayout(VerticalLayout value) { validSpacing(value.gap);gap_=value.gap;layout_=GuiLayout::Vertical; }
void Widget::setLayout(HorizontalLayout value) { validSpacing(value.gap);gap_=value.gap;layout_=GuiLayout::Horizontal; }
void Widget::setLayout(WrapLayout value) { validSpacing(value.gap);validSpacing(value.rowGap);gap_=value.gap;rowGap_=value.rowGap;layout_=GuiLayout::Wrap; }
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
void GuiSystem::layout(Vec2 logicalSize) {
    valid(logicalSize,true);logicalSize_=logicalSize;root_.size_=logicalSize;
    arrange(root_,{0,0,logicalSize.x,logicalSize.y},{0,0,logicalSize.x,logicalSize.y},root_.visible_);
    if(focus_&&(!focus_->active()||focus_->clip_.empty())) blur();
    if(captured_&&(!captured_->active()||captured_->clip_.empty())) captured_=nullptr;
    if(scrollCapture_&&!scrollCapture_->active()) { scrollCapture_=nullptr;scrollDragging_=false; }
    if(hover_&&!hover_->active()) hover_=nullptr;
}
void GuiSystem::arrange(Widget& widget,GuiRect bounds,GuiRect clip,bool shown) {
    shown=shown&&widget.visible_;widget.bounds_=bounds;widget.clip_=shown ? intersect(bounds,clip) : GuiRect{};
    const auto content=inset(bounds,widget.padding_*scale_);
    auto* scroll=dynamic_cast<ScrollPanel*>(&widget);
    const auto childClip=(widget.clipChildren_||scroll) ? intersect(content,clip) : clip;
    float cursor=0,rowHeight=0,rowY=0,extent=0;
    std::vector<GuiRect> rectangles;
    rectangles.reserve(widget.children_.size());
    for(const auto& child:widget.children_) {
        auto& c=*child;const auto position=Vec2{c.position_.x*scale_,c.position_.y*scale_};
        GuiRect r{content.x+position.x,content.y+position.y,c.stretchWidth_ ? content.width : c.size_.x*scale_,c.stretchHeight_ ? content.height : c.size_.y*scale_};
        if(auto* label=dynamic_cast<TextLabel*>(&c);label&&font_) {
            label->lines_=wrapText(label->text_,*font_,r.width/scale_,label->wrap_);
            if(label->wrap_&&label->autoHeight_) r.height=label->lines_.size()*font_->lineHeight()*scale_;
        }
        if(widget.layout_==GuiLayout::Vertical) { r.y=content.y+cursor+position.y;if(c.visible_) cursor+=r.height+widget.gap_*scale_; }
        else if(widget.layout_==GuiLayout::Horizontal) { r.x=content.x+cursor+position.x;if(c.visible_) cursor+=r.width+widget.gap_*scale_; }
        else if(widget.layout_==GuiLayout::Wrap) {
            if(c.visible_&&cursor>0&&cursor+r.width>content.width) { rowY+=rowHeight+widget.rowGap_*scale_;cursor=0;rowHeight=0; }
            r.x=content.x+cursor+position.x;r.y=content.y+rowY+position.y;
            if(c.visible_) { cursor+=r.width+widget.gap_*scale_;rowHeight=std::max(rowHeight,r.height); }
        }
        else switch(c.anchor_) {
            case GuiAnchor::TopRight:r.x=content.x+content.width-r.width-position.x;break;
            case GuiAnchor::BottomLeft:r.y=content.y+content.height-r.height-position.y;break;
            case GuiAnchor::BottomRight:r.x=content.x+content.width-r.width-position.x;r.y=content.y+content.height-r.height-position.y;break;
            case GuiAnchor::Center:r.x=content.x+(content.width-r.width)/2+position.x;r.y=content.y+(content.height-r.height)/2+position.y;break;
            default:break;
        }
        if(c.visible_) extent=std::max(extent,r.y+r.height-content.y);
        rectangles.push_back(r);
    }
    if(scroll) { scroll->maximum_=std::max(0.f,(extent-content.height)/scale_);scroll->offset_=std::min(scroll->offset_,scroll->maximum_); }
    for(std::size_t i=0;i<widget.children_.size();++i) {
        auto r=rectangles[i];if(scroll) r.y-=scroll->offset_*scale_;
        arrange(*widget.children_[i],r,childClip,shown);
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
bool GuiSystem::pointer(GuiPointer action,Vec2 position,bool extend) {
    valid(position,false);
    auto* target=hit(root_,position);hover_=target;
    if(captured_&&!captured_->active()) captured_=nullptr;
    if(action==GuiPointer::Cancel) { const bool consumed=captured_||scrollCapture_;captured_=nullptr;scrollCapture_=nullptr;scrollDragging_=false;hover_=nullptr;return consumed; }
    if(action==GuiPointer::Down) {
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
    } else if(action==GuiPointer::Move&&(captured_||scrollCapture_)) {
        if(scrollCapture_) {
            if(std::hypot(position.x-pointerStart_.x,position.y-pointerStart_.y)>6*scale_) scrollDragging_=true;
            if(scrollDragging_) {
                scrollCapture_->offset_=std::clamp(scrollCapture_->offset_+(pointerLast_.y-position.y)/scale_,0.f,scrollCapture_->maximum_);
                captured_=nullptr;layout(logicalSize_);
            }
            pointerLast_=position;
        }
        if(auto* input=dynamic_cast<TextInput*>(captured_);input&&font_) {
            const auto offset=font_->byteAt(input->text_,(position.x-input->bounds_.x)/scale_-9+input->scroll_);
            input->setSelection(selectionAnchor_,offset,offset<selectionAnchor_);resetCaret();
        }
    } else if(action==GuiPointer::Up) {
        if(scrollDragging_) { scrollCapture_=nullptr;scrollDragging_=false;captured_=nullptr;return true; }
        scrollCapture_=nullptr;
        auto* previous=captured_;captured_=nullptr;
        if(previous&&previous==target) if(auto* button=dynamic_cast<Button*>(previous)) activate(*button);
        return previous||target;
    }
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
        arrange(root_,{0,0,logicalSize_.x,logicalSize_.y},{0,0,logicalSize_.x,logicalSize_.y},root_.visible_);
    }
}
bool GuiSystem::focusWidget(std::uint64_t id) {
    Widget* target=nullptr;
    const auto visit=[&](auto&& self,Widget& w)->void { if(w.id_==id) target=&w;for(const auto& child:w.children_) self(self,*child); };
    visit(visit,root_);
    if(!target||!target->focusable()||!reachable(*target)) return false;
    blur();reveal(*target);focus_=target;resetCaret();return true;
}
bool GuiSystem::activateWidget(std::uint64_t id) {
    if(!focusWidget(id)) return false;
    if(auto* button=dynamic_cast<Button*>(focused())) { activate(*button);return true; }
    return false;
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
        if(n.name.empty()&&n.role!=GuiRole::Textbox) n.name=w.displayedText();
        if((n.role!=GuiRole::Group||!n.name.empty())&&(!w.clip_.empty()||reachable(w))) nodes.push_back(std::move(n));
        for(const auto& child:w.children_) self(self,*child,enabled);
    };visit(visit,root_,true);return nodes;
}
bool GuiSystem::key(GuiKey key,bool shift,bool repeat) {
    if(key==GuiKey::Tab) {
        std::vector<Widget*> widgets;
        const auto collect=[&](auto&& self,Widget& w)->void {
            if(!w.active()) return;
            if(w.focusable()&&reachable(w)) widgets.push_back(&w);
            for(const auto& child:w.children_) self(self,*child);
        };
        collect(collect,root_);if(widgets.empty()) return false;
        const auto it=std::find(widgets.begin(),widgets.end(),focused());
        const auto next=it==widgets.end() ? (shift ? widgets.size()-1 : 0) : (std::size_t(it-widgets.begin())+widgets.size()+(shift ? -1 : 1))%widgets.size();
        return focusWidget(widgets[next]->id_);
    }
    if(!focused()) return false;
    if(key==GuiKey::Escape) { blur();return true; }
    if(key>=GuiKey::Up) {
        // Single-line text navigation remains with the native text service.
        if(focusedInput()) return false;
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
    lastTime_=seconds;draw_.clear();
    if(font_) preflight(root_,*font_); // Finish atlas changes before commands reference snapshots.
    paint(root_,seconds);return draw_;
}
void GuiSystem::paint(Widget& widget,double seconds) {
    if(!widget.visible_) return;
    if(!widget.clip_.empty()) {
        auto foreground=widget.style.foreground;auto background=widget.style.background;
        if(!widget.active()) { foreground.a*=0.4f;background.a*=0.6f; }
        const bool focused=&widget==this->focused();
        const auto box=[&](GuiColor fill) {
            draw_.quad(widget.bounds_,widget.clip_,focused ? widget.style.accent : widget.style.border);
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
            if(widget.active()&&(&widget==hover_||focused)) background=widget.style.accent;
            if(&widget==captured_) { background.r*=0.7f;background.g*=0.7f;background.b*=0.7f; }
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
            if(focused&&start!=end) draw_.quad({x+start*scale_,y,(end-start)*scale_,line},content,{0.08f,0.38f,0.6f,0.85f});
            if(input->text_.empty()) { auto muted=foreground;muted.a*=0.55f;text(input->placeholder_,{widget.bounds_.x+9*scale_,y},content,muted); }
            else text(input->text_,{x,y},content,foreground);
            if(focused&&(input->composing_||std::fmod(std::max(0.0,seconds-caretEpoch_),1.0)<0.55)) draw_.quad({x+caret*scale_,y,scale_,line},content,foreground);
            if(focused&&input->composing_) draw_.quad({x+start*scale_,y+line-scale_,std::max(2.f,end-start)*scale_,scale_},content,foreground);
        }
    }
    for(const auto& child:widget.children_) paint(*child,seconds);
    if(const auto* scroll=dynamic_cast<const ScrollPanel*>(&widget);scroll&&scroll->maximum_>0&&!widget.clip_.empty()) {
        const auto content=inset(widget.bounds_,widget.padding_*scale_);
        const float height=content.height*content.height/(content.height+scroll->maximum_*scale_);
        const float y=content.y+(content.height-height)*scroll->offset_/scroll->maximum_;
        draw_.quad({widget.bounds_.x+widget.bounds_.width-4*scale_,y,3*scale_,height},widget.clip_,widget.style.accent);
    }
}
}
