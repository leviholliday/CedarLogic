// The find bar (Ctrl+F) over the canvas, as the Mac app's FindBar: labels,
// TO/FROM names and parts on every page. Enter goes to the next one (Shift
// the one before), the arrows either way, Escape closes it. With a label or a
// TO/FROM selected, it opens looking for that name, so a TO's FROMs are one
// Enter away.

#ifndef CL_WINDOWS_FINDBAR_H
#define CL_WINDOWS_FINDBAR_H

#include "Drawn.h"
#include <string>
#include <vector>

class CircuitWindow;

class FindBar : public Drawn {
public:
	FindBar(CircuitWindow* window, HWND parent);
	static float barWidth() { return 600; }
	static float barHeight() { return 42; }

	void open(const std::string& query);
	void close();
	bool isOpen() const { return IsWindowVisible(hwnd) != FALSE; }
	// Look again (the circuit changed); jump to the first hit when asked.
	void run(bool jump);
	void step(int delta);
	void dpiChanged();

protected:
	void paint(ID2D1RenderTarget* rt, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick) override;
	LRESULT message(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;

private:
	struct Hit { int page; long gate; double x, y; std::string text, kind; };
	CircuitWindow* win;
	HWND edit = nullptr;
	std::vector<Hit> hits;
	int total = 0, index = 0;
	int hot = -1;   // 0 up, 1 down, 2 done
	D2D1_RECT_F up{}, down{}, done{};

	void show(int i);
	void layoutEdit();
	static LRESULT CALLBACK editProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
};

#endif  // CL_WINDOWS_FINDBAR_H
