// Headless check of dropping a saved part (My Parts): paste its text so it
// floats on the pointer, move it about, then click it down -- on an empty
// page and next to a copy of itself.
//   part_check <cl_gatedefs.xml> <part.txt> [<page.cdl>]
#include "CedarCore.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
	if (argc < 3 || !cl_library_load(argv[1])) return 2;
	std::ifstream f(argv[2]);
	std::stringstream ss; ss << f.rdbuf();
	const std::string text = ss.str();
	char err[256];
	CLDocument* doc = argc > 3 ? cl_document_open(argv[3], err, sizeof err) : cl_document_new();
	if (!doc) { printf("%s\n", err); return 1; }
	const double upp = 0.05;
	for (int round = 0; round < 3; round++) {
		const double x = 10 + round * 7, y = 10 - round * 3;
		const char* back = nullptr;
		if (!cl_edit_paste(doc, 0, text.c_str(), x, y, true, &back)) { printf("paste failed\n"); return 1; }
		if (!cl_edit_float_begin(doc, 0, x, y)) { printf("float failed\n"); return 1; }
		for (int i = 1; i <= 10; i++) cl_edit_hover(doc, 0, x + i * 1.3, y - i * 0.7, upp);
		const double dx = x + 13, dy = y - 7;
		cl_edit_press(doc, 0, dx, dy, 0, upp);
		cl_edit_release(doc, dx, dy);
		printf("dropped part %d\n", round + 1);
	}
	printf("ok\n");
	return 0;
}
