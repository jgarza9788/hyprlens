import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Ui

// Hyprlens settings window: adjust zoom, size, shape and smoothing (kept by the
// hyprlens plugin in ~/.config/omarchy/jgarza.hyprlens/settings.json), preview the
// lens live, and set up the key bindings in ~/.config/hypr/bindings.lua.
//
// Owned by HyprLens.qml, which passes itself in as `lens`. Opened with
// `omarchy-shell hyprlens settings`.
Item {
  id: root

  property var lens: null
  property bool opened: false
  // Which pane shows: "lens" (the look of the lens) or "keys" (key bindings). Remembered.
  readonly property string pane: lens ? lens.pane : "lens"
  property bool showLua: false
  readonly property var saved: lens ? lens.saved : ({ zoom: 2.5, size: "medium", shape: "circle", smooth: false, style: "ring", outline: "accent", capture: "show" })

  // Accent outline around a keyboard-focused control. The theme's own focus
  // styling is faint (buttons, toggle) or missing (the zoom slider).
  component FocusRing: Rectangle {
    property Item target
    anchors.fill: parent
    anchors.margins: -Style.spacing.sm
    radius: Style.cornerRadius
    color: "transparent"
    border.width: 1
    border.color: Color.accent
    visible: target !== null && target.activeFocus
  }

  // Accent outline around the chip a focused ButtonGroup would pick on Enter.
  // Place it beside the group in a plain Item, so the group sits at (0, 0).
  component ChipRing: Rectangle {
    property Item group
    readonly property Item chip: {
      if (!group || !group.activeFocus) return null;
      var kids = group.children, want = group._focusedIndex;
      for (var i = 0; i < kids.length; i++)
        if (kids[i].modelData !== undefined && kids[i].index === want) return kids[i];
      return null;
    }
    readonly property real m: Style.spacing.xs
    visible: chip !== null
    x: chip ? group.x + chip.x - m : 0
    y: chip ? group.y + chip.y - m : 0
    width: chip ? chip.width + 2 * m : 0
    height: chip ? chip.height + 2 * m : 0
    radius: Style.cornerRadius
    color: "transparent"
    border.width: 2
    border.color: Color.accent
  }

  readonly property string homeDir: Quickshell.env("HOME")
  readonly property string bindingsPath: homeDir + "/.config/hypr/bindings.lua"
  readonly property string hyprlensPath: lens ? lens.binPath("hyprlens") : ""
  readonly property string helperPath: lens ? lens.binPath("hyprlens-bindings") : ""
  // Written as `os.getenv("HOME") .. "/…"` when the plugin lives under $HOME,
  // so the snippet survives a change of user name.
  readonly property string hyprlensLua: hyprlensPath.indexOf(homeDir + "/") === 0
    ? 'os.getenv("HOME") .. "' + hyprlensPath.slice(homeDir.length) + '"'
    : '"' + hyprlensPath + '"'

  // `label` doubles as the Hyprland bind description, which is how the live
  // binds are recognised (Lua binds don't expose their command). `code` is the
  // keycode Omarchy's defaults bind the same key by (`was` is what it did);
  // unbinding by name doesn't free a keycode bind, so those get their own line.
  readonly property var bindings: [
    { keys: "SUPER + ALT + Z",            name: "Open / close",    label: "Hyprlens",                cmd: "toggle" },
    { keys: "SUPER + ALT + equal",        name: "Zoom in",         label: "Hyprlens zoom in",        cmd: "zoom-in",
      code: 21, was: "Shrink window left a little" },
    { keys: "SUPER + ALT + minus",        name: "Zoom out",        label: "Hyprlens zoom out",       cmd: "zoom-out",
      code: 20, was: "Expand window left a little" },
    { keys: "SUPER + ALT + bracketright", name: "Bigger",          label: "Hyprlens bigger",         cmd: "bigger",
      code: 35, was: "Make webcam overlay larger" },
    { keys: "SUPER + ALT + bracketleft",  name: "Smaller",         label: "Hyprlens smaller",        cmd: "smaller",
      code: 34, was: "Make webcam overlay smaller" },
    { keys: "SUPER + ALT + backslash",    name: "Cycle size",      label: "Hyprlens cycle size",     cmd: "size" },
    { keys: "SUPER + ALT + O",            name: "Circle / square", label: "Hyprlens circle/square",  cmd: "shape" },
    { keys: "SUPER + ALT + P",            name: "Smooth / sharp",  label: "Hyprlens smooth/sharp",   cmd: "smooth" },
    { keys: "SUPER + CTRL + mouse_up",    name: "Zoom in (scroll)",  label: "Hyprlens zoom in (scroll)",  cmd: "zoom-in" },
    { keys: "SUPER + CTRL + mouse_down",  name: "Zoom out (scroll)", label: "Hyprlens zoom out (scroll)", cmd: "zoom-out" }
  ]

  // The marker lines let bin/hyprlens-bindings find and replace this block.
  readonly property string snippet: {
    var pad = function (s, n) { while (s.length < n) s += " "; return s; };
    var lines = ["-- >>> hyprLens (managed by Hyprlens Settings; Update replaces this block)",
                 "local hyprlens = " + hyprlensLua, "",
                 "-- Free the combos first, in case something else already uses them."];
    for (var j = 0; j < bindings.length; j++) lines.push('hl.unbind("' + bindings[j].keys + '")');
    lines.push("-- Omarchy binds these by keycode, and unbinding by name doesn't free them.");
    for (var c = 0; c < bindings.length; c++) {
      var bc = bindings[c];
      if (bc.code)
        lines.push(pad('hl.unbind("SUPER + ALT + code:' + bc.code + '")', 37) + " -- was: " + bc.was);
    }
    lines.push("");
    for (var i = 0; i < bindings.length; i++) {
      var b = bindings[i];
      lines.push(pad('o.bind("' + b.keys + '",', 37) + " " + pad('"' + b.label + '",', 26) +
                 ' hyprlens .. " ' + b.cmd + '")');
    }
    lines.push("-- <<< hyprLens");
    return lines.join("\n");
  }

  function open() {
    root.opened = true;
    root.status = "";
    refreshBindings();
    // Start each visit with nothing focused, so Tab begins at the top.
    keys.forceActiveFocus();
  }
  function close() { root.opened = false; }
  function toggle() { if (root.opened) close(); else open(); }

  function setZoom(z) { root.lens.setSaved("zoom", z); }

  // ── Key bindings: what's live, and what's in bindings.lua ──────────────
  // One row per action: { name, want, live, taken }. `live` is the combo
  // Hyprland has bound right now ("" if none); `taken` names whatever holds
  // the suggested combo when the action isn't bound.
  property var bindRows: []
  readonly property int boundCount: bindRows.filter(function (r) { return r.live !== ""; }).length
  property var fileState: ({ exists: false, marked: false, loose: 0 })
  property string status: ""

  readonly property var modBits: [["SUPER", 64], ["CTRL", 4], ["ALT", 8], ["SHIFT", 1]]
  function comboText(mask, key) {
    var parts = [];
    for (var i = 0; i < modBits.length; i++) if (mask & modBits[i][1]) parts.push(modBits[i][0]);
    parts.push(key);
    return parts.join(" + ");
  }
  function comboMask(keys) {
    var parts = keys.split("+").map(function (s) { return s.trim().toUpperCase(); }), mask = 0;
    for (var i = 0; i < parts.length - 1; i++)
      for (var j = 0; j < modBits.length; j++) if (parts[i] === modBits[j][0]) mask |= modBits[j][1];
    return mask;
  }
  function comboKey(keys) { var p = keys.split("+"); return p[p.length - 1].trim(); }

  function readBinds(json) {
    var all = [];
    try { all = JSON.parse(json) || []; } catch (e) { all = []; }
    var rows = [];
    for (var i = 0; i < bindings.length; i++) {
      var b = bindings[i], live = "", taken = "";
      var wantMask = comboMask(b.keys), wantKey = comboKey(b.keys).toLowerCase();
      for (var k = 0; k < all.length; k++)
        if (all[k].description === b.label && !all[k].submap) { live = comboText(all[k].modmask, all[k].key); break; }
      if (live === "")
        for (var n = 0; n < all.length; n++)
          if (all[n].modmask === wantMask && !all[n].submap &&
              (String(all[n].key).toLowerCase() === wantKey || (b.code && all[n].keycode === b.code))) {
            taken = all[n].description || all[n].dispatcher || "another bind";
            break;
          }
      rows.push({ name: b.name, want: b.keys, live: live, taken: taken });
    }
    root.bindRows = rows;
  }

  Process {
    id: bindsProc
    command: ["hyprctl", "binds", "-j"]
    stdout: StdioCollector { onStreamFinished: root.readBinds(String(text || "[]")) }
  }
  Process {
    id: fileProc
    command: [root.helperPath, "status", root.bindingsPath]
    stdout: StdioCollector {
      onStreamFinished: {
        try { root.fileState = JSON.parse(String(text || "{}")); } catch (e) {}
      }
    }
  }
  Process {
    id: writeProc
    command: [root.helperPath, "write", root.bindingsPath, root.snippet]
    stdout: StdioCollector {
      onStreamFinished: {
        var r = {};
        try { r = JSON.parse(String(text || "{}")); } catch (e) {}
        if (!r.action) { root.status = "Couldn't write " + root.bindingsPath; return; }
        var msg = (r.action === "updated" ? "Updated the hyprLens block in" : "Added an hyprLens block to") + " bindings.lua" +
                  (r.backup ? " (old copy in " + String(r.backup).split("/").pop() + ")." : ".");
        if (root.fileState.loose > 0)
          msg += " Your older hand-written hyprLens lines are still there. Use Open bindings.lua to remove them.";
        root.status = msg;
        root.refreshBindings();
        reloadTimer.restart();
      }
    }
  }
  // Hyprland reloads bindings.lua on its own shortly after it changes.
  Timer { id: reloadTimer; interval: 1200; onTriggered: root.refreshBindings() }

  function refreshBindings() {
    bindsProc.running = false; bindsProc.running = true;
    fileProc.running = false; fileProc.running = true;
  }
  function writeBindings() { writeProc.running = false; writeProc.running = true; }
  function copySnippet() {
    Quickshell.execDetached(["wl-copy", "--", root.snippet]);
    root.status = "Copied the key bindings to the clipboard.";
  }
  function editBindings() {
    Quickshell.execDetached(["omarchy-launch-editor", root.bindingsPath]);
    root.close();
  }

  // ── Window ──────────────────────────────────────────────────────────────
  PanelWindow {
    id: win
    visible: root.opened
    anchors { top: true; bottom: true; left: true; right: true }
    color: "transparent"

    WlrLayershell.namespace: "omarchy-hyprlens-settings"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
    exclusionMode: ExclusionMode.Ignore

    Rectangle {
      anchors.fill: parent
      color: Util.alpha(Color.background, 0.6)
      TapHandler { onTapped: root.close() }
    }

    Item {
      id: keys
      anchors.fill: parent
      focus: true
      Keys.onEscapePressed: root.close()

      // Tab / Shift+Tab walk the controls. The platform's own Tab chain
      // doesn't reach them on a layer surface, so step through them here.
      readonly property var focusRing: root.pane === "lens"
        ? [paneGroup, lensToggle, zoomSlider, sizeGroup, shapeGroup, pixelGroup, styleGroup, colourGroup,
           captureToggle, resetBtn, undoBtn, closeBtn]
        : [paneGroup, luaToggle, copyBtn, addBtn, editBtn, closeBtn]
      function focusStep(dir) {
        // Skip anything not showing (the Undo button only appears after a reset).
        var ring = focusRing.filter(function (it) { return it.visible; }), cur = -1;
        for (var i = 0; i < ring.length; i++) if (ring[i].activeFocus) { cur = i; break; }
        var next = cur < 0 ? (dir > 0 ? 0 : ring.length - 1) : (cur + dir + ring.length) % ring.length;
        ring[next].forceActiveFocus(dir < 0 ? Qt.BacktabFocusReason : Qt.TabFocusReason);
      }
      Keys.onPressed: function (event) {
        if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab) return;
        var back = event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) !== 0;
        focusStep(back ? -1 : 1);
        event.accepted = true;
      }

      Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(720, parent.width * 0.92)
        height: Math.min(content.implicitHeight + 2 * Style.space(20), parent.height * 0.92)
        radius: Math.max(8, Style.cornerRadius)
        color: Color.background
        border.width: 2
        border.color: Color.accent
        clip: true

        // Keep clicks inside the card from reaching the scrim.
        MouseArea { anchors.fill: parent }

        Flickable {
          anchors.fill: parent
          anchors.margins: Style.space(20)
          contentHeight: content.implicitHeight
          boundsBehavior: Flickable.StopAtBounds

          Column {
            id: content
            width: parent.width
            spacing: Style.space(14)

            // Header
            Item {
              width: parent.width
              height: Math.max(titleRow.implicitHeight, closeBtn.implicitHeight)
              Row {
                id: titleRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: Style.spacing.xl
                // The lens glyph, as in the Omarchy menu row.
                Text {
                  anchors.verticalCenter: parent.verticalCenter
                  text: "󰍉"
                  color: Color.accent
                  font.family: Style.font.family
                  font.pixelSize: Math.round(Style.font.title * 2)
                }
                Text {
                  anchors.verticalCenter: parent.verticalCenter
                  text: "Hyprlens"
                  color: Color.foreground
                  font.family: Style.font.family
                  font.pixelSize: Math.round(Style.font.title * 1.6)
                  font.bold: true
                }
              }
              Button {
                id: closeBtn
                focusable: true
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconText: "󰅖"
                tooltipText: "Close (Esc)"
                onClicked: root.close()
                FocusRing { target: closeBtn }
              }
            }

            // Which pane: the lens itself, or its key bindings. Remembered.
            Item {
              implicitWidth: paneGroup.implicitWidth
              implicitHeight: paneGroup.implicitHeight
              ButtonGroup {
                id: paneGroup
                options: [
                  { value: "lens", label: "Lens" },
                  { value: "keys", label: "Key bindings" }
                ]
                value: root.pane
                onChanged: function (v) { root.lens.setPane(v); }
              }
              ChipRing { group: paneGroup }
            }

            Column {
              id: lensPane
              visible: root.pane === "lens"
              width: parent.width
              spacing: Style.space(14)

            Toggle {
              id: lensToggle
              width: parent.width
              label: "Show the lens"
              description: "Preview your changes live. Point anywhere to look through it."
              checked: root.lens && root.lens.active
              onClicked: root.lens.toggle()
              FocusRing { target: lensToggle }
            }

            Text {
              width: parent.width
              visible: root.lens && !root.lens.loaded
              wrapMode: Text.Wrap
              color: Color.urgent
              font.family: Style.font.family
              font.pixelSize: Style.font.body
              text: "The hyprlens plugin isn't loaded, so the lens can't show. Run " +
                    "hyprctl plugin load " + (root.lens ? root.lens.pluginDir : "") + "/hyprlens.so"
            }

            // Zoom: the slider works in log space so each step feels the same.
            Column {
              width: parent.width
              spacing: Style.spacing.md
              Row {
                width: parent.width
                PanelSectionHeader { text: "ZOOM"; width: parent.width - zoomText.width }
                Text {
                  id: zoomText
                  text: root.lens ? root.lens.zoomLabel(root.saved.zoom) : ""
                  color: Color.foreground
                  font.family: Style.font.family
                  font.pixelSize: Style.font.body
                  font.bold: true
                }
              }
              PanelSlider {
                id: zoomSlider
                width: parent.width
                minimum: Math.log(root.lens ? root.lens.minZoom : 1.25)
                maximum: Math.log(root.lens ? root.lens.maxZoom : 24)
                step: Math.log(1.25)
                value: Math.log(root.saved.zoom)
                trackColor: Style.selectedFillFor(Color.foreground, Color.accent)
                fillColor: Color.accent
                knobColor: Color.accent
                onMoved: function (v) { root.setZoom(Math.exp(v)); }

                // Keyboard: Tab to it, then arrows (or h/j/k/l) step ×1.25,
                // Page Up/Down step ×2, Home/End jump to the ends.
                activeFocusOnTab: true
                Keys.onPressed: function (event) {
                  var z = root.saved.zoom, t = event.text;
                  switch (event.key) {
                  case Qt.Key_Right: case Qt.Key_Up:     z *= 1.25; break;
                  case Qt.Key_Left:  case Qt.Key_Down:   z /= 1.25; break;
                  case Qt.Key_PageUp:                    z *= 2; break;
                  case Qt.Key_PageDown:                  z /= 2; break;
                  case Qt.Key_Home:                      z = root.lens.minZoom; break;
                  case Qt.Key_End:                       z = root.lens.maxZoom; break;
                  default:
                    if (t === "l" || t === "k") z *= 1.25;
                    else if (t === "h" || t === "j") z /= 1.25;
                    else return;
                  }
                  root.setZoom(z);
                  event.accepted = true;
                }

                FocusRing { target: zoomSlider }
              }
              // Tick marks at the usual jumps, on the same log scale as the slider.
              // Click one to jump there.
              Item {
                id: ticks
                width: parent.width
                height: Style.space(30)
                readonly property real inset: Style.space(8)
                readonly property var marks: [
                  { v: 1.25, text: "1.25×" }, { v: 2, text: "2×" }, { v: 4, text: "4×" },
                  { v: 8, text: "8×" }, { v: 16, text: "16×" }, { v: 24, text: "24×" }
                ]
                Repeater {
                  model: ticks.marks
                  delegate: Item {
                    required property var modelData
                    readonly property real lo: Math.log(root.lens ? root.lens.minZoom : 1.25)
                    readonly property real hi: Math.log(root.lens ? root.lens.maxZoom : 24)
                    x: ticks.inset + (Math.log(modelData.v) - lo) / (hi - lo) * (ticks.width - 2 * ticks.inset) - width / 2
                    width: Style.space(44)
                    height: ticks.height
                    Rectangle {
                      anchors.horizontalCenter: parent.horizontalCenter
                      width: 1; height: Style.space(6)
                      color: Util.alpha(Color.foreground, Math.abs(root.saved.zoom - modelData.v) / modelData.v < 0.02 ? 0.9 : 0.4)
                    }
                    Text {
                      anchors.horizontalCenter: parent.horizontalCenter
                      anchors.bottom: parent.bottom
                      text: modelData.text
                      color: Math.abs(root.saved.zoom - modelData.v) / modelData.v < 0.02 ? Color.accent : Util.alpha(Color.foreground, 0.7)
                      font.family: Style.font.family
                      font.pixelSize: Style.font.caption
                    }
                    MouseArea {
                      anchors.fill: parent
                      cursorShape: Qt.PointingHandCursor
                      onClicked: root.setZoom(modelData.v)
                    }
                  }
                }
              }
            }

            Grid {
              width: parent.width
              columns: 3
              columnSpacing: Style.space(18)
              rowSpacing: Style.spacing.md

              PanelSectionHeader { text: "SIZE" }
              PanelSectionHeader { text: "SHAPE" }
              PanelSectionHeader { text: "PIXELS" }

              // Each group is wrapped so its ChipRing can overlay it without
              // the Grid laying the ring out as a cell of its own.
              Item {
                implicitWidth: sizeGroup.implicitWidth
                implicitHeight: sizeGroup.implicitHeight
                ButtonGroup {
                  id: sizeGroup
                  options: [
                    { value: "small",  label: "Small" },
                    { value: "medium", label: "Medium" },
                    { value: "large",  label: "Large" }
                  ]
                  value: root.saved.size
                  onChanged: function (v) { root.lens.setSaved("size", v); }
                }
                ChipRing { group: sizeGroup }
              }
              Item {
                implicitWidth: shapeGroup.implicitWidth
                implicitHeight: shapeGroup.implicitHeight
                ButtonGroup {
                  id: shapeGroup
                  options: [
                    { value: "circle", label: "Circle" },
                    { value: "square", label: "Square" }
                  ]
                  value: root.saved.shape
                  onChanged: function (v) { root.lens.setSaved("shape", v); }
                }
                ChipRing { group: shapeGroup }
              }
              Item {
                implicitWidth: pixelGroup.implicitWidth
                implicitHeight: pixelGroup.implicitHeight
                ButtonGroup {
                  id: pixelGroup
                  options: [
                    { value: "sharp",  label: "Sharp" },
                    { value: "smooth", label: "Smooth" }
                  ]
                  value: root.saved.smooth ? "smooth" : "sharp"
                  onChanged: function (v) { root.lens.setSaved("smooth", v === "smooth"); }
                }
                ChipRing { group: pixelGroup }
              }
            }

            // Outline: the look of the rim.
            Column {
              width: parent.width
              spacing: Style.spacing.md
              PanelSectionHeader { text: "OUTLINE"; width: parent.width }
              Grid {
                width: parent.width
                columns: 2
                columnSpacing: Style.space(18)
                rowSpacing: Style.spacing.md
                PanelSectionHeader { text: "STYLE" }
                PanelSectionHeader { text: "COLOUR" }
                Item {
                  implicitWidth: styleGroup.implicitWidth
                  implicitHeight: styleGroup.implicitHeight
                  ButtonGroup {
                    id: styleGroup
                    options: [
                      { value: "ring",  label: "Ring" },
                      { value: "glass", label: "Glass" },
                      { value: "glow",  label: "Glow" },
                      { value: "none",  label: "None" }
                    ]
                    value: root.saved.style
                    onChanged: function (v) { root.lens.setSaved("style", v); }
                  }
                  ChipRing { group: styleGroup }
                }
                Item {
                  implicitWidth: colourGroup.implicitWidth
                  implicitHeight: colourGroup.implicitHeight
                  ButtonGroup {
                    id: colourGroup
                    options: [
                      { value: "accent",  label: "Accent" },
                      { value: "#ffffff", label: "White" },
                      { value: "#000000", label: "Black" }
                    ]
                    value: root.saved.outline
                    onChanged: function (v) { root.lens.setSaved("outline", v); }
                  }
                  ChipRing { group: colourGroup }
                }
              }
              Text {
                width: parent.width
                visible: ["accent", "#ffffff", "#000000"].indexOf(root.saved.outline) < 0
                wrapMode: Text.Wrap
                color: Util.alpha(Color.foreground, 0.7)
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                text: "Custom colour " + root.saved.outline + ". Set another with: hyprctl hyprlens set outline #rrggbb"
              }
            }

            Toggle {
              id: captureToggle
              width: parent.width
              label: "Hide from screen shares"
              description: "Leave the lens out while the screen is shared or recorded. Screenshots still include it."
              checked: root.saved.capture === "hide"
              onClicked: root.lens.setSaved("capture", root.saved.capture === "hide" ? "show" : "hide")
              FocusRing { target: captureToggle }
            }

            Row {
              spacing: Style.spacing.lg
              Button {
                id: resetBtn
                text: "Reset to defaults"
                iconText: "󰑓"
                bordered: true
                focusable: true
                onClicked: root.lens.resetSettings()
                FocusRing { target: resetBtn }
              }
              Button {
                id: undoBtn
                visible: root.lens && root.lens.canUndo
                text: "Undo"
                iconText: "󰕌"
                bordered: true
                focusable: true
                onClicked: root.lens.undoReset()
                FocusRing { target: undoBtn }
              }
            }
            }

            // Key bindings
            Column {
              id: keysPane
              visible: root.pane === "keys"
              width: parent.width
              spacing: Style.spacing.md

              Row {
                width: parent.width
                PanelSectionHeader { text: "KEY BINDINGS"; width: parent.width - bindState.width }
                Text {
                  id: bindState
                  text: root.boundCount + " of " + root.bindings.length + " bound"
                  color: root.boundCount === root.bindings.length ? Color.accent : Util.alpha(Color.foreground, 0.7)
                  font.family: Style.font.family
                  font.pixelSize: Style.font.caption
                }
              }

              Text {
                width: parent.width
                wrapMode: Text.Wrap
                color: Util.alpha(Color.foreground, 0.7)
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                text: "These keep working while the lens is open. " +
                      "Only Open / close is needed; the rest adjust the lens while it's up. " +
                      "This table shows what Hyprland has bound right now."
              }

              // Live bindings table.
              Column {
                id: bindTable
                width: parent.width
                spacing: Style.spacing.xs
                readonly property real nameW: width * 0.28
                readonly property real keysW: width * 0.44

                Repeater {
                  model: root.bindRows
                  delegate: Row {
                    required property var modelData
                    width: bindTable.width
                    Text {
                      width: bindTable.nameW
                      text: modelData.name
                      color: Color.foreground
                      font.family: Style.font.family
                      font.pixelSize: Style.font.body
                      elide: Text.ElideRight
                    }
                    Text {
                      width: bindTable.keysW
                      text: modelData.live !== "" ? modelData.live : modelData.want
                      color: modelData.live !== "" ? Color.foreground : Util.alpha(Color.foreground, 0.65)
                      font.family: Style.font.family
                      font.pixelSize: Style.font.body
                      elide: Text.ElideRight
                    }
                    Text {
                      width: bindTable.width - bindTable.nameW - bindTable.keysW
                      text: modelData.live !== "" ? "✓ bound"
                          : modelData.taken !== "" ? "✗ taken by " + modelData.taken
                          : "not bound"
                      color: modelData.live !== "" ? Color.accent
                           : modelData.taken !== "" ? Color.urgent
                           : Util.alpha(Color.foreground, 0.7)
                      font.family: Style.font.family
                      font.pixelSize: Style.font.body
                      elide: Text.ElideRight
                    }
                  }
                }
              }

              Text {
                width: parent.width
                wrapMode: Text.Wrap
                color: Util.alpha(Color.foreground, 0.7)
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                text: (root.fileState.marked
                        ? "bindings.lua has an hyprLens block. Update rewrites it. "
                        : "Add puts them at the end of ~/.config/hypr/bindings.lua. ") +
                      (root.fileState.loose > 0 && !root.fileState.marked
                        ? "The file also has hand-written hyprLens lines. The block unbinds first, so it wins, " +
                          "but delete the old lines afterwards. "
                        : "") +
                      "The hl.unbind lines free each combo, so they replace whatever used it before. " +
                      "Hyprland reloads the file on save; run hyprctl configerrors to check."
              }

              Button {
                id: luaToggle
                text: root.showLua ? "Hide the Lua" : "Show the Lua"
                iconText: root.showLua ? "󰅃" : "󰅀"
                focusable: true
                onClicked: root.showLua = !root.showLua
                FocusRing { target: luaToggle }
              }
              Rectangle {
                width: parent.width
                visible: root.showLua
                height: snippetText.implicitHeight + 2 * Style.space(10)
                radius: Style.cornerRadius
                color: Util.alpha(Color.foreground, 0.05)
                border.width: 1
                border.color: Util.alpha(Color.foreground, 0.1)
                clip: true
                Text {
                  id: snippetText
                  x: Style.space(10)
                  y: Style.space(10)
                  width: parent.width - 2 * Style.space(10)
                  text: root.snippet
                  textFormat: Text.PlainText
                  color: Color.foreground
                  font.family: Style.font.family
                  font.pixelSize: Style.font.bodySmall
                  elide: Text.ElideRight
                }
              }

              Row {
                spacing: Style.spacing.lg
                Button {
                  id: copyBtn
                  text: "Copy"
                  iconText: "󰆏"
                  bordered: true
                  focusable: true
                  onClicked: root.copySnippet()
                  FocusRing { target: copyBtn }
                }
                Button {
                  id: addBtn
                  text: root.fileState.marked ? "Update bindings.lua" : "Add to bindings.lua"
                  iconText: root.fileState.marked ? "󰑓" : "󰐕"
                  bordered: true
                  focusable: true
                  onClicked: root.writeBindings()
                  FocusRing { target: addBtn }
                }
                Button {
                  id: editBtn
                  text: "Open bindings.lua"
                  iconText: "󰏫"
                  bordered: true
                  focusable: true
                  onClicked: root.editBindings()
                  FocusRing { target: editBtn }
                }
              }

              Text {
                width: parent.width
                visible: root.status !== ""
                text: root.status
                wrapMode: Text.Wrap
                color: Color.accent
                font.family: Style.font.family
                font.pixelSize: Style.font.body
              }
            }
          }
        }
      }
    }
  }
}
