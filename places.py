#!/usr/bin/env python3
"""Add (or with --remove, drop) the Claude Sessions entry in Dolphin's Places panel.

Edits ~/.local/share/user-places.xbel as text so the rest of the file stays
byte-for-byte the same; a backup is kept next to it.
"""
import os
import re
import shutil
import sys
import time

PLACES = os.path.expanduser("~/.local/share/user-places.xbel")
HREF = "claude-sessions:/"
ENTRY = f""" <bookmark href="{HREF}">
  <title>Claude Sessions</title>
  <info>
   <metadata owner="http://freedesktop.org">
    <bookmark:icon name="claude-desktop"/>
   </metadata>
   <metadata owner="http://www.kde.org">
    <ID>{int(time.time())}/claude-sessions</ID>
   </metadata>
  </info>
 </bookmark>
"""
OURS = re.compile(r'[ \t]*<bookmark href="' + re.escape(HREF) + r'">.*?</bookmark>\n?', re.S)


def main():
    remove = "--remove" in sys.argv
    try:
        with open(PLACES, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        if remove:
            return 0
        text = ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE xbel>\n'
                '<xbel xmlns:bookmark="http://www.freedesktop.org/standards/desktop-bookmarks">\n</xbel>\n')
    present = bool(OURS.search(text))
    if present != remove:
        return 0  # already as wanted
    if remove:
        new = OURS.sub("", text, count=1)
    else:
        # Right after Home, so it shows near the top of Places.
        home = re.search(r'<bookmark href="file://' + re.escape(os.path.expanduser("~")) + r'/?">.*?</bookmark>\n', text, re.S)
        at = home.end() if home else text.rindex("</xbel>")
        new = text[:at] + ENTRY + text[at:]
    if os.path.exists(PLACES):
        shutil.copy2(PLACES, PLACES + ".bak-claude-sessions")
    with open(PLACES + ".tmp", "w", encoding="utf-8") as f:
        f.write(new)
    os.replace(PLACES + ".tmp", PLACES)
    return 0


if __name__ == "__main__":
    sys.exit(main())
