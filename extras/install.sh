#!/bin/bash
# Wire jgarza.hyprlens into the shell: build the native plugin, enable the
# overlay plugin, add "Hyprlens" rows to the Omarchy menu (SUPER+SPACE), and
# drop an app-launcher entry. Idempotent. Does not touch hyprland.lua or
# bindings.lua: load the plugin at login with hyprpm (see the README), and add
# the key bindings from Hyprlens Settings.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")

# 0. Requirements, checked up front so a missing tool says so plainly.
for c in make g++ pkg-config python3 hyprctl; do
  command -v "$c" >/dev/null 2>&1 || { echo "hyprlens: missing '$c'; install it and run this again." >&2; exit 1; }
done
pkg-config --exists hyprland || { echo "hyprlens: the Hyprland development files (pkg-config 'hyprland') aren't installed." >&2; exit 1; }

# 1. Build the native plugin (and load it into this session if it isn't).
make -C "$root" || { echo "hyprlens: the build failed. It needs Hyprland 0.56+ and its headers; see the output above." >&2; exit 1; }
if ! hyprctl plugin list 2>/dev/null | grep -q "Plugin hyprlens"; then
  hyprctl plugin load "$root/hyprlens.so" || echo "couldn't load it now; see the README"
fi

# 2. Enable the overlay plugin so `omarchy-shell shell toggle` will summon it.
omarchy-shell shell rescanPlugins >/dev/null 2>&1 || true
if command -v omarchy >/dev/null 2>&1; then
  omarchy plugin enable jgarza.hyprlens >/dev/null 2>&1 || true
fi

# 3. App-launcher entry.
mkdir -p ~/.local/share/applications
cp "$here/jgarza-hyprlens.desktop" ~/.local/share/applications/
update-desktop-database ~/.local/share/applications 2>/dev/null || true

# 4. Omarchy menu entries: merge one line each into the user extension file.
menu=~/.config/omarchy/extensions/omarchy-menu.jsonc
mkdir -p "$(dirname "$menu")"
[[ -f $menu ]] || printf '{\n}\n' > "$menu"

# add_menu_entry KEY LINE: insert LINE before the file's closing brace unless KEY is there.
add_menu_entry() {
  if grep -q "\"$1\"" "$menu"; then
    echo "menu entry $1 already present"
    return
  fi
  # Rewrites the symlink's target (dotfiles) in place of the link, via a temp file
  # beside it that carries the original's permissions.
  python3 - "$menu" "$2" <<'PY'
import os, sys, tempfile

path = os.path.realpath(sys.argv[1])
entry = sys.argv[2]
text = open(path).read()
end = text.rindex("}")
head, tail = text[:end], text[end:]
# The last real line before the brace gets a comma unless it opens the object
# or already has one. Blank lines and // comments don't count.
lines = head.split("\n")
for i in range(len(lines) - 1, -1, -1):
    s = lines[i].strip()
    if s and not s.startswith("//"):
        if not s.endswith(("{", ",")):
            lines[i] = lines[i].rstrip() + ","
        break
head = "\n".join(lines).rstrip(" \t")
if not head.endswith("\n"):
    head += "\n"
mode = os.stat(path).st_mode & 0o7777
fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".hyprlens-")
os.fchmod(fd, mode)
with os.fdopen(fd, "w") as f:
    f.write(head + entry + "\n" + tail)
os.replace(tmp, path)
PY
  echo "added $1 to the Omarchy menu"
}

add_menu_entry hyprlens '  "hyprlens": {"icon":"󰍉","label":"Hyprlens","aliases":["hyprlens","lens","magnifier","magnify","zoom"],"description":"Magnifying lens centered on the pointer","action":"hyprctl hyprlens toggle"}'
add_menu_entry hyprlens-settings '  "hyprlens-settings": {"icon":"󰒓","label":"Hyprlens Settings","aliases":["hyprlens-settings","lens settings","magnifier settings"],"description":"Adjust the lens and set up its key bindings","action":"omarchy-shell hyprlens settings"}'

omarchy menu refresh >/dev/null 2>&1 || true

echo "done. Open it from the Omarchy menu (SUPER+SPACE -> Hyprlens), the app launcher, or:"
echo "  hyprctl hyprlens toggle"
