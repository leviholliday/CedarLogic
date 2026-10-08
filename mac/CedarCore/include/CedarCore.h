// CedarCore -- what the native Mac app asks of the shared C++ engine.
//
// A plain C interface on purpose: Swift imports it directly, with no C++
// interop settings, and nothing of the engine's C++ types leaks into Swift.
// Every call is made on the main thread.

#ifndef CEDARCORE_H
#define CEDARCORE_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>

// Sync (SYNC.md): its own header, so the Linux and Windows apps can use it too.
#include "CedarSync.h"
#include "CedarClassroom.h"

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
// A copy in an older format, for older CedarLogic (wx: Export as V2 / V1.x):
// format 2 is the v2 XML, 1 the v1.x compatible one. Returns 0 when written,
// 1 when written but with something the format can't hold (the reason in
// `error`), -1 when it couldn't be written (the reason in `error`).
int cl_document_export_legacy(CLDocument *doc, const char *path, int format, char *error, int errorLen);

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
// Step Clock: clocks with "Only on Step Clock" on (the MANUAL setting) hold
// still at 0 until told. How many are on a page:
int cl_document_manual_clock_count(const CLDocument *doc, int page);
// Each of them makes one full cycle: up to 1, the circuit settles, down to 0,
// it settles again (at most 1000 steps each, for a circuit that never does).
// Running or paused alike; running clocks carry on through those steps.
// False when the page has no manual clock.
bool cl_document_clock_step(CLDocument *doc, int page);

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
// The wire under the pointer, lit up whole (cl_edit_hover does this too; on
// its own for Simulation View). True if what's lit changed.
bool cl_edit_hover_wire(CLDocument *doc, int page, double x, double y, double unitsPerPoint);
bool cl_edit_hover_clear(CLDocument *doc);   // the pointer left: nothing lit
// What the lit wire carries, highest bit first: 0, 1, Z (floating), ! (a
// conflict) or X. Returns the number of bits, 0 if no wire is lit here.
int cl_edit_hover_wire_state(const CLDocument *doc, int page, char *out, int len);
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

// Build from Formula: gates (library name, place, and label text for a
// Label) and the wires between their pins, by index into `gates`, made as
// one undo step with the new wires routed together. Returns how many gates
// were made (left selected), or -1.
// `angle` turns a gate before it's wired (0, 90, 180 or 270; 0 if left out).
typedef struct { const char *gate; double x, y; const char *label; double angle; } CLBuildGate;
typedef struct { int from; const char *fromPin; int to; const char *toPin; } CLBuildWire;
// Find: labels, TO/FROM names and part types on every page, best first.
// Fills up to `max` results and returns how many there are in all. The
// strings last until the next cl_find or cl_gate_find_name.
typedef struct { int page; long gate; double x, y; const char *text; const char *kind; } CLFindResult;
int cl_find(CLDocument *doc, const char *query, CLFindResult *out, int max);
bool cl_edit_select_gate(CLDocument *doc, int page, long gate);   // just that one
// A label's text or a TO/FROM's name, to search for; "" for other parts.
const char *cl_gate_find_name(const CLDocument *doc, long gate);
int cl_edit_build(CLDocument *doc, int page, const CLBuildGate *gates, int gateCount,
                  const CLBuildWire *wires, int wireCount, const char *undoName);

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

// ---- Parts locked in place -------------------------------------------------
// A teaching aid, not security: a teacher locks the starter parts (switches,
// lights) and shares the circuit; students wire between them. Locked parts
// aren't moved (a drag, a box move or the arrow keys take only the unlocked
// ones along), rotated, deleted or cut, and Tidy Up leaves them put; their
// settings can still change, wires to them can be made and removed, and
// switches still flip. Saved as the gate's (gparam "LOCKED" "true"), which
// older versions keep without acting on it. A pasted copy comes unlocked.
bool cl_gate_is_locked(const CLDocument *doc, long gate);
// How many selected parts on a page are locked (`locked` true) or not.
int cl_edit_selected_locked_count(const CLDocument *doc, int page, bool locked);
// Lock (or unlock) the selected parts, as one undo step. Returns how many changed.
int cl_edit_lock_selection(CLDocument *doc, int page, bool lock);
// Locked parts on every page, and unlocking them all as one undo step
// (returns how many).
int cl_document_locked_count(const CLDocument *doc);
int cl_edit_unlock_all(CLDocument *doc, int page);
// Leave the page's locked parts (and the wires on them) out of the selection,
// before a Cut. Returns how many parts.
int cl_edit_deselect_locked(CLDocument *doc, int page);
// The last drag, nudge, delete or rotate left locked parts where they were,
// for a note saying why. Cleared by the next press.
bool cl_edit_locked_held(const CLDocument *doc);

// ---- Gate settings (the inspector) -----------------------------------------
// The one selected gate on a page, or -1 when it isn't exactly one.
long cl_edit_single_gate(const CLDocument *doc, int page);
// What the library calls it ("AND 2-input") and its settings, in the order the
// library lists them. Strings are valid until the next call.
const char *cl_gate_caption(const CLDocument *doc, long gate);
// The gate's library name ("AA_AND2"), for its picture.
const char *cl_gate_library_name(const CLDocument *doc, long gate);
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
// A table given outright (the app's copy, with any names the student
// changed): names (inputs, then outputs) and rows of cells as above.
CLTruthTable *cl_tt_new(int inputs, bool sequential, int unsettled);
void cl_tt_add_name(CLTruthTable *tt, const char *name);
void cl_tt_add_row(CLTruthTable *tt, const char *cells);

// ---- Check my circuit ----------------------------------------------------------
// Compares a circuit's truth table with what an assignment asks for, matching
// switches and lights by name (case and spaces don't matter). Clocked circuits
// (a count, a state table, a timing table) are below.
//
// `expected` is what the app's formula reader made, one tab-separated line
// each: "in<TAB>A<TAB>B<TAB>Cin", then "out<TAB>S<TAB>01101001" per output,
// a value per minterm ('0', '1', or '-' for don't care; the first input is the
// most significant bit). cl_check_table reads a truth table the student
// pasted or typed instead (a header of names, a | between inputs and outputs
// if it likes, then rows of 0, 1 and X or - for don't care).
//
// `names` matches names by hand, a line each: "Cin<TAB>C" says the asked-for
// Cin is the circuit's column C. Names left out are matched automatically.
typedef struct CLCheck CLCheck;
CLCheck *cl_check_expected(const CLTruthTable *tt, const char *expected, const char *names);
CLCheck *cl_check_table(const CLTruthTable *tt, const char *table, const char *names);
void cl_check_free(CLCheck *c);
int cl_check_verdict(const CLCheck *c);          // 0 matches, 1 wrong rows, 2 couldn't check (all of it)
const char *cl_check_summary(const CLCheck *c);  // one line, as the app shows it
// Notes: kind 0 for your information, 1 a warning, 2 a problem to fix.
int cl_check_note_count(const CLCheck *c);
const char *cl_check_note(const CLCheck *c, int index);
int cl_check_note_kind(const CLCheck *c, int index);
// The asked-for outputs: which column of the circuit's table each is (-1 when
// none could be matched) and on how many rows it's wrong.
int cl_check_outputs(const CLCheck *c);
const char *cl_check_output_name(const CLCheck *c, int output);
int cl_check_output_column(const CLCheck *c, int output);
int cl_check_output_wrong(const CLCheck *c, int output);
// Row by row (the circuit table's rows): what was asked for ('0', '1', '-'
// don't care, ' ' unknown) and whether the circuit gave it ('=' yes, 'x' no,
// '-' not checked: a don't care, or the output wasn't matched).
char cl_check_expected_cell(const CLCheck *c, int row, int output);
char cl_check_result(const CLCheck *c, int row, int output);
bool cl_check_row_wrong(const CLCheck *c, int row);
// Every asked-for name, inputs first: the column of the circuit's table it
// was matched with (-1 none) and whether that was by hand.
int cl_check_name_count(const CLCheck *c);
const char *cl_check_name(const CLCheck *c, int index);
bool cl_check_name_is_input(const CLCheck *c, int index);
int cl_check_name_column(const CLCheck *c, int index);
bool cl_check_name_by_hand(const CLCheck *c, int index);

// ---- Check my circuit: clocked circuits (docs/CHECK-SEQUENTIAL.md) ---------
// What kind of answer key a text is, from the text alone; *options (may be
// NULL) is set when it has start:, reset:, set: or clock: lines. FORMULA and
// TABLE without options go to today's check; everything else to
// cl_check_clocked.
enum { CL_KEY_EMPTY = 0, CL_KEY_FORMULA = 1, CL_KEY_TABLE = 2, CL_KEY_COUNT = 3,
       CL_KEY_STATES = 4, CL_KEY_TIMING = 5 };
int cl_check_key_kind(const char *text, bool *options);
// Checks a page against a count, a state table or a timing table, clock pulse
// by clock pulse, on a copy of the circuit (the document isn't changed, its
// clocks keep running). Reports the errors of a key it can't read too, so
// it takes any text. `names` as for cl_check_expected; a name's column is a
// port (below). Free with cl_check_free. The verdict, summary, notes and
// names calls above work on it; cl_check_outputs is 0.
CLCheck *cl_check_clocked(CLDocument *doc, int page, const char *key, const char *names);
// The same in two halves, so it can run off the main thread: _prepare (where
// the document is used: it saves the circuit, without marking the document
// saved, and only when the key can be run) copies all it needs; _run, on any
// thread, opens copies of its own and touches no open document. _cancel, from
// any thread while it runs, makes it stop soon (its result is then
// meaningless). Free the job with cl_check_job_free once _run has returned.
typedef struct CLCheckJob CLCheckJob;
CLCheckJob *cl_check_clocked_prepare(CLDocument *doc, int page, const char *key, const char *names);
CLCheck *cl_check_clocked_run(CLCheckJob *job);
void cl_check_job_cancel(CLCheckJob *job);
void cl_check_job_free(CLCheckJob *job);
// Whether a page cl_truth_table can't make a table for can still be checked
// clock pulse by clock pulse: no switches or lights selected, a light, no
// switches or more than 8, and a clock part, a flip-flop or the like, or a
// switch named CLK or Clock.
bool cl_check_clocked_page(CLDocument *doc, int page);
int cl_check_kind(const CLCheck *c);            // CL_KEY_*
const char *cl_check_error(const CLCheck *c);   // "" or the error's code: "no_clock"...
// The page's switches, then its lights, as the check named them.
int cl_check_port_count(const CLCheck *c);
const char *cl_check_port_name(const CLCheck *c, int port);
bool cl_check_port_is_input(const CLCheck *c, int port);
// The step strip's columns.
enum { CL_SIGNAL_INPUT = 0, CL_SIGNAL_STATE = 1, CL_SIGNAL_NEXT = 2, CL_SIGNAL_OUTPUT = 3 };
int cl_check_signal_count(const CLCheck *c);
const char *cl_check_signal_name(const CLCheck *c, int signal);
int cl_check_signal_role(const CLCheck *c, int signal);
int cl_check_signal_port(const CLCheck *c, int signal);
// The steps: the start (or a state set with switches) and each clock pulse.
enum { CL_STEP_START = 0, CL_STEP_SET = 1, CL_STEP_PULSE = 2 };
int cl_check_step_count(const CLCheck *c);
int cl_check_first_wrong(const CLCheck *c);     // a step, or -1
int cl_check_step_kind(const CLCheck *c, int step);
int cl_check_step_pulse(const CLCheck *c, int step);
int cl_check_step_row(const CLCheck *c, int step);  // the key's row, 0 for none
bool cl_check_step_wrong(const CLCheck *c, int step);
// A step's state (read before it), inputs, what was expected ('0', '1', '-')
// and what the lights showed ('0', '1', 'X', 'Z', '!', '-', '~' still
// changing), one character per signal of that role. Valid until the check is
// freed.
enum { CL_STEP_STATE_BITS = 0, CL_STEP_INPUTS = 1, CL_STEP_EXPECTED = 2, CL_STEP_GOT = 3 };
const char *cl_check_step_text(const CLCheck *c, int step, int what);

// ---- Load notes ----------------------------------------------------------

// What loading had to say (an older format was converted, a gate type the
// library doesn't have...). Valid until the document closes.
int cl_document_notice_count(const CLDocument *doc);
const char *cl_document_notice(const CLDocument *doc, int index);
bool cl_document_notice_is_warning(const CLDocument *doc, int index);

// ---- The CedarLogic interface (the wx app's look and extras) ----------------

// How a page is drawn: the screen style with the Appearance settings, the
// Simulation View palette, or a themed thumbnail (the minimap).
typedef struct {
	bool dark;
	int accent;          // 0..5: Blue, Purple, Pink, Orange, Green, Graphite
	double wireScale;    // 0.7 thin, 1 normal, 1.6 thick
	bool simView;
	bool thumbnail;      // topology only, hairline, themed by `dark`
	bool showSelection;  // the canvas being edited: selection halos and lock badges
	double selectionFade;  // 0..1: a new selection's halo fades in
	int ink;             // the drawing: CL_INK_FOLLOW, _NEVER, _ALWAYS, _ALWAYS_PRINT (below)
} CLDrawOptions;
void cl_document_draw_ex(CLDocument *doc, int page, CGContextRef ctx, double backingScale,
                         double originX, double originY, double unitsPerPoint,
                         const CLDrawOptions *options);
// The accent colour for an index, light or dark variant (0..1 RGB).
void cl_accent_color(int index, bool dark, double *r, double *g, double *b);
// The dots drawn on wires: at every bend (or only where wires join), and
// their radius in grid units. Shared by every document.
void cl_set_wire_dots(bool atBends, double radius);
// On the dark canvas, low wires (and their dots) in this colour instead of
// the engine's grey, which sits too close to the grid. Off: the grey.
void cl_set_low_wire_color(bool on, double r, double g, double b);

// Simulation View: dashes marching away from each wire's driver along wires
// that are on, and a bloom around lit lights. `phasePoints` grows with time.
void cl_simview_draw_flow(CLDocument *doc, int page, CGContextRef ctx, double backingScale,
                          double originX, double originY, double unitsPerPoint, double phasePoints,
                          double wireScale);
// The switches and lights on a page, top to bottom, left to right, for the
// control bar's chips. Returns how many there are (fills up to `max`).
typedef struct { bool isInput; bool lit; } CLSimChip;
int cl_simview_chips(CLDocument *doc, int page, CLSimChip *out, int max);

// Simulation View for a classroom. Projector mode: much thicker wires and
// outlines, bigger labels and lights, brighter colours (pass the flow's
// wireScale times CL_PROJECTOR_WIRE_SCALE, to match). Predict covers every
// light: lights and displays draw dark and every wire in its "off" colour
// (leave the flow out too), and cl_simview_draw_predict draws the covers.
#define CL_PROJECTOR_WIRE_SCALE 3.5
typedef struct {
	int accent;
	double wireScale;
	bool projector;
	bool predict;
	int ink;             // the drawing: CL_INK_FOLLOW, _NEVER or _ALWAYS (below)
} CLSimViewStyle;
void cl_simview_draw_page(CLDocument *doc, int page, CGContextRef ctx, double backingScale,
                          double originX, double originY, double unitsPerPoint, const CLSimViewStyle *style);
// The same, with Predict covering only `gates` (a live class's question names its
// lights; the others show as ever, and the wires still all draw "off").
void cl_simview_draw_page_covering(CLDocument *doc, int page, CGContextRef ctx, double backingScale,
                                   double originX, double originY, double unitsPerPoint, const CLSimViewStyle *style,
                                   const long *gates, int count);

// The page's lights (LEDs) and displays (hex displays, and anything else
// that shows a number), top to bottom, left to right. `value` is what it
// shows now: 0 or 1 for a light, the number for a display, -1 when it isn't
// a clean value (unknown, floating, conflicting, or nothing wired to it).
// The box is the lit part, in world units. Returns how many (fills up to
// `max`).
typedef struct {
	long gate;
	int digits;     // 0 for a light; the hex digits a display shows
	int value;
	double left, bottom, right, top;
} CLSimLight;
int cl_simview_lights(CLDocument *doc, int page, CLSimLight *out, int max);
// The light or display at a world point (generously), or -1. For a display,
// `digit` is which of its digits is under the point (0 = the leftmost).
long cl_simview_light_at(CLDocument *doc, int page, double x, double y, int *digit);
// What a light is called, for screen readers, as the truth table names it:
// the nearest short text label not already taken (switches first, then the
// lights in Tab order); with none close, Y (or Y1, Y2... counting the page's
// lights) for a light and 1, 2... for a display. Returns the length.
int cl_simview_light_name(CLDocument *doc, int page, long gate, char *buf, int len);

// Predict, then reveal: drawn over the page. While covered, each light gets a
// card with "?" or the student's guess; once revealed, a ring: green for a
// right guess, red for a wrong one, grey for no guess, and a dashed grey one
// tagged "No clear value" for a light that showed none (not scored).
// `focused` rings the light the keyboard is on.
enum { CL_PREDICT_COVERED = 0, CL_PREDICT_RIGHT = 1, CL_PREDICT_WRONG = 2, CL_PREDICT_UNGUESSED = 3,
       CL_PREDICT_UNCLEAR = 4 };
typedef struct {
	long gate;
	int guess;      // -1 for none yet
	int mark;       // CL_PREDICT_...
	bool focused;
} CLPredictMark;
void cl_simview_draw_predict(CLDocument *doc, int page, CGContextRef ctx, double backingScale,
                             double originX, double originY, double unitsPerPoint,
                             const CLPredictMark *marks, int count, bool projector);

int cl_document_gate_count(const CLDocument *doc, int page);

// Pages, undoably: closing takes the page and what's on it off (undo brings
// it back); Reopen is an undo of the last close. Moving reorders.
bool cl_document_close_page(CLDocument *doc, int page);
bool cl_edit_undo_is_close_page(const CLDocument *doc);
// After an undo or redo that added or removed a page: the page to show.
int cl_document_page_to_show(const CLDocument *doc);
void cl_document_move_page(CLDocument *doc, int from, int to);
// A page's identity, which survives other pages opening, closing and moving
// (0 for none), and the page with an identity now (-1 when it's gone).
unsigned long long cl_document_page_id(const CLDocument *doc, int page);
// Page link groups (docs/PAGE-LINKS.md): TO/FROM links connect by name only
// between pages in the same group. Every change is one undo step; each
// returns false (and adds no step) when nothing would change.
bool cl_pages_linked(const CLDocument *doc, int a, int b);
// The shared dot a tab shows: its group's number among the groups of more
// than one page (0, 1, ... by first page), or -1 for none (a page on its own,
// or every page connected).
int cl_page_link_mark(const CLDocument *doc, int page);
bool cl_pages_all_linked(const CLDocument *doc);
bool cl_pages_none_linked(const CLDocument *doc);   // 2+ pages, none connected
bool cl_pages_connect_all(CLDocument *doc);
bool cl_pages_disconnect_all(CLDocument *doc);
// `page` and the pages `with` marks (count = the page count) become one group;
// the rest keep theirs.
bool cl_page_connect_to(CLDocument *doc, int page, const bool *with, int count);
// Changes whenever which pages connect does (for redrawing tabs).
unsigned long long cl_pages_link_signature(const CLDocument *doc);
// Settings: whether a page added later joins the shared group (the default)
// or starts on its own.
void cl_set_new_pages_share_links(bool share);
bool cl_new_pages_share_links(void);
int cl_document_page_index(const CLDocument *doc, unsigned long long id);

// A paste, duplicate or new gate that follows the pointer until a click
// drops it (the selection floats). The anchor is where the pointer is now.
bool cl_edit_float_begin(CLDocument *doc, int page, double x, double y);
bool cl_edit_is_floating(const CLDocument *doc);
// Connect the selected gates' free pins to unambiguously close free pins
// (the C key while dragging). Returns how many connections were made.
// C while something is being moved (dragged, or floating on the pointer):
// connect its free pins to what's unambiguously close, and keep moving. The
// connections go on the undo stack at the drop, above the move. Returns how
// many, or -1 when nothing is being moved on that page.
int cl_edit_connect_while_moving(CLDocument *doc, int page, double unitsPerPoint);
// Escape mid-move: take back just those connections. Returns how many.
int cl_edit_take_back_connects(CLDocument *doc);
int cl_edit_connect_nearby(CLDocument *doc, int page, double unitsPerPoint);

// RAM and ROM contents (the wx app's memory editor).
bool cl_ram_info(const CLDocument *doc, long gate, int *addressBits, int *dataBits);
unsigned long cl_ram_value(CLDocument *doc, long gate, unsigned long address);
long cl_ram_last_read(CLDocument *doc, long gate);
long cl_ram_last_written(CLDocument *doc, long gate);
void cl_ram_set(CLDocument *doc, long gate, unsigned long address, unsigned long value);
void cl_ram_load_file(CLDocument *doc, long gate, const char *path);
void cl_ram_save_file(CLDocument *doc, long gate, const char *path);

// ---- Drawing on the circuit (the website's docs/DRAWING-NOTES.md) -------------
// Strokes pinned to a page in world units, saved in the .cdl. Editing the
// circuit never touches them; copy and paste never carry them.
enum { CL_INK_PEN = 0, CL_INK_HIGHLIGHTER = 1, CL_INK_ERASER = 2 };   // ERASER: the hover ring only
enum { CL_INK_OK = 0, CL_INK_FULL = 1, CL_INK_READ_ONLY = 2 };

// Shown or hidden: one flag for the circuit, saved with it (only when there
// is a drawing to hide). Not an undo step: the app marks the document edited.
bool cl_ink_shown(const CLDocument *doc);
void cl_ink_set_shown(CLDocument *doc, bool shown);
bool cl_ink_any(const CLDocument *doc);                   // strokes or a foreign drawing on any page
int  cl_ink_stroke_count(const CLDocument *doc, int page);
long cl_ink_point_count(const CLDocument *doc, int page);  // page -1: the whole circuit
// A page holding a drawing from a newer CedarLogic: kept, never drawn or changed.
bool cl_ink_page_read_only(const CLDocument *doc, int page);
// Whether a stroke can start on a page: CL_INK_OK, _FULL (a cap is reached:
// "This drawing is full...") or _READ_ONLY.
int  cl_ink_can_draw(const CLDocument *doc, int page);
// The drawing's extent on a page, widths included (world, y up). False when none.
bool cl_ink_bounds(const CLDocument *doc, int page, double *left, double *bottom, double *right, double *top);

// A stroke being drawn: samples in world coordinates (y up). The core
// filters, splits at 2,000, simplifies with eps = max(0.01, 0.35 * unitsPerPoint),
// quantizes and enforces the caps. `color` is a palette token ("ink", "red"...);
// `width` in world units.
typedef struct { double x, y; double pressure; } CLInkPoint;   // pressure 0..1, or -1 for none
int  cl_ink_begin(CLDocument *doc, int page, int tool, const char *color, double width, double unitsPerPoint);
int  cl_ink_add(CLDocument *doc, const CLInkPoint *points, int count);   // CL_INK_FULL once a cap is reached
int  cl_ink_end(CLDocument *doc);      // commits as one "Draw" step; returns the strokes added (0: none)
void cl_ink_cancel(CLDocument *doc);
bool cl_ink_drawing(const CLDocument *doc);   // a stroke is in progress

// Erasing whole strokes: a drag is one "Erase" step (only when it erased).
void cl_ink_erase_begin(CLDocument *doc, int page);
int  cl_ink_erase_to(CLDocument *doc, double x, double y, double radius);  // swept from the last point; strokes erased so far
int  cl_ink_erase_end(CLDocument *doc);
int  cl_ink_clear(CLDocument *doc, int page);            // one "Clear Drawing" step; returns how many

// After an undo or redo of a drawing step: the page it was on, so the
// window can show it (-1 for other steps).
int  cl_ink_history_page(const CLDocument *doc);

// Drawing it. cl_document_draw (as shown), cl_document_draw_ex and
// cl_simview_draw_page draw the drawing in its layers (highlighter under the
// parts, pen over them) by CLDrawOptions.ink / CLSimViewStyle.ink.
// ALWAYS_PRINT draws it in the colour-print colours (pictures of a page).
// Thumbnails never do.
enum { CL_INK_FOLLOW = 0, CL_INK_NEVER = 1, CL_INK_ALWAYS = 2, CL_INK_ALWAYS_PRINT = 3 };
// cl_document_draw_fitted, with the drawing when `ink` (the bounds include
// it then): LIGHT in the colour-print colours, PRINT in black and white.
bool cl_document_draw_fitted_ink(CLDocument *doc, int page, CGContextRef ctx, double width, double height,
                                 double margin, double backingScale, int style, bool ink);
// What the pointer would draw or erase: a dot of the stroke's width in its
// colour at half strength, or for CL_INK_ERASER a ring of `width` radius.
void cl_ink_hover(CLDocument *doc, int page, double x, double y, int tool, const char *color, double width);
void cl_ink_hover_clear(CLDocument *doc);
// The page the stroke in progress (or the hover) is on, or -1: the canvas
// showing it calls cl_ink_draw_live.
int  cl_ink_live_page(const CLDocument *doc);
// The stroke being drawn and the hover preview, over everything else.
void cl_ink_draw_live(CLDocument *doc, CGContextRef ctx, double backingScale,
                      double originX, double originY, double unitsPerPoint, bool dark, bool projector);
// A token's colour for a tool, for the palette buttons. `look`: 0 light,
// 1 dark, 2 colour print, 3 black and white.
void cl_ink_color(const char *token, int tool, int look, double *r, double *g, double *b, double *a);

// ---- Notes ----------------------------------------------------------------------
// The student's notes for the circuit: plain text, saved with it. Not an undo step.
const char *cl_notes(const CLDocument *doc);              // "" for none; valid until the next cl_notes_set
void cl_notes_set(CLDocument *doc, const char *utf8);     // normalized, cut at 20,000 characters

// ---- Saving for others ------------------------------------------------------------
// The circuit as cl_document_save_text writes it, without the notes and/or the
// drawing, and without marking the document saved: what Share Link (and,
// later, the classroom) sends. Valid until the next call.
enum { CL_SAVE_NO_NOTES = 1, CL_SAVE_NO_INK = 2 };
const char *cl_document_save_text_ex(CLDocument *doc, int flags);

// What the guided tour watches for on a page.
typedef struct {
	int switches, switchesOn, lights;
	bool hasAnd, lightWired, lightOn;
	int andInputsWired;
} CLTourStatus;
void cl_tour_status(CLDocument *doc, int page, CLTourStatus *out);

#ifdef __cplusplus
}
#endif

#endif  // CEDARCORE_H
