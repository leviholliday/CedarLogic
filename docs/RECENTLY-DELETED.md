# Recently Deleted (native apps)

- Deleting a circuit from Your Circuits moves its folder into `<library>/.Trash/<id>` (`<id> 2`, `<id> 3`… if taken), as before, and writes `deleted.txt` in it: the time, ISO 8601 UTC (`2026-10-31T17:04:00Z`).
- A circuit folder in `.Trash` with `circuit.cdl` but no `deleted.txt` (sync's `trash()` in the core, the wx app, older versions) is stamped the first time it's listed: with the folder's status-change time if that's within 30 days, otherwise now (so it still gets 30 days on the list). Folders without `circuit.cdl` (`repeated versions`) are not circuits: never listed, never swept.
- The list: name (`name.txt`, "Untitled" if empty), "Deleted <friendly time> · N gates", newest first. Heading "Recently Deleted"; line "Kept 30 days on this <Mac / PC / computer>, then deleted for good. These aren't synced."; empty: "Nothing here." Reached from Your Circuits' "Recently Deleted (N)" link.
- **Restore** moves the folder back to `<library>/<id>` (the trash name without a trailing ` N` if that's free; a new `yyyyMMdd-HHmmss-NNNNN` id if not), deletes `deleted.txt`, sets `circuit.cdl`'s modified time to now (top of the list), tells sync the library changed. Note: "“Name” is back in Your Circuits."
- **Delete Now…** asks ("Delete “Name” now?" / "It and all its versions will be deleted for good. This can't be undone." / Delete Now, Cancel), then removes the folder.
- **Sweep**: at launch (in the background) and whenever the list opens, circuits whose deleted time is over 30 days ago are removed.
- Mac: `mac/App/RecentlyDeleted.swift` (all of the above, files only), the sheet in `LibraryView.swift`, checked by `mac/Tools/launch-selftest.sh` (state `trash`).
