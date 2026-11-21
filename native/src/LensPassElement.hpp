#pragma once

#include "Globals.hpp"
#include "hlcompat.hpp"

#include <hyprland/src/render/pass/PassElement.hpp>

class CLensPassElement : public IPassElement {
  public:
    explicit CLensPassElement(const SLensFrame& frame);
    ~CLensPassElement() override = default;

    std::vector<UP<IPassElement>> draw(HLC_CTX_PARAM(ctx)) override;
    bool                          needsLiveBlur(HLC_CTX_PARAM()) override { return false; }
    bool                          needsPrecomputeBlur(HLC_CTX_PARAM()) override { return false; }
    std::optional<CBox>           boundingBox(HLC_CTX_PARAM(ctx)) override;
    CRegion                       opaqueRegion(HLC_CTX_PARAM()) override { return {}; }
    bool                          disableSimplification(HLC_CTX_PARAM()) override { return true; }

    const char*      passName() override { return "CLensPassElement"; }
    ePassElementType type() override { return EK_CUSTOM; }

    // Everything the lens can touch, global logical (includes rim and shadow).
    static CBox bounds(const SLensFrame& frame);

  private:
    SLensFrame m_frame;
};

namespace LensShader {
    // Development switch: copy the whole frame instead of just the lens' patch,
    // to compare the two.
    void setFullCopy(bool on);
    // Frames drawn so far (development aid, `hyprctl hyprlens debug frames`).
    unsigned long draws();
    // Compiled lazily on first draw (needs the GL context); freed on unload.
    void destroy();
    // Drop the frame copy so no old screen content survives (e.g. across a lock).
    void invalidate();
}
