# hyprlens todo

Status: 0.1, working in a live session. Items are in the order I'd do them.

## Must do

- [x] **Copy only the needed part of the frame.** Done: `copyFrame()` copies just the patch the lens samples,
      with the rect computed through the same projection the lens quad uses. Checked pixel-for-pixel against
      the whole-frame copy on all 8 transforms at 1x and 1.5x, at the centre and all four corners.
      Hyprland CPU use is about the same either way (2.6% vs 3.0% of a core); **GPU time was not measured.**
- [x] **Test rotated, fractional-scale and multi-monitor setups.** Done in a nested Hyprland: all 8 transforms
      at scale 1 and 1.67, and two side-by-side monitors (the lens stays on the monitor the pointer is on,
      including 10 px either side of the boundary). Not tested: real second monitors, a second monitor at a
      different scale (the nested output ignored the scale), mixed refresh rates.
- [ ] **Package for `hyprpm` and publish the repo.**
  - [ ] Check `hyprpm add` / `enable` / `update` end to end (not run: it changes hyprpm state and may ask for a password; needs a committed repo)
  - [x] Clear message after a Hyprland update when the plugin no longer matches (the hash check exists, notification on mismatch)
  - [x] Tests for the settings logic: `make test`, 46 checks under ASan + UBSan
  - [x] LICENSE (MIT) and the repo URL in `manifest.json`; the repo itself isn't published yet
  - [x] `extras/install.sh` checked in an isolated HOME with stubbed commands: clean, existing, one-line `{}`, trailing-comma, comment-last and symlinked menu files, a re-run (no change), missing tool, failing build, and an uninstall round trip. Not run on a real clean machine.
  - [x] `extras/uninstall.sh`


## Decisions

- [x] **Screenshots and recordings include the lens.** Added `capture hide` ("Hide from screen shares" in the window, default off):
      the lens is left out while Hyprland reports a screen share. **Limits:** one-off `grim` screenshots do *not* raise a share
      event, so they still include the lens; only the share-count path was tested (simulated with `debug shares 1`), not a real recording
      (no `wf-recorder` here).
- [x] **Outline color and width** as settings (accent stays the default): `outline accent|#rrggbb`, `width 1-12`; the window offers Accent / White / Black and Thin / Normal / Bold.
- [x] **styles** border, no border, and a few others: `ring`, `none`, `glow`, `double` (checked in a nested Hyprland, circle and square).

## Settings window

- [x] Two panes, **Lens** | **Key Bindings**, restoring the last pane (kept in `~/.config/omarchy/jgarza.hyprlens/ui.json`, checked across a shell restart). The Lua snippet is behind "Show the Lua".
- [x] Zoom tick marks (1.25x, 2x, 4x, 8x, 16x, 24x) in place of the preset chips; click a tick to jump.
- [x] Reset to defaults is undoable (an Undo button for 8 seconds; checked with the keyboard, the settings came back exactly).
- [x] New `settings.png` (and `settings-keys.png`, `preview.png`) for the README.

## Polish

- [x] Open/close timing checked by you ("looks good"): kept at the defaults, 240 ms open / 160 ms close. Measured on a 30 Hz display: about 262 ms and 8 frames to open, 180 ms and 5-6 frames to close. `hyprctl hyprlens debug speed <open> <close>` still tries other timings live.
- [x] Square lens opening (dot squares off) and reduced motion (fade only): checked with `debug progress` and `debug reduce on`. Not checked with Hyprland animations actually turned off.
- [x] Idle power: no frames of its own when nothing changes and the pointer is still (0 frames in 3 s, down from about 30-60 per second); it redraws on pointer moves, animations, and other screen changes.
- [x] README updated (panes, outline styles, capture limits, idle behavior, screenshots).

## Done

- [x] Native plugin: live lens centered on the pointer, drawn by the compositor (no self-capture)
- [x] Accent outline from the Omarchy theme
- [x] Dot-grow / ring-thin open animation, interruptible, with a reduced-motion fade
- [x] Lens held on screen at edges, with a crosshair at the pointer's spot
- [x] Saved settings (`~/.config/omarchy/jgarza.hyprlens/settings.json`)
- [x] `hyprctl hyprlens` commands and the `bin/hyprlens` wrapper
- [x] Hyprlens Settings window, bindings helper, menu rows, desktop entry
- [x] Copy button fixed (`wl-copy --`), also in omaLens
- [x] `make test`: 60+ checks now cover outline style, colour, width and capture
- [x] Fixed: the lens only redrew when something else changed on screen, so the open animation could stall on a static screen
