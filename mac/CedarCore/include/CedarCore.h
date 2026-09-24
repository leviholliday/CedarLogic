// CedarCore -- what the native Mac app asks of the shared C++ engine.
//
// A plain C interface on purpose: Swift imports it directly, with no C++
// interop settings, and nothing of the engine's C++ types leaks into Swift.
// Every call is made on the main thread.

#ifndef CEDARCORE_H
#define CEDARCORE_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Load the gate library (cl_gatedefs.xml). Call once, before opening files.
// Returns false if it couldn't be read.
bool cl_library_load(const char *xmlPath);

typedef struct CLDocument CLDocument;

// Open a .cdl file (any version). On failure returns NULL and writes a reason
// into `error` (at most errorLen bytes, always terminated).
CLDocument *cl_document_open(const char *path, char *error, int errorLen);
// The same, from a file's contents already in memory.
CLDocument *cl_document_open_text(const char *text, long length, char *error, int errorLen);
void cl_document_close(CLDocument *doc);

int cl_document_page_count(const CLDocument *doc);
// The page's name as the user set it, or "" for an unnamed one. Valid until
// the document closes.
const char *cl_document_page_name(const CLDocument *doc, int page);

// World-space extent of everything on a page (y up). False for an empty page.
bool cl_document_page_bounds(const CLDocument *doc, int page,
                             double *left, double *bottom, double *right, double *top);

// Draw a page's gates and wires. `ctx` is in view points, y down (a flipped
// NSView); `backingScale` is points to pixels (2 on Retina). The camera: the
// world point at the view's top-left corner, and world units per point.
void cl_document_draw(CLDocument *doc, int page, CGContextRef ctx,
                      double backingScale, double originX, double originY,
                      double unitsPerPoint, bool dark);

#ifdef __cplusplus
}
#endif

#endif  // CEDARCORE_H
