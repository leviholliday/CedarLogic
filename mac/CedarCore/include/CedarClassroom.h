// CedarClassroom -- the classroom core (CLASSROOM.md) for Swift, as a plain C
// interface over clclass::Engine (Classroom.h). CedarCore.h includes it, so
// the Mac app sees it through its bridging header. Create, call and destroy
// the engine on the main thread; the hooks say on which thread each is called.
//
// This is CLASSROOM.md 6.3 written out, with the optional hooks check_circuit
// and lights_of added (Check My Circuit and a circuit's lights come from the
// app) and one getter per field of each list.

#ifndef CEDARCLASSROOM_H
#define CEDARCLASSROOM_H

#include "CedarSync.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CLClassroom CLClassroom;

// The hooks. Every function gets `ctx` first. `sync` brings the crypto, http and on_main
// hooks of CedarSync.h (SyncHooks.swift as it is). The P-256 and file hooks are called on
// the engine thread, the UI ones (classes_changed ... sync_delete_side) on the main thread.
typedef struct CLClassroomHooks {
	void *ctx;
	const CLSyncHooks *sync;                         // random, sha256, hmac, aes_gcm_*, deflate/inflate, http, on_main
	bool (*p256_generate)(void *ctx, uint8_t d[32], uint8_t pub[65]);
	bool (*p256_public)(void *ctx, const uint8_t d[32], uint8_t pub[65]);
	bool (*p256_ecdh)(void *ctx, const uint8_t d[32], const uint8_t peer[65], uint8_t x[32]);
	bool (*pbkdf2_sha256)(void *ctx, const uint8_t *pw, size_t pwLen, const uint8_t *salt, size_t saltLen,
	                      uint32_t rounds, uint8_t out[32]);          // may be NULL: the core loops on hmac_sha256
	char *(*load_file)(void *ctx, const char *name);                 // malloc'd or NULL
	bool (*save_file)(void *ctx, const char *name, const char *text);
	void (*remove_tree)(void *ctx, const char *name);
	bool (*try_lock)(void *ctx, const char *path);
	void (*unlock)(void *ctx);
	void (*classes_changed)(void *ctx);
	void (*live_changed)(void *ctx, const char *classId);            // then cl_classroom_live_*
	void (*answers_changed)(void *ctx, const char *classId);
	void (*status_changed)(void *ctx, const char *classId);
	void (*notice)(void *ctx, const char *text);
	int (*sync_side_count)(void *ctx); const char *(*sync_side)(void *ctx, int i, const char **rid);
	void (*sync_put_side)(void *ctx, const char *rid, const char *json);
	void (*sync_delete_side)(void *ctx, const char *rid);
	// (added; may be NULL) on the engine thread. Check My Circuit on a hand-in: the verdict
	// (0 matches, 1 wrong rows, 2 couldn't check) and a malloc'd one-line summary.
	bool (*check_circuit)(void *ctx, const char *cdl, const char *keyText, const char *keyNames, int *verdict, char **summary);
	// (added; may be NULL) on the engine thread. The named lights of a circuit once it settles: values[i] 0 or 1.
	bool (*lights_of)(void *ctx, const char *cdl, const char *const *lights, int count, int *values);
} CLClassroomHooks;

// The servers are https://cedarlogic.netlify.app/api/classroom/v1 and .../api/live/v1 unless
// CL_CLASSROOM_URL and CL_LIVE_URL name others (https, or http for localhost / 127.0.0.1 only).
CLClassroom *cl_classroom_create(const CLClassroomHooks *, const char *dir, const char *appKey, const char *client);
void cl_classroom_set_device_name(CLClassroom *, const char *name);   // the sync side record's `device` (before start)
void cl_classroom_destroy(CLClassroom *);
void cl_classroom_start(CLClassroom *);

// Done callbacks run on the main thread: ok, or a sentence for the person; result per call.
typedef void (*CLClassroomDone)(void *ctx, bool ok, const char *message, const char *result);

// Classes (the strings are valid until the next cl_classroom_class_count).
int cl_classroom_class_count(CLClassroom *);
const char *cl_classroom_class_id(CLClassroom *, int i);
const char *cl_classroom_class_name(CLClassroom *, int i);
bool cl_classroom_class_teaching(CLClassroom *, int i);
const char *cl_classroom_class_teacher_key(CLClassroom *, int i);
const char *cl_classroom_class_join_code(CLClassroom *, int i);
bool cl_classroom_class_join_open(CLClassroom *, int i);
const char *cl_classroom_class_student_name(CLClassroom *, int i);
int64_t cl_classroom_class_expires_at(CLClassroom *, int i);
bool cl_classroom_class_live(CLClassroom *, int i);
const char *cl_classroom_class_warning(CLClassroom *, int i);
// A class's status: kind 0 idle, 1 working, 2 offline, 3 error, 4 gone; the sentence.
int cl_classroom_status_kind(CLClassroom *, const char *classId);
const char *cl_classroom_status_text(CLClassroom *, const char *classId);

// Teacher (4.1-4.4).
void cl_classroom_create_class(CLClassroom *, const char *name, CLClassroomDone, void *ctx);              // result = classId
void cl_classroom_preview_teacher_key(CLClassroom *, const char *text, CLClassroomDone, void *ctx);       // result = the class name
void cl_classroom_add_teacher_key(CLClassroom *, const char *text, CLClassroomDone, void *ctx);
void cl_classroom_rename_class(CLClassroom *, const char *classId, const char *name, CLClassroomDone, void *ctx);
void cl_classroom_set_join_open(CLClassroom *, const char *classId, bool open, CLClassroomDone, void *ctx);
void cl_classroom_new_join_code(CLClassroom *, const char *classId, CLClassroomDone, void *ctx);
void cl_classroom_forget_class(CLClassroom *, const char *classId);
void cl_classroom_delete_class(CLClassroom *, const char *classId, CLClassroomDone, void *ctx);

// Assignments (valid until the next cl_classroom_assignment_count).
int cl_classroom_assignment_count(CLClassroom *, const char *classId);
const char *cl_classroom_assignment_id(CLClassroom *, int i);
const char *cl_classroom_assignment_title(CLClassroom *, int i);
const char *cl_classroom_assignment_instructions(CLClassroom *, int i);
const char *cl_classroom_assignment_cdl(CLClassroom *, int i);
int64_t cl_classroom_assignment_due_at(CLClassroom *, int i);          // -1: none
bool cl_classroom_assignment_close_after_due(CLClassroom *, int i);
int64_t cl_classroom_assignment_ver(CLClassroom *, int i);
const char *cl_classroom_assignment_key_text(CLClassroom *, int i);    // "" for a student when the key is sealed
const char *cl_classroom_assignment_key_names(CLClassroom *, int i);
bool cl_classroom_assignment_key_sealed(CLClassroom *, int i);
int64_t cl_classroom_assignment_handed_in_at(CLClassroom *, int i);
int cl_classroom_assignment_attempts(CLClassroom *, int i);
bool cl_classroom_assignment_changed_since(CLClassroom *, int i);
bool cl_classroom_assignment_pending(CLClassroom *, int i);
bool cl_classroom_assignment_closed(CLClassroom *, int i);
bool cl_classroom_assignment_unreadable(CLClassroom *, int i);
const char *cl_classroom_assignment_problem(CLClassroom *, int i);
void cl_classroom_post_assignment(CLClassroom *, const char *classId, const char *aidOrNull, const char *title, const char *instructions,
                                  int64_t dueAt, bool closeAfterDue, const char *cdl, const char *keyText, const char *keyNames,
                                  bool studentsCanCheck, CLClassroomDone, void *ctx);
void cl_classroom_delete_assignment(CLClassroom *, const char *classId, const char *aid, CLClassroomDone, void *ctx);

// Students (valid until the next cl_classroom_student_count).
int cl_classroom_student_count(CLClassroom *, const char *classId);
const char *cl_classroom_student_id(CLClassroom *, int i);
const char *cl_classroom_student_name(CLClassroom *, int i);
int64_t cl_classroom_student_joined_at(CLClassroom *, int i);
int64_t cl_classroom_student_seen_at(CLClassroom *, int i);
bool cl_classroom_student_unreadable(CLClassroom *, int i);
void cl_classroom_refresh_students(CLClassroom *, const char *classId, CLClassroomDone, void *ctx);
// One student or several ("\n"-separated ids); deleteHandIns: "Also delete everything they handed in".
void cl_classroom_remove_students(CLClassroom *, const char *classId, const char *sids, bool deleteHandIns, CLClassroomDone, void *ctx);
void cl_classroom_remove_student(CLClassroom *, const char *classId, const char *sid, bool deleteHandIns, CLClassroomDone, void *ctx);

// Hand-ins (valid until the next cl_classroom_submission_count).
int cl_classroom_submission_count(CLClassroom *, const char *classId, const char *aid);
const char *cl_classroom_submission_student_id(CLClassroom *, int i);
const char *cl_classroom_submission_name(CLClassroom *, int i);
const char *cl_classroom_submission_cdl(CLClassroom *, int i);
int64_t cl_classroom_submission_handed_in_at(CLClassroom *, int i);
int cl_classroom_submission_attempts(CLClassroom *, int i);
int cl_classroom_submission_check_verdict(CLClassroom *, int i);      // -1 not checked, 0 matches, 1 wrong rows, 2 couldn't check
const char *cl_classroom_submission_check_summary(CLClassroom *, int i);
bool cl_classroom_submission_unreadable(CLClassroom *, int i);
const char *cl_classroom_submission_problem(CLClassroom *, int i);
bool cl_classroom_submission_left(CLClassroom *, int i);
void cl_classroom_refresh_submissions(CLClassroom *, const char *classId, const char *aid, CLClassroomDone, void *ctx);

// The live view (4.4, 4.6). The getters read the class's current state (valid until the next call).
void cl_classroom_go_live(CLClassroom *, const char *classId, const char *cdl, CLClassroomDone, void *ctx);
void cl_classroom_push(CLClassroom *, const char *classId, const char *cdl, const char *promptOrNull, const char *const *lights, int lightCount,
                       bool reveal, CLClassroomDone, void *ctx);
void cl_classroom_end_live(CLClassroom *, const char *classId, CLClassroomDone, void *ctx);
void cl_classroom_take_over_live(CLClassroom *, const char *classId, CLClassroomDone, void *ctx);
bool cl_classroom_live_on(CLClassroom *, const char *classId);
bool cl_classroom_live_ended(CLClassroom *, const char *classId);
bool cl_classroom_live_reveal(CLClassroom *, const char *classId);
bool cl_classroom_live_take_over(CLClassroom *, const char *classId);   // teacher: another device is live
const char *cl_classroom_live_session(CLClassroom *, const char *classId);
const char *cl_classroom_live_cdl(CLClassroom *, const char *classId);
int64_t cl_classroom_live_ver(CLClassroom *, const char *classId);
int cl_classroom_live_step(CLClassroom *, const char *classId);
bool cl_classroom_live_has_predict(CLClassroom *, const char *classId);
const char *cl_classroom_live_prompt(CLClassroom *, const char *classId);
int cl_classroom_live_light_count(CLClassroom *, const char *classId);
const char *cl_classroom_live_light(CLClassroom *, const char *classId, int i);
int cl_classroom_live_my_right(CLClassroom *, const char *classId);    // -1: no score yet
int cl_classroom_live_my_total(CLClassroom *, const char *classId);
int cl_classroom_answers_answered(CLClassroom *, const char *classId);
int cl_classroom_answers_students(CLClassroom *, const char *classId);
int cl_classroom_answers_right(CLClassroom *, const char *classId);
int cl_classroom_answers_wrong(CLClassroom *, const char *classId);
int cl_classroom_answers_light_count(CLClassroom *, const char *classId);
const char *cl_classroom_answers_light(CLClassroom *, const char *classId, int i, int *ones, int *zeros);

// Student (4.5-4.7).
void cl_classroom_preview_join_code(CLClassroom *, const char *text, CLClassroomDone, void *ctx);   // result = "<name>\n<1|0 open>"
void cl_classroom_join(CLClassroom *, const char *text, const char *name, CLClassroomDone, void *ctx);   // result = classId
void cl_classroom_rename(CLClassroom *, const char *classId, const char *name, CLClassroomDone, void *ctx);
void cl_classroom_hand_in(CLClassroom *, const char *classId, const char *aid, const char *cdl, CLClassroomDone, void *ctx);
void cl_classroom_follow(CLClassroom *, const char *classId, bool following);
void cl_classroom_send_answer(CLClassroom *, const char *classId, const char *const *lights, const int *values, int n, CLClassroomDone, void *ctx);
void cl_classroom_make_move_code(CLClassroom *, const char *classId, CLClassroomDone, void *ctx);   // result = the code
void cl_classroom_preview_move_code(CLClassroom *, const char *text, CLClassroomDone, void *ctx);  // result = "<class name>\n<student name>"
void cl_classroom_import_move_code(CLClassroom *, const char *text, CLClassroomDone, void *ctx);
void cl_classroom_leave_class(CLClassroom *, const char *classId, CLClassroomDone, void *ctx);
void cl_classroom_forget_membership(CLClassroom *, const char *classId);

// Triggers (4.8).
void cl_classroom_page_open(CLClassroom *, const char *classId, bool open);
void cl_classroom_app_activated(CLClassroom *);
void cl_classroom_app_deactivated(CLClassroom *);
void cl_classroom_user_active(CLClassroom *);
void cl_classroom_sync_side_changed(CLClassroom *);

// Codes (no engine; the sync hooks for SHA-256): kind 0 teacher, 1 join, 2 move.
// Writes the canonical code (29 bytes with NUL) or returns false with why = "length" | "symbol" | "checksum" | "kind".
bool cl_classroom_parse_code(const CLSyncHooks *, int kind, const char *text, char code[29], char why[16]);
const char *cl_classroom_why_text(int kind, const char *why, const char *text);   // valid until the next call
void cl_classroom_group_code(const char *code, char out[35]);
const char *cl_classroom_web_link(int kind, const char *code);                    // valid until the next call
const char *cl_classroom_app_link(int kind, const char *code);

// Tests: the report (malloc'd; caller frees); false if anything failed. serverBase and liveBase may be NULL.
bool cl_classroom_self_test(const CLClassroomHooks *, const char *tempDir, const char *serverBase, const char *liveBase, char **report);

#ifdef __cplusplus
}
#endif

#endif  // CEDARCLASSROOM_H
