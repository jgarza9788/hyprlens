#include "LensPassElement.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "Theme.hpp"

#include <algorithm>
#include <cmath>

namespace {
    const char* LENS_FRAG =
#include "lens.frag.inc"
        ;

    struct SUniforms {
        GLint style = -1, pxScale = -1, openAmt = -1, accent = -1, pointerOff = -1, boxSize = -1, radius = -1, zoom = -1, roundness = -1, cornerPx = -1, rimPx = -1, sampleRect = -1, alpha = -1, tex = -1, fbSize = -1;
    };

    SP<CShader>              s_shader;
    SUniforms                s_u;
    bool                     s_failed = false;
    bool                     g_fullCopy = false;
    // Glass bends the view near the rim by up to this much (keep in sync with lens.frag).
    constexpr float GLASS_BEND = 0.45f;
    SP<Render::IFramebuffer> s_sample; // copy of the frame, so the lens reads what's under it

    constexpr float SHADOW      = 28.f; // shadow and glow reach beyond the rim, logical px
    constexpr float CORNER      = 14.f; // corner radius of the square lens, logical

    Render::GL::CGLFramebuffer* asGL(const SP<Render::IFramebuffer>& fb) {
        return fb ? dynamic_cast<Render::GL::CGLFramebuffer*>(fb.get()) : nullptr;
    }

    // Copy what has been rendered so far into s_sample, so the lens reads
    // pixels it isn't drawing over. `region` is the part the lens can sample, in
    // monitor-local physical pixels (screen orientation, top-left origin); with
    // none, or with `fullCopy` set, the whole frame is copied. Anything outside
    // the region is stale, which is fine: the shader never samples there.
    bool copyFrame(const std::optional<CBox>& region, float rect[4]) {
        const auto src   = g_pHyprRenderer->m_renderData.currentFB;
        auto*      srcGL = asGL(src);
        if (!srcGL || src->m_size.x < 1 || src->m_size.y < 1)
            return false;

        if (!s_sample)
            s_sample = g_pHyprRenderer->createFB("hyprlens-sample");
        const bool realloc = s_sample->m_size != src->m_size || s_sample->m_drmFormat != src->m_drmFormat;
        if (realloc)
            s_sample->alloc((int)src->m_size.x, (int)src->m_size.y, src->m_drmFormat);
        auto* dstGL = asGL(s_sample);
        if (!dstGL)
            return false;
        if (realloc) { // never leave uninitialised or old texels around
            glBindFramebuffer(GL_FRAMEBUFFER, dstGL->getFBID());
            glClearColor(0.f, 0.f, 0.f, 0.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        const int w = (int)src->m_size.x, h = (int)src->m_size.y;
        int       x0 = 0, y0 = 0, x1 = w, y1 = h;
        const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
        if (region && !g_fullCopy && mon) {
            // Into framebuffer pixels with the same projection the lens quad is
            // drawn with, so any monitor rotation or flip is handled the same way.
            const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(mon->m_transform));
            const auto m         = g_pHyprRenderer->projectBoxToTarget(*region, transform).getMatrix(); // row-major, clip = M * (u, v, 1)
            double     minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
            for (int c = 0; c < 4; ++c) {
                const float u = c & 1, v = (c >> 1) & 1;
                const double px = ((m[0] * u + m[1] * v + m[2]) * 0.5 + 0.5) * w;
                const double py = ((m[3] * u + m[4] * v + m[5]) * 0.5 + 0.5) * h;
                minX = std::min(minX, px); maxX = std::max(maxX, px);
                minY = std::min(minY, py); maxY = std::max(maxY, py);
            }
            x0 = std::clamp((int)std::floor(minX) - 2, 0, w);
            y0 = std::clamp((int)std::floor(minY) - 2, 0, h);
            x1 = std::clamp((int)std::ceil(maxX) + 2, 0, w);
            y1 = std::clamp((int)std::ceil(maxY) + 2, 0, h);
        }
        if (x1 <= x0 || y1 <= y0)
            return false;
        rect[0] = x0; rect[1] = y0; rect[2] = x1; rect[3] = y1;
        // The pass scissors elements to their damage; that would clip the blit.
        Render::GL::g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, srcGL->getFBID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstGL->getFBID());
        glBlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, srcGL->getFBID());
        return true;
    }

    bool ensureShader() {
        if (s_shader)
            return true;
        if (s_failed)
            return false;

        auto shader = makeShared<CShader>();
        if (!shader->createProgram(Render::GL::g_pHyprOpenGL->m_shaders->TEXVERTSRC, LENS_FRAG, true)) {
            s_failed = true;
            HyprlandAPI::addNotification(PHANDLE, std::format("[{}] lens shader failed to compile", PLUGIN_NAME), CHyprColor{1.0, 0.2, 0.2, 1.0}, 6000);
            return false;
        }

        const auto p = shader->program();
        s_u.boxSize   = glGetUniformLocation(p, "boxSize");
        s_u.pointerOff = glGetUniformLocation(p, "pointerOff");
        s_u.accent    = glGetUniformLocation(p, "accent");
        s_u.openAmt   = glGetUniformLocation(p, "openAmt");
        s_u.style     = glGetUniformLocation(p, "style");
        s_u.pxScale   = glGetUniformLocation(p, "pxScale");
        s_u.radius    = glGetUniformLocation(p, "radius");
        s_u.zoom      = glGetUniformLocation(p, "zoom");
        s_u.roundness = glGetUniformLocation(p, "roundness");
        s_u.cornerPx  = glGetUniformLocation(p, "cornerPx");
        s_u.rimPx     = glGetUniformLocation(p, "rimPx");
        s_u.sampleRect = glGetUniformLocation(p, "sampleRect");
        s_u.alpha     = glGetUniformLocation(p, "alpha");
        s_u.tex       = glGetUniformLocation(p, "tex");
        s_u.fbSize    = glGetUniformLocation(p, "fbSize");
        s_shader      = shader;
        return true;
    }
}

void LensShader::setFullCopy(bool on) {
    g_fullCopy = on;
}

void LensShader::invalidate() {
    s_sample.reset();
}

void LensShader::destroy() {
    if (s_shader)
        s_shader->destroy();
    s_shader.reset();
    s_sample.reset();
    s_failed = false;
}

CBox CLensPassElement::bounds(const SLensFrame& f) {
    const float r = f.lens.size / 2.f + Logic::RIM + SHADOW;
    return CBox{f.lensCenter.x - r, f.lensCenter.y - r, r * 2.0, r * 2.0};
}

CLensPassElement::CLensPassElement(const SLensFrame& frame) : m_frame(frame) {}

std::optional<CBox> CLensPassElement::boundingBox() {
    const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (!mon)
        return std::nullopt;
    CBox box = bounds(m_frame);
    box.translate(-mon->m_position);
    return box;
}

namespace { unsigned long g_draws = 0; }
unsigned long LensShader::draws() {
    return g_draws;
}

std::vector<UP<IPassElement>> CLensPassElement::draw() {
    ++g_draws;
    const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (!mon || m_frame.alpha <= 0.001f || !ensureShader())
        return {};

    const float scale = mon->m_scale;
    const auto& lens  = m_frame.lens;

    // The patch the lens can sample: the square of half-width radius/zoom around
    // the pointer, plus a little for smooth filtering.
    const float    radiusPx = lens.size / 2.f * m_frame.scale * scale;
    const Vector2D pointerPx = (m_frame.center - mon->m_position) * scale;
    // Glass samples up to (1 + GLASS_BEND)x further out near the rim, and square
    // corners reach sqrt(2)x the half-width.
    const double   patchHalf = radiusPx * std::sqrt(2.0) * (1.0 + GLASS_BEND) / std::max(lens.zoom, 1.f) + 4.0;
    float          sampleRect[4];
    if (!copyFrame(CBox{pointerPx.x - patchHalf, pointerPx.y - patchHalf, patchHalf * 2.0, patchHalf * 2.0}, sampleRect))
        return {};

    // Square box centred on the lens, monitor-local physical pixels.
    const Vector2D centerPx = (m_frame.lensCenter - mon->m_position) * scale;
    const float    half     = (lens.size / 2.f + Logic::RIM + SHADOW) * scale;
    CBox           box{centerPx.x - half, centerPx.y - half, half * 2.0, half * 2.0};
    box.round();

    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(mon->m_transform));
    Mat3x3     proj      = g_pHyprRenderer->projectBoxToTarget(box, transform);
    proj.transpose();

    Render::GL::g_pHyprOpenGL->blend(true);
    auto shader = Render::GL::g_pHyprOpenGL->useShader(s_shader);

    glActiveTexture(GL_TEXTURE0);
    s_sample->getTexture()->bind();
    const GLint filter = lens.smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glUniform1i(s_u.tex, 0);
    glUniform2f(s_u.fbSize, s_sample->m_size.x, s_sample->m_size.y);

    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, proj.getMatrix());
    glUniform2f(s_u.boxSize, box.w, box.h);
    // Where the pointer is relative to the lens centre (nonzero at screen edges).
    const Vector2D off = (m_frame.center - m_frame.lensCenter) * scale;
    glUniform2f(s_u.pointerOff, off.x, off.y);
    glUniform4f(s_u.sampleRect, sampleRect[0], sampleRect[1], sampleRect[2], sampleRect[3]);
    glUniform1f(s_u.radius, radiusPx);
    glUniform1f(s_u.zoom, lens.zoom);
    // A square lens is still a round dot until it opens.
    glUniform1f(s_u.roundness, std::max(lens.round ? 1.f : 0.f, 1.f - m_frame.open));
    glUniform1f(s_u.openAmt, m_frame.open);
    glUniform1f(s_u.cornerPx, CORNER * m_frame.scale * scale);
    // Opening: the ring starts thick enough to fill the lens (a solid dot) and
    // thins down to its resting width, revealing the view.
    const bool  glass = lens.style == "glass" && !m_frame.reduced; // reduced motion: a plain ring
    const float rest  = (lens.style == "none" ? 0.f : glass ? Logic::RIM * 0.6f : Logic::RIM) * scale;
    glUniform1f(s_u.rimPx, rest + (radiusPx - rest) * (1.f - m_frame.open));
    glUniform1f(s_u.alpha, m_frame.alpha);
    std::array<float, 3> colour = Theme::accent();
    if (lens.outline != "accent") {
        if (const auto c = Logic::parseHexColor(lens.outline))
            colour = *c;
    } else if (glass)
        for (auto& ch : colour) // glass is mostly neutral: white with a faint trace of the accent
            ch = 1.f + (ch - 1.f) * 0.12f;
    glUniform3f(s_u.accent, colour[0], colour[1], colour[2]);
    glUniform1i(s_u.style, lens.style == "glass" && !glass ? 0 : Logic::styleIndex(lens.style));
    glUniform1f(s_u.pxScale, scale);

    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
    Render::GL::g_pHyprOpenGL->scissor(box);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    Render::GL::g_pHyprOpenGL->scissor(nullptr);
    glBindVertexArray(0);

    return {};
}
