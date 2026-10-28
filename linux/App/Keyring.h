// The desktop's keyring (the Secret Service: GNOME Keyring, KWallet...), for
// the sync code, through libsecret -- loaded when it's there (dlopen), so the
// app needs neither it nor a keyring to run. GLib only, no GTK, so the
// headless tools use it too.
//
// An item per folder: the attribute "folder" is the path of the folder whose
// secret file it stands in for, under the schema com.leviholliday.cedarlogic.Secret.

#ifndef CL_LINUX_KEYRING_H
#define CL_LINUX_KEYRING_H

#include <string>

namespace keyring {

enum class Lookup {
	Found,
	Missing,       // the keyring answered: there's none
	Unavailable,   // no libsecret, no keyring, or it wouldn't answer (locked, and the unlock was cancelled)
};

// The secret for `folder`. A locked keyring asks the desktop to unlock it, as
// any app's would.
Lookup lookup(const std::string& folder, std::string& secret);

// Stored only into a default keyring that is already unlocked: never a
// question for the person (to make a keyring, or unlock one). False otherwise,
// and the caller keeps its file.
bool store(const std::string& folder, const std::string& label, const std::string& secret);

// Gone, if the keyring can be reached at all.
void clear(const std::string& folder);

}  // namespace keyring

#endif  // CL_LINUX_KEYRING_H
