#pragma once

#include "Globals.hpp"

#include <hyprland/src/render/pass/PassElement.hpp>

class CLensPassElement : public IPassElement {
  public:
    explicit CLensPassElement(const SLensFrame& frame);
    ~CLensPassElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    bool                          needsLiveBlur() override { return false; }
    bool                          needsPrecomputeBlur() override { return false; }
    std::optional<CBox>           boundingBox() override;
    CRegion                       opaqueRegion() override { return {}; }
    bool                          disableSimplification() override { return true; }

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
}
