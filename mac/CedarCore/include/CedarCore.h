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

// ---- Simulation --------------------------------------------------------

// Advance the simulation by the wall time that has passed (called every frame
// while the window is up). Steps at the document's step length, at most a
// frame's worth of catch-up. Returns CL_TICK_* flags.
enum { CL_TICK_CHANGED = 1, CL_TICK_PAUSED = 2 };
int cl_document_tick(CLDocument *doc, double elapsedMs);
void cl_document_set_running(CLDocument *doc, bool running);
bool cl_document_is_running(const CLDocument *doc);
// One step, paused or not.
void cl_document_step(CLDocument *doc);
// Milliseconds of simulated time per step (1..500; 25 by default).
void cl_document_set_step_ms(CLDocument *doc, int ms);
int cl_document_step_ms(const CLDocument *doc);

// A click at a world point: flips a switch, presses a keypad key... Returns
// true when a part took the click.
bool cl_document_click(CLDocument *doc, int page, double x, double y);

// ---- Load notes ----------------------------------------------------------

// What loading had to say (an older format was converted, a gate type the
// library doesn't have...). Valid until the document closes.
int cl_document_notice_count(const CLDocument *doc);
const char *cl_document_notice(const CLDocument *doc, int index);
bool cl_document_notice_is_warning(const CLDocument *doc, int index);

#ifdef __cplusplus
}
#endif

#endif  // CEDARCORE_H
