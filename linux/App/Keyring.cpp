// The desktop's keyring through libsecret, loaded at run time (see Keyring.h).

#include "Keyring.h"

#include <dlfcn.h>
#include <gio/gio.h>

#include <mutex>
#include <type_traits>

namespace keyring {

namespace {

// libsecret's own declarations (secret-schema.h, secret-password.h,
// secret-service.h, secret-collection.h), written out so the build needs
// neither its headers nor the library. Its ABI has been stable since 0.18.
enum SchemaAttributeType { kAttributeString = 0 };
struct SchemaAttribute {
	const gchar* name;
	SchemaAttributeType type;
};
struct Schema {
	const gchar* name;
	int flags;
	SchemaAttribute attributes[32];
	gint reserved;
	gpointer reserved1, reserved2, reserved3, reserved4, reserved5, reserved6, reserved7;
};
const int kServiceLoadCollections = 1 << 2;   // SECRET_SERVICE_LOAD_COLLECTIONS
const int kCollectionNone = 0;                // SECRET_COLLECTION_NONE

struct Api {
	gchar* (*lookupv)(const Schema*, GHashTable*, GCancellable*, GError**) = nullptr;
	gboolean (*storev)(const Schema*, GHashTable*, const gchar* collection, const gchar* label, const gchar* password,
	                   GCancellable*, GError**) = nullptr;
	gboolean (*clearv)(const Schema*, GHashTable*, GCancellable*, GError**) = nullptr;
	void (*freePassword)(gchar*) = nullptr;
	GObject* (*serviceGet)(int flags, GCancellable*, GError**) = nullptr;
	GObject* (*collectionForAlias)(GObject* service, const gchar* alias, int flags, GCancellable*, GError**) = nullptr;
	gboolean (*collectionLocked)(GObject* collection) = nullptr;
	bool loaded = false;
};

const Api& api() {
	static Api a;
	static std::once_flag once;
	std::call_once(once, [] {
		void* lib = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
		if (!lib) return;
		auto find = [lib](auto& fn, const char* name) {
			fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
			return fn != nullptr;
		};
		a.loaded = find(a.lookupv, "secret_password_lookupv_sync") && find(a.storev, "secret_password_storev_sync") &&
		           find(a.clearv, "secret_password_clearv_sync") && find(a.freePassword, "secret_password_free") &&
		           find(a.serviceGet, "secret_service_get_sync") && find(a.collectionForAlias, "secret_collection_for_alias_sync") &&
		           find(a.collectionLocked, "secret_collection_get_locked");
		// Never closed: GLib types it registers must stay.
	});
	return a;
}

const Schema kSchema = { "com.leviholliday.cedarlogic.Secret", 0, { { "folder", kAttributeString }, { nullptr, kAttributeString } },
                         0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

// {"folder": folder}, for the calls that take attributes as a table.
struct Attributes {
	GHashTable* table = g_hash_table_new(g_str_hash, g_str_equal);
	explicit Attributes(const std::string& folder) { g_hash_table_insert(table, (gpointer) "folder", (gpointer)folder.c_str()); }
	~Attributes() { g_hash_table_unref(table); }
	Attributes(const Attributes&) = delete;
	Attributes& operator=(const Attributes&) = delete;
};

// A keyring answers, and its default collection exists and is unlocked: storing asks nobody anything.
bool defaultUnlocked() {
	const Api& a = api();
	GObject* service = a.serviceGet(kServiceLoadCollections, nullptr, nullptr);
	if (!service) return false;
	GObject* collection = a.collectionForAlias(service, "default", kCollectionNone, nullptr, nullptr);
	const bool unlocked = collection && !a.collectionLocked(collection);
	if (collection) g_object_unref(collection);
	g_object_unref(service);
	return unlocked;
}

// A keyring answers at all (so a lookup won't wait on D-Bus for nothing).
bool reachable() {
	GObject* service = api().serviceGet(0, nullptr, nullptr);
	if (service) g_object_unref(service);
	return service != nullptr;
}

}  // namespace

Lookup lookup(const std::string& folder, std::string& secret) {
	const Api& a = api();
	if (!a.loaded || !reachable()) return Lookup::Unavailable;
	Attributes attrs(folder);
	GError* err = nullptr;
	gchar* found = a.lookupv(&kSchema, attrs.table, nullptr, &err);
	if (err) {
		g_error_free(err);
		return Lookup::Unavailable;
	}
	if (!found) return Lookup::Missing;
	secret = found;
	a.freePassword(found);
	return Lookup::Found;
}

bool store(const std::string& folder, const std::string& label, const std::string& secret) {
	const Api& a = api();
	if (!a.loaded || !defaultUnlocked()) return false;
	Attributes attrs(folder);
	GError* err = nullptr;
	const gboolean ok = a.storev(&kSchema, attrs.table, "default", label.c_str(), secret.c_str(), nullptr, &err);
	if (err) g_error_free(err);
	return ok && !err;
}

void clear(const std::string& folder) {
	const Api& a = api();
	if (!a.loaded || !reachable()) return;
	Attributes attrs(folder);
	GError* err = nullptr;
	a.clearv(&kSchema, attrs.table, nullptr, &err);
	if (err) g_error_free(err);
}

}  // namespace keyring
