// Stand-ins for the handful of wxWidgets types the shared model and command
// code name, for the native Mac front end (CL_NO_WX). They do what those
// classes do for this code and nothing more: a command with Do/Undo, and a
// processor with an undo stack.

#ifndef CL_NOWXCOMPAT_H
#define CL_NOWXCOMPAT_H

#ifndef CL_NO_WX
#error "NoWxCompat.h is only for the native (CL_NO_WX) build"
#endif

#include <memory>
#include <string>
#include <vector>

typedef std::string wxString;

class wxCommand {
public:
	wxCommand(bool canUndo = false, const wxString& name = wxString()) : canUndo(canUndo), name(name) {}
	virtual ~wxCommand() = default;
	virtual bool Do() = 0;
	virtual bool Undo() = 0;
	virtual bool CanUndo() const { return canUndo; }
	virtual wxString GetName() const { return name; }
private:
	bool canUndo;
	wxString name;
};

// Owns every command it's given. Submit runs it and keeps it for undo; Store
// keeps one that has already been applied. A new command drops the redo stack.
class wxCommandProcessor {
public:
	bool Submit(wxCommand* cmd, bool storeIt = true) {
		if (cmd == nullptr) return false;
		if (!cmd->Do()) { delete cmd; return false; }
		if (storeIt) Store(cmd); else delete cmd;
		return true;
	}
	void Store(wxCommand* cmd) {
		if (cmd == nullptr) return;
		redoStack.clear();
		undoStack.emplace_back(cmd);
		if (undoStack.size() > kLimit) undoStack.erase(undoStack.begin());
		changed = true;
	}
	bool Undo() {
		if (undoStack.empty()) return false;
		std::unique_ptr<wxCommand> cmd = std::move(undoStack.back());
		undoStack.pop_back();
		cmd->Undo();
		redoStack.push_back(std::move(cmd));
		changed = true;
		return true;
	}
	bool Redo() {
		if (redoStack.empty()) return false;
		std::unique_ptr<wxCommand> cmd = std::move(redoStack.back());
		redoStack.pop_back();
		cmd->Do();
		undoStack.push_back(std::move(cmd));
		changed = true;
		return true;
	}
	bool CanUndo() const { return !undoStack.empty(); }
	bool CanRedo() const { return !redoStack.empty(); }
	wxString GetUndoName() const { return undoStack.empty() ? wxString() : undoStack.back()->GetName(); }
	wxString GetRedoName() const { return redoStack.empty() ? wxString() : redoStack.back()->GetName(); }
	void ClearCommands() { undoStack.clear(); redoStack.clear(); }
	// Whether anything was done or undone since the flag was last cleared
	// (the document's "edited" dot).
	bool changed = false;
private:
	static const size_t kLimit = 500;
	std::vector<std::unique_ptr<wxCommand>> undoStack, redoStack;
};

#endif  // CL_NOWXCOMPAT_H
