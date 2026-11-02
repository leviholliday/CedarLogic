// The side panel's gates, as the Mac app's (CLSidePanel): a category menu
// over tiles on the canvas's own colour, sized by Settings > Appearance >
// Gate size. Drag a tile out and the gate is on the canvas at once,
// following the pointer until you let go (it slides out from under the panel
// as it's pulled across the edge); click one and it follows the pointer
// until you click. A search finds any gate by name. My Parts are the last
// category; right-click one to rename or delete it.

#ifndef CL_LINUX_PALETTE_H
#define CL_LINUX_PALETTE_H

#include "Anim.h"
#include "App.h"
#include <map>
#include <string>
#include <vector>

class CircuitWindow;
class Canvas;
class CategoryButton;
class TileGrid;

class GatePalette {
public:
	explicit GatePalette(CircuitWindow* window);
	~GatePalette();
	GtkWidget* widget() const { return root; }
	void showCategory(int index);
	void focusSearch();
	void themeChanged();
	void partsChanged();   // My Parts changed

	// For the category button and the tiles.
	struct Gate { std::string name, caption; };
	struct Category { std::string title; std::vector<Gate> gates; };
	std::string categoryTitle() const;
	void chooseCategory(GtkWidget* from, GdkEvent* e);
	const std::vector<Gate>& shownGates() const { return shown; }
	bool showingParts() const;
	CircuitWindow* window() const { return win; }

private:
	CircuitWindow* win;
	GtkWidget* root;
	GtkWidget* search;
	CategoryButton* picker = nullptr;
	TileGrid* tiles = nullptr;
	std::vector<Category> categories;
	int category = 0;
	std::vector<Gate> shown;

	void loadCategories();
	void fill();
	static void searchChangedCb(GtkSearchEntry*, gpointer);
};

#endif  // CL_LINUX_PALETTE_H
