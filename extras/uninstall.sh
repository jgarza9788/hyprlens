#!/bin/bash
# Undo extras/install.sh for jgarza.hyprlens: unload the native plugin, remove the
# Omarchy menu rows and the launcher entry. Leaves your settings
# (~/.config/omarchy/jgarza.hyprlens) and bindings.lua alone, and says what is left.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")

echo "unloading the native plugin"
hyprctl plugin unload "$root/hyprlens.so" >/dev/null 2>&1 || true
if command -v hyprpm >/dev/null 2>&1 && hyprpm list 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | grep -q "Plugin hyprlens"; then
  hyprpm disable hyprlens || true
  hyprpm remove hyprlens || echo "hyprpm remove failed; run it yourself: hyprpm remove hyprlens"
fi

rm -f ~/.local/share/applications/jgarza-hyprlens.desktop
update-desktop-database ~/.local/share/applications 2>/dev/null || true

menu=~/.config/omarchy/extensions/omarchy-menu.jsonc
if [[ -f $menu ]] && grep -q '"hyprlens' "$menu"; then
  python3 - "$menu" <<'PY'
import os, re, sys, tempfile

path = os.path.realpath(sys.argv[1])
text = open(path).read()
# Drop the two menu lines, then a trailing comma left on the new last entry.
text = re.sub(r'^  "hyprlens(-settings)?":.*\n', "", text, flags=re.M)
text = re.sub(r",(\s*(?://[^\n]*\n\s*)*)\}", r"\1}", text)
mode = os.stat(path).st_mode & 0o7777
fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".hyprlens-")
os.fchmod(fd, mode)
with os.fdopen(fd, "w") as f:
    f.write(text)
os.replace(tmp, path)
PY
  echo "removed the Hyprlens rows from the Omarchy menu"
fi
omarchy menu refresh >/dev/null 2>&1 || true
omarchy plugin disable jgarza.hyprlens >/dev/null 2>&1 || true

echo
echo "left in place (delete them yourself if you want):"
echo "  the hyprLens block in ~/.config/hypr/bindings.lua  (-- >>> hyprLens ... -- <<< hyprLens)"
echo "  your saved settings:  rm -r ~/.config/omarchy/jgarza.hyprlens"
echo "  the plugin itself:    omarchy plugin remove jgarza.hyprlens"
