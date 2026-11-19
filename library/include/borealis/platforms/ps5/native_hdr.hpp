#pragma once

#include <cmath>
#include <string>
#include <cstdint>
#include "native_hdr_mode.hpp"

// The owning SDL context supplies GL declarations. All calls stay on its UI
// thread. mpv 0.36 target-trc=linear,target-peak=10000 normalizes output to
// 10000 nits, including its color-managed subtitles (video_shaders.c).
namespace ps5_native_hdr {
constexpr float peakNits = 10000.0f;
constexpr float paperWhiteNits = 203.0f;

inline void clearColor(float r, float g, float b, float a) {
    auto linear = [](float s) {
        s = std::fmax(0.0f, std::fmin(1.0f, s));
        return s <= .04045f ? s / 12.92f : std::pow((s + .055f) / 1.055f, 2.4f);
    };
    r = linear(r); g = linear(g); b = linear(b);
    const float scale = paperWhiteNits / peakNits * a;
    glClearColor((.6274040f*r + .3292820f*g + .0433136f*b)*scale,
        (.0690970f*r + .9195400f*g + .0113612f*b)*scale,
        (.0163916f*r + .0880132f*g + .8955950f*b)*scale, a);
}

class Frame {
public:
    Frame() = default;
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    // Explicit close while the owner's context is current, never in a global
    // destructor after SDL/EGL teardown. No per-frame allocation or readback.
    bool open(unsigned w, unsigned h) {
        if (fbo || !w || !h || w > 3840 || h > 2160 || glGetError() != GL_NO_ERROR)
            return false;
        width = w; height = h;
        glGenTextures(1, &texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glGenRenderbuffers(1, &stencil);
        glBindRenderbuffer(GL_RENDERBUFFER, stencil);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, w, h);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return fail();
        const char* vertex = "#version 330 core\n"
            "out vec2 uv; void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
            "uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}";
        // Same ST2084 constants as the qualified HDR compositor. Input is
        // premultiplied linear BT.2020 normalized to 10000 nits over black.
        const char* fragment = "#version 330 core\n"
            "uniform sampler2D scene; in vec2 uv; out vec4 color;"
            "void main(){vec3 l=clamp(texture(scene,uv).rgb,0.0,1.0);"
            "vec3 p=pow(l,vec3(2610.0/16384.0));"
            "color=vec4(pow((3424.0/4096.0+2413.0/128.0*p)/"
            "(1.0+2392.0/128.0*p),vec3(2523.0/32.0)),1);}";
        GLuint vs = shader(GL_VERTEX_SHADER, vertex), fs = shader(GL_FRAGMENT_SHADER, fragment);
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return fail();
        }
        program = glCreateProgram();
        glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
        glDeleteShader(vs); glDeleteShader(fs);
        GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) return fail();
        glUseProgram(program);
        GLint location = glGetUniformLocation(program, "scene");
        if (location < 0) return fail();
        glUniform1i(location, 0);
        glGenVertexArrays(1, &vao);
        glUseProgram(0); glBindTexture(GL_TEXTURE_2D, 0);
        if (!texture || !stencil || !fbo || !vao || glGetError() != GL_NO_ERROR) return fail();
        // PQ video layer (RGBA16F holds encoded values exactly) and the
        // composite/overlay programs. Same ST 2084 constants as the conversion.
        glGenTextures(1, &videoTexture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, videoTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glGenFramebuffers(1, &videoFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, videoFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, videoTexture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return fail();
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBindTexture(GL_TEXTURE_2D, 0);
        const char* pq = "vec3 pqEncode(vec3 l){vec3 p=pow(clamp(l,0.0,1.0),vec3(2610.0/16384.0));"
            "return pow((3424.0/4096.0+2413.0/128.0*p)/(1.0+2392.0/128.0*p),vec3(2523.0/32.0));}"
            "vec3 pqDecode(vec3 e){vec3 p=pow(clamp(e,0.0,1.0),vec3(32.0/2523.0));"
            "return pow(max(p-3424.0/4096.0,vec3(0.0))/(2413.0/128.0-2392.0/128.0*p),vec3(16384.0/2610.0));}";
        const std::string compositeSource = std::string("#version 330 core\n"
            "uniform sampler2D scene; uniform sampler2D video; in vec2 uv; out vec4 color;") + pq +
            "void main(){vec4 ui=texture(scene,uv);"
            "vec3 l=ui.rgb+(1.0-clamp(ui.a,0.0,1.0))*pqDecode(texture(video,uv).rgb);"
            "color=vec4(pqEncode(l),1);}";
        const std::string overlaySource = std::string("#version 330 core\n"
            "uniform sampler2D scene; in vec2 uv; out vec4 color;") + pq +
            "void main(){vec4 ui=texture(scene,uv); if(ui.a<=0.0) discard;"
            "color=vec4(pqEncode(ui.rgb/ui.a),clamp(ui.a,0.0,1.0));}";
        compositeProgram = link(vertex, compositeSource.c_str());
        overlayProgram = link(vertex, overlaySource.c_str());
        if (!compositeProgram || !overlayProgram) return fail();
        glUseProgram(compositeProgram);
        GLint sceneAt = glGetUniformLocation(compositeProgram, "scene");
        GLint videoAt = glGetUniformLocation(compositeProgram, "video");
        if (sceneAt < 0 || videoAt < 0) return fail();
        glUniform1i(sceneAt, 0); glUniform1i(videoAt, 1);
        glUseProgram(overlayProgram);
        sceneAt = glGetUniformLocation(overlayProgram, "scene");
        if (sceneAt < 0) return fail();
        glUniform1i(sceneAt, 0);
        glUseProgram(0);
        if (glGetError() != GL_NO_ERROR) return fail();
        ready = true;
        return true;
    }
    bool begin() {
        if (!ready) return false;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
        glDisable(GL_FRAMEBUFFER_SRGB);
        return true;
    }
    bool present(GLuint destination = 0) {
        // Menu converts the opaque linear UI; Composite blends it over the
        // PQ video layer; Direct already wrote the window; DirectFallback blends
        // the UI over the directly rendered window. Any failure resets prediction.
        bool presented = false;
        switch (modes.pending()) {
        case FrameMode::Menu: presented = convert(destination); break;
        case FrameMode::Composite: presented = destination == 0 && presentComposite(); break;
        case FrameMode::Direct: presented = ready && destination == 0 && glGetError() == GL_NO_ERROR; break;
        case FrameMode::DirectFallback: presented = destination == 0 && presentOverlay(); break;
        case FrameMode::DirectBounded: presented = destination == 0 && presentOverlayRegion(); break;
        // the UI went straight into the window; nothing left to present.
        case FrameMode::DirectWindow: presented = ready && destination == 0 && glGetError() == GL_NO_ERROR; break;
        }
        modes.endFrame();
        if (!presented) modes.reset();
        return presented;
    }
    ModeState& modeState() { return modes; }
    const ModeState& modeState() const { return modes; }
private:
    bool convert(GLuint destination) {
        if (!ready || destination == fbo || glGetError() != GL_NO_ERROR) return false;
        glBindFramebuffer(GL_FRAMEBUFFER, destination);
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE); glDisable(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_DITHER);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(program); glBindVertexArray(vao);
        glActiveTexture(GL_TEXTURE0); glBindSampler(0, 0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, 0); glBindVertexArray(0); glUseProgram(0);
        return glGetError() == GL_NO_ERROR;
    }
public:
    // Full color/stencil clear of the linear UI target with the current clear color.
    bool clearComposition() {
        if (!ready) return false;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilMask(0xffffffff);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        return glGetError() == GL_NO_ERROR;
    }
    GLuint videoFramebuffer() const { return ready ? videoFbo : 0; }
    // Select the bounded UI rectangle for this frame from NanoVG vertex bounds.
    bool selectUiRegion(float minX, float minY, float maxX, float maxY, float viewWidth, float viewHeight) {
        return ready && boundedUiRegion(minX, minY, maxX, maxY, viewWidth, viewHeight, width, height, uiRegion);
    }
    // Transparent color clear of the selected rectangle only. NanoVG leaves the
    // stencil at zero after every stencil fill and stroke; full clears on Menu
    // and Composite frames re-establish it.
    bool clearCompositionRegion() {
        if (!ready || !uiRegion.width || !uiRegion.height) return false;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glEnable(GL_SCISSOR_TEST);
        glScissor(uiRegion.x, uiRegion.y, uiRegion.width, uiRegion.height);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        return glGetError() == GL_NO_ERROR;
    }
    // The DirectFallback blend restricted to the selected rectangle.
    bool presentOverlayRegion() {
        if (!ready || !uiRegion.width || !uiRegion.height || glGetError() != GL_NO_ERROR) return false;
        bindFullscreen(0, overlayProgram);
        glEnable(GL_SCISSOR_TEST);
        glScissor(uiRegion.x, uiRegion.y, uiRegion.width, uiRegion.height);
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glActiveTexture(GL_TEXTURE0); glBindSampler(0, 0); glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        glBindTexture(GL_TEXTURE_2D, 0); glBindVertexArray(0); glUseProgram(0);
        return glGetError() == GL_NO_ERROR;
    }
    const UiRegion& selectedUiRegion() const { return uiRegion; }
    // UI (premultiplied linear) over decoded PQ video, encoded once into the window.
    bool presentComposite() {
        if (!ready || glGetError() != GL_NO_ERROR) return false;
        bindFullscreen(0, compositeProgram);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, videoTexture); glBindSampler(1, 0);
        glActiveTexture(GL_TEXTURE0); glBindSampler(0, 0); glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
        glBindVertexArray(0); glUseProgram(0);
        return glGetError() == GL_NO_ERROR;
    }
    // One-frame fallback: UI blended over the PQ window in PQ space.
    bool presentOverlay() {
        if (!ready || glGetError() != GL_NO_ERROR) return false;
        bindFullscreen(0, overlayProgram);
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glActiveTexture(GL_TEXTURE0); glBindSampler(0, 0); glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, 0); glBindVertexArray(0); glUseProgram(0);
        return glGetError() == GL_NO_ERROR;
    }
    bool close() {
        // Completion is a prerequisite to freeing textures used by mpv/NanoVG.
        glFinish();
        if (glGetError() != GL_NO_ERROR) return false;
        ready = false;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (compositeProgram) glDeleteProgram(compositeProgram);
        if (overlayProgram) glDeleteProgram(overlayProgram);
        if (videoFbo) glDeleteFramebuffers(1, &videoFbo);
        if (videoTexture) glDeleteTextures(1, &videoTexture);
        compositeProgram = overlayProgram = videoFbo = videoTexture = 0;
        if (program) glDeleteProgram(program);
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (stencil) glDeleteRenderbuffers(1, &stencil);
        if (texture) glDeleteTextures(1, &texture);
        vao = program = fbo = stencil = texture = 0;
        return true;
    }
    GLuint framebuffer() const { return ready ? fbo : 0; }
    // Color + depth/stencil storage only, excluding scanout, decoder and mpv.
    uint64_t imageBytes() const { return uint64_t(width) * height * 24; }
private:
    bool fail() { close(); return false; }
    static GLuint shader(GLenum type, const char* source) {
        GLuint object = glCreateShader(type);
        if (!object) return 0;
        glShaderSource(object, 1, &source, nullptr); glCompileShader(object);
        GLint compiled = 0; glGetShaderiv(object, GL_COMPILE_STATUS, &compiled);
        if (!compiled) { glDeleteShader(object); return 0; }
        return object;
    }
    GLuint texture = 0, stencil = 0, fbo = 0, program = 0, vao = 0;
    GLuint videoTexture = 0, videoFbo = 0, compositeProgram = 0, overlayProgram = 0;
    ModeState modes;
    UiRegion uiRegion;
    void bindFullscreen(GLuint destination, GLuint selected) {
        glBindFramebuffer(GL_FRAMEBUFFER, destination);
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE); glDisable(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_DITHER);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(selected); glBindVertexArray(vao);
    }
    static GLuint link(const char* vertex, const char* fragment) {
        GLuint vs = shader(GL_VERTEX_SHADER, vertex), fs = shader(GL_FRAGMENT_SHADER, fragment);
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return 0;
        }
        GLuint linked = glCreateProgram();
        glAttachShader(linked, vs); glAttachShader(linked, fs); glLinkProgram(linked);
        glDeleteShader(vs); glDeleteShader(fs);
        GLint ok = 0; glGetProgramiv(linked, GL_LINK_STATUS, &ok);
        if (!ok) { glDeleteProgram(linked); return 0; }
        return linked;
    }
    unsigned width = 0, height = 0;
    bool ready = false;
};
}
