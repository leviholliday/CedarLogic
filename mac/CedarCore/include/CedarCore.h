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
// Whether opening runs the simulation until it settles (on by default). The
// save round-trip check turns it off: settling moves counters and clocks on,
// which a saved file rightly records.
void cl_set_settle_on_open(bool settle);
// A new, empty circuit with one page.
CLDocument *cl_document_new(void);
// The circuit as .cdl text (the current v3 format), as the wx app saves it.
// Valid until the next call.
const char *cl_document_save_text(CLDocument *doc);

// ---- Pages ---------------------------------------------------------------
int cl_document_add_page(CLDocument *doc);               // returns its index
void cl_document_rename_page(CLDocument *doc, int page, const char *name);
// Remove a page and everything on it. Clears the undo history (its steps
// could refer to the page).
void cl_document_delete_page(CLDocument *doc, int page);

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

// A whole page scaled to fit a width x height area (points, y down) with a
// margin, centered -- for export and printing. PRINT is black line drawings
// on white. False for an empty page.
enum { CL_STYLE_LIGHT = 0, CL_STYLE_DARK = 1, CL_STYLE_PRINT = 2 };
bool cl_document_draw_fitted(CLDocument *doc, int page, CGContextRef ctx,
                             double width, double height, double margin,
                             double backingScale, int style);

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

// ---- Editing -------------------------------------------------------------
// Pointer gestures, in world coordinates. `unitsPerPoint` is the current zoom,
// so "close enough to a wire" and "far enough to be a drag" stay the same on
// screen at any zoom. Every change goes on the document's undo stack.

enum { CL_MOD_SHIFT = 1, CL_MOD_COMMAND = 2, CL_MOD_OPTION = 4 };
enum { CL_PRESS_NOTHING = 0, CL_PRESS_PART = 1, CL_PRESS_BOX = 2 };

// A press: selects what's under it (shift adds or removes). Returns
// CL_PRESS_BOX when it landed on empty canvas (a drag draws a selection box).
int cl_edit_press(CLDocument *doc, int page, double x, double y, int modifiers, double unitsPerPoint);
void cl_edit_drag(CLDocument *doc, double x, double y);
// A release. A press and release in place on a switch or keypad operates it.
void cl_edit_release(CLDocument *doc, double x, double y);
void cl_edit_cancel(CLDocument *doc);
// The selection box being drawn, if any (world coordinates).
bool cl_edit_box(const CLDocument *doc, double *left, double *bottom, double *right, double *top);

void cl_edit_select_all(CLDocument *doc, int page);
void cl_edit_select_none(CLDocument *doc, int page);
int cl_edit_selected_gate_count(const CLDocument *doc, int page);
int cl_edit_selected_wire_count(const CLDocument *doc, int page);

void cl_edit_delete(CLDocument *doc, int page);
// Turn the selected gates a quarter turn clockwise (gates with wires attached
// stay put, as in the wx app).
void cl_edit_rotate(CLDocument *doc, int page);
// Move the selection by whole grid steps.
void cl_edit_nudge(CLDocument *doc, int page, double dx, double dy);
// Place a new gate from the library, centred on a world point; it becomes
// the selection.
bool cl_edit_add_gate(CLDocument *doc, int page, const char *libGateName, double x, double y);

// The selection as clipboard text (the wx app's copy format), or "" when
// nothing is selected. Valid until the next call.
const char *cl_edit_copy(CLDocument *doc, int page);
// Paste clipboard text with its top-left gate at a world point. Returns false
// when the text isn't a CedarLogic block. `shift` stops junction ids counting
// up. When it returns true, *clipboardOut is text to put back on the
// clipboard (or "" for none), valid until the next call.
bool cl_edit_paste(CLDocument *doc, int page, const char *text, double x, double y, bool shift,
                   const char **clipboardOut);

// How many steps are on the undo stack (the app mirrors each into macOS's
// undo manager, so the window knows it's edited and autosaves).
int cl_edit_undo_count(const CLDocument *doc);
bool cl_edit_undo(CLDocument *doc);
bool cl_edit_redo(CLDocument *doc);
bool cl_edit_can_undo(const CLDocument *doc);
bool cl_edit_can_redo(const CLDocument *doc);
// "Move", "Delete Selection"... for the Edit menu. Valid until the next call.
const char *cl_edit_undo_name(const CLDocument *doc);
const char *cl_edit_redo_name(const CLDocument *doc);
// Whether the circuit changed since it was opened or last saved.
bool cl_document_is_edited(const CLDocument *doc);

// ---- Wires -------------------------------------------------------------------
// Pressing on a pin starts a connection: drag to another pin or a wire and
// let go, or click the pin, then click the target (Escape or a click on
// nothing cancels). Pressing on a wire drags that segment.

// Update what's under the pointer as it moves (no button down), for the pin
// highlight, and move a click-started connection's line. Returns true when
// the canvas should redraw.
bool cl_edit_hover(CLDocument *doc, int page, double x, double y, double unitsPerPoint);
// A connection is following the pointer (after a click on a pin).
bool cl_edit_is_connecting(const CLDocument *doc);

// Right-click: what's under the point. A pin with a wire on it, a wire, a
// gate, or nothing; the wire or gate becomes the selection (unless it already
// was part of it).
enum { CL_CONTEXT_NOTHING = 0, CL_CONTEXT_PIN = 1, CL_CONTEXT_WIRE = 2, CL_CONTEXT_GATE = 3 };
int cl_edit_context(CLDocument *doc, int page, double x, double y, double unitsPerPoint);
// Take the wire off the pin the last cl_edit_context found (the whole wire,
// when that pin was one of its only two ends).
void cl_edit_disconnect_pin(CLDocument *doc, int page, double x, double y, double unitsPerPoint);

// Straighten (S): the selected wires, or the wires of the selected gates.
void cl_edit_straighten(CLDocument *doc, int page);

// Tidy Up (Shift-S): applies at once as a preview over a ghost of the old
// layout; end it by keeping or reverting. Any other edit keeps it.
// mode 0 keeps the layout's shape, 1 rearranges by signal flow.
bool cl_edit_tidy_begin(CLDocument *doc, int page, int mode);
void cl_edit_tidy_end(CLDocument *doc, bool keep);
bool cl_edit_tidy_active(const CLDocument *doc);
int cl_edit_tidy_mode(const CLDocument *doc);

// Draw what goes over the circuit: the Tidy ghost, a connection being made,
// and the pin under the pointer. Same camera as cl_document_draw; the accent
// is 0..1 RGB.
void cl_edit_draw_overlay(CLDocument *doc, int page, CGContextRef ctx, double backingScale,
                          double originX, double originY, double unitsPerPoint,
                          double accentR, double accentG, double accentB);

// ---- Gate settings (the inspector) -----------------------------------------
// The one selected gate on a page, or -1 when it isn't exactly one.
long cl_edit_single_gate(const CLDocument *doc, int page);
// What the library calls it ("AND 2-input") and its settings, in the order the
// library lists them. Strings are valid until the next call.
const char *cl_gate_caption(const CLDocument *doc, long gate);
int cl_gate_setting_count(const CLDocument *doc, long gate);
typedef struct {
	const char *label;   // shown to the user
	const char *name;    // the parameter
	const char *type;    // STRING, INT, BOOL, FLOAT, MULTI_STRING, FILE_IN, FILE_OUT
	const char *value;   // current value
	double min, max;     // for numbers
} CLGateSetting;
bool cl_gate_setting(const CLDocument *doc, long gate, int index, CLGateSetting *out);
// Change one setting (undoable).
bool cl_gate_set_setting(CLDocument *doc, long gate, const char *name, const char *value);

// ---- The gate library (the palette) ----------------------------------------
int cl_library_category_count(void);
const char *cl_library_category(int index);
int cl_library_gate_count(int category);
const char *cl_library_gate(int category, int index);
const char *cl_library_gate_caption(const char *libGateName);
// Draw one gate type fitted into a rectangle of `width` x `height` points
// (y down), for palette tiles.
void cl_library_draw_gate(const char *libGateName, CGContextRef ctx, double width, double height,
                          double backingScale, bool dark);

// ---- Oscilloscope ------------------------------------------------------------
// One sample per simulation step for each signal a TO label names. Values
// are the engine's states: 0 low, 1 high, 2 high-Z, 3 conflict, 4 unknown,
// 255 no data yet (the signal didn't exist then).
int cl_scope_signal_count(const CLDocument *doc);
const char *cl_scope_signal(const CLDocument *doc, int index);
long long cl_scope_length(const CLDocument *doc);        // samples held (same for every signal)
long long cl_scope_first_step(const CLDocument *doc);    // step number of sample 0
// Copy `count` samples starting at index `from` (from 0 to length-1); indices
// outside the recording read as 255. Returns how many were written.
int cl_scope_samples(const CLDocument *doc, int signal, long long from, int count, unsigned char *out);
void cl_scope_clear(CLDocument *doc);

// ---- Truth tables ------------------------------------------------------------
// Every combination of a page's switches (or the selected ones), with its
// lights read once the circuit settles -- as the wx app builds it. Cells are
// '0', '1', or 'X' unknown, 'Z' floating, '!' conflict, '-' not connected.
typedef struct CLTruthTable CLTruthTable;
CLTruthTable *cl_truth_table(CLDocument *doc, int page, char *error, int errorLen);
void cl_tt_free(CLTruthTable *tt);
int cl_tt_inputs(const CLTruthTable *tt);    // the first columns are inputs
int cl_tt_columns(const CLTruthTable *tt);
int cl_tt_rows(const CLTruthTable *tt);
const char *cl_tt_name(const CLTruthTable *tt, int column);
char cl_tt_cell(const CLTruthTable *tt, int row, int column);
bool cl_tt_sequential(const CLTruthTable *tt);   // clocks or flip-flops on the page
int cl_tt_unsettled(const CLTruthTable *tt);     // rows that never stopped changing

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
