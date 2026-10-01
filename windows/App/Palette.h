// The gate palette down the window's side -- a search box, the library's
// categories, each gate drawn as a tile (the engine draws it) -- and the
// minimap under it. Click a tile and the gate follows the pointer onto the
// canvas until a click drops it; or drag it there.

#ifndef CL_WINDOWS_PALETTE_H
#define CL_WINDOWS_PALETTE_H

#include "App.h"
#include <string>
#include <vector>

class CircuitWindow;
class MiniMap;

class GatePalette {
public:
	GatePalette(CircuitWindow* window, HWND parent);
	~GatePalette();
	HWND widget() const { return host; }
	MiniMap* miniMap() const { return map; }
	void showCategory(int index);
	void focusSearch();
	void themeChanged();
	void dpiChanged();

private:
	struct Gate { std::string name, caption; };
	struct Category { std::string title; std::vector<Gate> gates; };

	CircuitWindow* win;
	HWND host = nullptr, search = nullptr, combo = nullptr, tiles = nullptr;
	MiniMap* map = nullptr;
	WindowSurface surface;
	std::vector<Category> categories;
	std::vector<Gate> shown;
	int scrollY = 0;              // pixels
	int hover = -1, pressed = -1;
	bool dragging = false;
	POINT pressAt{};

	void fill();
	void layout();
	void layoutTiles(int& columns, int& tileW, int& tileH, int& gap, int& pad) const;
	int contentHeight() const;
	void updateScroll();
	int tileAt(int x, int y) const;
	void paintTiles();
	void drop();

	LRESULT hostMessage(UINT msg, WPARAM wp, LPARAM lp);
	LRESULT tilesMessage(UINT msg, WPARAM wp, LPARAM lp);
	static LRESULT CALLBACK hostProc(HWND, UINT, WPARAM, LPARAM);
	static LRESULT CALLBACK tilesProc(HWND, UINT, WPARAM, LPARAM);
	friend void registerPaletteClasses();
};

// The whole page small, with the part the canvas shows outlined. Click or
// drag to go there.
class MiniMap {
public:
	MiniMap(CircuitWindow* window, HWND parent);
	~MiniMap();
	HWND widget() const { return hwnd; }
	void queueDraw();
	void themeChanged() { cacheKey.clear(); queueDraw(); }

private:
	CircuitWindow* win;
	HWND hwnd = nullptr;
	WindowSurface surface;
	ID2D1BitmapRenderTarget* cache = nullptr;
	std::string cacheKey;
	bool dragging = false;

	bool fit(double w, double h, double& left, double& bottom, double& right, double& top, double& upp) const;
	void paint();
	void goTo(double vx, double vy);
	LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
	static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
	friend void registerPaletteClasses();
};

void registerPaletteClasses();

#endif  // CL_WINDOWS_PALETTE_H
