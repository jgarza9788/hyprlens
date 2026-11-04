// hyprlens: a live magnifier lens centered on the pointer, drawn by the
// compositor itself so it never magnifies itself.
//
//   hyprctl hyprlens toggle | on | off
//   hyprctl hyprlens zoom-in | zoom-out | bigger | smaller | size [small|medium|large]
//   hyprctl hyprlens shape | smooth
//   hyprctl hyprlens set <zoom|size|shape|smooth> <value>
//   hyprctl hyprlens get
//
// Each frame while the lens is on, a pass element copies the frame rendered so
// far and draws a magnified patch around the pointer over it.

#include "Globals.hpp"
#include "LensPassElement.hpp"

#include <hyprland/src/Compositor.hpp>
#include <lauxlib.h>
#include <lua.h>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <vector>

using namespace std::chrono;

namespace {
    constexpr float EDGE_MARGIN = 8.f; // logical px kept between the lens and a screen edge
    // Opening and closing. An accent dot grows to full size, then its ring thins
    // from solid down to the rim, revealing the view as the zoom lifts. Closing
    // runs the same steps backwards. The lens comes and goes often, so both are brief.
    constexpr float DEFAULT_OPEN_MS = 240.f, DEFAULT_CLOSE_MS = 160.f, REDUCED_MS = 100.f;
    constexpr float DOT_PX = 14.f; // the accent dot the lens grows from (logical px)

    struct SState {
        SLens                                     lens;
        float                                     shown  = 0.f; // 0 gone .. 1 settled; moves toward `target`
        float                                     target = 0.f;
        bool                                      reduceMotion = false;
        float                                     openMs  = DEFAULT_OPEN_MS;  // tunable at run time: hyprctl hyprlens debug speed <open> <close>
        float                                     closeMs = DEFAULT_CLOSE_MS;
        // What the last animation actually did, for `debug anim`.
        steady_clock::time_point                  animStart = steady_clock::now();
        unsigned long                             animFrames0 = 0;
        bool                                      animRunning = false, animOpening = false;
        std::string                               lastAnim = "none yet";
        std::optional<bool>                       debugReduce;  // development override of reduce motion
        int                                       shares       = 0; // active screen shares / recordings
        steady_clock::time_point                  lastTick     = steady_clock::now();
        steady_clock::time_point                  lastMove     = steady_clock::now() - seconds(10);
        std::optional<CBox>                       lastBounds;
        std::vector<Hyprutils::Signal::CHyprSignalListener> listeners;
        SP<SHyprCtlCommand>                       ctl;
    };
    std::unique_ptr<SState> g_state;

    float easeOutCubic(float t) { t = std::clamp(t, 0.f, 1.f); return 1.f - std::pow(1.f - t, 3.f); }

    // 0..1 smoothstep of t between a and b; the grow and open phases overlap.
    float phase(float t, float a, float b) {
        const float x = std::clamp((t - a) / (b - a), 0.f, 1.f);
        return x * x * (3.f - 2.f * x);
    }

    // Hyprland's own animations switch is the "reduce motion" signal: when it's
    // off the lens only fades, with no scale or magnification ramp.
    bool animationsOn() {
        const auto r = Config::mgr()->getConfigValue("animations:enabled");
        if (!r.dataptr || !*r.dataptr || !r.type)
            return true;
        if (*r.type == typeid(int64_t))
            return *static_cast<const int64_t*>(*r.dataptr) != 0;
        if (*r.type == typeid(bool))
            return *static_cast<const bool*>(*r.dataptr);
        return true;
    }

    // Move `shown` toward `target` from wherever it is, so reversing mid-way
    // continues smoothly instead of restarting.
    void advance() {
        const auto  now = steady_clock::now();
        const float dt  = (float)duration_cast<microseconds>(now - g_state->lastTick).count() / 1000.f;
        g_state->lastTick = now;
        const float ms   = g_state->reduceMotion ? REDUCED_MS : (g_state->target > g_state->shown ? g_state->openMs : g_state->closeMs);
        const float step = std::max(dt, 0.f) / ms; // real time: frames can be sparse, the animation still finishes on time
        if (g_state->target > g_state->shown)
            g_state->shown = std::min(g_state->target, g_state->shown + step);
        else
            g_state->shown = std::max(g_state->target, g_state->shown - step);
        if (g_state->animRunning && g_state->shown == g_state->target) {
            g_state->animRunning = false;
            g_state->lastAnim = std::format("{} took {:.0f} ms, {} frames drawn", g_state->animOpening ? "open" : "close",
                                            (float)duration_cast<microseconds>(now - g_state->animStart).count() / 1000.f, LensShader::draws() - g_state->animFrames0);
        }
    }

    bool visible() { return g_state && (g_state->lens.enabled || g_state->shown > 0.f); }

    // "Hide from captures": leave the lens out while the screen is shared or recorded.
    bool hiddenNow() { return g_state->lens.hideWhenSharing && g_state->shares > 0; }

    // Frames we ask for ourselves: while the lens animates, and briefly after the
    // pointer moves. Otherwise it only redraws when something else on screen does.
    bool wantsFrames() {
        if (g_state->shown != g_state->target)
            return true;
        return steady_clock::now() - g_state->lastMove < milliseconds(150);
    }

    PHLMONITOR monitorAt(const Vector2D& p) {
        for (const auto& m : State::monitorState()->monitors())
            if (m && CBox{m->m_position, m->m_size}.containsPoint(p))
                return m;
        return Desktop::focusState()->monitor();
    }

    // The lens sits on the pointer. Near a screen edge it is held on screen
    // (like macOS Zoom) while still magnifying the pointer's spot.
    SLensFrame frame() {
        const Vector2D pointer = g_pInputManager->getMouseCoordsInternal();
        Vector2D       centre  = pointer;
        if (const auto mon = monitorAt(pointer)) {
            const float half = g_state->lens.size / 2.f + EDGE_MARGIN;
            const float x0 = mon->m_position.x + half, x1 = mon->m_position.x + mon->m_size.x - half;
            const float y0 = mon->m_position.y + half, y1 = mon->m_position.y + mon->m_size.y - half;
            // A monitor smaller than the lens: centre it.
            centre.x = x0 > x1 ? mon->m_position.x + mon->m_size.x / 2.0 : std::clamp(pointer.x, (double)x0, (double)x1);
            centre.y = y0 > y1 ? mon->m_position.y + mon->m_size.y / 2.0 : std::clamp(pointer.y, (double)y0, (double)y1);
        }
        const float e = easeOutCubic(g_state->shown);
        SLensFrame  f{.lens = g_state->lens, .center = pointer, .lensCenter = centre, .alpha = e};
        if (!g_state->reduceMotion) {
            const float dot  = std::min(1.f, DOT_PX / std::max(1.f, g_state->lens.size));
            const float grow = phase(e, 0.f, 0.6f);
            f.open       = phase(e, 0.35f, 1.f);
            f.scale      = dot + (1.f - dot) * grow;
            f.alpha      = std::min(1.f, e * 4.f);
            f.lens.zoom  = 1.f + (g_state->lens.zoom - 1.f) * f.open; // the glass lifts off the screen
        }
        return f;
    }

    void damage(const SLensFrame& f) {
        const CBox b = CLensPassElement::bounds(f);
        if (g_state->lastBounds)
            g_pHyprRenderer->damageBox(*g_state->lastBounds);
        g_pHyprRenderer->damageBox(b);
        g_state->lastBounds = b;
    }

    void setEnabled(bool on) {
        if (g_state->lens.enabled == on)
            return;
        g_state->lens.enabled  = on;
        g_state->target        = on ? 1.f : 0.f;
        g_state->reduceMotion  = g_state->debugReduce.value_or(!animationsOn());
        g_state->lastTick      = steady_clock::now();
        g_state->animStart     = g_state->lastTick;
        g_state->animFrames0   = LensShader::draws();
        g_state->animRunning   = true;
        g_state->animOpening   = on;
        damage(frame());
    }

    // Runs before every frame: keep the lens' area damaged so it redraws with
    // the live screen and follows the pointer at the monitor's refresh rate.
    void onPreRender(PHLMONITOR mon) {
        if (!visible() || !mon)
            return;
        advance();
        if ((!g_state->lens.enabled && g_state->shown <= 0.f) || hiddenNow()) {
            if (g_state->lastBounds)
                g_pHyprRenderer->damageBox(*g_state->lastBounds);
            g_state->lastBounds.reset();
            return;
        }
        damage(frame());
    }

    // After a frame: damage the lens again and ask for the next one, so it keeps
    // animating and following the pointer at the monitor's refresh rate even
    // when nothing else on screen is changing. The renderer's current monitor
    // is already cleared at this stage, so look the monitors up ourselves.
    void onPostRender() {
        if (!visible() || !g_state->lastBounds || hiddenNow() || !wantsFrames())
            return;
        bool any = false;
        for (const auto& m : State::monitorState()->monitors()) {
            if (!m || !g_state->lastBounds->overlaps(CBox{m->m_position, m->m_size}))
                continue;
            m->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
            any = true;
        }
        if (any)
            g_pHyprRenderer->damageBox(*g_state->lastBounds);
    }

    void onRenderStage(eRenderStage stage) {
        if ((stage != RENDER_LAST_MOMENT && stage != RENDER_POST) || !visible() || g_pHyprRenderer->m_bRenderingSnapshot)
            return;
        if (stage == RENDER_POST) {
            onPostRender();
            return;
        }
        const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
        if (!mon)
            return;
        if (g_state->shown <= 0.f || hiddenNow())
            return;
        const auto f = frame();
        if (!CLensPassElement::bounds(f).overlaps(CBox{mon->m_position, mon->m_size}))
            return;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CLensPassElement>(f));
    }

    // ── saved settings ───────────────────────────────────────────────────────
    // ~/.config/omarchy/jgarza.hyprlens/settings.json: zoom, size, shape, smooth.
    // Written on every change from hyprctl or the settings window, read at load.

    std::string settingsPath() {
        const char* home = std::getenv("HOME");
        return std::string(home ? home : "") + "/.config/omarchy/jgarza.hyprlens/settings.json";
    }

    void loadSettings() {
        std::ifstream f(settingsPath());
        if (!f)
            return;
        std::stringstream ss;
        ss << f.rdbuf();
        Logic::fromFileJson(g_state->lens, ss.str());
    }

    void saveSettings() {
        const std::string path = settingsPath();
        std::error_code   ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        const std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            if (!f)
                return;
            f << Logic::toFileJson(g_state->lens);
        }
        std::filesystem::rename(tmp, path, ec);
    }

    // ── hyprctl ──────────────────────────────────────────────────────────────

    std::string json() {
        const auto& l = g_state->lens;
        return std::format(R"({{"enabled":{},"zoom":{:.3g},"size":{:.0f},"sizeName":"{}","shape":"{}","smooth":{},"style":"{}","outline":"{}","width":{:.3g},"capture":"{}"}})",
                           l.enabled, l.zoom, l.size, Logic::sizeName(l.size), l.round ? "circle" : "square", l.smooth, l.style, l.outline, l.width,
                           l.hideWhenSharing ? "hide" : "show");
    }

    std::string onHyprctl(eHyprCtlOutputFormat, std::string request) {
        if (!g_state)
            return "hyprlens: not loaded";
        if (request.size() > 512)
            return "error: request too long";

        std::vector<std::string> args;
        {
            std::istringstream in(request);
            for (std::string tok; in >> tok && args.size() < 8;)
                args.push_back(tok);
        }
        while (!args.empty() && (args.front() == "hyprlens" || args.front().starts_with("-") || args.front().starts_with("/")))
            args.erase(args.begin());

        const std::string cmd = args.empty() ? "get" : args[0];
        auto&             l   = g_state->lens;
        std::string       out = "ok";

        if (cmd == "get" || cmd == "status")
            return json();
        else if (cmd == "toggle")
            setEnabled(!l.enabled);
        else if (cmd == "on" || cmd == "open")
            setEnabled(true);
        else if (cmd == "off" || cmd == "close")
            setEnabled(false);
        else if (cmd == "zoom-in")
            Logic::zoomBy(l, 1.25f);
        else if (cmd == "zoom-out")
            Logic::zoomBy(l, 1.f / 1.25f);
        else if (cmd == "bigger")
            Logic::stepSize(l, +1);
        else if (cmd == "smaller")
            Logic::stepSize(l, -1);
        else if (cmd == "size" && args.size() < 2)
            Logic::cycleSize(l);
        else if (cmd == "size" || cmd == "set") {
            const std::string key = cmd == "size" ? "size" : (args.size() > 1 ? args[1] : "");
            const std::string val = cmd == "size" ? args[1] : (args.size() > 2 ? args[2] : "");
            if (const auto err = Logic::apply(l, key, val); !err.empty())
                out = "error: " + err;
        } else if (cmd == "debug" && args.size() >= 3 && args[1] == "fullcopy") {
            LensShader::setFullCopy(args[2] == "on");
            return "ok";
        } else if (cmd == "debug" && args.size() >= 3 && args[1] == "progress") {
            // Hold the open/close animation at a point (0..1) to look at it.
            const auto v = Logic::parseFloat(args[2]);
            if (!v)
                return "error: progress must be a number";
            l.enabled              = true;
            g_state->shown         = g_state->target = std::clamp(*v, 0.f, 1.f);
            g_state->reduceMotion  = g_state->debugReduce.value_or(!animationsOn());
            g_state->lastTick      = steady_clock::now();
            damage(frame());
            return "ok";
        } else if (cmd == "debug" && args.size() >= 2 && args[1] == "anim") {
            return std::format("last {}; open {:.0f} ms, close {:.0f} ms", g_state->lastAnim, g_state->openMs, g_state->closeMs);
        } else if (cmd == "debug" && args.size() >= 4 && args[1] == "speed") {
            // Try other timings without rebuilding: hyprctl hyprlens debug speed <open ms> <close ms>
            const auto o = Logic::parseFloat(args[2]), c = Logic::parseFloat(args[3]);
            if (!o || !c)
                return "error: usage: hyprctl hyprlens debug speed <open ms> <close ms>";
            g_state->openMs  = std::clamp(*o, 30.f, 2000.f);
            g_state->closeMs = std::clamp(*c, 30.f, 2000.f);
            return "ok";
        } else if (cmd == "debug" && args.size() >= 3 && args[1] == "reduce") {
            g_state->debugReduce = args[2] == "auto" ? std::nullopt : std::optional<bool>(args[2] == "on");
            g_state->reduceMotion = g_state->debugReduce.value_or(!animationsOn());
            return "ok";
        } else if (cmd == "debug" && args.size() >= 3 && args[1] == "shares") {
            g_state->shares = std::max(0, std::atoi(args[2].c_str()));
            if (g_state->lastBounds)
                g_pHyprRenderer->damageBox(*g_state->lastBounds);
            return "ok";
        } else if (cmd == "debug" && args.size() >= 2 && args[1] == "frames") {
            return std::format("frames drawn: {}, shares: {}", LensShader::draws(), g_state->shares);
        } else if (cmd == "shape")
            l.round = !l.round;
        else if (cmd == "smooth")
            l.smooth = !l.smooth;
        else
            return "usage: hyprctl hyprlens toggle|on|off|zoom-in|zoom-out|bigger|smaller|size|shape|smooth|set <key> <value>|get";

        if (out == "ok") {
            if (cmd != "toggle" && cmd != "on" && cmd != "open" && cmd != "off" && cmd != "close")
                saveSettings();
            return json();
        }
        return out;
    }

    // hl.plugin.hyprlens.run("zoom-in") - same commands as hyprctl hyprlens.
    // Its existence also lets hyprlens.lua test `if hl.plugin.hyprlens`.
    int luaRun(lua_State* L) {
        if (!g_state)
            return 0;
        const std::string out = onHyprctl(eHyprCtlOutputFormat::FORMAT_NORMAL, luaL_checkstring(L, 1));
        if (out.starts_with("error") || out.starts_with("usage"))
            return luaL_error(L, "hyprlens: %s", out.c_str());
        return 0;
    }
}

// ── plugin entry points ──────────────────────────────────────────────────────

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, std::format("[{}] built for a different Hyprland - rebuild it (hyprpm update)", PLUGIN_NAME), CHyprColor{1.0, 0.2, 0.2, 1.0},
                                     8000);
        throw std::runtime_error("[hyprlens] version mismatch");
    }

    g_state      = std::make_unique<SState>();
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprlens", "run", luaRun);
    g_state->ctl = HyprlandAPI::registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{.name = "hyprlens", .exact = false, .fn = onHyprctl});

    loadSettings();

    auto& ev = Event::bus()->m_events;
    // The pointer moved: redraw the lens at its new place. (A hardware cursor moves
    // without any frame being rendered, so nothing else would.)
    g_state->listeners.push_back(ev.input.mouse.move.listen([](const Vector2D&, Event::SCallbackInfo&) {
        if (!g_state || !visible())
            return;
        g_state->lastMove = steady_clock::now();
        damage(frame());
    }));
    g_state->listeners.push_back(ev.screenshare.state.listen([](bool start, uint8_t, const std::string&) {
        if (!g_state)
            return;
        g_state->shares = std::max(0, g_state->shares + (start ? 1 : -1));
        if (g_state->lastBounds)
            g_pHyprRenderer->damageBox(*g_state->lastBounds); // redraw with or without the lens
    }));
    g_state->listeners.push_back(ev.render.pre.listen([](PHLMONITOR mon) { onPreRender(mon); }));
    g_state->listeners.push_back(ev.render.stage.listen([](eRenderStage stage) { onRenderStage(stage); }));

    return {std::string(PLUGIN_NAME), std::string(PLUGIN_DESCRIPTION), std::string(PLUGIN_AUTHOR), std::string(PLUGIN_VERSION)};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (!g_state)
        return;

    g_state->listeners.clear();
    if (g_state->ctl)
        HyprlandAPI::unregisterHyprCtlCommand(PHANDLE, g_state->ctl);
    if (g_state->lastBounds)
        g_pHyprRenderer->damageBox(*g_state->lastBounds);

    g_pHyprRenderer->m_renderPass.removeAllOfType("CLensPassElement");
    LensShader::destroy();
    g_state.reset();
}
