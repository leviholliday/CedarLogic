# dmgbuild settings for the Mac app's disk image: the same window as the wx
# app's (res/macos/dmg-background.tiff, laid out by dmg-setup.applescript):
# 660 x 400, the app on the left glass panel, Applications on the right, big
# icons. dmgbuild writes the window's .DS_Store itself, so nothing opens in
# Finder while it's made. mac/package.sh runs it from the repo root with
# -D app=<path to the .app>.
import os

app = defines["app"]  # noqa: F821 (dmgbuild provides `defines`)
name = os.path.basename(app)

format = "UDZO"
filesystem = "HFS+"
files = [app]
symlinks = {"Applications": "/Applications"}
icon = os.path.join(app, "Contents/Resources/CedarLogic.icns")

background = defines.get("background", "res/macos/dmg-background.tiff")  # noqa: F821 (run from the repo root)
window_rect = ((200, 120), (660, 400))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
arrange_by = None
icon_size = 128
text_size = 13
icon_locations = {name: (165, 185), "Applications": (495, 185)}
