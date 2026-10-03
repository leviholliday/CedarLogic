// The gate palette down the window's side: the library's categories, each
// gate drawn as a tile (the engine draws it), and a search box. Click a tile
// and the gate follows the pointer onto the canvas until a click drops it;
// or drag it there.

#ifndef CL_LINUX_PALETTE_H
#define CL_LINUX_PALETTE_H

#include "App.h"
#include <string>
#include <vector>

class CircuitWindow;

class GatePalette {
public:
	explicit GatePalette(CircuitWindow* window);
	~GatePalette();
	GtkWidget* widget() const { return root; }
	void showCategory(int index);
	void focusSearch();
	void themeChanged();
	void partsChanged();   // My Parts changed

private:
	struct Gate { std::string name, caption; };
	struct Category { std::string title; std::vector<Gate> gates; };

	CircuitWindow* win;
	GtkWidget* root;
	GtkWidget* search;
	GtkWidget* combo;
	GtkWidget* flow;
	std::vector<Category> categories;
	std::vector<std::string*> tileNames;   // owned; freed on rebuild

	void fill();
	void loadCategories();
	GtkWidget* tile(const Gate& g);
	void clearTiles();

	static void comboChangedCb(GtkComboBox*, gpointer);
	static void searchChangedCb(GtkSearchEntry*, gpointer);
	static void activatedCb(GtkFlowBox*, GtkFlowBoxChild*, gpointer);
	static gboolean tilePressCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean drawTileCb(GtkWidget*, cairo_t*, gpointer);
	static void dragDataGetCb(GtkWidget*, GdkDragContext*, GtkSelectionData*, guint, guint, gpointer);
	static void dragBeginCb(GtkWidget*, GdkDragContext*, gpointer);
};

#endif  // CL_LINUX_PALETTE_H
