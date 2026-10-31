// The engine's one off-main-thread path, under load, for ThreadSanitizer:
//   thread_check <cl_gatedefs.xml> <cases.json> [rounds] [workers]
//
// Check My Circuit's clock-by-clock check runs as the apps run it
// (CedarCore.h: cl_check_clocked_prepare on the main thread, _run on another,
// _cancel from the main thread while it runs): many jobs at once on worker
// threads, each opening and simulating copies of its own, while the main
// thread keeps every circuit they came from running, opens, edits, saves and
// closes other documents, and cancels some of the jobs part-way. Every job
// that wasn't cancelled must give exactly what the same check gives with
// nothing else running. Circuits and keys come from Check My Circuit's shared
// cases (tests/check-sequential/cases.json), the clocked ones.
//
// Built with -DCL_TSAN=ON (mac/Tools/headless/CMakeLists.txt) it is the CI's
// ThreadSanitizer run; in a plain build it still checks the results.

#include "CedarCore.h"
#include "SyncJson.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using clsync::json::Value;

namespace {

struct Case {
	std::string id, circuit, key, names;
	int page = 0;
	std::string reference;   // the result with nothing else running
};

// Everything a result says, as one text, to compare.
std::string describe(CLCheck* c) {
	std::string out = std::to_string(cl_check_kind(c)) + "|" + std::to_string(cl_check_verdict(c)) + "|" + cl_check_error(c) +
	                  "|" + cl_check_summary(c);
	for (int i = 0; i < cl_check_note_count(c); i++) out += "|" + std::to_string(cl_check_note_kind(c, i)) + ":" + cl_check_note(c, i);
	for (int i = 0; i < cl_check_name_count(c); i++) out += "|" + std::string(cl_check_name(c, i));
	return out;
}

CLDocument* open(const std::string& text) {
	char err[256];
	return cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
}

struct Job {
	const Case* c = nullptr;
	CLCheckJob* job = nullptr;
	bool cancel = false;          // the main thread cancels this one part-way
	bool cancelSent = false;
	std::atomic<bool> started{ false }, done{ false };
	std::string result;           // written by the worker before `done`
};

}  // namespace

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: thread_check <cl_gatedefs.xml> <cases.json> [rounds] [workers]\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "couldn't load the gate library\n"); return 1; }
	const int rounds = argc > 3 ? std::max(1, atoi(argv[3])) : 3;
	const int workers = argc > 4 ? std::max(1, atoi(argv[4])) : 6;
	std::ifstream in(argv[2], std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	Value all;
	if (!in || !clsync::json::parse(ss.str(), all)) { fprintf(stderr, "couldn't read %s\n", argv[2]); return 1; }
	cl_set_settle_on_open(true);

	// The clocked cases, each with its result on a quiet machine.
	std::vector<Case> cases;
	const Value* circuits = all.get("circuits");
	if (const Value* list = all.get("cases"))
		for (const Value& v : list->a) {
			bool options = false;
			const int kind = cl_check_key_kind(v.str("key").c_str(), &options);
			if ((kind == CL_KEY_FORMULA || kind == CL_KEY_TABLE) && !options) continue;   // today's check: no thread
			const Value* text = circuits ? circuits->get(v.str("circuit")) : nullptr;
			if (!text) continue;
			Case c;
			c.id = v.str("id");
			c.circuit = text->s;
			c.key = v.str("key");
			c.page = (int)v.integer("page");
			if (const Value* n = v.get("names"))
				for (auto& m : n->o) c.names += m.first + "\t" + m.second.s + "\n";
			CLDocument* doc = open(c.circuit);
			if (!doc) { printf("FAIL %s: doesn't open\n", c.id.c_str()); return 1; }
			CLCheck* r = cl_check_clocked(doc, c.page, c.key.c_str(), c.names.c_str());
			c.reference = describe(r);
			cl_check_free(r);
			cl_document_close(doc);
			cases.push_back(c);
		}
	if (cases.empty()) { fprintf(stderr, "no clocked cases in %s\n", argv[2]); return 1; }

	int bad = 0, ran = 0, cancelled = 0, mainTicks = 0, cancelledRunning = 0;
	for (int round = 0; round < rounds; round++) {
		// The documents the jobs come from, left running as a window would be.
		std::vector<CLDocument*> live;
		for (const Case& c : cases) live.push_back(open(c.circuit));
		std::vector<Job> jobs(cases.size() * 2);
		for (size_t i = 0; i < jobs.size(); i++) {
			const size_t k = i % cases.size();
			jobs[i].c = &cases[k];
			for (int s = 0; s < (int)(i % 7); s++) cl_document_step(live[k]);
			jobs[i].job = cl_check_clocked_prepare(live[k], cases[k].page, cases[k].key.c_str(), cases[k].names.c_str());
			jobs[i].cancel = (i + round) % 5 == 0;
		}

		std::atomic<size_t> next{ 0 };
		std::vector<std::thread> pool;
		for (int w = 0; w < workers; w++)
			pool.emplace_back([&] {
				for (size_t i; (i = next++) < jobs.size();) {
					jobs[i].started = true;
					CLCheck* r = cl_check_clocked_run(jobs[i].job);
					jobs[i].result = describe(r);
					cl_check_free(r);
					jobs[i].done = true;
				}
			});

		// Meanwhile, on the main thread: the live circuits run, other documents are
		// opened, edited, saved and closed, and some jobs are cancelled as they run.
		size_t finished = 0;
		for (int tick = 0; finished < jobs.size(); tick++, mainTicks++) {
			for (CLDocument* d : live) cl_document_step(d);
			CLDocument* other = open(cases[(size_t)tick % cases.size()].circuit);
			if (other) {
				cl_document_clock_step(other, 0);
				cl_edit_select_all(other, 0);
				cl_edit_delete(other, 0);
				cl_edit_undo(other);
				const std::string text = cl_document_save_text(other);
				if (text.empty()) { printf("FAIL round %d: a save on the main thread came back empty\n", round); bad++; }
				cl_document_close(other);
			}
			finished = 0;
			for (Job& j : jobs) {
				if (j.cancel && j.started && !j.cancelSent) {
					cl_check_job_cancel(j.job);
					j.cancelSent = true;
					if (!j.done) cancelledRunning++;
				}
				if (j.done) finished++;
			}
		}
		for (std::thread& t : pool) t.join();

		for (Job& j : jobs) {
			ran++;
			if (j.cancel) { cancelled++; }   // its result means nothing (CedarCore.h)
			else if (j.result != j.c->reference) {
				bad++;
				printf("FAIL round %d %s:\n  alone:    %s\n  threaded: %s\n", round, j.c->id.c_str(), j.c->reference.c_str(), j.result.c_str());
			}
			cl_check_job_free(j.job);
		}
		for (CLDocument* d : live) cl_document_close(d);
	}
	printf("%d jobs on %d threads (%d cancelled, %d of them while running), the main thread working %d times meanwhile; "
	       "%d differed from the check run alone\n", ran, workers, cancelled, cancelledRunning, mainTicks, bad);
	return bad ? 1 : 0;
}
