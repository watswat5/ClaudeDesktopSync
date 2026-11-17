# ClaudeDesktopSync

See your **Claude Code** sessions from the Claude desktop app right inside
**KDE Dolphin**. Each session shows up in the folder it works in. Double-click it
to jump back into that session.

```
~/Projects/my-game/
├── Assets/
├── Packages/
├── ProjectSettings/
└── Unity project setup  (Nov 17)      ← double-click: opens this session in Claude
```

The Claude desktop app's sidebar is a flat list. If you keep many sessions across
many projects, this gives you the folder view instead.

## Features

- **Sessions in their project folders.** Every live session gets a link in its
  working directory, showing the session title and last-activity date.
- **Always in sync.** New sessions appear, archived ones disappear, and renamed
  ones update within seconds, driven by a systemd path unit. Nothing to refresh.
- **Git-friendly.** In git repos the links are ignored through the repo's local
  `.git/info/exclude`, so they never show as untracked and nothing is committed.
- **One place for everything.** A **Claude Sessions** entry in Dolphin's Places
  panel (`claude-sessions:/`, a KIO worker) shows all sessions as a tree that
  mirrors your folders. It also has *No workspace* (scratch sessions), *Archived*,
  and *Cloud*.
- **Right-click menu.** *Claude sessions here* on any folder opens its view in the
  tree. *Open project folder* goes back the other way.
- **Read-only.** It never writes to the Claude app's data.

## Requirements

- Linux with KDE Plasma 6 / Dolphin (KDE Frameworks 6)
- The Claude desktop app, with Claude Code sessions
- systemd (user session), Python 3, git
- Build tools for the KIO worker: CMake, a C++ compiler, extra-cmake-modules,
  Qt 6 and KF6 KIO development files

```bash
# Debian / Ubuntu / KDE neon
sudo apt install cmake g++ extra-cmake-modules qt6-base-dev libkf6kio-dev
# Fedora
sudo dnf install cmake gcc-c++ extra-cmake-modules qt6-qtbase-devel kf6-kio-devel
# Arch
sudo pacman -S cmake extra-cmake-modules qt6-base kio
```

## Install

```bash
git clone https://github.com/watswat5/ClaudeDesktopSync.git
cd ClaudeDesktopSync
./install.sh
```

Everything is per-user, except the KIO worker, which goes into Qt's plugin folder
(e.g. `/usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/kio/`), so the script asks for
sudo once. Restart Dolphin afterwards so it picks up the right-click entries.

Uninstall with `./install.sh --uninstall`. It removes every link, launcher, git
exclude line, unit and menu entry it added, and the Places entry.

## How it works

```
Claude desktop app                     ClaudeDesktopSync
~/.config/Claude/claude-code-sessions/
  <account>/<org>/local_<id>.json  ──▶  claude-sessions-sync.path (inotify)
                                           │
                                           ▼
                                   claude_sessions_sync.py
                                     ├─ ~/.local/share/claude-dolphin/sessions/<id>.desktop
                                     │     launcher: xdg-open claude://claude.ai/epitaxy/<id>
                                     ├─ <session cwd>/<title> [<id>].claude-session.desktop
                                     │     symlink to that launcher (live sessions only)
                                     ├─ <repo>/.git/info/exclude  += *.claude-session.desktop
                                     └─ D-Bus KDirNotify → open claude-sessions:/ views re-list

Dolphin ── claude-sessions:/ ──▶ kio/claudesessions.cpp (reads the session files on every listing)
```

- The desktop app stores one JSON file per local Claude Code session, with its
  title, working directory (`cwd`), timestamps and archived flag. The tool only
  reads these files.
- Each session gets an executable launcher that opens its deep link
  (`claude://claude.ai/epitaxy/<sessionId>`). It's a `Type=Application` launcher
  rather than a `Type=Link`, because Dolphin tries to *browse* a link's URL.
  Executable, because KDE only runs launchers, and shows their names, when
  they're executable.
- Project folders get a **symlink** to the launcher, so a renamed session only
  changes the central file. The sync only ever removes symlinks it made itself:
  they're listed in `~/.local/share/claude-dolphin/state.json` and point into its
  own folder.
- Sessions in scratch workspaces, and sessions whose folder no longer exists, get
  no link. They're still listed in `claude-sessions:/`.

### `claude-sessions:/` layout

```
claude-sessions:/
├── Projects/my-game/        ← folders mirror session working directories under ~
├── No workspace/            ← scratch sessions
├── Archived/
├── Cloud/                   ← link to claude.ai/code
└── Outside home/            ← sessions whose folder is outside ~
```

## Limitations

- **Linux / KDE only.** The paths are those of the Claude desktop app on Linux.
- **Cloud sessions aren't listed individually.** The desktop app doesn't keep
  them on disk, so *Cloud/* just links to https://claude.ai/code.
- **Relies on the app's internal files.** The session file format isn't a public
  API and may change with app updates.
- **Synced folders.** If a project folder is synced to another machine, the
  symlinks go along but point to a path that only exists here. On other machines
  they're broken links, which is harmless.
- **Signed into another account or organization later?** Re-run `./install.sh`
  so the watcher covers its session folder.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| Right-click entries missing | Restart Dolphin. |
| A link didn't update | `systemctl --user start claude-sessions-sync.service`, then check `journalctl --user -u claude-sessions-sync.service`. |
| `claude-sessions:/` says the protocol is unknown | Re-run `./install.sh`, e.g. after a KDE Frameworks upgrade. |
| Session opens an "Open with" dialog | Check that `xdg-mime query default x-scheme-handler/claude` names the Claude desktop app. |

## Repository layout

| Path | What |
| --- | --- |
| `claude_sessions_sync.py` | launchers, project-folder symlinks, git excludes, view refresh |
| `kio/` | the `claude-sessions:/` KIO worker (C++, KF6) |
| `bin/claude-sessions-here` | right-click helper |
| `servicemenus/` | Dolphin right-click entries |
| `systemd/` | user units that run the sync on change and at login |
| `places.py` | adds or removes the Places entry |
| `install.sh` | build, install, uninstall |

## License

MIT. See [LICENSE](LICENSE).

Unofficial community project, not affiliated with or endorsed by Anthropic.
"Claude" is a trademark of Anthropic.
