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
	s += "#palette, #sidepanel { background-color: " + css(c.panel()) + "; }\n";
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
	// The notebook holding the pages draws nothing of its own.
	s += "notebook, notebook > stack { background: transparent; border: none; }\n";
	gtk_css_provider_load_from_data(provider(), s.c_str(), -1, nullptr);
}
