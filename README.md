# hyprlens

A live magnifying lens for Hyprland, centered on the pointer like the lens in
macOS Zoom and Windows Magnifier. It is drawn by the compositor itself, so it
never magnifies itself, stays live, and costs nothing when it's off.

![hyprlens magnifying text under the pointer](preview.png)

It is the native sibling of [omaLens](../jgarza.omalens), which draws a lens
beside the pointer from a Quickshell layer surface and needs no build step.
hyprlens trades that for a lens that can sit right on the pointer.

**Status: 0.1, early.** Tested in a nested Hyprland 0.56.2 and in a live session. Not yet packaged.

## Install (for now)

Needs Hyprland 0.56+, `g++`, `make`, `pkg-config`, and Omarchy for the settings window.

```sh
omarchy plugin add https://github.com/jgarza9788/hyprlens.git --enable   # once it is published
~/.config/omarchy/plugins/jgarza.hyprlens/extras/install.sh
```

`extras/install.sh` builds the plugin, loads it into the running session,
enables the settings overlay, and adds **Hyprlens** and **Hyprlens Settings**
rows to the Omarchy menu (and an app-launcher entry). It is safe to run again
and never edits `hyprland.lua` or `bindings.lua`.

To load it at every login, use `hyprpm add` on this repo and `hyprpm enable
hyprlens`. Plugins must match your exact Hyprland build; after a Hyprland
update run `hyprpm update`.

A plugin bug can crash the compositor. Try it nested first if you're careful.

To remove it: `extras/uninstall.sh` (unloads the plugin, removes the menu rows and
launcher entry, and tells you what it left: your settings and the bindings block).

## Developing

```sh
make          # build hyprlens.so
make test     # unit tests for the settings logic (native/src/Logic.cpp), under ASan + UBSan
hyprctl plugin load "$PWD/hyprlens.so"
hyprctl hyprlens debug frames     # frames drawn so far
hyprctl hyprlens debug fullcopy on  # copy the whole frame, to compare with the default (just the patch under the lens)
```

Run a nested Hyprland to try changes without risking your session, and give it its own
`HOME` so it doesn't overwrite your saved settings.

## Use

```sh
hyprctl hyprlens toggle | on | off
hyprctl hyprlens zoom-in | zoom-out | bigger | smaller | size [small|medium|large]
hyprctl hyprlens shape | smooth
hyprctl hyprlens set zoom 4        # zoom, size (px or a name), shape, smooth
hyprctl hyprlens set style glow    # outline: ring (default), none, glow, double
hyprctl hyprlens set outline "#ff8800"   # accent (default) or #rrggbb
hyprctl hyprlens set width 6       # outline thickness, 1-12 px
hyprctl hyprlens set capture hide  # leave the lens out while the screen is shared
hyprctl hyprlens get
```

`bin/hyprlens` is the same thing as a short command.

## Settings window

![The Hyprlens settings window](settings.png)

`SUPER + SPACE` -> **Hyprlens Settings**, or `omarchy-shell hyprlens settings`.
Two panes, remembered between visits:

- **Lens:** zoom (a slider with tick marks at 2x / 4x / 8x / 16x; click a tick to jump),
  size, shape, smooth or sharp pixels, the outline (style, colour, width), hiding the lens
  from screen shares, and **Reset to defaults** (with an **Undo** for a few seconds).
- **Key bindings:** which Hyprland binds are in place, and a button that adds or updates a
  marked `-- >>> hyprLens` block in `~/.config/hypr/bindings.lua` (backing the file up
  first). The Lua is behind **Show the Lua**.

![The key bindings pane](settings-keys.png)

The block uses the same keys as omaLens, so adding it takes those keys over from omaLens.
Use one of the two. `SUPER + CTRL + scroll` zooms in and out.

Everything works from the keyboard: `Tab` / `Shift + Tab` move between controls, `←` `→` pick
a choice in a group and `Enter` selects it, and on the zoom slider `←` `→` `↑` `↓` (or
`h` `j` `k` `l`) step the zoom, `Page Up` / `Page Down` double or halve it, `Home` / `End`
jump to the ends. `Esc` closes the window.

Settings are kept by the plugin in `~/.config/omarchy/jgarza.hyprlens/settings.json`, so they
survive restarts and apply whether you change them from the window, the key bindings, or
`hyprctl`.

**Outline styles:** *ring* (a solid rim, the default), *none* (no rim, just a soft shadow),
*glow* (the rim with a halo) and *double* (a second thin ring just inside). The colour is your
theme's accent unless you set another, and the lens opens with the accent dot growing and its
ring thinning out to reveal the view (a plain fade if Hyprland animations are off).

**Hide from screen shares:** with it on, the lens is left out while Hyprland reports a screen
share or recording. It does *not* affect a one-off screenshot with `grim`, which still includes
the lens. Only the share-count path was tested (by simulating it); a real recording was not.

## How it works

While the lens is on, a render pass element copies the part of the frame
rendered so far that the lens samples (not the whole frame) and draws a magnified patch of it, centered on the pointer, in
a circle or rounded square with a rim in your Omarchy theme's accent colour and a soft shadow. Because
the copy is taken before the lens is drawn, there is no feedback loop. Near a
screen edge the lens is held on screen and a small crosshair marks the
pointer's spot.

When nothing on screen changes and the pointer is still, the lens asks for no frames of its own, so
it costs nothing at idle. It redraws when the pointer moves, while it opens or closes, and whenever
something else on screen changes.

## Not done yet

See `todo.md`. The main gaps: `hyprpm` packaging has not been run end to end, the repo isn't
published, and a real screen recording, real second monitors, and mixed refresh rates are untested.
