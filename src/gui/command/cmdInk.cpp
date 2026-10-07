#include "cmdInk.h"
#include "../GUICanvas.h"

#include <algorithm>

cmdInkAdd::cmdInkAdd(GUICanvas *page, std::vector<cl::InkStroke> s) :
		klsCommand(true, "Draw"), strokes(std::move(s)) {
	gCanvas = page;
	gCircuit = nullptr;
}

bool cmdInkAdd::Do() {
	if (gCanvas == nullptr) return false;
	for (const cl::InkStroke &s : strokes) gCanvas->ink.strokes.push_back(s);
	return true;
}

bool cmdInkAdd::Undo() {
	if (gCanvas == nullptr) return false;
	std::vector<cl::InkStroke> &list = gCanvas->ink.strokes;
	list.resize(list.size() >= strokes.size() ? list.size() - strokes.size() : 0);
	return true;
}

cmdInkErase::cmdInkErase(GUICanvas *page, std::vector<std::pair<size_t, cl::InkStroke>> e) :
		klsCommand(true, "Erase"), erased(std::move(e)) {
	gCanvas = page;
	gCircuit = nullptr;
	std::sort(erased.begin(), erased.end(),
	          [](const std::pair<size_t, cl::InkStroke> &a, const std::pair<size_t, cl::InkStroke> &b) {
		          return a.first < b.first;
	          });
}

bool cmdInkErase::Do() {
	if (gCanvas == nullptr) return false;
	std::vector<cl::InkStroke> &list = gCanvas->ink.strokes;
	for (auto it = erased.rbegin(); it != erased.rend(); ++it)
		if (it->first < list.size()) list.erase(list.begin() + (std::ptrdiff_t)it->first);
	return true;
}

bool cmdInkErase::Undo() {
	if (gCanvas == nullptr) return false;
	std::vector<cl::InkStroke> &list = gCanvas->ink.strokes;
	for (const auto &e : erased)
		list.insert(list.begin() + (std::ptrdiff_t)std::min(e.first, list.size()), e.second);
	return true;
}

cmdInkClear::cmdInkClear(GUICanvas *page) : klsCommand(true, "Clear Drawing") {
	gCanvas = page;
	gCircuit = nullptr;
}

bool cmdInkClear::Do() {
	if (gCanvas == nullptr) return false;
	cleared.swap(gCanvas->ink.strokes);
	gCanvas->ink.strokes.clear();
	return true;
}

bool cmdInkClear::Undo() {
	if (gCanvas == nullptr) return false;
	gCanvas->ink.strokes = cleared;
	cleared.clear();
	return true;
}
