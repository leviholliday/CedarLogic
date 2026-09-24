// Print a page's truth table, as the app would show it.
//   tt_check <cl_gatedefs.xml> <file.cdl> <page>
#include "CedarCore.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
	if (argc < 4 || !cl_library_load(argv[1])) return 2;
	char err[256];
	CLDocument* doc = cl_document_open(argv[2], err, sizeof err);
	if (!doc) { printf("%s\n", err); return 1; }
	CLTruthTable* tt = cl_truth_table(doc, atoi(argv[3]), err, sizeof err);
	if (!tt) { printf("no table: %s\n", err); return 1; }
	for (int c = 0; c < cl_tt_columns(tt); c++) printf("%s%-4s", c == cl_tt_inputs(tt) ? "| " : "", cl_tt_name(tt, c));
	printf("\n");
	for (int r = 0; r < cl_tt_rows(tt); r++) {
		for (int c = 0; c < cl_tt_columns(tt); c++) printf("%s%-4c", c == cl_tt_inputs(tt) ? "| " : "", cl_tt_cell(tt, r, c));
		printf("\n");
	}
	printf("sequential=%d unsettled=%d\n", cl_tt_sequential(tt), cl_tt_unsettled(tt));
	cl_tt_free(tt);
	cl_document_close(doc);
}
