// CedarSync -- the sync engine (SYNC.md) for Swift, as a plain C interface
// over clsync::Engine (Sync.h). CedarCore.h includes it, so the Mac app sees
// it through its bridging header. Create, call and destroy the engine on the
// main thread; the hooks say on which thread each is called.

#ifndef CEDARSYNC_H
#define CEDARSYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CLSyncEngine CLSyncEngine;

// The hooks. Every function gets `ctx` first. Crypto and http are called on the
// engine thread; on_main must run `fn(arg)` on the main thread and return after it ran.
typedef struct CLSyncHooks {
	void *ctx;
	bool (*random)(void *ctx, uint8_t *out, size_t n);
	void (*sha256)(void *ctx, const uint8_t *p, size_t n, uint8_t out[32]);
	void (*hmac_sha256)(void *ctx, const uint8_t *key, size_t keyLen, const uint8_t *p, size_t n, uint8_t out[32]);
	bool (*aes_gcm_seal)(void *ctx, const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aadLen,
	                     const uint8_t *plain, size_t n, uint8_t *outCtTag /* n + 16 */);
	bool (*aes_gcm_open)(void *ctx, const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aadLen,
	                     const uint8_t *ctTag, size_t n /* incl. tag */, uint8_t *outPlain /* n - 16 */);
	// deflate may be NULL. Both return malloc'd buffers (the engine frees them), or NULL.
	uint8_t *(*deflate_raw)(void *ctx, const uint8_t *in, size_t n, size_t *outLen);
	uint8_t *(*inflate_raw)(void *ctx, const uint8_t *in, size_t n, size_t maxOut, size_t *outLen);
	// HTTP: headers as "name: value\r\n" lines. Fills status (0 = no answer), sent, and malloc'd
	// response headers ("name: value\r\n", names any case) and body; the engine frees them.
	void (*http)(void *ctx, const char *method, const char *url, const char *headers, const uint8_t *body, size_t bodyLen,
	             int *status, bool *sent, char **respHeaders, uint8_t **respBody, size_t *respLen);
	void (*on_main)(void *ctx, void (*fn)(void *), void *arg);
	// secret at rest; load returns a malloc'd C string or NULL
	char *(*load_secret)(void *ctx);
	bool (*save_secret)(void *ctx, const char *code);
	void (*forget_secret)(void *ctx);
	bool (*try_lock)(void *ctx, const char *path);
	void (*unlock)(void *ctx);
	// main thread
	void (*flush_open)(void *ctx, void *token);                  // when saved: cl_sync_flush_done(token), any thread
	void (*window_state)(void *ctx, const char *folderId, bool *open, bool *dirty, int64_t *lastInputAt);
	void (*circuit_replaced)(void *ctx, const char *folderId, const char *fromDevice);
	void (*close_circuit)(void *ctx, const char *folderId);
	void (*library_changed)(void *ctx);
	void (*status_changed)(void *ctx);                 // then call cl_sync_status_*
	void (*notice)(void *ctx, const char *text);
	void (*ask_mass_delete)(void *ctx, int count, void *token);                        // cl_sync_answer(token, yes)
	void (*ask_incoming_deletes)(void *ctx, int count, const char *fromDevices, void *token);   // cl_sync_answer(token, yes)
	// the gate library's default for a param; returns false if there is none
	bool (*gate_default)(void *ctx, const char *lib, bool gui, const char *name, char *out, size_t outLen);
} CLSyncHooks;

// The server is https://cedarlogic.netlify.app/api/sync/v1 unless the environment
// variable CL_SYNC_URL names another (https, or http for localhost / 127.0.0.1 only).
CLSyncEngine *cl_sync_create(const CLSyncHooks *hooks, const char *libraryRoot, const char *syncDir,
                             const char *appKey, const char *client, const char *defaultDeviceName);
void cl_sync_destroy(CLSyncEngine *e);
void cl_sync_start(CLSyncEngine *e);

bool cl_sync_enabled(CLSyncEngine *e);
const char *cl_sync_code(CLSyncEngine *e);              // valid until the next call; "" when off
const char *cl_sync_device_name(CLSyncEngine *e);
void cl_sync_set_device_name(CLSyncEngine *e, const char *name);
int cl_sync_device_count(CLSyncEngine *e);
const char *cl_sync_device(CLSyncEngine *e, int i, int64_t *lastSyncAt);

// Status: kind is CL_SYNC_OFF ... ; text is the sentence to show.
enum { CL_SYNC_OFF, CL_SYNC_SYNCED, CL_SYNC_SYNCING, CL_SYNC_OFFLINE, CL_SYNC_ERROR, CL_SYNC_FULL, CL_SYNC_GONE, CL_SYNC_BUSY };
int cl_sync_status_kind(CLSyncEngine *e);
const char *cl_sync_status_text(CLSyncEngine *e);
int64_t cl_sync_last_sync(CLSyncEngine *e);
int cl_sync_circuit_count(CLSyncEngine *e);
int cl_sync_problem_count(CLSyncEngine *e);
const char *cl_sync_problem(CLSyncEngine *e, int i, const char **folderId);

// Done callbacks run on the main thread: ok, or a sentence for the person.
typedef void (*CLSyncDone)(void *ctx, bool ok, const char *message);
// The preview's done: the confirmation sentence and the numbers behind it (devices "\n"-separated).
typedef void (*CLSyncPreviewDone)(void *ctx, bool ok, const char *message, int circuits, const char *devices);
void cl_sync_turn_on(CLSyncEngine *e, CLSyncDone done, void *ctx);
void cl_sync_preview(CLSyncEngine *e, const char *code, CLSyncPreviewDone done, void *ctx);
void cl_sync_link(CLSyncEngine *e, const char *code, CLSyncDone done, void *ctx);
void cl_sync_turn_off(CLSyncEngine *e, bool removeSyncedCircuits);
void cl_sync_delete_synced_copy(CLSyncEngine *e, CLSyncDone done, void *ctx);
void cl_sync_start_over(CLSyncEngine *e, CLSyncDone done, void *ctx);
void cl_sync_answer(void *token, bool yes);
void cl_sync_flush_done(void *token);

void cl_sync_now(CLSyncEngine *e);
void cl_sync_note_library_changed(CLSyncEngine *e);
void cl_sync_app_activated(CLSyncEngine *e);
void cl_sync_app_deactivated(CLSyncEngine *e);
void cl_sync_user_active(CLSyncEngine *e);
typedef void (*CLSyncQuitDone)(void *ctx);                // called on the engine thread (or, past 5 s, a timer thread)
void cl_sync_quitting(CLSyncEngine *e, CLSyncQuitDone done, void *ctx);

// Pairing (SYNC.md §11): this device joins by showing a QR code that a device
// that syncs scans. Sync must be off. `show` gets the QR code's text (the
// pairing link) once the website has the request; `done` once:
//   CL_SYNC_PAIR_CODE    text = the sync code (then cl_sync_preview and
//                        cl_sync_link it), from = the sending device's name
//   CL_SYNC_PAIR_EXPIRED text = the sentence ("This QR code expired.")
//   CL_SYNC_PAIR_FAILED  text = the sentence (can't reach the website, damaged)
// Both on the main thread. cl_sync_pair_cancel (or a new start, or destroy)
// stops it: then done isn't called. Cancel deletes the slot (best effort).
// Call start and cancel on the main thread. The strings are valid during the call.
enum { CL_SYNC_PAIR_CODE, CL_SYNC_PAIR_EXPIRED, CL_SYNC_PAIR_FAILED };
typedef void (*CLSyncPairShow)(void *ctx, const char *link);
typedef void (*CLSyncPairDone)(void *ctx, int result, const char *text, const char *from);
void cl_sync_pair_start(CLSyncEngine *e, CLSyncPairShow show, CLSyncPairDone done, void *ctx);
void cl_sync_pair_cancel(CLSyncEngine *e);

// Codes (no engine needed; the hooks for SHA-256)
// Writes the canonical code (29 bytes with NUL) or returns false with why = "length" | "symbol" | "checksum".
bool cl_sync_parse_code(const CLSyncHooks *hooks, const char *text, char code[29], char why[16]);
const char *cl_sync_why_text(const char *why, const char *text);
void cl_sync_group_code(const char *code, char out[35]);
const char *cl_sync_web_link(const char *code);         // valid until the next call
const char *cl_sync_app_link(const char *code);
// QR modules of text: returns size (modules per side) and writes size*size bytes (1 = dark) into out (>= 177*177).
int cl_sync_qr(const char *text, uint8_t *out);

// A gate_default hook over the core's own gate library (load it first: cl_library_load on the Mac,
// the app's library on Linux and Windows). ctx is unused.
bool cl_sync_core_gate_default(void *ctx, const char *lib, bool gui, const char *name, char *out, size_t outLen);

// Tests: the report (malloc'd; caller frees); false if anything failed. serverBase may be NULL.
bool cl_sync_self_test(const CLSyncHooks *hooks, const char *tempDir, const char *serverBase, char **report);

#ifdef __cplusplus
}
#endif

#endif  // CEDARSYNC_H
