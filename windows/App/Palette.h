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

class CategoryButton;

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
	// My Parts gained or lost one.
	void partsChanged();

	// For the category button.
	std::string categoryTitle() const;
	void chooseCategory(POINT screen);

private:
	struct Gate { std::string name, caption; };
	struct Category { std::string title; std::vector<Gate> gates; };

	CircuitWindow* win;
	HWND host = nullptr, search = nullptr, tiles = nullptr;
	CategoryButton* picker = nullptr;
	MiniMap* map = nullptr;
	WindowSurface surface;
	HBRUSH fieldBrush = nullptr;
	std::vector<Category> categories;
	int category = 0;
	std::vector<Gate> shown;
	float scrollY = 0;            // points
	int hover = -1, pressed = -1;
	bool dragging = false, scrollDrag = false, scrollHot = false;
	float scrollGrab = 0;
	POINT pressAt{};

	void fill();
	void loadCategories();
	void layout();
	RECT searchFrame() const;     // the search field's rounded box (host pixels)
	void layoutTiles(int& columns, float& tileW, float& tileH) const;
	float contentHeight() const;
	float viewHeight() const;
	void clampScroll();
	D2D1_RECT_F scrollThumb() const;
	int tileAt(float x, float y) const;
	void paintTiles();
	void paintHost(HDC dc);
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
