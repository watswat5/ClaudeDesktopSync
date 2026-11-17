#!/bin/bash
# Install ClaudeDesktopSync for the current user.
#   ./install.sh               build and install (asks for sudo once, for the KIO worker)
#   ./install.sh --uninstall   remove everything it installed, including every session link
set -eu
cd "$(dirname "$0")"

SHARE="$HOME/.local/share/claude-dolphin"
BIN="$HOME/.local/bin"
MENU="$HOME/.local/share/kio/servicemenus"
UNITS="$HOME/.config/systemd/user"
SESSIONS="$HOME/.config/Claude/claude-code-sessions"

die() { echo "error: $*" >&2; exit 1; }

command -v qtpaths6 >/dev/null || die "qtpaths6 not found (install qt6-base-dev)"
PLUGIN="$(qtpaths6 --query QT_INSTALL_PLUGINS)/kf6/kio/claude-sessions.so"

if [ "${1:-}" = "--uninstall" ]; then
    systemctl --user disable --now claude-sessions-sync.path claude-sessions-sync.service 2>/dev/null || true
    [ -f "$SHARE/claude_sessions_sync.py" ] && python3 "$SHARE/claude_sessions_sync.py" --remove
    rm -f "$UNITS"/claude-sessions-sync.{service,path} "$BIN/claude-sessions-here" \
          "$MENU/claude-sessions.desktop" "$MENU/claude-sessions-project.desktop"
    rm -rf "$SHARE"
    python3 places.py --remove
    systemctl --user daemon-reload
    if [ -e "$PLUGIN" ]; then
        echo "Removing $PLUGIN (needs sudo)"
        sudo rm -f "$PLUGIN"
    fi
    echo "Uninstalled."
    exit 0
fi

# ---- checks -----------------------------------------------------------------
missing=()
for cmd in cmake g++ python3 git gdbus systemctl; do
    command -v "$cmd" >/dev/null || missing+=("$cmd")
done
[ -d /usr/share/ECM ] || missing+=("extra-cmake-modules")
compgen -G "/usr/lib/*/cmake/KF6KIO" >/dev/null || compgen -G "/usr/lib*/cmake/KF6KIO" >/dev/null || missing+=("KF6 KIO headers")
if [ ${#missing[@]} -gt 0 ]; then
    echo "Missing: ${missing[*]}" >&2
    echo "On Debian/Ubuntu/KDE neon:  sudo apt install cmake g++ extra-cmake-modules qt6-base-dev libkf6kio-dev" >&2
    echo "On Fedora:                  sudo dnf install cmake gcc-c++ extra-cmake-modules qt6-qtbase-devel kf6-kio-devel" >&2
    echo "On Arch:                    sudo pacman -S cmake extra-cmake-modules qt6-base kio" >&2
    exit 1
fi
compgen -G "$SESSIONS/*/*/" >/dev/null || die "no sessions found in $SESSIONS (is the Claude desktop app installed and signed in?)"

# ---- KIO worker (claude-sessions:/) ---------------------------------------
generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
cmake -S kio -B kio/build "${generator[@]}" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 \
    || cmake -S kio -B kio/build "${generator[@]}" -DCMAKE_BUILD_TYPE=Release
cmake --build kio/build
if ! cmp -s kio/build/bin/kf6/kio/claude-sessions.so "$PLUGIN"; then
    echo "Installing the KIO worker to $PLUGIN (needs sudo)"
    sudo install -Dm644 kio/build/bin/kf6/kio/claude-sessions.so "$PLUGIN"
fi

# ---- sync, menus, units, Places ---------------------------------------------
mkdir -p "$SHARE" "$BIN" "$MENU" "$UNITS"
install -m 755 claude_sessions_sync.py "$SHARE/"
install -m 755 bin/claude-sessions-here "$BIN/"
# Plasma 6 only runs service menus that are executable.
for m in servicemenus/*.desktop; do
    sed "s|^Exec=claude-sessions-here|Exec=$BIN/claude-sessions-here|" "$m" > "$MENU/$(basename "$m")"
    chmod 755 "$MENU/$(basename "$m")"
done
install -m 644 systemd/claude-sessions-sync.service "$UNITS/"
# Watch every account/org folder the app has (inotify only sees direct children).
watch=""
for d in "$SESSIONS"/*/*/; do
    [ -d "$d" ] && watch+="PathChanged=${d%/}"$'\n'
done
awk -v w="$watch" '{print} /^\[Path\]/{printf "%s", w}' systemd/claude-sessions-sync.path > "$UNITS/claude-sessions-sync.path"

systemctl --user daemon-reload
systemctl --user enable --now claude-sessions-sync.path
systemctl --user enable claude-sessions-sync.service 2>/dev/null
systemctl --user start claude-sessions-sync.service
python3 places.py
echo "Installed. Each session now has a link in its project folder; all of them are also under 'Claude Sessions' in Dolphin's Places."
