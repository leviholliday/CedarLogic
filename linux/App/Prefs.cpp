// Settings, resources, colours and small helpers (see App.h).

#include "App.h"
#include "Window.h"

#include <cmath>
#include <cstdarg>
#include <cstring>
#include <unistd.h>
#include <algorithm>

// ---- Settings ------------------------------------------------------------------

Prefs& prefs() {
	static Prefs p;
	return p;
}

double Prefs::wireScale() const {
	static const double scales[] = { 0.7, 1.0, 1.6 };
	return scales[std::min(std::max(wireThickness, 0), 2)];
}

static std::string prefsPath() {
	gchar* dir = g_build_filename(g_get_user_config_dir(), "CedarLogic", nullptr);
	g_mkdir_with_parents(dir, 0700);
	gchar* file = g_build_filename(dir, "native.ini", nullptr);
	std::string s = file;
	g_free(file);
	g_free(dir);
	return s;
}

namespace {
const char* kGroup = "CedarLogic";

int readInt(GKeyFile* k, const char* key, int fallback, int lo, int hi) {
	GError* e = nullptr;
	const int v = g_key_file_get_integer(k, kGroup, key, &e);
	if (e) { g_error_free(e); return fallback; }
	return std::min(std::max(v, lo), hi);
}

bool readBool(GKeyFile* k, const char* key, bool fallback) {
	GError* e = nullptr;
	const gboolean v = g_key_file_get_boolean(k, kGroup, key, &e);
	if (e) { g_error_free(e); return fallback; }
	return v;
}

double readDouble(GKeyFile* k, const char* key, double fallback, double lo, double hi) {
	GError* e = nullptr;
	const double v = g_key_file_get_double(k, kGroup, key, &e);
	if (e) { g_error_free(e); return fallback; }
	return std::isfinite(v) ? std::min(std::max(v, lo), hi) : fallback;
}

std::string readString(GKeyFile* k, const char* key, const std::string& fallback) {
	gchar* v = g_key_file_get_string(k, kGroup, key, nullptr);
	if (!v) return fallback;
	std::string s = v;
	g_free(v);
	return s;
}
}  // namespace

void Prefs::load() {
	GKeyFile* k = g_key_file_new();
	if (g_key_file_load_from_file(k, prefsPath().c_str(), G_KEY_FILE_NONE, nullptr)) {
		themeMode = readInt(k, "themeMode", themeMode, 0, 3);
		dark = readBool(k, "lastDark", dark);
		accent = readInt(k, "accent", accent, 0, 6);
		// The icon's green became the default (and everyone's accent, once), as on the Mac.
		if (!readBool(k, "brandAccentSet", false)) accent = 6;
		showGrid = readBool(k, "showGrid", showGrid);
		gridStyle = readInt(k, "gridStyle", gridStyle, 0, 1);
		majorGrid = readBool(k, "majorGrid", majorGrid);
		wireThickness = readInt(k, "wireThickness", wireThickness, 0, 2);
		wireDots = readBool(k, "wireDots", wireDots);
		wireDotSize = readDouble(k, "wireDotSize", wireDotSize, 0.05, 0.5);
		lowWire = readInt(k, "lowWire", lowWire, 0, 3);
		mouseWheel = readInt(k, "mouseWheel", mouseWheel, 0, 1);
		touchpadScroll = readInt(k, "touchpadScroll", touchpadScroll, 0, 1);
		reverseWheel = readBool(k, "reverseWheel", reverseWheel);
		reverseTouchpad = readBool(k, "reverseTouchpad", reverseTouchpad);
		rightClickRotate = readBool(k, "rightClickRotate", rightClickRotate);
		duplicateUsesClipboard = readBool(k, "duplicateClipboard", duplicateUsesClipboard);
		showPalette = readBool(k, "showPalette", showPalette);
		showStatus = readBool(k, "showStatus", showStatus);
		showGateNames = readBool(k, "showGateNames", showGateNames);
		tidyMode = readInt(k, "tidyMode", tidyMode, 0, 1);
		hasSeenWelcome = readBool(k, "hasSeenWelcome", hasSeenWelcome);
		windowWidth = readInt(k, "windowWidth", windowWidth, 400, 20000);
		windowHeight = readInt(k, "windowHeight", windowHeight, 300, 20000);
		windowMaximized = readBool(k, "windowMaximized", windowMaximized);
		paletteWidth = readInt(k, "paletteWidth", paletteWidth, 120, 800);
		lastFolder = readString(k, "lastFolder", lastFolder);
		lastCircuit = readString(k, "lastCircuit", lastCircuit);
		studentName = readString(k, "studentName", studentName);
		lastFormula = readString(k, "lastFormula", lastFormula);
		buildShape = readInt(k, "buildShape", buildShape, 0, 2);
		buildStyle = readInt(k, "buildStyle", buildStyle, 0, 2);
		buildTwoInput = readBool(k, "buildTwoInput", buildTwoInput);
		buildNewPage = readBool(k, "buildNewPage", buildNewPage);
		feedbackName = readString(k, "feedbackName", feedbackName);
		feedbackEmail = readString(k, "feedbackEmail", feedbackEmail);
		lastSeenVersion = readString(k, "lastSeenVersion", lastSeenVersion);
		playLaunchSound = readBool(k, "playLaunchSound", playLaunchSound);
		gsize n = 0;
		if (gchar** list = g_key_file_get_string_list(k, kGroup, "recent", &n, nullptr)) {
			recent.clear();
			for (gsize i = 0; i < n && recent.size() < 10; i++) if (list[i][0]) recent.push_back(list[i]);
			g_strfreev(list);
		}
	}
	g_key_file_free(k);
	switch (themeMode) {
	case 1: dark = false; break;
	case 2: dark = true; break;
	case 3: break;   // as last time
	default: dark = systemPrefersDark(); break;
	}
}

void Prefs::save() const {
	GKeyFile* k = g_key_file_new();
	g_key_file_set_integer(k, kGroup, "themeMode", themeMode);
	g_key_file_set_boolean(k, kGroup, "lastDark", dark);
	g_key_file_set_integer(k, kGroup, "accent", accent);
	g_key_file_set_boolean(k, kGroup, "brandAccentSet", TRUE);
	g_key_file_set_boolean(k, kGroup, "showGrid", showGrid);
	g_key_file_set_integer(k, kGroup, "gridStyle", gridStyle);
	g_key_file_set_boolean(k, kGroup, "majorGrid", majorGrid);
	g_key_file_set_integer(k, kGroup, "wireThickness", wireThickness);
	g_key_file_set_boolean(k, kGroup, "wireDots", wireDots);
	g_key_file_set_double(k, kGroup, "wireDotSize", wireDotSize);
	g_key_file_set_integer(k, kGroup, "lowWire", lowWire);
	g_key_file_set_integer(k, kGroup, "mouseWheel", mouseWheel);
	g_key_file_set_integer(k, kGroup, "touchpadScroll", touchpadScroll);
	g_key_file_set_boolean(k, kGroup, "reverseWheel", reverseWheel);
	g_key_file_set_boolean(k, kGroup, "reverseTouchpad", reverseTouchpad);
	g_key_file_set_boolean(k, kGroup, "rightClickRotate", rightClickRotate);
	g_key_file_set_boolean(k, kGroup, "duplicateClipboard", duplicateUsesClipboard);
	g_key_file_set_boolean(k, kGroup, "showPalette", showPalette);
	g_key_file_set_boolean(k, kGroup, "showStatus", showStatus);
	g_key_file_set_boolean(k, kGroup, "showGateNames", showGateNames);
	g_key_file_set_integer(k, kGroup, "tidyMode", tidyMode);
	g_key_file_set_boolean(k, kGroup, "hasSeenWelcome", hasSeenWelcome);
	g_key_file_set_integer(k, kGroup, "windowWidth", windowWidth);
	g_key_file_set_integer(k, kGroup, "windowHeight", windowHeight);
	g_key_file_set_boolean(k, kGroup, "windowMaximized", windowMaximized);
	g_key_file_set_integer(k, kGroup, "paletteWidth", paletteWidth);
	g_key_file_set_string(k, kGroup, "lastFolder", lastFolder.c_str());
	g_key_file_set_string(k, kGroup, "lastCircuit", lastCircuit.c_str());
	g_key_file_set_string(k, kGroup, "studentName", studentName.c_str());
	g_key_file_set_string(k, kGroup, "lastFormula", lastFormula.c_str());
	g_key_file_set_integer(k, kGroup, "buildShape", buildShape);
	g_key_file_set_integer(k, kGroup, "buildStyle", buildStyle);
	g_key_file_set_boolean(k, kGroup, "buildTwoInput", buildTwoInput);
	g_key_file_set_boolean(k, kGroup, "buildNewPage", buildNewPage);
	g_key_file_set_string(k, kGroup, "feedbackName", feedbackName.c_str());
	g_key_file_set_string(k, kGroup, "feedbackEmail", feedbackEmail.c_str());
	g_key_file_set_string(k, kGroup, "lastSeenVersion", lastSeenVersion.c_str());
	g_key_file_set_boolean(k, kGroup, "playLaunchSound", playLaunchSound);
	std::vector<const gchar*> list;
	for (const std::string& r : recent) list.push_back(r.c_str());
	g_key_file_set_string_list(k, kGroup, "recent", list.data(), list.size());
	g_key_file_save_to_file(k, prefsPath().c_str(), nullptr);
	g_key_file_free(k);
}

void Prefs::applyWireDots() const {
	cl_set_wire_dots(wireDots, wireDotSize);
	static const double colors[][3] = { { 0.62, 0.67, 0.76 }, { 0.47, 0.58, 0.84 }, { 0.86, 0.87, 0.90 } };
	if (lowWire >= 0 && lowWire < 3) cl_set_low_wire_color(true, colors[lowWire][0], colors[lowWire][1], colors[lowWire][2]);
	else cl_set_low_wire_color(false, 0, 0, 0);
}

void Prefs::noteRecent(const std::string& path) {
	if (path.empty()) return;
	recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
	recent.insert(recent.begin(), path);
	if (recent.size() > 10) recent.resize(10);
	gchar* dir = g_path_get_dirname(path.c_str());
	lastFolder = dir;
	g_free(dir);
	save();
	// The desktop's own list too, so the file manager and file choosers know it.
	if (gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr)) {
		gtk_recent_manager_add_item(gtk_recent_manager_get_default(), uri);
		g_free(uri);
	}
	rebuildRecentMenus();
}

// ---- The theme -----------------------------------------------------------------

bool systemPrefersDark() {
	// GNOME 42+ (and others that follow it) keep the choice in color-scheme.
	// Look the schema up first: g_settings_new on a missing one aborts.
	if (GSettingsSchemaSource* src = g_settings_schema_source_get_default()) {
		if (GSettingsSchema* schema = g_settings_schema_source_lookup(src, "org.gnome.desktop.interface", TRUE)) {
			const bool has = g_settings_schema_has_key(schema, "color-scheme");
			g_settings_schema_unref(schema);
			if (has) {
				GSettings* s = g_settings_new("org.gnome.desktop.interface");
				gchar* scheme = g_settings_get_string(s, "color-scheme");
				const bool dark = scheme && strcmp(scheme, "prefer-dark") == 0;
				g_free(scheme);
				g_object_unref(s);
				if (dark) return true;
			}
		}
	}
	// Otherwise a dark GTK theme says so by name ("Adwaita-dark", "PiXnoir"...).
	gchar* theme = nullptr;
	g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme, nullptr);
	bool dark = false;
	if (theme) {
		gchar* lower = g_ascii_strdown(theme, -1);
		dark = strstr(lower, "dark") != nullptr || strstr(lower, "noir") != nullptr;
		g_free(lower);
		g_free(theme);
	}
	return dark;
}

// Whether the theme GTK is using now has a dark background.
static bool themeLooksDark() {
	GtkWidget* probe = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkStyleContext* sc = gtk_widget_get_style_context(probe);
	GdkRGBA bg;
	bool dark;
	if (gtk_style_context_lookup_color(sc, "theme_bg_color", &bg)) {
		dark = 0.2126 * bg.red + 0.7152 * bg.green + 0.0722 * bg.blue < 0.5;
	} else {
		GdkRGBA fg;
		gtk_style_context_get_color(sc, GTK_STATE_FLAG_NORMAL, &fg);
		dark = 0.2126 * fg.red + 0.7152 * fg.green + 0.0722 * fg.blue > 0.5;   // light text
	}
	gtk_widget_destroy(probe);
	return dark;
}

void applyTheme() {
	// The desktop's own theme first, asked for its dark or light side. Not
	// every theme has both (and a theme that isn't installed falls back to a
	// light one): then GTK's built-in Adwaita, which always has both, so the
	// window matches the canvas.
	static std::string desktopTheme;
	GtkSettings* settings = gtk_settings_get_default();
	if (desktopTheme.empty()) {
		gchar* t = nullptr;
		g_object_get(settings, "gtk-theme-name", &t, nullptr);
		desktopTheme = t && *t ? t : "Adwaita";
		g_free(t);
	}
	const bool dark = prefs().dark;
	g_object_set(settings, "gtk-theme-name", desktopTheme.c_str(),
	             "gtk-application-prefer-dark-theme", dark ? TRUE : FALSE, nullptr);
	if (themeLooksDark() != dark) g_object_set(settings, "gtk-theme-name", "Adwaita", nullptr);
	applyStyle();
	for (CircuitWindow* w : circuitWindows()) w->themeChanged();
}

// ---- Files ---------------------------------------------------------------------

static bool hasLibrary(const std::string& dir) {
	const std::string f = dir + "/cl_gatedefs.xml";
	return access(f.c_str(), R_OK) == 0;
}

const std::string& resourcesDir() {
	static std::string dir = []() -> std::string {
		std::vector<std::string> candidates;
		if (const char* env = g_getenv("CEDARLOGIC_RESOURCES")) candidates.push_back(env);
		char exe[4096];
		const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
		if (n > 0) {
			exe[n] = 0;
			gchar* bin = g_path_get_dirname(exe);
			const std::string b = bin;
			g_free(bin);
			candidates.push_back(b + "/../share/CedarLogic");   // installed, or in the AppImage
			candidates.push_back(b + "/res");
			candidates.push_back(b + "/../../res");              // linux/build in the source tree
			candidates.push_back(b + "/../../../res");
		}
		candidates.push_back("/usr/share/CedarLogic");
		candidates.push_back("res");
		for (const std::string& c : candidates) {
			if (!hasLibrary(c)) continue;
			if (char* real = realpath(c.c_str(), nullptr)) {
				std::string s = real;
				free(real);
				return s;
			}
			return c;
		}
		return std::string();
	}();
	return dir;
}

// ---- Colours ---------------------------------------------------------------------

RGBA Palette::canvas() const {
	if (simView) return { 0.030, 0.038, 0.050, 1 };
	return dark ? RGBA{ 0.075, 0.082, 0.098, 1 } : RGBA{ 1, 1, 1, 1 };
}

RGBA Palette::grid(double intensity) const {
	if (simView) return { 0.25, 0.80, 1.0, intensity * 0.55 };
	return dark ? RGBA{ 1, 1, 1, intensity } : RGBA{ 0, 0, intensity, intensity };
}

RGBA accentColor(bool dark) {
	RGBA c{ 0, 0, 0, 1 };
	cl_accent_color(prefs().accent, dark, &c.r, &c.g, &c.b);
	return c;
}

// ---- Helpers ---------------------------------------------------------------------

std::string baseName(const std::string& path) {
	gchar* b = g_path_get_basename(path.c_str());
	std::string s = b;
	g_free(b);
	if (s.size() > 4 && g_ascii_strcasecmp(s.c_str() + s.size() - 4, ".cdl") == 0) s.resize(s.size() - 4);
	return s;
}

std::string format(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	gchar* s = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	std::string out = s ? s : "";
	g_free(s);
	return out;
}

void showMessage(GtkWindow* parent, GtkMessageType type, const std::string& title, const std::string& text) {
	GtkWidget* d = gtk_message_dialog_new(parent, (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
	                                      type, GTK_BUTTONS_OK, "%s", title.c_str());
	if (!text.empty()) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", text.c_str());
	gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
}

bool askYesNo(GtkWindow* parent, const std::string& title, const std::string& text) {
	GtkWidget* d = gtk_message_dialog_new(parent, (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
	                                      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", title.c_str());
	if (!text.empty()) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", text.c_str());
	gtk_dialog_add_buttons(GTK_DIALOG(d), "_No", GTK_RESPONSE_NO, "_Yes", GTK_RESPONSE_YES, nullptr);
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_YES);
	const int r = gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
	return r == GTK_RESPONSE_YES;
}

void reportException(const char* where, const char* what) {
	g_critical("CedarLogic: %s failed: %s", where, what);
	// Tell the user once a minute at most, in every window's status bar.
	static gint64 lastTold = 0;
	const gint64 now = g_get_monotonic_time();
	if (lastTold && now - lastTold < 60 * G_USEC_PER_SEC) return;
	lastTold = now;
	for (CircuitWindow* w : circuitWindows())
		w->note(format("Something went wrong (%s). CedarLogic kept going; saving a copy is a good idea.", where));
}

void openExternally(GtkWindow* parent, const std::string& uri) {
	GError* e = nullptr;
	if (!gtk_show_uri_on_window(parent, uri.c_str(), GDK_CURRENT_TIME, &e)) {
		showMessage(parent, GTK_MESSAGE_WARNING, "Couldn't open it", e ? e->message : uri);
		if (e) g_error_free(e);
	}
}
