// Every command the menus, the toolbar, the right-click menu and the keyboard
// shortcuts can run, by id (the WM_COMMAND id).

#ifndef CL_WINDOWS_COMMANDS_H
#define CL_WINDOWS_COMMANDS_H

enum Command {
	CMD_NONE = 0,
	// File
	CMD_NEW = 1000, CMD_OPEN, CMD_IMPORT, CMD_VERSIONS, CMD_NEW_TEMPLATE, CMD_SAVE_TEMPLATE, CMD_OPEN_SAMPLE, CMD_SAVE, CMD_SAVE_AS, CMD_EXPORT_IMAGE, CMD_EXPORT_V2, CMD_EXPORT_V1,
	CMD_PRINT, CMD_CLOSE_WINDOW, CMD_QUIT,
	// Edit
	CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_DUPLICATE, CMD_DELETE, CMD_SELECT_ALL,
	CMD_ADD_GATE, CMD_GATE_SETTINGS, CMD_ROTATE, CMD_STRAIGHTEN, CMD_TIDY, CMD_TIDY_FLOW, CMD_TIDY_KEEP,
	CMD_TIDY_REVERT, CMD_TIDY_SWITCH, CMD_CONNECT_NEARBY, CMD_SAVE_PART, CMD_BUILD_FORMULA, CMD_FIND, CMD_PREFERENCES,
	// View
	CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ZOOM_FIT, CMD_ZOOM_ACTUAL, CMD_DARK, CMD_PALETTE, CMD_STATUS_BAR,
	// Simulate
	CMD_RUNNING, CMD_STEP, CMD_SIM_VIEW, CMD_LOCK, CMD_TRUTH_TABLE, CMD_CHECK_CIRCUIT, CMD_SCOPE,
	// Tabs
	CMD_NEW_TAB, CMD_CLOSE_TAB, CMD_REOPEN_TAB, CMD_RENAME_TAB, CMD_NEXT_TAB, CMD_PREVIOUS_TAB,
	// Split view and focus mode
	CMD_SPLIT_VIEW, CMD_SWITCH_PANE, CMD_CLOSE_SPLIT, CMD_FOCUS_MODE,
	// Help
	CMD_SHORTCUTS, CMD_TOUR, CMD_HELP, CMD_CHECK_UPDATES, CMD_ABOUT, CMD_WHATS_NEW, CMD_FEEDBACK,
	// The right-click menu's Disconnect, and the recent files (CMD_RECENT + i).
	CMD_DISCONNECT,
	// Help's Add to / Remove from Start Menu, for a copy run from the zip.
	CMD_START_MENU,
	// File's Export Lab Report.
	CMD_EXPORT_REPORT,
	// File > Share Link…
	CMD_SHARE_LINK,
	// Simulate > Step Clock: one full cycle of each clock set to "Only on Step Clock".
	CMD_STEP_CLOCK,
	// Parts locked in place: Edit's Lock in Place (Unlock Part when every
	// selected part is locked), the right-click menu's lock and unlock, and
	// Edit's Unlock All Parts.
	CMD_LOCK_IN_PLACE, CMD_LOCK_PARTS, CMD_UNLOCK_PARTS, CMD_UNLOCK_ALL_PARTS,
	// Tabs > Connect All Pages and Disconnect All Pages: which pages' TO/FROM
	// links connect (docs/PAGE-LINKS.md; a tab's right-click menu does it page by page).
	CMD_CONNECT_ALL_PAGES, CMD_DISCONNECT_ALL_PAGES,
	CMD_RECENT = 1900,
	CMD_RECENT_LAST = 1909,
};

#endif  // CL_WINDOWS_COMMANDS_H
