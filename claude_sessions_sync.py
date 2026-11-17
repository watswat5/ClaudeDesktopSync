#!/usr/bin/env python3
"""Keep a link to each Claude Code session in the folder the session works in.

For every live (not archived) local session of the Claude desktop app:
  ~/.local/share/claude-dolphin/sessions/<sessionId>.desktop   a launcher opening the session
                                                               (every session, archived too)
  <session cwd>/<title> [<id>].claude-session.desktop          a symlink to it
and, when <cwd> is in a git repo, one line in that repo's .git/info/exclude
(local only, never committed) so the symlinks don't show up as untracked.

Archived or deleted sessions lose their symlink; deleted ones their launcher too. Only symlinks
this script made (listed in its state file and pointing into its own folder)
are ever removed. Also tells open Dolphin views of claude-sessions:/ to re-list.
Reads the app's session files; never writes to them.

  claude_sessions_sync.py            sync
  claude_sessions_sync.py --remove   remove every symlink, link file and exclude line it made
"""
import glob
import json
import os
import re
import subprocess
import sys
import time
from urllib.parse import quote

HOME = os.path.expanduser("~")
APP_DIR = os.path.join(HOME, ".config", "Claude")
SESSIONS_DIR = os.path.join(APP_DIR, "claude-code-sessions")
SCRATCH_ROOT = os.path.join(APP_DIR, "scratch-workspaces")
DATA = os.path.join(HOME, ".local", "share", "claude-dolphin")
LINKS = os.path.join(DATA, "sessions")
STATE = os.path.join(DATA, "state.json")
SUFFIX = ".claude-session.desktop"
EXCLUDE_LINE = "*" + SUFFIX
EXCLUDE_COMMENT = "# Claude session links (claude-dolphin)"


# ---- reading the app's sessions --------------------------------------------

def load_sessions():
    archived = set()
    for idx in glob.glob(os.path.join(SESSIONS_DIR, "*", "*", "archived-sessions.idx")):
        try:
            with open(idx, encoding="utf-8") as f:
                archived.update(json.load(f).get("archived", []))
        except (OSError, ValueError):
            pass
    sessions, unreadable = [], False
    for path in glob.glob(os.path.join(SESSIONS_DIR, "*", "*", "local_*.json")):
        try:
            with open(path, encoding="utf-8") as f:
                d = json.load(f)
        except (OSError, ValueError):
            unreadable = True  # mid-write; keep its old link until the next run
            continue
        if d.get("sessionId", "").startswith("local_"):
            d["isArchived"] = bool(d.get("isArchived")) or d["sessionId"] in archived
            sessions.append(d)
    return sessions, unreadable


# ---- link files and symlinks ------------------------------------------------

def esc(s):
    return s.replace("\\", "\\\\").replace("\n", " ").replace("\r", "")


def link_content(d):
    ts = (d.get("lastActivityAt") or d.get("createdAt") or 0) / 1000
    when = time.strftime("%b %d", time.localtime(ts)) if ts else ""
    title = (d.get("title") or "").strip() or "Untitled"
    summary = (d.get("postTurnSummary") or {}).get("status_detail") or ""
    comment = "Claude session" + (f" — {summary}" if summary else "")
    return "\n".join([
        "[Desktop Entry]",
        # A launcher, not Type=Link: Dolphin tries to browse a Link's URL as a
        # folder ("Invalid protocol 'claude'") instead of handing it to the app.
        "Type=Application",
        f"Name={esc(title)}" + (f"  ({when})" if when else ""),
        f"Comment={esc(comment)}",
        "Icon=claude-desktop",
        f"Exec=xdg-open claude://claude.ai/epitaxy/{d['sessionId']}",
        "Terminal=false",
        "",
    ])


def symlink_name(d):
    title = re.sub(r"[/\x00-\x1f]", " ", (d.get("title") or "").strip() or "Untitled")
    title = re.sub(r"\s+", " ", title)[:80].rstrip(". ")
    return f"{title} [{d['sessionId'][6:14]}]{SUFFIX}"


def wanted_links(sessions):
    """{symlink path: (session id, link content)} for sessions that get one."""
    out = {}
    for d in sessions:
        cwd = d.get("cwd") or ""
        if d["isArchived"] or not cwd or cwd.startswith(SCRATCH_ROOT + os.sep):
            continue
        if not (os.path.isdir(cwd) and os.access(cwd, os.W_OK)):
            continue  # folder gone or not ours to write in
        out[os.path.join(cwd, symlink_name(d))] = (d["sessionId"], link_content(d))
    return out


def is_ours(path):
    return os.path.islink(path) and os.path.dirname(os.readlink(path)) == LINKS


def write_if_changed(path, content, mode=0o644):
    try:
        with open(path, encoding="utf-8") as f:
            if f.read() == content and os.stat(path).st_mode & 0o777 == mode:
                return
    except OSError:
        pass
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(content)
    os.chmod(tmp, mode)
    os.replace(tmp, path)


# ---- git exclude ------------------------------------------------------------

def exclude_file(folder):
    try:
        out = subprocess.run(["git", "-C", folder, "rev-parse", "--git-path", "info/exclude"],
                             capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if out.returncode != 0 or not out.stdout.strip():
        return None
    return os.path.normpath(os.path.join(folder, out.stdout.strip()))


def add_exclude(path):
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        text = ""
    if EXCLUDE_LINE in text.splitlines():
        return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a", encoding="utf-8") as f:
        if text and not text.endswith("\n"):
            f.write("\n")
        f.write(f"{EXCLUDE_COMMENT}\n{EXCLUDE_LINE}\n")
    return True


def remove_exclude(path):
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines(keepends=True)
    except OSError:
        return
    keep = [l for l in lines if l.rstrip("\n") not in (EXCLUDE_LINE, EXCLUDE_COMMENT)]
    if keep != lines:
        with open(path, "w", encoding="utf-8") as f:
            f.writelines(keep)


# ---- claude-sessions:/ refresh ----------------------------------------------

def virtual_dir(cwd):
    if not cwd or cwd.startswith(SCRATCH_ROOT + os.sep):
        return ["No workspace"]
    cwd = os.path.normpath(cwd)
    if cwd == HOME:
        return []
    if cwd.startswith(HOME + os.sep):
        return os.path.relpath(cwd, HOME).split(os.sep)
    return ["Outside home"] + cwd.strip(os.sep).split(os.sep)


def virtual_dirs(sessions):
    """Folders of claude-sessions:/; must match kio/claudesessions.cpp."""
    dirs = {(), ("Cloud",), ("No workspace",), ("Archived",)}
    for d in sessions:
        if d["isArchived"]:
            continue
        parts = virtual_dir(d.get("cwd"))
        for i in range(len(parts) + 1):
            dirs.add(tuple(parts[:i]))
    return dirs


def notify_kio(dirs):
    for parts in sorted(dirs):
        url = "claude-sessions:/" + "/".join(quote(p) for p in parts)
        subprocess.run(["gdbus", "emit", "--session", "--object-path", "/",
                        "--signal", "org.kde.KDirNotify.FilesAdded", url],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


# ---- main -------------------------------------------------------------------

def load_state():
    try:
        with open(STATE, encoding="utf-8") as f:
            s = json.load(f)
    except (OSError, ValueError):
        s = {}
    return {"links": s.get("links", []), "excludes": s.get("excludes", []),
            "dirs": [tuple(d) for d in s.get("dirs", [])]}


def save_state(state):
    os.makedirs(DATA, exist_ok=True)
    state = dict(state, dirs=[list(d) for d in state["dirs"]])
    write_if_changed(STATE, json.dumps(state, indent=1, sort_keys=True) + "\n")


def remove_all():
    state = load_state()
    for link in state["links"]:
        if is_ours(link):
            os.remove(link)
    for path in state["excludes"]:
        remove_exclude(path)
    for f in glob.glob(os.path.join(LINKS, "*.desktop")):
        os.remove(f)
    if os.path.exists(STATE):
        os.remove(STATE)


def sync():
    state = load_state()
    sessions, unreadable = load_sessions()
    wanted = wanted_links(sessions)
    os.makedirs(LINKS, exist_ok=True)

    # A launcher for every session, archived ones too: the symlinks and the
    # claude-sessions:/ worker both open sessions through these. Executable,
    # since KDE only trusts (runs, and shows the Name of) executable launchers.
    for d in sessions:
        write_if_changed(os.path.join(LINKS, d["sessionId"] + ".desktop"), link_content(d), 0o755)

    for link, (sid, _) in wanted.items():
        target = os.path.join(LINKS, sid + ".desktop")
        if os.path.lexists(link):
            if not is_ours(link) or os.readlink(link) == target:
                continue  # already right, or someone else's file: leave it
            os.remove(link)
        os.symlink(target, link)

    # Symlinks that are no longer wanted (archived, renamed, folder changed).
    known_ids = {d["sessionId"] for d in sessions}
    kept = []
    for link in state["links"]:
        if link in wanted or not is_ours(link):
            continue
        sid = os.path.basename(os.readlink(link))[:-len(".desktop")]
        if unreadable and sid not in known_ids:
            kept.append(link)  # its session file may just be mid-write
            continue
        os.remove(link)
    if not unreadable:  # launchers of deleted sessions
        for f in glob.glob(os.path.join(LINKS, "*.desktop")):
            if os.path.basename(f)[:-len(".desktop")] not in known_ids:
                os.remove(f)

    excludes = set(state["excludes"])
    for folder in {os.path.dirname(l) for l in wanted}:
        path = exclude_file(folder)
        if path and path not in excludes and add_exclude(path):
            excludes.add(path)  # only lines we added get removed again

    dirs = virtual_dirs(sessions)
    notify_kio(dirs | set(state["dirs"]))
    save_state({"links": sorted(set(wanted) | set(kept)), "excludes": sorted(excludes), "dirs": sorted(dirs)})


def main():
    if "--remove" in sys.argv:
        remove_all()
    else:
        sync()
    return 0


if __name__ == "__main__":
    sys.exit(main())
