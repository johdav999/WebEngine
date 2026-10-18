#include "webgl_renderer.hpp"
#include "underwater_render.hpp"

namespace engine {
void WebGlRenderer::releaseWaterTargets(bool lost) {
    if(!lost) {
        glDeleteFramebuffers(1,&sceneFramebuffer_);glDeleteFramebuffers(1,&waterFramebuffer_);
        glDeleteTextures(1,&sceneColor_);glDeleteTextures(1,&sceneDepth_);glDeleteTextures(1,&waterColor_);glDeleteTextures(1,&waterDepth_);
    }
    sceneFramebuffer_=sceneColor_=sceneDepth_=waterFramebuffer_=waterColor_=waterDepth_=0;
    waterWidth_=waterHeight_=0;waterCapture_=false;
}
void WebGlRenderer::prepareWaterTargets(bool required) {
    const auto w=required ? width_ : 1u,h=required ? height_ : 1u;
    if(waterWidth_==w&&waterHeight_==h) {waterCapture_=required;return;}
    std::array<GLuint,2> framebuffers{},colors{},depths{};
    try {
        for(unsigned i=0;i<2;++i) {
            glGenFramebuffers(1,&framebuffers[i]);glBindFramebuffer(GL_FRAMEBUFFER,framebuffers[i]);
            glGenTextures(1,&colors[i]);glBindTexture(GL_TEXTURE_2D,colors[i]);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,colors[i],0);
            glGenTextures(1,&depths[i]);glBindTexture(GL_TEXTURE_2D,depths[i]);
            glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,w,h,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depths[i],0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glGetError()!=GL_NO_ERROR)
                throw std::runtime_error("Cannot create water color/depth framebuffer");
        }
    } catch(...) {
        glDeleteFramebuffers(2,framebuffers.data());glDeleteTextures(2,colors.data());glDeleteTextures(2,depths.data());throw;
    }
    releaseWaterTargets();sceneFramebuffer_=framebuffers[0];sceneColor_=colors[0];sceneDepth_=depths[0];
    waterFramebuffer_=framebuffers[1];waterColor_=colors[1];waterDepth_=depths[1];waterWidth_=w;waterHeight_=h;waterCapture_=required;
    glBindFramebuffer(GL_FRAMEBUFFER,sampleCount_>1 ? msaaFramebuffer_ : required ? sceneFramebuffer_ : 0);
}
void WebGlRenderer::captureWater() {
    const auto source=sampleCount_>1 ? msaaFramebuffer_ : sceneFramebuffer_;
    glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_READ_FRAMEBUFFER,source);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,waterFramebuffer_);
    // Identical sizes and RGB8/DEPTH_COMPONENT24 formats permit MSAA color/depth resolve.
    glBlitFramebuffer(0,0,width_,height_,0,0,width_,height_,GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    const auto error=glGetError();glBindFramebuffer(GL_FRAMEBUFFER,source);glEnable(GL_SCISSOR_TEST);
    if(error!=GL_NO_ERROR) throw std::runtime_error("Water opaque color/depth resolve failed");
    ++counters_.passes;
}
void WebGlRenderer::releaseReflectionTargets(bool lost) {
    if(!lost) {glDeleteFramebuffers(1,&reflectionFramebuffer_);glDeleteTextures(1,&reflectionColor_);glDeleteRenderbuffers(1,&reflectionDepth_);}
    reflectionFramebuffer_=reflectionColor_=reflectionDepth_=0;reflectionWidth_=reflectionHeight_=0;
}
void WebGlRenderer::prepareReflectionTargets(bool required) {
    const auto w=required ? width_ : 1u,h=required ? height_ : 1u;
    if(reflectionWidth_==w&&reflectionHeight_==h) return;
    GLuint fbo=0,color=0,depth=0;
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glGetError()!=GL_NO_ERROR) {
        glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&color);glDeleteRenderbuffers(1,&depth);throw std::runtime_error("Cannot create planar reflection framebuffer");
    }
    releaseReflectionTargets();reflectionFramebuffer_=fbo;reflectionColor_=color;reflectionDepth_=depth;reflectionWidth_=w;reflectionHeight_=h;
}
void WebGlRenderer::drawUnderwater(const UnderwaterView& water,const CameraComponent& camera,const ResolvedRenderView& resolved) {
    auto viewport=resolved.viewport;viewport.y=height_-viewport.y-viewport.height;const auto uniforms=prepareUnderwater(water,camera,viewport,waterTime_);
    glUseProgram(underwaterProgram_);glBindVertexArray(underwaterVao_);
    glUniformMatrix4fv(glGetUniformLocation(underwaterProgram_,"inverseViewProjection"),1,GL_FALSE,uniforms.inverseViewProjection.m.data());
    glUniformMatrix4fv(glGetUniformLocation(underwaterProgram_,"worldToLocal"),1,GL_FALSE,uniforms.worldToLocal.m.data());
    const std::array<const char*,8> names={"viewport","eye","minimum","maximum","absorptionStrength","colorDensity","options","caustics"};
    const std::array values={uniforms.viewport,uniforms.eye,uniforms.minimum,uniforms.maximum,uniforms.absorptionStrength,uniforms.colorDensity,uniforms.options,uniforms.caustics};
    for(std::size_t i=0;i<names.size();++i) glUniform4fv(glGetUniformLocation(underwaterProgram_,names[i]),1,values[i].data());
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,waterColor_);glUniform1i(glGetUniformLocation(underwaterProgram_,"sourceColor"),0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,waterDepth_);glUniform1i(glGetUniformLocation(underwaterProgram_,"sourceDepth"),1);glActiveTexture(GL_TEXTURE0);
    // Fullscreen triangle respects the current view's scissor and leaves World depth intact.
    glViewport(0,0,width_,height_);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glDrawArrays(GL_TRIANGLES,0,3);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glUseProgram(meshProgram_);
    glViewport(resolved.viewport.x,height_-resolved.viewport.y-resolved.viewport.height,resolved.viewport.width,resolved.viewport.height);
    ++counters_.passes;++counters_.drawCalls;++counters_.triangles;
}
} // namespace engine
