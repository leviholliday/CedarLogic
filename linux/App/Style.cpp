// The app's CSS: the Mac app's look for the GTK widgets around the drawn
// parts (the side panel, its search and family menu, the divider, the
// dialogs), in the Mac palette's colours for the theme in use. Rebuilt on
// every theme change (applyTheme).

#include "App.h"
#include "Chrome.h"

#include <string>

namespace {

std::string css(const Color& c) {
	return format("rgba(%d,%d,%d,%.3f)", (int)(c.r * 255 + 0.5f), (int)(c.g * 255 + 0.5f), (int)(c.b * 255 + 0.5f), c.a);
}

GtkCssProvider* provider() {
	static GtkCssProvider* p = [] {
		GtkCssProvider* made = gtk_css_provider_new();
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(made),
		                                          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		return made;
	}();
	return p;
}

}  // namespace

void applyStyle() {
	const Chrome c = chrome();
	const bool dark = c.dark;
	const Color ink = c.barInk();
	const Color field = dark ? rgb255(41, 45, 53) : colorF(1, 1, 1);
	const Color line = dark ? colorF(1, 1, 1, 0.10f) : colorF(0, 0, 0, 0.10f);
	std::string s;
	// The side panel.
	// The side panel is the canvas's own colour, as the Mac's is.
	s += "#palette, #sidepanel { background-color: " + css(c.canvas()) + "; }\n";
	s += "#sash { background-color: " + css(c.sash()) + "; }\n";
	// A tab renamed in place: a bare field on its card.
	s += "#tab-rename { background: transparent; border: none; box-shadow: none; outline: none; min-height: 20px; padding: 0 2px; "
	     "font-weight: bold; font-size: 11.5px; color: " + css(c.tabInk()) + "; caret-color: " + css(c.accent()) + "; }\n";
	s += "#palette entry { border-radius: 8px; min-height: 28px; background-color: " + css(field) + "; border: 1px solid " + css(line) +
	     "; box-shadow: none; }\n";
	s += "#palette entry:focus { border-color: " + css(c.accent()) + "; }\n";
	s += "#palette combobox button { border-radius: 8px; min-height: 28px; padding: 0 10px; background-image: none; background-color: " +
	     css(withAlpha(ink, dark ? 0.08f : 0.05f)) + "; border: 1px solid " + css(line) + "; box-shadow: none; font-weight: bold; }\n";
	s += "#palette combobox button:hover { background-color: " + css(withAlpha(ink, dark ? 0.13f : 0.09f)) + "; }\n";
	s += "#palette flowboxchild { padding: 2px; border-radius: 8px; }\n";
	s += "#palette flowboxchild:hover { background-color: " + css(withAlpha(ink, 0.06f)) + "; }\n";
	s += "#palette label.dim-label { color: " + css(withAlpha(ink, 0.55f)) + "; }\n";
	s += "#palette scrollbar { background: transparent; border: none; }\n";
	// A hairline between the side panel and the canvas, easy to grab: the
	// line is 1 point, the handle around it wider and invisible.
	s += "paned > separator, paned.wide > separator { min-width: 1px; min-height: 1px; background-color: " + css(c.sash()) +
	     "; background-image: none; margin: 0 -4px; border-left: 4px solid transparent; border-right: 4px solid transparent; "
	     "background-clip: content-box; }\n";
	s += "paned > separator:hover { background-color: " + css(withAlpha(c.accent(), 0.6f)) + "; }\n";
	// The status bar, when it's on.
	s += "#status { background-color: " + css(c.bar()) + "; }\n";
	s += "#status label { font-size: 0.9em; }\n";
	// Dialogs, as the Mac's sheets: paper, filled rounded fields that light
	// in the accent when focused, soft rounded buttons with the default one
	// in the accent, switches in the accent.
	const Color paper = dark ? rgb255(28, 31, 37) : rgb255(250, 250, 252);
	const Color fieldFill = dark ? rgb255(41, 45, 53) : rgb255(239, 240, 243);
	const Color text = dark ? rgb255(226, 230, 238) : rgb255(30, 33, 40);
	s += "dialog, dialog > box, dialog .dialog-vbox, messagedialog .dialog-vbox { background-color: " + css(paper) + "; }\n";
	s += "dialog .dialog-action-area { padding: 6px 12px 12px 12px; }\n";
	s += "dialog entry, dialog spinbutton, dialog textview, dialog combobox button { border-radius: 7px; background-image: none; background-color: " +
	     css(fieldFill) + "; border: 1px solid " + css(line) + "; box-shadow: none; min-height: 28px; color: " + css(text) + "; }\n";
	s += "dialog entry:focus, dialog spinbutton:focus { border-color: " + css(c.accent()) + "; box-shadow: 0 0 0 1px " + css(c.accent()) + "; }\n";
	s += "dialog button { border-radius: 8px; background-image: none; background-color: " + css(withAlpha(text, 0.07f)) +
	     "; border: none; box-shadow: none; min-height: 30px; padding: 0 14px; text-shadow: none; }\n";
	s += "dialog button:hover { background-color: " + css(withAlpha(text, 0.12f)) + "; }\n";
	s += "dialog button.default, dialog button.suggested-action { background-color: " + css(c.accent()) + "; color: " + css(c.onAccent()) +
	     "; font-weight: bold; }\n";
	s += "dialog button.default label, dialog button.suggested-action label { color: " + css(c.onAccent()) + "; }\n";
	s += "dialog button.default:hover, dialog button.suggested-action:hover { background-color: " + css(withAlpha(c.accent(), 0.88f)) + "; }\n";
	s += "dialog button.destructive-action { background-color: rgba(229,72,77,0.14); color: rgb(229,72,77); }\n";
	s += "switch { border-radius: 14px; } switch:checked { background-color: " + css(c.accent()) + "; border-color: " + css(c.accent()) + "; }\n";
	s += "check:checked, radio:checked { background-color: " + css(c.accent()) + "; border-color: " + css(c.accent()) + "; color: " +
	     css(c.onAccent()) + "; }\n";
	// The pickers' search: a bare entry over the drawn field.
	s += "#picker-search { background: transparent; border: none; box-shadow: none; outline: none; min-height: 24px; color: " + css(text) +
	     "; }\n";
	// The find bar: a pill floating over the top of the page.
	s += "#findbar { background-color: " + css(dark ? rgb255(40, 43, 50) : rgb255(250, 250, 252)) + "; border: 1px solid " +
	     css(withAlpha(ink, 0.18f)) + "; border-radius: 21px; padding: 4px 6px 4px 8px; box-shadow: 0 2px 8px " +
	     css(colorF(0, 0, 0, dark ? 0.35f : 0.12f)) + "; min-height: 32px; }\n";
	s += "#findbar entry, #findbar-entry { background: transparent; border: none; box-shadow: none; min-height: 24px; color: " + css(ink) +
	     "; }\n";
	s += "#findbar button { border-radius: 8px; background-image: none; border: none; box-shadow: none; min-height: 26px; min-width: 26px; }\n";
	s += "#findbar-done { background-color: " + css(withAlpha(ink, 0.07f)) + "; padding: 0 12px; }\n";
	s += "#findbar label.warning { color: rgb(245,148,33); }\n";
	// The welcome's name box: a bare entry on its drawn field.
	s += "#welcome-name { background: transparent; border: none; box-shadow: none; outline: none; color: rgb(242,242,242); "
	     "caret-color: rgb(56,255,107); font-size: 15px; min-height: 28px; }\n";
	// Settings: paper, with quiet hints and key caps.
	s += "#settings, #settings > box { background-color: " + css(paper) + "; }\n";
	s += "#settings .hint { font-size: 0.85em; color: " + css(withAlpha(text, 0.55f)) + "; }\n";
	s += "#settings .heading, #settings .style-name { font-weight: bold; }\n";
	s += "#settings .section { font-size: 0.8em; font-weight: bold; color: " + css(c.accent()) + "; }\n";
	s += "#settings .recording { color: " + css(c.accent()) + "; font-size: 0.9em; }\n";
	s += ".keycap { border-radius: 5px; padding: 1px 6px; min-width: 14px; font-size: 0.85em; font-weight: bold; background-color: " +
	     css(withAlpha(text, 0.07f)) + "; border: 1px solid " + css(withAlpha(text, 0.16f)) + "; }\n";
	s += "#settings #shortcut-keys { padding: 2px 4px; border-radius: 6px; background-color: transparent; min-height: 24px; }\n";
	s += "#settings #shortcut-keys:hover { background-color: " + css(withAlpha(text, 0.06f)) + "; }\n";
	s += "#settings entry, #settings combobox button, #settings spinbutton { border-radius: 7px; background-image: none; background-color: " +
	     css(fieldFill) + "; border: 1px solid " + css(line) + "; box-shadow: none; min-height: 28px; color: " + css(text) + "; }\n";
	s += "#settings entry:focus { border-color: " + css(c.accent()) + "; box-shadow: 0 0 0 1px " + css(c.accent()) + "; }\n";
	s += "#settings button { border-radius: 8px; background-image: none; background-color: " + css(withAlpha(text, 0.07f)) +
	     "; border: none; box-shadow: none; min-height: 28px; padding: 0 12px; text-shadow: none; }\n";
	s += "#settings button:hover { background-color: " + css(withAlpha(text, 0.12f)) + "; }\n";
	s += "#settings scale highlight { background-color: " + css(c.accent()) + "; border: none; }\n";
	// A gate's settings: the Mac's sheet, its settings in a card.
	const Color cardColor = dark ? rgb255(36, 40, 47) : rgb255(255, 255, 255);
	s += "#gate-settings, #gate-settings > box { background-color: " + css(paper) + "; }\n";
	s += "#gate-settings .sheet-title { font-weight: bold; font-size: 16px; }\n";
	s += "#gate-settings .hint { font-size: 0.85em; color: " + css(withAlpha(text, 0.5f)) + "; }\n";
	s += "#gate-settings .problem { color: rgb(229,72,77); }\n";
	s += "#gate-settings entry.problem { border-color: rgb(229,72,77); }\n";
	s += "#settings-card { background-color: " + css(cardColor) + "; border: 1px solid " + css(line) + "; border-radius: 12px; }\n";
	s += "#settings-card separator { background-color: " + css(line) + "; min-height: 1px; }\n";
	s += "#gate-settings button.destructive-action { background-color: rgba(229,72,77,0.10); color: rgb(229,72,77); }\n";
	s += "#gate-settings button.destructive-action label { color: rgb(229,72,77); }\n";
	// Tooltips: small dark rounded labels, as the Mac's.
	s += "tooltip { border-radius: 6px; background-color: " + css(dark ? rgb255(52, 56, 64, 0.96f) : rgb255(40, 43, 50, 0.94f)) +
	     "; color: white; padding: 2px; }\n";
	s += "tooltip label { color: white; }\n";
	// The notebook holding the pages draws nothing of its own.
	s += "notebook, notebook > stack { background: transparent; border: none; }\n";
	gtk_css_provider_load_from_data(provider(), s.c_str(), -1, nullptr);
}
