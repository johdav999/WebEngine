#include "engine/gui.hpp"
#include "gui_utf8.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <unordered_map>

namespace engine {
struct GuiFont::Impl {
    static constexpr int side=512;
    struct Page {
        std::vector<std::uint8_t> pixels=std::vector<std::uint8_t>(side*side*4,0);
        std::shared_ptr<const Texture> snapshot;
        int x=1,y=1,row=0;
    };
    std::vector<unsigned char> bytes;
    stbtt_fontinfo font{};
    float scale=0,height=0,ascent=0;
    std::unordered_map<std::uint32_t,Glyph> glyphs;
    std::vector<Page> pages;
};
GuiFont::GuiFont() : impl_(std::make_unique<Impl>()) {}
GuiFont::~GuiFont()=default;
std::shared_ptr<GuiFont> GuiFont::loadTrustedTrueType(std::span<const std::byte> bytes,float height) {
    if(bytes.size()<12 || !std::isfinite(height) || height<8 || height>128)
        throw std::invalid_argument("Invalid trusted GUI font or size");
    auto result=std::shared_ptr<GuiFont>(new GuiFont);
    auto& p=*result->impl_;
    p.bytes.assign(reinterpret_cast<const unsigned char*>(bytes.data()),reinterpret_cast<const unsigned char*>(bytes.data())+bytes.size());
    // stb_truetype assumes well-formed font tables. This API explicitly accepts
    // only vetted application font assets; never call it with user uploads.
    if(!stbtt_InitFont(&p.font,p.bytes.data(),0)) throw std::invalid_argument("Unsupported trusted GUI font");
    p.scale=stbtt_ScaleForPixelHeight(&p.font,height);
    int ascent,descent,gap;stbtt_GetFontVMetrics(&p.font,&ascent,&descent,&gap);
    p.ascent=ascent*p.scale;p.height=(ascent-descent+gap)*p.scale;
    result->glyph(0); // Reserve one shared missing-glyph fallback.
    return result;
}
const GuiFont::Glyph& GuiFont::glyph(std::uint32_t codepoint) {
    auto& p=*impl_;
    const int index=stbtt_FindGlyphIndex(&p.font,static_cast<int>(codepoint));
    if(const auto it=p.glyphs.find(index);it!=p.glyphs.end()) return it->second;
    int advance,bearing,x0,y0,x1,y1;
    stbtt_GetGlyphHMetrics(&p.font,index,&advance,&bearing);
    stbtt_GetGlyphBitmapBox(&p.font,index,p.scale,p.scale,&x0,&y0,&x1,&y1);
    const int width=x1-x0,height=y1-y0;
    if(width+2>Impl::side || height+2>Impl::side) throw std::runtime_error("GUI glyph exceeds atlas page");
    if(p.pages.empty()) p.pages.emplace_back();
    if(p.pages.back().x+width+1>Impl::side) {
        auto& page=p.pages.back();page.x=1;page.y+=page.row+1;page.row=0;
    }
    if(p.pages.back().y+height+1>Impl::side) {
        if(p.pages.size()>=16) return p.glyphs.at(0);
        p.pages.emplace_back();
    }
    auto& page=p.pages.back();
    Glyph glyph;glyph.advance=advance*p.scale;glyph.bounds={float(x0),float(y0),float(width),float(height)};
    glyph.page=p.pages.size()-1;
    glyph.uv={float(page.x)/Impl::side,float(page.y)/Impl::side,float(width)/Impl::side,float(height)/Impl::side};
    if(width&&height) {
        std::vector<unsigned char> alpha(width*height);
        stbtt_MakeGlyphBitmap(&p.font,alpha.data(),width,height,width,p.scale,p.scale,index);
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            const auto dest=((page.y+y)*Impl::side+page.x+x)*4;
            page.pixels[dest]=page.pixels[dest+1]=page.pixels[dest+2]=255;
            page.pixels[dest+3]=alpha[y*width+x];
        }
        page.snapshot.reset();
    }
    page.x+=width+1;page.row=std::max(page.row,height);
    return p.glyphs.emplace(index,glyph).first->second;
}
float GuiFont::measure(std::string_view value) {
    float width=0;
    for(std::size_t offset=0;offset<value.size();) width+=glyph(gui_detail::next(value,offset)).advance;
    return width;
}
std::size_t GuiFont::byteAt(std::string_view value,float x) {
    float width=0;
    for(std::size_t offset=0;offset<value.size();) {
        const auto previous=offset;const float advance=glyph(gui_detail::next(value,offset)).advance;
        if(x<width+advance*0.5f) return previous;
        width+=advance;
    }
    return value.size();
}
float GuiFont::lineHeight() const { return impl_->height; }
float GuiFont::ascent() const { return impl_->ascent; }
std::shared_ptr<const Texture> GuiFont::atlas(std::size_t pageIndex) {
    auto& page=impl_->pages.at(pageIndex);
    if(!page.snapshot) {
        auto texture=std::make_shared<Texture>();texture->name="GUI font atlas";
        texture->width=texture->height=Impl::side;texture->pixels=page.pixels;
        texture->wrapU=texture->wrapV=TextureWrap::Clamp;page.snapshot=std::move(texture);
    }
    return page.snapshot;
}
}
