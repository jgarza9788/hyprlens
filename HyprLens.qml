import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons

// Hyprlens — the Omarchy side of the hyprlens Hyprland plugin.
//
// The lens itself is drawn by the compositor (hyprlens.so), so this file draws
// nothing. It shows the settings window (LensSettings.qml), keeps it in step
// with the plugin by talking to it with `hyprctl hyprlens`, and lets
//   omarchy-shell shell toggle jgarza.hyprlens '{}'
// open and close the lens. Settings are kept by the plugin itself, in
// ~/.config/omarchy/jgarza.hyprlens/settings.json.
//
//   omarchy-shell hyprlens settings      the settings window
Item {
  id: root

  // ── Injected by the Omarchy shell loader ─────────────────────────────────
  property var shell: null
  property var manifest: null
  property string omarchyPath: ""
  readonly property string pluginId: String((manifest && manifest.id) || "jgarza.hyprlens")

  // ── What the settings window reads (see LensSettings.qml) ───────────────
  readonly property real minZoom: 1.25
  readonly property real maxZoom: 24
  property bool loaded: true       // false when hyprctl can't reach the plugin
  property bool active: false      // the lens is showing
  readonly property var defaults: ({ zoom: 2.5, size: "medium", shape: "circle", smooth: false,
                                     style: "ring", outline: "accent", width: 3, capture: "show" })
  property var saved: defaults

  readonly property string pluginDir: Qt.resolvedUrl(".").toString().replace(/^file:\/\//, "").replace(/\/$/, "")
  function binPath(name) { return Qt.resolvedUrl("bin/" + name).toString().replace(/^file:\/\//, ""); }
  function zoomLabel(z) { return z.toFixed(z < 10 ? 2 : 1).replace(/\.?0+$/, "") + "×"; }

  // ── Talking to the plugin ───────────────────────────────────────────────
  Process {
    id: getProc
    command: ["hyprctl", "hyprlens", "get"]
    stdout: StdioCollector {
      onStreamFinished: {
        var j = null;
        try { j = JSON.parse(String(text || "")); } catch (e) {}
        if (!j || j.zoom === undefined) { root.loaded = false; return; }
        root.loaded = true;
        root.active = j.enabled === true;
        root.saved = { zoom: j.zoom, size: j.sizeName, shape: j.shape, smooth: j.smooth === true,
                       style: j.style || "ring", outline: j.outline || "accent",
                       width: j.width === undefined ? 3 : j.width, capture: j.capture || "show" };
      }
    }
  }
  function refresh() { getProc.running = false; getProc.running = true; }

  // Run `hyprctl hyprlens <args>`, then re-read the plugin's state.
  function run(args) {
    Quickshell.execDetached(["hyprctl", "hyprlens"].concat(args));
    refreshTimer.restart();
  }
  Timer { id: refreshTimer; interval: 120; onTriggered: root.refresh() }

  function setSaved(key, value) {
    // Show the change at once; the plugin's answer settles it.
    var next = Object.assign({}, root.saved);
    next[key] = value;
    root.saved = next;
    root.run(["set", key, String(value)]);
  }
  // Reset keeps what was there for a few seconds, so it can be undone.
  property var beforeReset: null
  readonly property bool canUndo: beforeReset !== null
  function resetSettings() {
    root.beforeReset = Object.assign({}, root.saved);
    undoTimer.restart();
    for (var k in root.defaults) setSaved(k, root.defaults[k]);
  }
  function undoReset() {
    if (!root.beforeReset) return;
    var old = root.beforeReset;
    root.beforeReset = null;
    undoTimer.stop();
    for (var k in old) setSaved(k, old[k]);
  }
  Timer { id: undoTimer; interval: 8000; onTriggered: root.beforeReset = null }
  function toggle() { root.run(["toggle"]); }

  // ── Which pane the settings window shows (remembered between visits) ─────
  property string pane: "lens"
  readonly property string configDir: Quickshell.env("HOME") + "/.config/omarchy/" + pluginId
  FileView {
    id: uiFile
    path: root.configDir + "/ui.json"
    watchChanges: false
    atomicWrites: true
    printErrors: false
    onLoaded: {
      try {
        var j = JSON.parse(String(text() || "{}"));
        if (j.pane === "lens" || j.pane === "keys") root.pane = j.pane;
      } catch (e) {}
    }
  }
  function setPane(p) {
    if (p !== "lens" && p !== "keys") return;
    root.pane = p;
    Quickshell.execDetached(["mkdir", "-p", root.configDir]);
    uiFile.setText(JSON.stringify({ pane: p }) + "\n");
  }

  // The settings window polls while it's open, so the "Show the lens" switch
  // follows a key binding that opens or closes the lens.
  Timer {
    interval: 600; repeat: true
    running: settingsWindow.opened
    onTriggered: root.refresh()
  }

  LensSettings { id: settingsWindow; lens: root }
  Connections {
    target: settingsWindow
    function onOpenedChanged() { if (settingsWindow.opened) root.refresh(); }
  }

  // ── Lifecycle verbs (overlay contract) ──────────────────────────────────
  // Summoning toggles the lens. `opened` stays false, so the shell always
  // calls open(): whether the lens is up is the plugin's to know.
  readonly property bool opened: false
  function open(payloadJson) { root.run(["toggle"]); }
  function close() { root.run(["off"]); }
  function toggleLens() { root.run(["toggle"]); }
  function summon(payloadJson) { root.run(["toggle"]); }
  function hide() { root.run(["off"]); }

  IpcHandler {
    target: "hyprlens"
    function toggle(): string { root.run(["toggle"]); return "ok" }
    function close(): string { root.run(["off"]); return "off" }
    function settings(): string {
      settingsWindow.toggle();
      return settingsWindow.opened ? "open" : "closed"
    }
  }
}
