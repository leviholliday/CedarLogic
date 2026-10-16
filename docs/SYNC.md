# SYNC.md — Your Circuits on every device

Status: design, protocol version 1, **revision 2** (2026-10-04), after a
security review (`review-security.md`) and a sync-correctness review
(`review-sync.md`). Written for six independent work packages (§10) that must
interoperate on the first try. Everything marked MUST is checked by the test
vectors (§7.1) or the protocol scenarios (§7.2). The reference implementation in
`ref/` (Python, checked again with WebCrypto, CryptoKit, OpenSSL plus the apps'
own `.cdl` readers, and an independent node:crypto script) is the tie-breaker
whenever this text and an implementation disagree — fix the text or the code
until `sh ref/run_all.sh` and every client's own suite agree.

## 0. The whole thing on one page

**What the owner chose.** Sync with a code, no account. Turning on sync makes a
secret code (also shown as a QR code). Entering or scanning it on another device
links that device. Circuits are end-to-end encrypted, so the website can't read
them. Free: Netlify Functions + Netlify Blobs on the existing site
(`cedarlogic.netlify.app`). Your Circuits is unlocked in CedarLogic Online and
the phone app (`/app/`), as a local list that works without sync and syncs when
sync is on. Version history stays app-only and local; a change that arrives
from another device becomes a new local version, so nothing is lost.

**How it works.**

```
  16 random bytes (the secret)
        │  shown as 000G-40R4-0M30-E209-185G-R38E-1YZ4 (28 Crockford base32 symbols, 12-bit checksum)
        │  and as https://cedarlogic.netlify.app/sync/#k=000G40R40M30E209185GR38E1YZ4 (QR)
        ▼
  HKDF-SHA256 ──► spaceId      (16 B, hex)    public: names the storage, appears in URLs
              ──► authToken    (32 B, b64url) "Authorization: Bearer"; the server keeps SHA-256 of it
              ──► deleteToken  (32 B, b64url) sent only to delete the whole synced copy
              ──► recordKey    (32 B)         AES-256-GCM; never leaves the device

  one circuit = one record { id: random UUID, ver: rising integer } whose data is
  AES-GCM( deflate?( JSON {kind:"circuit", name, cdl, modifiedAt, device, base, …} ) ), AAD = id + ver
  a deletion  = a sealed record {kind:"deleted"}; each device also keeps one {kind:"device"} record
```

The server stores opaque envelopes with optimistic concurrency (a write names
the `ver` it was based on, or it gets **412**). Clients pull changes since a
cursor, then push their own in batches. Every edit carries the hashes of the
version it started from (`base`), so a device can tell "built on what I have"
(take it quietly) from "changed on both" (a real conflict). Name and circuit
are merged separately. In a real conflict the newer becomes the circuit and the
other is kept — as a version in the apps, as a copy "Name (from Device)" online.
"The same circuit" is decided on a parsed, format-independent structure digest
(§2.4), because the apps save v3 and Online saves v1 XML.

**Decisions in one table.**

| Question | Decision |
|---|---|
| Secret | 128-bit random; code = 28 Crockford base32 symbols (7 groups of 4) with a 12-bit SHA-256 checksum |
| Links / QR | `https://cedarlogic.netlify.app/sync/#k=<code>` (QR, website) and `cedarlogic://sync#k=<code>` (apps); `?k=` only for `cedarlogic:` |
| KDF | HKDF-SHA256, salt `cedarlogic-sync-v1`, info `space-id` / `auth-token` / `delete-token` / `record-key` |
| Cipher | AES-256-GCM, random 96-bit nonce, 128-bit tag, AAD `cedarlogic-sync/1|<id>|<ver>|<flags>` |
| Envelope | `01 ‖ flags ‖ nonce(12) ‖ ciphertext ‖ tag(16)`, base64url (no padding) in JSON; flag bit 0 = deflate-raw |
| Record kinds | `circuit` (name, cdl, times, device, base), `deleted` (a sealed tombstone), `device` (name, last sync) |
| Record id / ver | random UUID v4, never from the name; `ver` only ever rises for an id, never reused (§2.1) |
| Change detection | `nameHash`, `cdlHash` (text), `structureHash` (parsed gates, params and connections: same for v3 and XML) |
| Conflicts | three-way against `base`, name and circuit separately; fast-forward when built on what is here; both changed → newer (`modifiedAt` clamped to the server's receipt time, clocks corrected by the server's `Date`) wins, loser kept (version / copy); only runtime state differs → newer, quietly |
| Deletes | a sealed tombstone a device must verify before trashing; many at once → ask; a record the server "forgot" is sent again, never trashed |
| Server misbehaviour | per-record high-water mark (`seen`): an older or swapped copy is ignored and this device's copy sent again; a new `epoch` or a falling `seq` → everything checked and re-sent |
| Join / link | preview first (decrypt, show "14 circuits from Bob’s laptop"), upload only after a second press; same content or same name + same structure → mapped, not doubled |
| Limits | 512 KiB per circuit, 1,000 circuits and 10 MiB per space, plus up to 20 device records; batches of 50; per-IP, per-space and site-wide byte and request budgets |
| Expiry | unused 365 days → removed, the id answers 410 forever; never-used empty spaces after 7 idle days (silently re-creatable) |
| Engine | one C++ engine (`mac/CedarCore/Sync*.cpp`) for Mac, Linux and Windows, crypto/HTTP/UI behind hooks; `sync-core.js` for the web |
| Secret at rest | apps: a 0600 file in a per-machine folder (Windows: DPAPI-protected); web: IndexedDB |

**Who builds what** — six packages, with owned files, dependencies and
acceptance tests in §10: **S** server (+ in-memory Blobs stub + a mock server
every other package tests against), **W** web and phone client, **E** the shared
C++ engine, **M** Mac, **L** Linux, **X** Windows.

**Out of scope for v1** (not designed here; nothing below blocks them): version
history online; sharing a circuit with another person through sync; scanning a
QR code inside the desktop apps; live collaboration; request signing (proof of
possession) instead of a bearer token (§8.2).

### 0.1 What revision 2 changed (review finding → where)

| Finding | Fix | Where |
|---|---|---|
| Sec V1: no surrogate-pair escape in the vectors | record 4 is hand-escaped JSON (`😀`, ` `, `\/`, raw U+2028); refused-payload vectors include an unpaired surrogate | §2.2, §7.1.6–7 |
| Sec V2: Unicode upper-casing in the parser | ASCII-only upper-casing, one character at a time; `ı`, `ﬆ`, full-width digits → `symbol` | §1.2, §7.1.2 |
| Sec H1: server rollback applied | per-record high-water `seen[id] = (ver, h)`; lower ver or same ver with another `h` is ignored and this device's copy re-sent at a higher ver | §4.5, §4.9 |
| Sec H2: forged tombstones / purges trash circuits | deletions are sealed `kind:"deleted"` records; a "forgotten" record is re-sent, never trashed; incoming mass-delete guard; a notice for every remote delete | §2.2, §4.7 |
| Sec H3: reset space unnoticed | random `epoch` per space document; epoch change or `seq < cursor` → full pull and re-send; deleted/expired ids answer 410 forever | §3.3, §3.10, §4.5 |
| Sec H4: "someone sends you their code" | link = preview (decrypt, nothing sent) → "This code has 14 circuits from Bob’s laptop…" → second press uploads | §4.8, §5.1 |
| Sec M1 / review: `ver` restarted for an id | `ver` only rises: writes use `max(ver, seen)+1`; the server accepts any `ver > base` and keeps purged ids' last ver (`ghosts`) | §2.1, §3.3, §3.6 |
| Sec M2: storage/egress/quota abuse | 512 KiB / 10 MiB caps; site byte budget; per-IP bytes written, per-space bytes served; site-wide daily request breaker; Netlify rate-limit rule | §3.5 |
| Sec M3: bearer token alone could delete everything | a separate `deleteToken` (HKDF) only for `DELETE /spaces`; §8.2 rows corrected | §1.3, §3.3, §8.2 |
| Sec M4: no CSP | strict CSP + `Permissions-Policy` on `/app/`, the simulator and `/sync/`; names only via `textContent` | §4.13, §5.4 |
| Sec M5: rate-limit design | HMAC with a server pepper, IPv6 /64, per-space counters only after auth, small auth record read first, counters fail open | §3.5, §3.8 |
| Sec M6 / review 16: clocks decide conflicts | stamps written in server-corrected time (`Date` header); compared as `min(modifiedAt, updatedAt + 60 s)` | §4.6 |
| Sec L1–L5 | `?k=` only for `cedarlogic:`; parse-based structure; RNG failure aborts a seal, no nonce parameter in production; Start Over with a New Code + device list; wording | §1.2, §2.4, §1.4, §4.11, §8.1 |
| Sec: previews share production Blobs | store names per deploy context | §3.1 |
| Sync 1: `@netlify/blobs` reports failed CAS as success | success only if `modified && etag`; never delete blobs inline; write time in blob names | §3.7, §3.10 |
| Sync 2: v1 XML vs v3 never "same shape" | parsed structure digest, identical for both formats; vectors with the same circuit in both; the simulator's web client writes XML | §2.4, §7.1.5 |
| Sync 3: lost answer + another device's edit → copy | payload `base`; fast-forward when `base` = what is here; base = our lost write when it matches | §4.6 |
| Sync 4: rename vs edit lost the rename | name and circuit merged separately | §4.6 |
| Sync 5: apps lose edits made after the snapshot | apply step re-checks the window; dirty → skip, cursor held; files before state; Mac async saves; quitting path never runs window code off the main thread | §4.5, §4.12, §6.2 |
| Sync 6: version thinning deletes sync versions | sync-made versions stamped with the arrival time, file time set to match; original time in the note | §4.12 |
| Sync 7: empty space deleted under linked devices | the 7-day rule needs 7 idle days; a 404 on a synced device re-creates the space with the same code | §3.10, §4.11 |
| Sync 8: restore from backup deletes/duplicates | `.sync-library-gen` generation marker; re-join with the old mapping as hints; never send deletes while re-joining | §2.5, §4.12 |
| Sync P1 9–19 | batch write endpoint; classroom limits; parallel fetch ≤ 50 ids; unreadable records skipped in push; ns mtimes + racily-clean rule; web `rev` compare-and-set; lazy runtime-only pushes; per-page purge check; windows-1252 fallback; delete notice; orphan notes; `http://localhost` for tests | §3, §4 |
| Crypto/platform notes | 20 MB inflate caps (Mac, web); Windows links `bcrypt`/`crypt32`, MinGW opens providers; Linux `.deb` tested on 24.04 | §1.4, §6.4, §10 |
| Phone UX | Scan Code in the web app (camera + vendored decoder), Paste Code, `/sync/#k=`, code field attributes, service worker cache list, iOS hint | §5.4 |

---

## 1. The sync secret and the cryptography

### 1.1 The secret

- 16 bytes (128 bits) from the platform's cryptographic RNG: WebCrypto
  `crypto.getRandomValues`, Mac `SecRandomCopyBytes`, Linux OpenSSL `RAND_bytes`
  (never `g_random_*`), Windows `BCryptGenRandom(NULL, …,
  BCRYPT_USE_SYSTEM_PREFERRED_RNG)`. If the RNG reports failure, the operation
  (Turn On, a seal, a new record id) MUST stop with an error — never continue
  with a zeroed or partial buffer.
- One secret per *space* (a set of synced circuits). Every device linked to the
  space holds the same secret.

### 1.2 The code (what people see and type)

```
checksum12 = (SHA256(secret)[0] << 4) | (SHA256(secret)[1] >> 4)         12 bits
N          = secret (big-endian, 128 bits) << 12 | checksum12             140 bits
code       = N as 28 symbols of 5 bits, most significant first,
             alphabet "0123456789ABCDEFGHJKMNPQRSTVWXYZ" (Crockford)
shown as   = 7 groups of 4 joined with "-":  000G-40R4-0M30-E209-185G-R38E-1YZ4
```

C/C++ implementers: pack `secret ‖ (checksum12 << 4 as 2 bytes)` = 18 bytes and
read 140 bits five at a time (see `ref/check_openssl.cpp`); no big integers needed.

**Parsing input** (typing, pasting, links, a scanned QR code) MUST behave exactly
as `ref/clsync.py: normalize_code/decode_code`:

1. If the text contains `#k=`, take the value after it, up to the first
   character that isn't an ASCII letter, digit, `-` or space. Otherwise, only if
   the text starts with `cedarlogic:` (ASCII, any case), do the same with `?k=`
   or `&k=`. (The website never puts a code in a query string, where it would
   reach the server and its logs.)
2. Drop `-`, space, tab, CR, LF and U+00A0.
3. Look at each remaining character on its own (a Unicode scalar value, or a
   byte in C++ where every byte ≥ 0x80 is already wrong): upper-case it **only
   if it is ASCII `a`–`z`** (no Unicode case mapping: `ı` U+0131, the ligature
   `ﬆ` U+FB06 and full-width digits are errors, never letters); then map
   `O`→`0`, `I`→`1`, `L`→`1`.
4. Anything not in the alphabet (that includes `U`) → error **symbol**
   ("‘U’ can't be in a sync code").
5. Not exactly 28 symbols → error **length** ("A sync code has 28 letters and
   digits; this has 27").
6. Checksum mismatch → error **checksum** ("That code has a typo in it. Check
   it against the other device.").

Canonical form for storage and links: the 28 symbols, upper case, no dashes.

**Links and the QR code.**

```
https://cedarlogic.netlify.app/sync/#k=000G40R40M30E209185GR38E1YZ4     the QR code, Copy Link, the website
cedarlogic://sync#k=000G40R40M30E209185GR38E1YZ4                        "Open in the app" from /sync/
```

The code is in the fragment so it is never sent to any server (not even in the
`Referer`); the path has its trailing slash so there is no redirect. The QR code
encodes the **https** link (phone cameras open it): byte mode, error correction
**M**, quiet zone 4 modules, always dark modules on white (also in dark mode), at
least 160 px / 160 pt wide (67 bytes → QR version 5).

Pages that receive a link MUST remove the fragment from the address bar at once
(`history.replaceState(null, "", location.pathname)`) and MUST NOT link a device
without the person pressing a button after seeing the preview (§4.8). A code in a
link stays in browser history (and in synced browser history), in a camera app's
history and in a screenshot; the Link sheet says so on shared computers (§5.1).

### 1.3 Key derivation

HKDF-SHA256 (RFC 5869), input keying material = the 16-byte secret,
salt = the 18 ASCII bytes `cedarlogic-sync-v1`, one derivation per output:

| Output | info (ASCII) | Length | Encoding | Use |
|---|---|---|---|---|
| `spaceId` | `space-id` | 16 bytes | 32 lowercase hex chars | storage namespace; in URLs; not secret |
| `authToken` | `auth-token` | 32 bytes | base64url, no padding (43 chars) | `Authorization: Bearer <authToken>` on every request |
| `deleteToken` | `delete-token` | 32 bytes | base64url, no padding (43 chars) | `x-cedarlogic-delete` on `DELETE /spaces/{space}` only |
| `recordKey` | `record-key` | 32 bytes | raw | AES-256-GCM key; never sent anywhere |

The server stores `authHash` = lowercase hex SHA-256 of the 43 ASCII characters
of `authToken`, and `deleteHash` likewise of `deleteToken` (the client sends
`deleteHash` when it creates the space). It compares in constant time. It never
sees the secret or the recordKey and can't derive either from what it stores
(HKDF outputs are independent; the secret is 128 random bits). The delete token
is separate so that someone who sees ordinary sync traffic (a TLS-inspecting
school or company proxy sees the bearer token) still can't delete the whole
synced copy.

Per platform:

- **Web**: `crypto.subtle.importKey("raw", secret, "HKDF", false, ["deriveBits"])`,
  then `deriveBits({name:"HKDF", hash:"SHA-256", salt, info}, key, bits)`.
- **Mac / Linux / Windows**: HKDF is written once in the shared engine
  (`SyncProtocol.cpp`) on top of the `hmacSha256` hook (PRK = HMAC(salt, secret);
  T(i) = HMAC(PRK, T(i-1) ‖ info ‖ i)) — so no platform needs an HKDF API.
  (CryptoKit's `HKDF<SHA256>` gives the same bytes; `ref/check_cryptokit.swift`
  checks it.)

### 1.4 The record envelope

```
offset  size  field
0       1     envelope version = 0x01
1       1     flags: bit 0 = plaintext is deflate-raw (RFC 1951, no zlib/gzip header); other bits MUST be 0
2       12    nonce (random, fresh for every encryption)
14      n     AES-256-GCM ciphertext of the plaintext
14+n    16    GCM tag
```

- `AAD` = the ASCII bytes of `cedarlogic-sync/1|<recordId>|<ver>|<flags>`
  where `recordId` is the lowercase UUID, `ver` the decimal record version this
  envelope will have on the server (the `ver` in the write), `flags` the decimal
  flags byte. Example: `cedarlogic-sync/1|3b241101-e2bb-4255-8caf-4136c566a962|1|0`.
  Binding `ver` means a server can't present an old envelope as a newer version
  of the record; binding the id means it can't swap two records. (It *can*
  present an old envelope at its own old `ver`: §4.5's high-water mark is what
  stops that.)
- `plaintext` = the UTF-8 JSON payload (§2.2), deflated when flag bit 0 is set.
- Writers SHOULD deflate circuits (they shrink ~5–10×); tombstones and device
  records are written uncompressed. Readers MUST support both. Inflate MUST stop
  at 20,000,000 bytes (`MAX_PLAINTEXT`) and fail beyond. Neither the web's
  `DecompressionStream` nor the Mac's one-shot `compression_decode_buffer` has a
  cap of its own: the web counts bytes while reading the stream and cancels past
  the cap; the Mac decodes into a buffer of `MAX_PLAINTEXT + 1` bytes and fails
  when it fills (or uses `compression_stream`).
- Whole envelope ≤ 524,288 bytes (512 KiB) before base64url for a circuit;
  ≤ 4,096 bytes for a tombstone or a device record.
- In JSON the envelope travels as `data`: base64url, no padding.
- A reader MUST treat as **damaged** (without trying to decrypt) an envelope
  shorter than 30 bytes, with byte 0 ≠ 1, or with flag bits other than bit 0; and
  as damaged too when the tag doesn't match or the inflate fails.
- Nonces: 96 bits from the CSPRNG for every seal. With ≪ 2³² encryptions per key
  (the write cap allows ~26 million a year at most), random nonces are safe.
  The production seal function MUST NOT take a nonce parameter; the byte-exact
  vector is produced by test code only (the engine's `sealForTest`, `ref/`).
  Every self-test MUST also check that two seals of the same payload differ.

Primitives per platform (every one exists today, nothing to download):

| | random | SHA-256 / HMAC | AES-256-GCM | deflate-raw |
|---|---|---|---|---|
| Web | `getRandomValues` | `subtle.digest` (HMAC not needed) | `subtle.encrypt/decrypt({name:"AES-GCM", iv, additionalData, tagLength:128})` — output is ciphertext‖tag | `CompressionStream("deflate-raw")` (already used by `sim-share.js`); `DecompressionStream` with a byte count |
| Mac | `SecRandomCopyBytes` | CryptoKit `SHA256`, `HMAC<SHA256>` | CryptoKit `AES.GCM.seal(_:using:nonce:authenticating:)`; the envelope gets `ciphertext + tag` (not `.combined`, which prepends the nonce) | Compression `COMPRESSION_ZLIB` (raw deflate; already used by `ShareLinkCodec.swift`), capped as above |
| Linux | `RAND_bytes` | `EVP_Digest(EVP_sha256())`, one-shot `HMAC(EVP_sha256(), …)` (or GLib `GChecksum`/`GHmac`) | `EVP_aes_256_gcm`, `EVP_CTRL_GCM_SET_IVLEN 12`, AAD via `EVP_EncryptUpdate(ctx, NULL, …)`, `EVP_CTRL_GCM_GET_TAG/SET_TAG 16` | GIO `GZlibCompressor/GZlibDecompressor` `G_ZLIB_COMPRESSOR_FORMAT_RAW` (already used by `ShareLink.cpp`) |
| Windows | `BCryptGenRandom` | `BCryptHash` with `BCRYPT_SHA256_ALG_HANDLE` / `BCRYPT_HMAC_SHA256_ALG_HANDLE` (MinGW: providers opened once with `BCryptOpenAlgorithmProvider` and cached) | `BCRYPT_AES_ALGORITHM` + `BCRYPT_CHAIN_MODE_GCM`, `BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO` (pbNonce 12, pbAuthData = AAD, pbTag 16); `STATUS_AUTH_TAG_MISMATCH` = damaged | `windows/App/Deflate.h` (already used by `ShareCodec.cpp`; has a capped decompress) |

Linux packages: build needs `libssl-dev` (Debian, Ubuntu, Raspberry Pi OS),
`openssl-devel` (Fedora), `openssl` (Arch); CMake `find_package(OpenSSL REQUIRED)`
→ link `OpenSSL::Crypto` (libcrypto only; no TLS from OpenSSL is used). The `.deb`
and AppImage are built on Ubuntu 22.04 (OpenSSL 3.0); the `.deb` gets its
dependency from `dpkg-shlibdeps` (`libssl3`, which Ubuntu 24.04 / Debian 13
provide as `libssl3t64` — CI installs the built `.deb` on 24.04 to prove it);
the AppImage MUST bundle `libcrypto.so.3` (CI checks `ldd` of the AppImage's
binary resolves it inside the bundle). Windows adds `bcrypt` and `crypt32` to
`target_link_libraries` (neither is linked today). Mac: CryptoKit + Compression.

### 1.5 Versioning

Every layer carries a version so a future change can coexist: the API path
(`/api/sync/v1/…`), the HKDF salt (`-v1`), the envelope byte (`0x01`), the AAD
prefix (`cedarlogic-sync/1`), the payload field `v` (1), and the structure text
header (`cedarlogic-structure/1`). A reader that meets a payload `v` or `kind`
it doesn't know MUST NOT apply or overwrite that record; it marks it "needs a
newer CedarLogic" (§4.10). A code with a different length than 28 is not a v1
code.

---

## 2. Data model

### 2.1 What one synced record is

One circuit in Your Circuits = one record. Each linked device also owns one
small device record, and a deleted circuit's record becomes a sealed tombstone.

| Field | Who sees it | Meaning |
|---|---|---|
| `id` | server, clients | random UUID v4, lowercase, made by the device that first uploads the record; **never** from the name or content |
| `ver` | server, clients | the record's version: chosen by the writer, MUST be greater than the version the write is based on, and only ever rises for an id — a writer uses `max(the ver it has, the highest ver it has seen for that id) + 1` (usually just +1) |
| `seq` | server, clients | space-wide change counter at this record's last change (cursor for pulls) |
| `size` | server, clients | envelope bytes |
| `updatedAt` | server, clients | server time of the last change (ms since 1970, UTC) |
| `deleted` | server, clients | the writer says this is a tombstone (a hint for the server's counting; clients trust only the decrypted payload) |
| `h` | server, clients | first 32 hex of SHA-256 of the envelope bytes (replay detection; the clients' high-water check) |
| `data` | server (opaque), clients | the envelope (§1.4) |

There is no separate manifest: listing the space's records (§3.6) is the
manifest, and everything a person would call metadata (name, device, times of
editing, whether a deletion is real) is inside the encrypted payload.

### 2.2 The encrypted payload (JSON, UTF-8)

Three kinds. A circuit:

```json
{"v":1,"kind":"circuit","name":"Half adder","cdl":"(cedarlogic\n  (version 3)\n…","createdAt":1759572000000,
 "modifiedAt":1759575600123,"device":"Levi’s MacBook Air","deviceId":"5f0c2a9e7d3b41c8a6e2f1b0c9d8e7a6",
 "base":{"name":"<64 hex>","cdl":"<64 hex>"}}
```

A deletion, and a device:

```json
{"v":1,"kind":"deleted","deletedAt":1759575900000,"device":"Levi’s MacBook Air","deviceId":"5f0c…","base":{"name":"…","cdl":"…"}}
{"v":1,"kind":"device","device":"Levi’s MacBook Air","deviceId":"5f0c…","client":"mac/0.3.7+801","lastSyncAt":1759575960000}
```

| Field | Kinds | Type | Rules |
|---|---|---|---|
| `v` | all | integer | 1. Readers refuse other values: a larger one is "newer" (§4.10). |
| `kind` | all | string | `"circuit"`, `"deleted"` or `"device"`. Any other string is "newer". |
| `name` | circuit | string | The name in Your Circuits. Writers trim ASCII whitespace (space, tab, CR, LF) at both ends and cut to 200 Unicode scalar values; empty → `"Untitled"`. Readers trim and cut the same way. |
| `cdl` | circuit | string | The circuit file's whole text, all pages, in any format CedarLogic reads (v1/v2 XML, v3 s-expressions), as sync text (§2.3). |
| `createdAt` | circuit | integer, optional | when the circuit was first made (apps: from the folder id's stamp if it parses, else the folder's creation time). |
| `modifiedAt` | circuit | integer | when this content was last changed on the writing device, **in server-corrected time** (§4.6). Picks the newer in a conflict; shown as "Edited …". |
| `deletedAt` | deleted | integer | when the person deleted it (server-corrected). |
| `lastSyncAt`, `client` | device | integer, string | the device's last successful sync; its app and version (`mac/0.3.7+801`, `web/…`). |
| `device` | all | string | The writing device's name (Settings), ≤ 64 characters. |
| `deviceId` | all | string | The writing device's install id: 32 lowercase hex, random, made once per install of sync (kept in the sync state). The conflict tie-breaker. |
| `base` | circuit, deleted | object, optional | `{"name": nameHash, "cdl": cdlHash}` (§2.3) of the server version this change started from — the writer's `records[id].n/.c` when it sealed. Absent on a circuit's first write. |

"Integer" means a JSON number whose value is a whole number from 0 to 2⁵³−1
(`5`, `5.0` and `5e0` are all 5). Readers MUST refuse as **invalid** (§4.10)
a payload that isn't valid UTF-8, isn't a JSON object, has a required field
missing or of the wrong type, has a `base` that isn't two 64-hex strings, or has
an unpaired surrogate escape (`\ud83d` alone) anywhere. Readers MUST ignore
unknown fields (a later v1 writer may add optional ones); writers needn't keep
them. JSON writers MUST escape `"`, `\` and U+0000–U+001F (as `\uXXXX` or the
short escapes) and may write everything else as raw UTF-8; readers MUST accept
every JSON escape — including surrogate pairs (`😀` → 😀), `\/` and
` ` — and raw U+2028/U+2029. Record 4 of §7.1.6 has all of them, written by
hand; record 3 has the raw forms (BEL — CedarLogic's stand-in for `<` inside
legacy values — quotes, a backslash, a raw U+2028 and a raw emoji).

**Which cdl format.** Each app's library stores `circuit.cdl` as the engine
saves it: v3 s-expressions (`cl_document_save_text`). CedarLogic Online reads
v1, v2 and v3 (`sim-files.js: clFromCdl`) and writes v1 XML
(`cdl-export.js: clToCdl`, which all the apps open). So the payload's `cdl` is
"the text of a .cdl file", not a fixed format: each client writes what it
saves, every client reads all of them, and the structure digest (§2.4) — not the
text — decides whether two copies are the same circuit. Clients MUST store a
received `cdl` byte for byte (apps: `circuit.cdl`; web: the IndexedDB record)
and MUST NOT rewrite it until the person edits the circuit — otherwise opening a
circuit would look like an edit and bounce between formats. Known limitation:
v1 has no page names and no buses, so a circuit edited online loses its page
names and keeps only each bus's first line (fix later with a v3 writer for
Online; nothing in the protocol changes).

### 2.3 Text and hashes every client computes the same way

```
fileText(bytes)   = bytes as UTF-8 if they are valid UTF-8, else decoded as WHATWG windows-1252
                    (TextDecoder("windows-1252"): 0x80–0x9F through its table, the five undefined
                    bytes kept as U+0081 etc., every other byte its Latin-1 code point); then a
                    leading U+FEFF dropped
normalizeCdl(t)   = t without a leading U+FEFF, with every CR LF replaced by LF
normalizeName(n)  = n without a leading U+FEFF, without leading/trailing ASCII space, tab, CR, LF
nameHash          = lowercase hex SHA-256( UTF-8(normalizeName(name)) )
cdlHash           = lowercase hex SHA-256( UTF-8(normalizeCdl(cdl)) )
contentHash       = lowercase hex SHA-256( UTF-8(normalizeName(name)) ‖ 0x00 ‖ UTF-8(normalizeCdl(cdl)) )
structureHash     = lowercase hex SHA-256( UTF-8(structureText(cdl)) )       (§2.4)
```

The apps read `circuit.cdl` and `name.txt` through `fileText`: old circuits saved
by the wx app on Windows can hold windows-1252 bytes in labels, and JSON must be
UTF-8. The file on disk isn't rewritten (a payload's `cdl` is the decoded text;
hashes are always over the decoded text, so the hash doesn't change when a
received UTF-8 copy replaces the file). The web has strings already.

- `nameHash`/`cdlHash`: change detection and the three-way merge (§4.6).
- `contentHash`: "same circuit, same name" (join de-duplication, `tooBig`).
- `structureHash`: "same circuit apart from what running it changes, which app
  and which format wrote it, and how its wires are routed" — whether a both-sides
  change is a real conflict, whether a change is worth a version, the join's
  fallback match, and which pushes may wait (§4.3).

### 2.4 The structure digest

`structureText(cdl)` turns a circuit file into a canonical list of its gates and
connections. v3 and v1/v2 XML of the same circuit give the same text — §7.1.5
proves it on a real 27-gate app file and its CedarLogic Online export.

**Read.** `t = normalizeCdl(cdl)`. Detect the format as `docs/CDL-Format.md` §6 /
`format/legacy_cdl.cpp: detectFormat` does (first non-space character `(` and
the text contains `cedarlogic` → v3; else `<throw_away>`, `<version>` or
`<circuit>` anywhere → legacy XML; else unknown). Parse with the readers of
`docs/CDL-Format.md` §3–§5 **without migration** — in C++ exactly
`cl::readCircuitFile` (v3) or `cl::readLegacyCdl` (v1/v2) from `format/`
(not `loadCircuit`); the web client and `ref/` carry small readers that follow
the same rules (`sync-core.js`, `ref/clsync.py: read_v3/read_legacy`), checked
against the same vectors. If detection says unknown or the reader fails, the
structure text is `"cedarlogic-structure/1 unparsed\n" + t` (so two unreadable
texts are "the same" only when identical).

**Canonical lines.** For each page (by its index; page names ignored):

- Each gate: its token `T = esc(libName) "@" milli(x) "," milli(y)`, then the line
  `G <page> <T> <angle> <params…>` where
  - `milli(v) = floor(v × 1000 + 0.5)` as a decimal integer (coordinates in
    thousandths; double precision);
  - `angle = floor(angle + 0.5) mod 360`, in 0…359;
  - params are the gate's lparams and gparams **except**: lparams `OUTPUT_NUM`
    and `CURRENT_VALUE` (runtime state); gparams named `angle` or containing
    `_BOX` (click and display boxes, never edited); and any param whose
    canonical value equals the gate library's default for that gate, kind and
    name. Each kept param is written `l:<esc(name)>=<esc(value)>` (lparam) or
    `g:…` (gparam), where the value is first trimmed of ASCII whitespace and, if
    it is a decimal number (`[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?`), replaced
    by `#` + `milli(value)`; the params are de-duplicated and sorted.
- Each wire: the set of its connections over all its segments, each written
  `<T of the gate>.<esc(pin)>` (a connection to a uuid with no gate on the page
  is `?<esc(uuid)>.<esc(pin)>`), de-duplicated and sorted, as the line
  `W <page> <endpoint> <endpoint>…`. Wires with no connection are left out.
  Wire ids, segments, routing, junction points and buses' extra ids are
  ignored.

- The drawing and the notes (the website's `docs/DRAWING-NOTES.md` §3.6), only
  when there is something to say, so a circuit without them keeps its text and
  hash: a line per stroke on a page with a readable drawing,
  `D <page> <esc(tool)> <esc(color)> <width in thousandths> <x0>,<y0>,<x1>,<y1>,… [p:<pressure>]`
  (points in absolute centi-units), and `N <esc(notes)>` when the notes hold
  something other than space, tab and LF. A drawing of an unknown version and
  the show/hide flag add nothing (showing or hiding is runtime-only). Vectors:
  `format/tests/fixtures/drawing/structure.json`, checked by
  `mac/Tools/ink_check`.

`esc(s)` = the UTF-8 bytes of `s`, each byte outside `[A-Za-z0-9_.:-]` written
`%XX` (upper-case hex). All lines are sorted (they are ASCII, so byte order),
each ends with LF, and the text is `"cedarlogic-structure/1\n"` followed by the
lines.

**Gate defaults** come from the gate library each client already has: the C++
core's library (compiled from `cl_gatedefs.xml`), the site's
`public/assets/engine/gatedefs.json` (`lp`/`gp`) on the web, and
`ref/gate_defaults.json` (extracted from that file) in `ref/`. A gate type a
client doesn't know has no defaults (all its params are kept). If two libraries
ever disagreed on a default, the cost is a spurious "changed" or conflict
version for that gate type, never lost data; the vectors include gates with
defaults that matter (a toggle, an LED, a register, a label).

A default added to the library later must not change the digest of files
written before it in clients that don't have it yet. So a parameter added with
a default is written only when it differs from that default: the clock's
`MANUAL` ("Only on Step Clock", library default `false`) is saved only as
`"true"`, by the apps and by CedarLogic Online alike. A running clock's file is
then byte for byte what older versions wrote, and an older client (no `MANUAL`
in its library) keeps `l:MANUAL=true` exactly as a newer one does. An older
app opens a manual clock as a running one, keeps the parameter and saves it
again.

What it means: moving a gate, adding or removing one, changing a parameter a
person sets (label text, input count, a register's max count) or a connection
changes the structure; flipping a switch, a register counting, the app version,
the file format, page names, wire routing and view state don't.

### 2.5 Local storage

**Apps (Mac, Linux, Windows)** keep the library exactly as today — one folder
per circuit in the library root (`~/Library/Application Support/CedarLogic/Library`,
`~/.local/share/CedarLogic/Library`, `%APPDATA%\CedarLogic\Library`) with
`name.txt`, `circuit.cdl`, `versions/<yyyyMMdd-HHmmss>.cdl` (+ optional
`versions/<stamp>.txt` notes), deleted circuits in `.Trash/`. The wx app shares
that folder and knows nothing of sync; that is fine, because changes are found
by hashing, not by events.

Sync's own files live in a **per-machine** folder outside the library (the
Windows library is in the *roaming* AppData; a lab PC's roaming profile must
not carry another PC's sync state or secret):

| | Sync folder (mode 0700) |
|---|---|
| Mac | `~/Library/Application Support/CedarLogic/Sync/` |
| Linux | `$XDG_DATA_HOME/CedarLogic/Sync/` (`g_get_user_data_dir()`) |
| Windows | `%LOCALAPPDATA%\CedarLogic\Sync\` |

Files in it:

- `secret` — the 28-symbol code, mode 0600. Windows: `secret.dpapi`, the code
  protected with `CryptProtectData` (no UI, current user). Not the Keychain on
  the Mac: the app is ad-hoc signed, so every update would make the Keychain ask
  for permission again.
- `state.json` — written atomically (temp file + rename) after every applied
  record and every write answered:

```json
{
  "v": 2,
  "spaceId": "9d8d8b91b2ab7698e8b8e2b01513d9eb",
  "epoch": "<32 hex: the space document's epoch>",
  "libraryId": "c3a9…(32 hex)",
  "libraryGen": 41,
  "deviceId": "5f0c2a9e7d3b41c8a6e2f1b0c9d8e7a6",
  "deviceName": "Levi’s MacBook Air",
  "offset": -1250,
  "cursor": 57,
  "purgedSeq": 0,
  "lastSyncAt": 1759575600123,
  "joining": false,
  "records": {
    "3b241101-e2bb-4255-8caf-4136c566a962": { "folder": "20261004-101010-48213", "ver": 3,
      "n": "<nameHash>", "c": "<cdlHash>", "st": "<structureHash>", "sent": null }
  },
  "seen":     { "3b241101-…": [3, "<h>", "20261004-101010-48213"] },
  "force":    [],
  "refetch":  [],
  "device":   { "id": "<uuid of this device's record>", "ver": 4, "at": 1759575960000 },
  "lazySince": null,
  "hashCache": { "20261004-101010-48213": { "cdlMtimeNs": "1759575600123456789", "cdlSize": 4211, "cdlFileId": "…",
                 "nameMtimeNs": "…", "nameSize": 11, "checkedAt": 1759575700000,
                 "n": "…", "c": "…", "st": "…", "ch": "<contentHash>" } },
  "tooBig":     { "<recordId>": "<contentHash that was too big>" },
  "unreadable": { "<recordId>": [4, "newer"] }
}
```

  `n`/`c`/`st` are the *base*: the hashes of the server version this circuit
  was last in step with (`""` = unknown). `sent` is `{ver, n, c, st}` or
  `{ver, deleted: true}` for a write whose answer hasn't come back, written
  **before** the request goes out. `seen[id] = [ver, h, folder]` is the highest
  version this device has verified for the id (kept after the record is
  unmapped). `force`: ids whose local copy must be sent even if it matches the
  base. `refetch`: ids to fetch even if `seen` says they were handled.
  `offset` = server time − this device's time (§4.6).
- `lock` — held (`flock` LOCK_EX|LOCK_NB on Mac/Linux, `LockFileEx` on
  Windows) while the engine runs, so two copies of the app (an AppImage and the
  .deb) never sync the same library at once. The second says "Syncing in
  another CedarLogic window".

The library itself gets two hidden files:

- `<library>/.sync-library-id` (32 hex, made once);
- `<library>/.sync-library-gen` (a decimal integer). After every cycle that
  changed the library or the sync state, the engine writes `gen + 1` to both this
  file and `state.json`'s `libraryGen`.

If the id differs from `libraryId`, or the generation differs from `libraryGen`
(the library, or the sync folder, was restored from a backup, replaced, or
another folder is in its place), the engine MUST NOT treat missing folders as
deletions: it re-joins with hints (§4.12).

**Web (CedarLogic Online and the `/app/` phone app)**: IndexedDB database
`cl-library`, version 1 (the existing `cl-app` database for file handles stays
as it is):

| Store | Key | Value |
|---|---|---|
| `circuits` | `id` (keyPath) | `{ id: uuid, name, cdl: string, snap: object\|null, rev, createdAt, modifiedAt, openedAt, gates, from, hashes: {n, c, st, ch}\|null }` — `snap` is CedarLogic Online's own snapshot (`app.snapshot()`), kept after an edit here so reopening is lossless; `cdl` is always current (`clToCdl(snap)` written with every save, debounced 1 s); a circuit that came from sync or a file has `snap: null` until edited. `rev` rises with every write to the record (§4.13). `from` = the device name of the last remote change applied (for copies). `hashes` caches the §2.3 hashes for this `cdl`. |
| `trash` | `id` | deleted circuits, `{…circuit, deletedAt}`, kept 30 days, never synced |
| `syncrecs` | `rid` (keyPath) | `{ rid, local: circuitId, ver, n, c, st, sent }` — same meaning as the apps' `records` |
| `meta` | string keys | `"sync"` → `{ v:2, enabled, code, spaceId, epoch, deviceId, deviceName, offset, cursor, purgedSeq, lastSyncAt, joining, seen, force, refetch, device, lazySince, tooBig, unreadable }`; `"current"` → the open circuit's id |

The web client asks for `navigator.storage.persist()` when sync is turned on or
the library first gets a second circuit. Safari can still clear a website's
storage after 7 days without a visit (not a Home Screen app's): the UI says so
(§5.4) — with sync on, the circuits are safe in the synced copy, but the code
goes with the storage.

### 2.6 What the server can see

Per space: that it exists, when it was created and last used, its `epoch`, how
many records it has, each record's random id, version, size (of the compressed,
encrypted envelope), whether the writer called it a tombstone or a device record,
the time of each change, and the IP addresses requests come from (used for rate
limits, keyed through an HMAC with a server secret). It cannot see names,
contents, device names, or which circuits are which. See §8 for what that does
and doesn't reveal.

---

## 3. Server API (Netlify Functions)

### 3.1 Files

- `netlify/lib/sync.mjs` — all the logic, exporting `handle(req, context, deps)`
  where `deps = { store, misc, now, env, ip }` so tests and the mock server run it
  on the in-memory stub (`netlify/lib/memory-blobs.mjs`).
- `netlify/functions/sync.mjs` — `export const config = { path: "/api/sync/*", rateLimit: {…} }` (§3.5);
  `export default (req, context) => handle(req, context, { store: getStore({ name: storeName("sync", context), consistency: "strong" }), misc: getStore({ name: storeName("sync-misc", context), consistency: "strong" }), now: Date.now, env: process.env, ip: context.ip })`.
  `storeName(base, context)` = `base` when `context.deploy.context === "production"`,
  else `base + "-preview"` (deploy previews and branch deploys) or `base + "-dev"`
  (`netlify dev`): previews and local runs never touch the real synced copies.
  Like `feedback.mjs`: every thrown error becomes `500 {"error":"server_error"}`,
  never a stack.
- `netlify/functions/sync-cleanup.mjs` — `export const config = { schedule: "@daily" }` (§3.10).
- `scripts/sync-mock-server.mjs` — a plain Node HTTP server (no Netlify needed)
  serving `handle()` on the memory stub at `http://localhost:8787/api/sync/v1/…`,
  with test-only controls under `/__mock/` (§10, package S). Every other package's
  integration tests run against it.
- Reuse from `netlify/lib/common.mjs`: `json`, `sha256`, `sameSecret`, `env`, and
  the counter pattern (`allow()`), extended: `allowIn(store, key, limit,
  windowSec, {cost, failOpen})`, keyed by `rateKey(ip)` (§3.5). Fix the same
  Blobs trap there (§3.7): `allow()` and `changeItem()` count a write as done only
  when `modified && etag`.

### 3.2 Requests

Base URL `https://cedarlogic.netlify.app/api/sync/v1`. Apps read an override
from the environment variable `CL_SYNC_URL` (as `CL_FEEDBACK_URL` today) for
testing against the mock server, `netlify dev` or a deploy preview; `http://` is
allowed only for `localhost` / `127.0.0.1`.

Every request carries:

| Header | Value |
|---|---|
| `Authorization` | `Bearer <authToken>` (all endpoints except `/health`) |
| `x-cedarlogic-client` | `mac/0.3.7+801`, `linux/…`, `windows/…`, `web/<build>` — logged nowhere, for support only |
| `x-cedarlogic-key` | apps only: the same key the apps send to `/api/feedback` (`APP_KEY`) |
| `x-cedarlogic-delete` | `DELETE /spaces/{space}` only: the `deleteToken` |
| `content-type` | `application/json` when there's a body |

Bodies are JSON (≤ 4,000,000 bytes; larger → `413 too_large`; Netlify's own
request limit is 6 MB). Every response is JSON with `cache-control: no-store`
and a `Date` header (the function sets it from `deps.now()`; clients use it for
their clock offset, §4.6); errors are
`{"error":"<code>","message":"<a sentence a person can read>"}`, plus
`retryAfter` (seconds) with 429/503 and a `Retry-After` header.

**Who may call** (a speed bump, not security — the security is the token and
the quotas):

```
origin = request "Origin" header; site = "Sec-Fetch-Site" header
if origin present:   allowed only if origin == "https://cedarlogic.netlify.app" or in env SYNC_ORIGINS (comma list) → else 403 forbidden
else if site present: allowed only if site is "same-origin" → else 403 forbidden
else (an app):        if env APP_KEY is set, x-cedarlogic-key must equal it (sameSecret) → else 403 forbidden
```

The web app is same-origin, so **no CORS** is needed. For origins in
`SYNC_ORIGINS` only (deploy previews, `http://localhost:8888`; empty in
production), the function answers `OPTIONS` with 204 and sends
`Access-Control-Allow-Origin: <origin>`, `Access-Control-Allow-Methods: GET, PUT, POST, DELETE`,
`Access-Control-Allow-Headers: authorization, content-type, x-cedarlogic-client, x-cedarlogic-delete`,
`Access-Control-Expose-Headers: date, retry-after`, `Vary: Origin`. Any other
origin gets no CORS headers. (Browsers that send neither `Origin` on GETs nor
`Sec-Fetch-Site` — Safari before 16.4 — can't sync; they also lack the
`CompressionStream("deflate-raw")` share links already need.)

### 3.3 Endpoints

`{space}` = 32 lowercase hex; `{id}` = lowercase UUID v4 (regex
`^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$`).
Anything else → `404 not_found`.

| Method | Path | Does | Success |
|---|---|---|---|
| GET | `/health` | no auth; `{ok:true, protocol:1, limits}` | 200 |
| PUT | `/spaces/{space}` | create the space (first device), re-create a lost one, or confirm it (idempotent) | 201 `{created:true,…}` / 200 `{created:false,…}` |
| GET | `/spaces/{space}` | status | 200 |
| DELETE | `/spaces/{space}` | delete the synced copy (needs `x-cedarlogic-delete`); the id answers 410 forever | 200 `{deleted:true}` |
| GET | `/spaces/{space}/changes?since=<seq>&limit=<n>` | entries changed after `since` (metadata only) | 200 |
| POST | `/spaces/{space}/fetch` | body `{ids:[≤ 50]}` → records with data (tombstones and device records too) | 200 |
| POST | `/spaces/{space}/write` | body `{writes:[≤ 50 items]}` → one result per item | 200 |

`limits` everywhere =
`{"maxRecordBytes":524288,"maxSmallRecordBytes":4096,"maxRecords":1000,"maxDevices":20,"maxBytes":10485760,"maxEntries":5000,"maxFetchIds":50,"maxWriteItems":50,"pollSeconds":600,"pairSeconds":600}`
(`pairSeconds`: §11.5).

**Space status** (PUT and GET `/spaces/{space}`):
`{ "created": bool (PUT only), "epoch": "<32 hex>", "seq": 57, "purgedSeq": 0, "count": 12, "bytes": 48211, "devices": 3, "createdAt": ms, "activeAt": ms, "limits": {…} }`.

**Entry** (in lists and write results):
`{ "id": "…", "ver": 3, "seq": 57, "size": 2048, "updatedAt": ms, "deleted": false, "h": "<32 hex>" }`;
a **record** is an entry plus `"data": "<base64url>"`.

#### `PUT /spaces/{space}` — create, re-create or confirm

Body `{"deleteHash": "<64 hex>"}` (required to create; ignored otherwise).

1. A `gone/<space>` marker → `410 space_deleted` or `410 space_expired`.
2. `a/<space>` (the auth record) exists with another `authHash` → `401 wrong_code`.
3. `a/<space>` missing: new space. Rate check `create/<ipkey>` (50 per IP per
   day), the site's daily creation brake (env `SYNC_NEW_SPACES_PER_DAY`, default
   5,000) and the site byte budget (§3.5) → `503 sync_busy` when over. Write
   `a/<space>` = `{authHash, deleteHash, createdAt}` with `onlyIfNew` (if that
   loses, re-read and go to step 2).
4. `s/<space>` exists → `200 {created:false, …status}` (no write, except
   `activeAt`, §3.8).
5. `s/<space>` missing (a new space, or one whose document was lost): write a new
   space document with a fresh random `epoch` (16 bytes, hex) and `onlyIfNew` →
   `201 {created:true, …status}`.

**Turn On Sync** uses PUT with a brand-new code (must be 201; on 200 —
practically impossible — make a new secret). A synced device that meets
`404 no_space` uses PUT with its own code to re-create a lost space (§4.11).
**Link This Device** uses GET first and stops on 404 — a code with an undetected
typo (1 in 4,096) must not quietly create an empty new space.

#### `DELETE /spaces/{space}` — delete the synced copy

Requires `Authorization` and `x-cedarlogic-delete: <deleteToken>` whose SHA-256
equals the stored `deleteHash` (else `403 wrong_delete_token`). Steps: set
`deleted: true` in `a/<space>` (every later request now gets 410), write
`gone/<space>` `{at, reason:"deleted"}` (kept forever, a few bytes), write
`todo/<space>` (the cleanup's list of unfinished deletions), delete `s/<space>`,
then every `r/<space>/…` blob (in batches of 25, as time allows; the cleanup
finishes), then `a/<space>` and `todo/<space>` → `200 {deleted:true}`.
A second call gets 410, which the client treats as done. Rate: 5 per space per day.

#### `GET /spaces/{space}/changes?since=S&limit=L`

`S` ≥ 0 (default 0), `L` 1–1000 (default 1000). Returns
`{ "epoch": E, "seq": <space seq now>, "purgedSeq": P, "entries": [entries with seq > S, by seq ascending, at most L], "more": bool, "next": <seq of the last entry if more, else seq>, "pollSeconds": 600 }`.
Clients call again with `since=next` while `more`. Tombstones and device records
are included.

#### `POST /spaces/{space}/fetch`

Body `{ "ids": [≤ 50 ids] }` (duplicates ignored). Returns
`{ "records": [records], "missing": [ids not in the space], "deferred": [ids not sent this time] }`.
The server reads the blobs 16 at a time and adds records in the order asked
until the total `data` length would pass 4,000,000 characters (always at least
one) or ~6 s have passed; the rest go in `deferred` and the client asks again.
Bytes served count against the space's daily budget (§3.5).

#### `POST /spaces/{space}/write`

Body `{ "writes": [ item, … ] }`, 1–50 items, total `data` ≤ 4,000,000
characters. An item:

```json
{ "id": "<uuid>", "base": 3, "ver": 4, "data": "<base64url envelope>", "deleted": false, "device": false }
```

`base` = the version this change is based on (**0** = "I believe there is no
such record"); `ver` = the new version (> `base`); `deleted: true` for a sealed
tombstone; `device: true` for a device record. Answer:
`200 { "results": [ one per item, same order ], "seq": <space seq after> }`,
each result `{ "id", "status", "entry" }` (200/201), `{ "id", "status": 412, "error": "conflict", "current": entry | null }`,
or `{ "id", "status", "error" }`. Items are decided one after another, against
the document as the earlier items left it. Per item:

1. Validate: `id` matches the regex; `base` and `ver` integers with
   `0 ≤ base < ver ≤ 2⁵³−1`; `data` is base64url decoding to ≥ 30 bytes with byte
   0 = 1 and byte 1 ∈ {0, 1} → else `400 bad_request`. Size: > 524,288 bytes
   (or > 4,096 for `deleted`/`device`) → `413 record_too_large`.
2. `h` = first 32 hex of SHA-256(envelope bytes).
3. **Replay**: the record exists with `ver == item.ver` and `h == item's h` →
   `200` with the entry (the same write arriving twice).
4. **Precondition**: `cur` = the record's current ver, or, if the id was purged,
   its ghost ver (§3.6), else 0. `base ≠ cur` → `412 conflict` with `current` =
   the entry, or for a ghost `{id, ver, seq:0, size:0, updatedAt:0, deleted:true, h:"", purged:true}`,
   or `null`.
5. **Quotas**, for an item that adds a live record or makes one bigger: live
   circuits (records neither deleted nor device) ≤ 1,000 and their bytes
   ≤ 10,485,760; device records ≤ 20; all entries including tombstones ≤ 5,000;
   the site byte budget not exceeded → else `507 space_full` (with `{count,
   bytes}`) or `507 site_full`. A write that doesn't grow anything (an edit that
   shrinks a circuit, a tombstone) is always accepted, so a full space can still
   be edited and emptied.
6. Apply: `seq += 1`; the record becomes `{ver, seq, size, at: now, h, b: <blob
   name>, del?, dev?}`; its ghost, if any, is removed; `count`/`bytes`/`devices`
   change by what the item adds or removes.
7. `201` if there was no record (or only a ghost), `200` otherwise, with the entry.

Before any item is decided, the request is counted against `write/<space>`
(3,000 items per hour) and the client IP's daily bytes written (50 MB) → whole
request `429` when over. All envelopes are stored as blobs first (§3.7), then one
conditional write of the space document applies every accepted item.

### 3.4 Status codes and errors (complete list)

Request-level (the whole request):

| Status | `error` | `message` (shown to people as is, so plain words) | Client does |
|---|---|---|---|
| 400 | `bad_request` | "That request wasn't understood." | bug: log, status error, back off |
| 401 | `wrong_code` | "This code doesn't match the synced circuits." | status error; stop |
| 403 | `forbidden` | "Not from CedarLogic." | status error; stop until restart |
| 403 | `wrong_delete_token` | "That code can't delete the synced copy." | bug |
| 404 | `no_space` | "No circuits are synced with this code." | link: tell the person; a synced device: re-create (§4.11) |
| 404 | `not_found` | "Not here." | bug |
| 405 | `method_not_allowed` | "That can't be done here." | bug |
| 410 | `space_deleted` | "The synced copy was deleted from another device." | stop (§4.11) |
| 410 | `space_expired` | "The synced copy was removed after a year without use." | stop (§4.11) |
| 413 | `too_large` | "That's too much to send at once." | bug (send smaller batches) |
| 429 | `rate_limited` | "Lots of syncing just now. Trying again in a minute." | wait `Retry-After`, ≥ 30 s |
| 500 | `server_error` | "Something went wrong on the website." | back off |
| 503 | `busy` | "Sync is busy. Trying again shortly." | back off (`Retry-After`) |
| 503 | `sync_busy` | "Sync can't take new devices today. Try again tomorrow." | show; don't retry automatically |
| 503 | `sync_paused` | "Sync is resting on the website for today. Your circuits are safe on this device." | back off until `Retry-After` (the next UTC day) |

Item-level (in `results` of a write):

| Status | `error` | Client does |
|---|---|---|
| 200 / 201 | — | done (§4.9) |
| 400 | `bad_request` | bug: log; don't retry the record until its content changes |
| 412 | `conflict` | resolve (§4.9) and retry, at most 3 times per record per cycle |
| 413 | `record_too_large` | mark too big: "Too big to sync (over 512 KB)" (§4.9) |
| 507 | `space_full` | "Your synced circuits are full (1,000 circuits or 10 MB). New circuits stay on this device until you delete some." (§4.9) |
| 507 | `site_full` | "The website's sync storage is full just now. New circuits stay on this device." (§4.9) |

### 3.5 Limits and abuse policy (stated plainly)

Free encrypted storage invites abuse, and the server can't look inside. The
real budget is the site's Netlify plan (function invocations, Blobs operations,
bandwidth) shared with feedback and stats; check its quotas before launch and set
the numbers below from them. So:

- **Caps per space**: 512 KiB per circuit (circuits are KB), 1,000 circuits,
  10 MiB in all, 20 device records, 5,000 entries counting tombstones (at most
  2,000 tombstones; the oldest beyond that are purged early).
- **Site-wide**: a byte budget (env `SYNC_MAX_TOTAL_BYTES`, default 2 GiB): the
  daily cleanup sums every space's `bytes` into `misc:budget`; while over it, new
  spaces and growing writes get `503 sync_busy` / `507 site_full` (edits that
  don't grow and deletes still work). A daily request breaker (env
  `SYNC_MAX_REQUESTS_PER_DAY`, default 100,000): each function instance counts
  its requests in memory and adds them to `day/<yyyymmdd>/req/<instance>` at most
  once a minute, and reads the day's sum (a `list` of that prefix) at most once a
  minute; past the limit every request except `/health` gets `503 sync_paused`
  until 00:00 UTC. `SYNC_CLOSED=1` answers new spaces and every write with
  `503 busy` (reads keep working, so nobody loses access).
- **Before the function runs**: a Netlify rate-limit rule in the function's
  config, `rateLimit: { windowLimit: 300, windowSize: 60, aggregateBy: ["ip", "domain"] }`
  (300 requests a minute per IP — a classroom behind one address stays well
  under it), if the site's plan offers rate limiting; it costs no invocation.
- **Counters in Blobs** (`sync-misc`, fixed windows, the `allow()` pattern), only
  where they matter, all keyed by `ipkey` = first 24 hex of
  `HMAC-SHA256(env SYNC_RATE_PEPPER, ip)` — for IPv6, of the /64 prefix — never a
  plain hash (IPv4 hashes are reversible in seconds):
  - new spaces: 50 per IP per day (a lab of 30 can all turn sync on);
  - failed auth (401/404 no_space): 60 per IP per hour, counted when they
    happen; past it, those requests get 429 instead;
  - per space, **only after the token checks out**: 3,000 written items per
    hour; 200 MB served by `fetch` per day; 5 space deletions per day;
  - per IP: 50 MB of envelopes written per day.
  Reads aren't counted in Blobs (the Netlify rule covers them). A counter's
  conditional write is tried twice; if it still loses (a busy counter), the
  request is let through (fail open) rather than answered 429.
- **App key**: apps send `x-cedarlogic-key`; it's in the app, so it only keeps
  out what wanders by.
- **Expiry** (in the UI and on the website, in these words): "If none of your
  devices syncs for a year, the synced copy is removed. Your devices keep their
  circuits." Also: a space that never got a record and wasn't used for 7 days is
  removed (silently: any device with its code re-creates it); a deleted circuit is
  remembered as a sealed tombstone for 400 days so other devices hear of it.
- **Polling budget**: clients poll at most every `pollSeconds` (server-tunable,
  default 600, minimum 60) and only while in use (§4.3). At 600 s, a person with
  two devices open all day is about 100 requests a day.
- **Logging**: no request bodies, tokens, codes or IPs are logged.

### 3.6 Storage layout (Netlify Blobs)

Store `sync` (per deploy context, §3.1), `consistency: "strong"`:

| Key | Value |
|---|---|
| `a/<space>` | the auth record, small: `{"authHash","deleteHash","createdAt","deleted"?}` — read first on every request |
| `s/<space>` | the space document (JSON, below) |
| `r/<space>/<id>/<ver>-<ms>-<rand8hex>` | one envelope (raw bytes); `<ms>` = the server time it was stored |
| `gone/<space>` | `{"at": ms, "reason": "deleted" \| "expired"}` — kept forever; the id answers 410 |
| `todo/<space>` | a deletion the cleanup must finish |

Store `sync-misc` (per deploy context), `consistency: "strong"`:
`rate/<key>` counters, `day/<yyyymmdd>/creates`, `day/<yyyymmdd>/req/<instance>`,
`budget` (`{bytes, at}`), `cleanup/cursor`.

The space document:

```json
{
  "v": 2,
  "epoch": "9f2c4e1a…(32 hex)",
  "createdAt": 1759572000000,
  "activeAt": 1759575600000,
  "seq": 57,
  "purgedSeq": 0,
  "count": 12,
  "bytes": 48211,
  "devices": 3,
  "recs": {
    "3b241101-e2bb-4255-8caf-4136c566a962": { "ver": 3, "seq": 57, "size": 2048, "at": 1759575600000, "h": "a1b2…(32 hex)", "b": "3-1759575600000-9f2c4e1a" },
    "9b2e7c1a-5d34-4f8e-b1a2-3c4d5e6f7a8b": { "ver": 4, "seq": 51, "size": 214, "at": 1759570000000, "h": "…", "b": "4-1759570000000-1c2d3e4f", "del": true },
    "d072d9d7-722d-472b-aa6b-e355ad92c15c": { "ver": 9, "seq": 55, "size": 183, "at": 1759574000000, "h": "…", "b": "9-1759574000000-77aa0012", "dev": true }
  },
  "ghosts": { "0c1d…": 6 }
}
```

`ghosts`: id → last ver of each purged tombstone (at most 20,000; the oldest
dropped beyond that), so a write to a purged id must name that ver as its base
and gets a higher one — a `(id, ver)` pair is never used twice. At 5,000 entries
the document is ~600 KB; it is read once per authenticated request.

### 3.7 Consistency: every change is one conditional write

All changes to a space go through its space document, written with
`setJSON(key, doc, { onlyIfMatch: etag })` (or `onlyIfNew: true` to create).

**The `@netlify/blobs` trap** (10.7.x, `set`/`setJSON` with a condition): it
returns `{ modified: false }` only for HTTP 412 and `{ modified: true, etag }` for
*every other status* — including a 5xx or 429 still failing after its own
retries, where `etag` is missing. So a conditional write counts as done **only
if `w.modified && w.etag`**; anything else means "unknown": re-read the document
and decide again (the replay rule recognises a write that did land).

```
blobs = {}                                                             // id -> blob name, stored once per request
for attempt in 1..5:
    cur = await store.getWithMetadata("s/"+space, { type: "json" })   // { data, etag } or null
    decide every item against cur.data (§3.3 steps 1–7) → results, changes
    for each accepted item without a blob yet (8 at a time):
        blobs[id] = `${ver}-${now}-${rand8}`; await store.set(`r/${space}/${id}/${blobs[id]}`, bytes)
    if no changes: return results
    apply changes to cur.data
    w = await store.setJSON("s/"+space, cur.data, { onlyIfMatch: cur.etag })
    if (w.modified && w.etag) return results
// lost or unsure five times in a row:
return 503 busy (retryAfter 2)
```

The server **never deletes a blob inline** — not the record's previous blob, not
its own blob when it gives up. Every blob not named by a record's `b` is garbage
that the daily cleanup removes once it is more than an hour old (its name says
when it was stored). This also means a reader that has just read the space
document always finds the blobs it names.

Reads (`changes`, `fetch`) read the space document once and the blobs it names.
A named blob that is missing (only possible after storage loss) makes the id
`deferred` in `fetch`.

### 3.8 Auth and activity

On every request with a space: read `a/<space>`. Missing → read `gone/<space>`:
410 with its reason if present, else 404 `no_space` (counted as a failed auth).
`deleted: true` → 410 `space_deleted`. Compare `sha256hex(token)` with
`authHash` via `sameSecret` → 401 `wrong_code` (counted). Only now count the
per-space limits and read `s/<space>` (missing → 404 `no_space`; the token
holder may PUT to re-create it). Update `activeAt` only when it is more than
24 hours old (a conditional write whose failure is ignored), so reads don't
contend with writes.

### 3.9 Idempotency, in one place

- `PUT space`: idempotent (200 the second time).
- `write` item: a lost answer followed by the same request is answered 200 by
  the replay rule. A lost answer followed by a *different* write of the same
  record gets 412; the client recognises its own write (`sent`, §4.6) when it
  pulls.
- `DELETE space`: a second call gets 410, which the client treats as done.

### 3.10 The daily cleanup (`sync-cleanup.mjs`)

Walks `s/` with `list({ prefix: "s/", paginate: true })`, starting from
`cleanup/cursor`, stopping after 20 s and saving the cursor. For each space:

1. `seq == 0` (never got a record) and `activeAt` older than 7 days → delete the
   document and `a/<space>`, no marker (the same code can make it again).
2. `activeAt` older than 365 days → write `gone/<space>` `{reason:"expired"}`
   and `todo/<space>`, then delete the document, `a/<space>`, every blob.
3. Tombstones whose `at` is older than 400 days, and the oldest tombstones beyond
   2,000 → move each to `ghosts` (id → ver), remove from `recs`,
   `purgedSeq = max(purgedSeq, their seq)` (conditional write; retry next day if
   it loses).
4. Blobs under `r/<space>/` whose name isn't a record's `b` and whose `<ms>` is
   more than an hour old → delete.
5. Add the space's `bytes` to the day's total.

Then: finish every `todo/` deletion; write `budget`; delete `rate/` counters and
`day/` keys from past windows.

### 3.11 Server tests (`scripts/test_sync_server.mjs`)

On the memory stub (`netlify/lib/memory-blobs.mjs`: `get`, `getWithMetadata`,
`set`, `setJSON` with `onlyIfMatch`/`onlyIfNew` returning `{modified, etag}`,
`delete`, `list`, plus fault injection — §10 S): every row of §3.4 at least once;
the replay rule; two concurrent writes of the same `ver` with different data
(exactly one 2xx, one 412, both blobs stored, the loser's collected by the
cleanup an hour later); **the Blobs trap**: a conditional write that answers
`{modified: true}` without an etag is re-read, never reported as success, and
deletes nothing; quotas at the edge (1,000th circuit accepted, 1,001st 507, a
same-size update in a full space accepted, device records counted apart);
ghosts (a write with base 0 to a purged id gets 412 `purged`, then succeeds with
the ghost's ver as base); `changes` paging (`limit=1`); fetch's 4 MB / 50-id
`deferred` split; tombstone purge raising `purgedSeq`; a deleted space answering
410 to every endpoint and refusing PUT; an expired one likewise; an empty space
removed after 7 idle days and re-creatable; a lost `s/` re-created with a new
epoch; DELETE without or with a wrong delete token → 403; the gate
(Origin/Sec-Fetch-Site/key) table; the per-context store names; rate counters
keyed by HMAC and /64, failing open under contention; the request breaker.
Then the reference scenarios run against the mock server over HTTP
(`ref/sim_http.py`, §10 S) — the first end-to-end check.

---

## 4. The client sync algorithm (all clients)

The web client implements this in JS, the apps once in the shared C++ engine.
`ref/sim.py` (class `Client`) is the executable version of this section; its
names are used below.

### 4.1 Local sync state

Per space (apps: `state.json`; web: `meta.sync` + `syncrecs`; §2.5):

- `cursor` — the space `seq` up to which every change has been fully applied.
- `purgedSeq`, `epoch` — the server's, as last seen.
- `records[rid] = { local, ver, n, c, st, sent }`:
  - `local` — the circuit here (apps: folder id; web: circuit id).
  - `ver` — the server version this circuit was last in step with; **0** = the
    server has no record of it (a new circuit, or one to send again).
  - `n`, `c`, `st` — `nameHash`, `cdlHash`, `structureHash` at that moment: the
    **base**. `""` = unknown (after a restore). The circuit has changed here
    exactly when its current `(nameHash, cdlHash)` ≠ `(n, c)`.
  - `sent` — what a write whose answer hasn't come back carried:
    `{ver, n, c, st}` or `{ver, deleted:true}`, else `null`. Saved **before** the
    request goes out.
- `seen[rid] = [ver, h, local]` — the highest version this device has verified
  for the id and its envelope hash; kept after the record is unmapped. This is
  the high-water mark that stops a server from serving an older copy.
- `force` — ids whose circuit must be sent even if it matches the base (the
  server lost it, has it older, or has it damaged). `refetch` — ids to fetch even
  though `seen` says they were handled (Bring Them Back, a re-join).
- `tooBig[rid] = contentHash`, `unreadable[rid] = [ver, "newer"|"damaged"]`.
- `joining` — true from Turn On / Link / a re-join until a pull completes (§4.8).
- `device = {id, ver, at}` — this device's own device record.
- `offset` — server time − this device's time, from the last answer's `Date`.
- `lazySince` — when the oldest unsent runtime-only change was noticed (§4.3).
- `deviceId`, `deviceName`, `libraryId`/`libraryGen` (apps), `lastSyncAt`.

A circuit with no `records` entry is "not synced yet". A `records` entry whose
`local` no longer exists is "deleted here".

### 4.2 Finding local changes

No change events are trusted: each cycle compares every circuit's current
hashes with its base. Apps keep a hash cache per folder (`hashCache`, §2.5) keyed
by `circuit.cdl`'s mtime **in nanoseconds** (or the platform's finest: macOS
`st_mtimespec`, Linux `st_mtim`, Windows `FILETIME` 100 ns), size and file id
(inode / `FileId`), and `name.txt`'s mtime and size. An entry recorded less than
2 s after the file's mtime is not trusted (git's "racily clean" rule: a second
same-size save in the same second could follow) and is re-hashed next cycle. So
a cycle over 1,000 untouched circuits reads no files, and changes made by the wx
app or by hand in the library folder are still found.

What counts as a change: any byte of the (normalized) name or cdl. Toggling a
switch and autosaving is a change, but a **runtime-only** one (name and
structure equal to the base's): it is sent lazily (§4.3), never makes a version,
and never makes a conflict copy (§4.6).

### 4.3 When to sync

One cycle at a time per device (apps: the engine thread + the `lock` file;
web: `navigator.locks.request("cl-sync", …)` across tabs, plus a
`BroadcastChannel("cl-library")` message after every change so other tabs
reload their lists and editors).

| Trigger | Apps | Web / phone app |
|---|---|---|
| Start | 2 s after the first window shows | when the page has loaded |
| Back to the app | app activated or a window focused, if the last attempt was > 60 s ago | `visibilitychange` → visible, if > 30 s ago; `online` event |
| Local change (save, autosave, rename, import, duplicate, delete, restore a version) | 5 s after the last change, but within 60 s of the first unsynced one | 3 s after the last, within 30 s |
| Polling | every `pollSeconds` (600) while the app is frontmost or the person used it in the last 10 minutes; never while idle in the background | every `pollSeconds` while the page is visible |
| Sync Now | at once, ignoring backoff, sending lazy changes too | same |
| Quitting / leaving | §4.12 "Quitting": a final push, ≤ 5 s, sending lazy changes too | `pagehide`: best-effort push with `fetch(…, {keepalive:true})` for envelopes ≤ 60 KB |

**Lazy pushes.** A cycle sends a runtime-only change (switch settings, register
contents) only if it is a *flush* (Sync Now, quitting/`pagehide`, the app going to
the background or 2 minutes without input) or the oldest such change is 10
minutes old (`lazySince`). Names and real edits always go at once. So someone
playing with switches costs one write per circuit per 10 minutes, not one per
minute, and the other devices' windows aren't reloaded every minute.

Failures back off: 30 s, 1 min, 2 min, 5 min, 10 min, then every 15 min, each
±20 % random; a success resets it. `429`/`503` wait at least `Retry-After`.

### 4.4 One cycle

```
cycle(flush):
    if not enabled: return
    status = syncing
    try:
        if apps and (library id or generation ≠ the state's): reJoinWithHints()     # §4.12
        pull()
        push(flush)
        writeDeviceRecordIfDue()                 # in the same batch as push's last items (§4.9)
        apps: libraryGen += 1 (file and state) if anything changed
        status = full if spaceWasFull else synced; lastSyncAt = now
    except NetworkError:             status = offline   (back off)
    except SpaceGone(reason):        stop syncing; status = gone (§4.11)
    except HttpError(e):             status = error(e.message)   (back off)
```

Pull always comes first, so most conflicts are seen before writing; the 412
path (§4.9) covers the rest. State is saved after each record applied and each
write answered, so a crash anywhere loses at most the step in progress (and the
`sent` rule recovers that). Every answer's `Date` header updates `offset`.

### 4.5 Pull

```
pull():
    since = cursor
    loop:                                                        # restarts as a full pull when needed
        entries = []; s = since
        loop over pages:
            page = GET changes?since=s
            if (epoch ≠ "" and page.epoch ≠ epoch) or page.seq < cursor:        # not the copy we knew
                rewound(page.epoch); since = 0; restart
            if since > 0 and page.purgedSeq > since:              # tombstones we never saw were purged
                since = 0; restart                                # (checked on every page: the cleanup may run mid-way)
            entries += page.entries
            if not page.more: break; s = page.next
        break
    epoch = page.epoch; full = (since == 0)
    need = []
    for e in entries:
        sv = seen[e.id]; s = records[e.id]
        if sv and (e.ver < sv.ver or (e.ver == sv.ver and e.h ≠ sv.h and s)):
            wentBack(e); continue                                 # an older (or swapped) copy: ignored (H1)
        if s and s.ver == e.ver: continue                         # in step (often our own write)
        if sv and sv == (e.ver, e.h) and not s and e.id ∉ refetch: continue   # handled (deleted here; device records)
        if unreadable[e.id].ver == e.ver: continue
        need += e
    fetched = POST fetch for need, ≤ 50 ids per call, again for `deferred`
    held = []; incomingDeletes = []
    for e in need (ascending seq):
        r = fetched[e.id]; if r missing or r.ver ≠ e.ver: continue        # changed or purged meanwhile: next cycle
        p = open(recordKey, e.id, e.ver, r.data) and readPayload
            damaged / invalid → damaged(e); continue                      # §4.10
            newer             → unreadable[e.id] = [e.ver, "newer"]; seen[e.id] = (e.ver, e.h); continue
        done = kind circuit → onRemoteUpdate(e, p)
               kind deleted → onRemoteDelete(e, p, incomingDeletes)
               kind device  → remember it for the device list (§5.1); true
        if done: seen[e.id] = (e.ver, e.h, local); save state
        else:    held += e.seq                                            # an open window: next cycle
    if incomingDeletes: incomingDeleteGuard(incomingDeletes)              # §4.7
    if full:
        for rid in records with ver > 0 not among the entries: onRemoteForgotten(rid)
        drop unreadable[rid] and the remembered device records for every rid not among the entries
        if device.id not among the entries: device.ver = 0; device.at = 0  # this push sends it again (a reset copy)
    cursor = (min(held) − 1) if held else page.seq
    purgedSeq = page.purgedSeq
    if not held: joining = false; refetch = []
    save state

wentBack(e):            # the server lists a version lower than one this device verified, or swapped an envelope
    if records[e.id] and its circuit exists: records[e.id].ver = e.ver; force += e.id   # our copy goes back up
    notice once per cycle: "The website sent an older copy of a circuit. This device's copy was sent again."

rewound(newEpoch):      # a new epoch (the document was lost and re-created, or restored), or seq went down
    if epoch ≠ "": notice "The synced copy was reset on the website. Sending your circuits again."
    epoch = newEpoch; cursor = 0; purgedSeq = 0                          # the full pull re-sends what's missing
```

**The cursor rule.** The cursor never passes an entry that wasn't fully
processed: a record held back (a dirty window, §4.12) or whose apply failed
(disk full, permissions — the apply step reports failure) keeps the cursor
before it, and the next pull lists it again. Within an apply, files are written
before `records`/`seen` change, and the state is saved after: a crash in
between finds the files already equal to the remote version, which the next
cycle sees as "same already".

### 4.6 Applying a remote change, and conflicts

**Time.** Stamps are written in server-corrected time: `modifiedAt = local
modification time + offset`, where `offset = Date − local now` from the last
answer (§4.4). When a remote version is applied, the local file time becomes
`p.modifiedAt − offset`. A payload's time is never trusted beyond the moment
the server received it: `effective(p, e) = max(2020-01-01, min(p.modifiedAt,
e.updatedAt + 60 s))`. So a Raspberry Pi that booted a day slow still wins with
its later edit, and a device whose clock says 2030 can't win every conflict.

```
newer(p, e, here) = (effective(p, e), p.deviceId) > (here.modifiedAt + offset, myDeviceId)       # compared as a pair

onRemoteUpdate(e, p):                            # returns false to hold the record for the next cycle
    rid = e.id; ver = e.ver; rN, rC, rSt = nameHash(p.name), cdlHash(p.cdl), structureHash(p.cdl)
    s = records[rid]
    if s and s.sent (not deleted) and s.sent.ver == ver and (s.sent.n, s.sent.c) == (rN, rC):
        s.ver, s.n, s.c, s.st, s.sent = ver, rN, rC, rSt, null; return true     # our own write; its answer was lost
    if s is none:
        if hints[rid] names a local circuit not mapped yet:                     # a re-join (§4.12)
            s = records[rid] = {local: it, ver: 0, n: "", c: "", st: ""}        # base unknown; continue below
        else:
            match = (only while joining, §4.8) an unmapped local circuit with contentHash == contentHash(p),
                    else one with normalizeName equal and structureHash equal              # the join rule
            if match:
                if held(match): return false
                records[rid] = {local: match, ver, n: rN, c: rC, st: rSt}
                if content differs and newer(p, e, match): applyRemote(match, p.name, p.cdl, p)
                return true                                   # (if here is newer, it now counts as changed and is sent)
            records[rid] = {local: createLocal(p), ver, n: rN, c: rC, st: rSt}
            if seen[rid] exists and not joining:
                notice "“{p.name}” was changed on {p.device} after it was deleted here, so it's back."
            return true
    if s.local doesn't exist:                                             # deleted here, changed there: edits win
        s.local = createLocal(p); s.ver, s.n, s.c, s.st, s.sent = ver, rN, rC, rSt, null
        notice "“{p.name}” was changed on {p.device}, so it was kept."; return true
    here = the local circuit; lN, lC = its hashes
    runtimeOnly = lN == rN and structureHash(here) == rSt
    if (lN, lC) ≠ (rN, rC) and held(here, runtimeOnly): return false     # §4.12 (apps)
    bN, bC, bSt = s.n, s.c, s.st                                          # the base
    if s.sent and p.base == (s.sent.n, s.sent.c): bN, bC, bSt = s.sent.n, s.sent.c, s.sent.st
                                                                          # built on our write whose answer was lost
    s.ver, s.n, s.c, s.st, s.sent = ver, rN, rC, rSt, null
    if (lN, lC) == (rN, rC): return true                                  # same already
    if p.base == (lN, lC): applyRemote(here, p.name, p.cdl, p); return true   # built on exactly what is here
    # the name and the circuit are merged separately
    name = p.name   if lN == rN or lN == bN                               # same, or changed only there
           here.name if rN == bN                                          # changed only here
           newer(p, e, here) ? p.name : here.name                         # renamed on both: the newer
    cdl  = p.cdl    if lC == rC or lC == bC                               # same, or changed only there
           here.cdl if rC == bC                                           # changed only here: ours, no conflict
           newer ? p.cdl : here.cdl   if structureHash(here) == rSt       # only runtime state / writer differ: quietly
           p.cdl    if bSt ≠ "" and structureHash(here) == bSt            # only runtime state changed here: theirs, quietly
           here.cdl if bSt ≠ "" and rSt == bSt                            # only runtime state changed there: ours, quietly
           otherwise: changed on both — the newer wins, the other is kept:
               if newer(p, e, here): keepLoser(here.name, here.cdl, here time, myDeviceName); cdl = p.cdl
               else:                 keepLoser(p.name, p.cdl, p.modifiedAt, p.device);      cdl = here.cdl
               notice (below)
    applyRemote(here, name, cdl, p)                                       # no-op if nothing changes
    return true
```

Because the base becomes the remote version, whatever is kept "here" afterwards
counts as changed and the next push writes it over the remote version.

**keepLoser** — nothing is ever lost:

- **Apps**: the loser becomes a version of the circuit (§4.12: stamped with the
  arrival time; its note "From Levi’s iPhone · “Counter” · edited 4 Oct 10:31 ·
  changed on both"). Notice: "“Counter” was changed here and on Levi’s iPhone.
  The newer one is open; the other is in Version History."
- **Web**: a new circuit named `"<loser's name> (from <device>)"` holding the
  loser's cdl. It is a new circuit, so it syncs to every device. Notice:
  "“Counter” was changed here and on Levi’s MacBook. The newer one is open; the
  other is now “Counter (from Levi’s MacBook)”."

**applyRemote(here, name, cdl, p)** — the circuit here becomes (name, cdl),
written verbatim; its time becomes `p.modifiedAt − offset` if anything changed
(apps: §4.12's file rules and versions; web: §4.13).

### 4.7 Deletes

A deletion is a write of a sealed tombstone (`kind:"deleted"`, `base` = the
hashes of the version that was deleted) with `deleted: true`. The server's
`deleted` flag alone means nothing to a client: only a tombstone that decrypts
under this id and ver is a deletion; anything else listed as deleted is damaged
(§4.10) and this device's copy is sent back over it.

| Here | There | Result |
|---|---|---|
| deleted | unchanged | tombstone written (`base` = its version); other devices move it to their Recently Deleted |
| deleted | changed (new ver) | edits win: the circuit comes back here, with a notice (pull runs first; if the tombstone write gets 412, the same via §4.9) |
| unchanged | deleted (tombstone) | moved to the trash here (apps: the library's `.Trash/`, after closing its windows without saving; web: the `trash` store) — recoverable on this device; a notice for every one |
| changed | deleted | edits win: `s.ver = tombstone ver`, kept "changed"; the push writes it back and it returns on every device. Notice: "“ALU” was deleted on Levi’s iPhone, but you'd changed it here, so it was kept." |
| deleted | deleted | forget the record |
| anything | the server no longer lists it (full pull) | **never trashed**: the circuit is sent again (same id, `ver` above the server's ghost) |

```
onRemoteDelete(e, p, incoming):
    s = records[e.id]
    if s is none:
        if hints[e.id] names a local circuit: if its hashes == p.base: trash it quietly (deleted after the backup)
                                              else map it with ver = e.ver (edits win; it is sent back)
        return true
    if s.sent and s.sent.deleted and s.sent.ver == e.ver: delete records[e.id]; return true   # our own delete
    if s.local doesn't exist: delete records[e.id]; return true
    if hashes(local) == (s.n, s.c) or hashes(local) == p.base:
        if held(local): return false
        incoming += (e.id, local, e.ver, p.device)                          # trashed after the guard
    else:
        s.ver = e.ver; notice "…was deleted on {p.device}, but you'd changed it here, so it was kept."
    return true

incomingDeleteGuard(items):
    synced = records whose circuit exists
    if len(items) > 5 and 2 × len(items) > synced:
        ask "Levi’s iPhone deleted 14 of your 20 synced circuits. [Move Them to Recently Deleted] [Keep Them]"
        Keep Them → for each: records[id].ver = its tombstone ver; force += id    # they are sent back everywhere
    otherwise (or after Move): trash each; one notice listing them:
        "Moved to Recently Deleted: “Mux”, “ALU” (deleted on Levi’s iPhone)."

onRemoteForgotten(rid):                          # full pull: the server no longer has a record this device had
    s = records[rid]
    if s.local doesn't exist: delete records[rid]; return
    s.ver = 0; force += rid                       # sent again; the server's ghost makes the write ver higher (412 → base)
```

The apps ask asynchronously: while the question is open, the cycle stops before
trashing and doesn't save the cursor past those entries, so the answer is never
lost. A purged tombstone (older than 400 days) can no longer reach a device that
was away longer than that; such a device sends the circuit again, and it comes
back everywhere — the safe direction.

**Mass-delete guard (here)** (apps and web): if a cycle finds more than 5 synced
circuits *and* more than half of them deleted here at once, it pushes nothing
and asks: "23 synced circuits aren't on this computer any more. [Delete Them
Everywhere] [Bring Them Back]". Bring Them Back removes those mappings, adds the
ids to `refetch` and sets `cursor` to 0, so the next (full) pull recreates them
from the server (quietly). Deletes of 5 or fewer, or of at most half, go ahead
without asking. A library that can't be read at all (missing root, permissions)
stops the cycle with status error — never deletes.

### 4.8 Turning sync on, and linking a device (first-time join)

**Turn On Sync** (first device): new secret → code → `PUT /spaces/{space}` with
`deleteHash` (must be 201; on 200 — practically impossible — make a new secret)
→ save `secret`, a fresh state with `joining: true` → cycle. The pull is empty;
the push uploads every circuit with a new UUID (`base: 0`) and the device record.

**Link This Device** (others), in two steps — nothing is sent until the second:

1. **Preview.** Parse the code (§1.2) → `GET /spaces/{space}`: 404 → "No circuits
   are synced with this code. Check the code, or turn on sync on your other
   device first."; 410 → "That synced copy was deleted." / "…was removed after a
   year without use."; 401 → "This code doesn't match." On 200: `GET changes
   since=0`, `fetch` every live record, decrypt **in memory** (nothing saved,
   nothing written), and summarize: the number of circuits, the device names
   (device records first, then the circuits' `device` fields), a few circuit
   names, the newest edit time.
2. **Confirm** with that summary (§5.1): "This code has 14 circuits from Bob’s
   laptop and Chrome on Android, last changed yesterday. Linking adds your 23
   circuits to them, and anyone with this code can see and change them." With no
   circuits yet: "This code has no circuits yet. Only link with a code you made
   yourself." [Link] → save the secret, a fresh state with `joining: true` →
   cycle (the engine MAY reuse the preview's fetched records for the first pull).
   [Cancel] → nothing was stored anywhere.

The first pull brings in every record. For each, `onRemoteUpdate` with no
`records` entry runs **the join rule**: a local circuit not yet mapped with the
same `contentHash` (identical name and text) is the same circuit — mapped, not
duplicated. Failing that, one with the same name and the same `structureHash`
(the same circuit with switches set differently, saved by another app version,
or saved by Online as XML) is mapped too, and the newer content wins quietly.
Everything else arrives as a new circuit. Then the push uploads the local
circuits that matched nothing. So joining a device that already has circuits
never duplicates identical ones, and keeps everything else from both sides.
(Same name but a different circuit → both kept, both named the same; the person
can rename.)

The join rule applies only while **joining**: from Turn On or Link (or a re-join,
§4.12) until a pull completes. The flag is saved in the state, so a join
interrupted by a quit or a lost connection resumes as a join. After that, a
record new to the device always becomes a new circuit — two empty "Untitled"
circuits made separately on two linked devices stay two circuits.

During a join the status shows progress: "Bringing in 12 of 37 circuits…",
then "Sending 5 circuits…".

### 4.9 Push

```
push(flush):
    spaceFull = false
    if the mass-delete guard (§4.7) asks and the answer is Bring Them Back: push no deletes this cycle
    repeat ≤ 3 times (a record that got 412 is resolved and tried again):
        items = []
        # deleted here (not while joining / re-joining)
        for rid, s in records where s.local doesn't exist:
            if s.ver == 0 or unreadable[rid] is "newer": delete records[rid]; continue
            items += item(rid, s, tombstone{deletedAt: now + offset, device, deviceId, base: (s.n, s.c)}, deleted)
        # new or changed here, oldest first: changed ones before new ones
        for each local circuit c:
            rid = the record mapped to c, else (new) rid = uuid4(); records[rid] = {local: c, ver: 0, n: "", c: "", st: ""}
            s = records[rid]; h = hashes(c)
            if unreadable[rid] is "newer": problem "Saved by a newer CedarLogic: changes here stay on this device"; continue
            if s.ver > 0 and h == (s.n, s.c) and rid ∉ force: continue                       # in step
            if tooBig[rid] == contentHash(c): continue                                         # still too big
            if spaceFull and s.ver == 0 and rid ∉ seen: continue                               # no room for new ones
            if runtime-only (s.ver > 0, nameHash == s.n, structureHash == s.st, rid ∉ force)
               and not flush and now − lazySince < 10 min: continue                            # later (§4.3)
            items += item(rid, s, circuit payload{name, cdl, createdAt, modifiedAt: c's time + offset,
                                                 device, deviceId, base: (s.n, s.c) if s.n ≠ ""}, circuit)
        if the device record is due (none yet, a day old, or renamed): items += its write (device: true)
        send items in batches (≤ 50 items, ≤ 3,000,000 characters of data); for each result:
            200/201 → seen[rid] = (entry.ver, entry.h); tombstone: delete records[rid];
                      else s.ver = entry.ver; (s.n, s.c, s.st) = sent; s.sent = null; force −= rid
            413     → tooBig[rid] = contentHash; problem "Too big to sync (over 512 KB)"
            507     → spaceFull = true (status "full"; a device record's 507 is quiet)
            412     → resolve(rid, result.current), then try again
            400     → log; leave the record until its content changes

item(rid, s, payload, kind):
    ver = max(s.ver, seen[rid].ver) + 1                   # never reuses (id, ver)
    env = seal(recordKey, rid, ver, payload, deflate for circuits)
    s.sent = {ver, n, c, st} or {ver, deleted: true}; save state         # before the request goes out
    return {id: rid, base: s.ver, ver, data: b64u(env), deleted?, device?}

resolve(rid, current):
    if current is null or current.purged:                 # the server has no such record (or only its ghost)
        s.ver = current ? current.ver : 0; force += rid; return
    if seen[rid] and (current.ver < seen.ver or (current.ver == seen.ver and current.h ≠ seen.h)):
        wentBack(current); return                         # it went back: ours goes up again
    fetch rid; open → newer: unreadable, stop; damaged: damaged(current); circuit: onRemoteUpdate;
                      deleted: onRemoteDelete (+ the incoming guard)
```

**Payload for a local circuit**: `{v:1, kind:"circuit", name, cdl, createdAt,
modifiedAt, device: deviceName, deviceId, base}` (§2.2). Apps read `circuit.cdl`
and `name.txt` through `fileText` (§2.3) inside the snapshot step (§4.12).

**The device record** is this device's `{kind:"device", device, deviceId,
client, lastSyncAt}` under its own random id (state `device.id`). It is written
at Turn On / Link, when the device's name changes, and at most once a day after
that; it is the last item of a batch (a full space delays it, quietly). Turn Off
writes a tombstone for it. Other devices show the list in Settings › Sync (§5.1).
A `412` for it (written over, or gone from a reset copy: `current` null) takes the
base the answer gives (`current.ver`, or 0) and goes again within the hour, not a
day later; a full pull that doesn't list it sends it again in the same cycle
(§4.5), so after a reset every device is listed again at once.

### 4.10 Records that can't be read

Two cases, decided when a record is opened:

- **Damaged** — the envelope is malformed, the tag doesn't match, the inflate
  fails, or the payload is *invalid* (§2.2). Nothing local changes. If this device
  has the circuit (the record is mapped and its circuit exists), its copy is sent
  over the damaged one (`force`, `base` = the damaged ver) with the notice "“T”
  was damaged in the synced copy; this device's copy was sent again." A damaged
  record the change list calls deleted (a tombstone) of a circuit this device
  doesn't have only goes into `seen`: there's nothing to open and nothing to
  count. Otherwise `unreadable[rid] = [ver, "damaged"]`, it isn't fetched again
  until its ver changes, and Your Circuits says "1 synced circuit is damaged and
  can't be opened." Status stays "synced". The count drops when a readable
  version arrives, or when a full pull no longer lists the record (§4.5).
- **Newer** — the payload's `v` is above 1 or its `kind` unknown: written by a
  newer CedarLogic with the key, so authentic. `unreadable[rid] = [ver, "newer"]`,
  `seen` updated; it isn't fetched again until its ver changes; the device never
  writes over it — pushes skip that record (no write attempts, no 412 loop), and
  the circuit, if this device has one, shows "Saved by a newer CedarLogic:
  changes here stay on this device. Update to sync it."

### 4.11 Turning off, the synced copy, losing the code

- **Turn Off Sync** (one device): write a tombstone for this device's record
  (best effort), stop the engine, delete `secret` and `state.json` (web:
  `meta.sync`, `syncrecs`); circuits stay. Option **Remove synced circuits from
  this device**: every circuit with a `records` entry goes to the trash first
  (apps: `.Trash/`, web: the `trash` store). Other devices are not affected.
- **Delete Synced Copy** (Settings, behind a confirmation): `DELETE
  /spaces/{space}` with the delete token, then Turn Off here. Other devices get
  `410 space_deleted` at their next sync: they stop, keep their circuits, and say
  "Sync was turned off from another device, and the synced copy was deleted. Your
  circuits here are kept. If you started over with a new code, link this device
  again with it." (with [Turn On Sync] and [I Have a Code…]).
- **Start Over with a New Code** (Settings; for a code that got out): Delete
  Synced Copy, then Turn On Sync (a new code; this device's circuits upload),
  then the code sheet. Other devices have to link again with the new code; the
  sheet says so.
- **`404 no_space` on a synced device** (the space was never used for 7 days,
  or its document was lost): `PUT /spaces/{space}` with the same code and
  `deleteHash`, quietly. The new epoch makes every device re-send what the server
  lacks (§4.5 `rewound`). If the PUT answers 410, stop as below.
- **`410 space_expired`**: stop; "The synced copy was removed after a year
  without use. Your circuits here are kept. Turn on sync again to make a new
  code." (The id can't be used again.)
- **Show the code again**: Settings › Sync › Show Code, any time while sync is
  on (read from `secret`). Copy Code, Copy Link, and the QR code.
- **Losing the code**: there's no recovery — the server can't help (it has only
  hashes). While any linked device exists, its Settings shows the code. The Turn
  On and Show Code screens say: "Keep this code somewhere safe. If you lose every
  device that has it and this code, nobody can open the synced circuits — not
  even CedarLogic's website."

### 4.12 Apps: files, open windows and versions

The engine thread does network and crypto; **everything that touches the
library or a window runs on the UI thread** through the `onMain` hook, in short
steps, so a sync never races the app's own autosave:

1. **Snapshot step** (UI thread): `flushOpen(done)` — every open library circuit
   with unsaved changes is saved now (the apps already autosave every few
   seconds; this just doesn't wait). It finishes through `done`, which the engine
   waits for on its own thread (≤ 10 s): on the Mac, SwiftUI `DocumentGroup` saves
   are asynchronous and the main thread must not block. Then list folders, read
   names and the cdl of changed ones (through `fileText`), compute hashes (with
   the cache).
2. **Network** (engine thread): pull requests, decrypt.
3. **Apply step** (UI thread), per incoming record:
   - **Re-check the window** (`host.windowState(folder)` → open, dirty, last
     input): a window with unsaved edits → the record is **held** (not applied;
     the cursor stays before it; another cycle in 5 s). A *runtime-only* change to
     a window with input in the last 60 s → held too. Nothing is ever reverted or
     closed with edits in it.
   - Re-check the local hashes *now* (the files may have changed since step 1),
     then the rules of §4.6/§4.7.
   - `applyRemote(folder, name, cdl, p)`:
     1. If `structureHash(current cdl) ≠ structureHash(new cdl)`: keep the current
        `circuit.cdl` as a version (note "Before the change from <device>";
        skipped if the newest version already has that structure).
     2. Write `circuit.cdl` = the new cdl verbatim and `name.txt` = the name
        (atomic: temp + rename; on the Mac through `NSFileCoordinator`, so an open
        `NSDocument` isn't surprised), set `circuit.cdl`'s mtime to
        `p.modifiedAt − offset`.
     3. If the structure changed: keep the new content as a version too (note
        "From <device> · edited <time>"), so Version History shows the moment it
        arrived; delete `versions/.pending.cdl` (its content was just kept).
     4. `host.circuitReplaced(folder, p.device)`: every window showing the circuit
        reloads it from disk in place (same page, same view), with a quiet note
        "Updated from Levi’s iPhone". Undo history is cleared for that window (the
        apps' revert behaviour). (Mac: `revert(toContentsOf:)` builds a new
        `CircuitDocument`, so the page and camera are kept in a per-URL store
        outside the document and restored.)
   - `createLocal(p)`: a new folder named like the apps' own
     (`yyyyMMdd-HHmmss-NNNNN`), `name.txt`, `circuit.cdl`, `versions/` with the
     first version, mtime = `p.modifiedAt − offset`.
   - `moveToTrash(folder)`: `host.closeCircuit(folder)` (only clean windows reach
     here; they close without saving), then the folder moves into `.Trash/`
     (`<id>`, `<id> 2`, … as the apps do).
   - Files are written first, then `records`/`seen`, then the state is saved.
     A failed write (disk full, permissions) holds the record (cursor rule) and
     sets status error.
   - `host.libraryChanged()` once per apply step: Your Circuits lists refresh.
4. **Push** (engine thread), then an **update step** (UI thread) that saves
   the new `ver`/hashes in the state.

**Versions made by sync** (keepLoser, before/after an arriving change) are
stamped with the **arrival time** — `versions/<yyyyMMdd-HHmmss>.cdl` for the
current local time, the next free second if taken — and the file's mtime is set
to that same instant; the original edit time and device go in
`versions/<stamp>.txt`. So every app's version thinning (newest per hour for 1–7
days, per day after; Mac and Windows order by mtime, Linux by the name stamp)
sees them as new and keeps them, and all three apps order them the same way.
Each app's `thin()` also deletes the `.txt` beside a `.cdl` it removes (the wx
app's leaves it; a stray note is harmless). Versions are only made when the
circuit's *structure* changes, so switch flips and renames arriving from
elsewhere don't fill Version History.

**Re-join with hints** (library id or generation differs from the state's —
restored from a backup, replaced, or the sync folder restored instead): `hints =
{rid → folder}` from `records` and `seen`; `records = {}`, `refetch` = every
hinted id, `cursor = 0`, `joining = true`; no deletes are sent until the pull
completes. In the pull, a record whose hinted folder exists is mapped to it with
an **unknown base** (so a folder that differs goes through the conflict rules:
the newer wins and the older is kept as a version — never silently overwritten
either way); a hinted folder that's missing is re-created from the server; a
tombstone whose hinted folder still holds exactly the deleted version (`base`)
moves it to the trash quietly (it was deleted after the backup); unhinted local
circuits go through the join rule. Notice: "Your library was restored from a
backup. Syncing it again; nothing was deleted."

**Quitting.** `applicationShouldTerminate` (Mac, answering `.terminateLater`),
the last window's close (Linux, Windows): the UI thread first saves every open
library circuit with its own save code (Mac: waits for the asynchronous saves),
then calls `engine.quitting(done)`. The engine runs a **push-only** cycle on its
own thread — it reads the library files itself (nothing can edit them now), sends
everything including lazy changes, writes `state.json` from its thread, never
calls `onMain` — and calls `done` within 5 s however far it got. The host then
finishes quitting (Mac: `reply(toApplicationShouldTerminate: true)`;
Linux/Windows: the main loop / message pump it kept running while waiting).

### 4.13 Web specifics

- The library replaces "the one circuit in localStorage". On first load of the
  new version, if `localStorage["cl-online:v1"]` holds a circuit with parts, it
  becomes the first circuit in `cl-library` (its name, or "My circuit") and is
  opened; the localStorage key stays as a backup until the next release.
- **Every write to a circuit is a compare-and-set on `rev`**, inside one
  IndexedDB `readwrite` transaction: read the record, check `rev` is the one the
  writer last saw, write with `rev + 1`. The editor saves on every change,
  debounced 1 s (`snap`, `cdl`, `name`, `modifiedAt`, `gates`); opening a circuit
  MUST NOT save it (if the editor saves right after opening, compare the snapshot
  with the one it opened and skip when equal).
- **A remote change to a circuit** (`applyRemote`) sets `snap: null`, `from:
  <device>`, writes the new `cdl`/`name` with `rev + 1`, and posts
  `{type:"replaced", id}` on the BroadcastChannel. An editor showing that circuit
  with no unsaved edits reloads it from IndexedDB (toast "Updated from Levi’s
  iPhone"). An editor with unsaved edits finds its next save refused (`rev`
  moved): it reloads the stored circuit S, and if S's structure equals its own it
  saves its own content (newest, quietly); otherwise it first saves S as a new
  circuit "<S.name> (from <S.from>)" and then its own content — nothing is lost,
  and sync sends both. The same compare-and-set keeps two tabs editing the same
  circuit from silently overwriting each other (the second tab's refused save
  follows the same rule).
- **A remote delete of the open circuit**: with no unsaved edits, the editor
  switches to the next circuit with the toast "“X” was deleted on Levi’s iPhone.
  It's in Recently Deleted." With unsaved edits, the refused save puts the
  circuit back from the trash with the editor's content (edits win; sync sends
  it back).
- Opening a `.cdl` file (Open, ⌘O, drop, share target, `launchQueue`) or a
  `#c=` share link adds a new circuit to Your Circuits and opens it, as the
  apps do; opening the same file content again (same `contentHash`, made from a
  file within the last day) opens the existing copy instead.
- `pollSeconds`, backoff, notices and statuses as in §4.3–§4.11.
- **Untrusted text** — circuit names, device names, notices, file names — is
  only ever put in the page with `textContent` (or as attribute values through
  the DOM), never into `innerHTML` templates. A strict Content-Security-Policy
  (§5.4) backs this up: the code in IndexedDB, every circuit and the same-origin
  admin API are what an injected script would get.
- `sync-core.js` has no DOM and no IndexedDB: the library is passed in as an
  object with `list()`, `get(id)`, `put(circuit, expectedRev)`, `create(...)`,
  `trash(id)`, `restore(id)`, the sync state with `load()`/`save()`, the gate
  library's defaults, and `fetch`, so `scripts/test_sync_client.mjs` runs it on
  Maps against `netlify/lib/sync.mjs`'s `handle()` in-process and against the
  mock server.

---

## 5. What people see

Plain words, the apps' own styles. The same strings on every platform (⌘ vs
Ctrl as usual). "Sync" is the noun and the verb; never "cloud account".

### 5.1 Settings › Sync (Mac, Linux, Windows)

A sixth page after Shortcuts in each app's Settings window
(`SettingsPage.sync` in `mac/App/SettingsView.swift`; a page in
`linux/App/Settings.cpp` and `windows/App/Settings.cpp`), icon: two circling
arrows (SF Symbol `arrow.triangle.2.circlepath`; Adwaita
`emblem-synchronizing-symbolic`; drawn on Windows). Laid out like the other
pages: label on the left, control and a line of explanation on the right.

**Sync off:**

> **Sync Your Circuits**
> Keep Your Circuits the same on this Mac, your other computers, your phone and
> CedarLogic Online. There's no account: a secret code links your devices, and
> circuits are encrypted on this Mac before they're sent, so only your devices
> can read them.
>
> [Turn On Sync]   [I Have a Code…]
>
> Free. Up to 1,000 circuits. If none of your devices syncs for a year, the
> synced copy is removed (your devices keep theirs).

**Turn On Sync** → working ("Setting up…") → the code sheet:

> **Your sync code**
> `000G-40R4-0M30-E209-185G-R38E-1YZ4`  (large, monospaced, selectable)
> [QR code]
> On your other computer: Settings › Sync › I Have a Code. On your phone:
> scan this with the camera, or open cedarlogic.netlify.app/app, then Your
> Circuits › Sync › Scan Code.
> Keep it private: anyone with this code can see and change your synced
> circuits. Keep a copy somewhere safe: if you lose every device that has it
> and this code, nobody can open the synced circuits — not even CedarLogic's
> website.
> [Copy Code]  [Copy Link]  [Done]

**I Have a Code…** → a field ("Type or paste the code, or a sync link") that
checks as you type (a green tick at 28 valid symbols; the error sentences of
§1.2 under the field) → [Continue] → "Checking the code…" (the preview, §4.8) →
the confirmation, built from what the code holds:

> **Link this Mac?**
> This code has **14 circuits** from **Bob’s laptop** and **Chrome on Android**,
> last changed yesterday: “Lab 3 adder”, “Traffic light”, “ALU”, …
> Linking adds your 23 circuits here to them. Circuits that are already the same
> aren't doubled. Anyone with this code can see and change all of them.
> Only link with a code you made yourself.
> [Link]  [Cancel]

With no circuits yet: "This code has no circuits yet. Only link with a code you
made yourself — if someone sent it to you, your circuits would go to them."
Device and circuit names in this sheet are shown as plain text.

**Sync on:**

> **Sync** — Synced just now · 42 circuits   [Sync Now]
> **This Mac's name** — [Levi’s MacBook Air]  "Shown on your other devices when a circuit comes from here."
> **Devices** — Levi’s MacBook Air (this Mac) · Safari on iPhone, synced 2 days ago · Raspberry Pi, synced 3 months ago
> "Don't recognise one? Start over with a new code."
> **Sync code** — `000G-…-1YZ4` hidden as `••••-••••-…-1YZ4`  [Show Code]
> [Turn Off Sync…]   [Start Over with a New Code…]   [Delete Synced Copy…]

Status sentences (also in Your Circuits): "Synced just now" / "Synced 5 min
ago"; "Syncing…"; "Bringing in 12 of 37 circuits…"; "Sending 5 circuits…";
"Offline. Changes will sync when you're back online."; "Couldn't sync: <message>.
Trying again in 5 minutes."; "Lots of syncing just now. Trying again in a
minute."; the 507 sentences; "Syncing in another CedarLogic window."; the §3.4
and §4.5–§4.11 sentences.

**Turn Off Sync…** →

> **Turn off sync on this Mac?**
> Your circuits stay on this Mac. Your other devices keep syncing with each other.
> [ ] Also remove the synced circuits from this Mac (they go to the library's trash)
> [Turn Off]  [Cancel]

**Delete Synced Copy…** →

> **Delete the synced copy?**
> Sync stops on every device. Each device keeps the circuits it has now. This
> can't be undone.
> [Delete]  [Cancel]

**Start Over with a New Code…** →

> **Start over with a new code?**
> The synced copy is deleted and a new code is made. Your circuits here are sent
> with the new code. Every other device stops syncing until you link it again
> with the new code. Use this if someone else may have your code.
> [Start Over]  [Cancel]

Device name defaults: Mac `Host.current().localizedName` ("Levi’s MacBook
Air"); Linux the pretty hostname (`/etc/machine-info` PRETTY_HOSTNAME, else
`g_get_host_name()`, e.g. "raspberrypi"); Windows `GetComputerNameExW
(ComputerNamePhysicalDnsHostname)`. Up to 64 characters.

### 5.2 Your Circuits (apps)

Under the list, beside the existing buttons: a small sync line — icon + status
sentence + [Sync Now] (a circling-arrows button) — only while sync is on; while
it's off, a quiet "Sync…" link opening Settings › Sync. A circuit with a
problem says so on its second line: "Too big to sync (over 512 KB)", "Saved by a
newer CedarLogic: changes here stay on this device". Notices (conflicts kept,
deletes undone, deletes arrived) appear as the apps' usual one-line notes (Mac
`canvas.note`, the Linux/Windows status bar) and in the sync line for a minute.
The incoming and local mass-delete questions are ordinary alerts.

### 5.3 Links to the apps

`cedarlogic://sync#k=…` (and `cedarlogic://sync?k=…`) arrives the way
`cedarlogic://open` does today (Mac `kAEGetURL` in `ShareLink.swift`;
Linux/Windows the argument forwarded to the running instance,
`sharelink::isLink`). It opens Settings › Sync with the code filled in and runs
the preview, then shows the **Link this Mac?** confirmation — never linking by
itself. Already syncing with this code: "This Mac already syncs with this
code." Syncing with another: "This Mac syncs with another code. Switch to this
one? Circuits here stay; they'll be added to the other synced circuits." [Switch]
[Cancel] (then the preview and its confirmation).

### 5.4 CedarLogic Online and the phone app

**Your Circuits unlocked.** The toolbar's Your Circuits / Open (⌘O) sheet,
and the ••• menu's "Your Circuits…" (no lock badge; drop it from
`APP_ADDS`/locked lists in `sim.js`; the app-only list keeps "Version
history"). The sheet, in the online sheets' style (full screen on phones, from
`sim-mobile.js`):

- Search field; [New Circuit]; [Import .cdl…] (multiple files).
- Rows, newest first: gate tile, name, "Edited 12 min ago · 8 gates".
  Tap/click opens. Each row's ••• menu: Rename, Duplicate, Export .cdl
  (`clToCdl`, the existing save path), Delete (→ trash, with "Undo" in the
  toast). A "Recently Deleted (3)" row at the end opens the trash (Restore,
  Delete Now); 30 days.
- Footer: the sync line (as the apps) and [Sync…], which opens the Sync sheet.
- On an installed iPhone/iPad app with an empty library, a first-run line:
  "Using CedarLogic in Safari too? Link both with one code: Sync › Scan Code."

**The Sync sheet** has the same states and words as §5.1 with "this browser"
(online) or "this phone"/"this tablet" (the installed app) for "this Mac".
Device name default: "<Browser> on <OS>" from `sim-platform.js`'s OS detection —
"Safari on iPhone", "Chrome on Android", "Chrome on Chromebook", "Edge on
Windows"; installed: "CedarLogic app on iPhone". The QR code is drawn with the
vendored `qrcodegen.js` as an SVG. Code entry:

- a text field with `autocapitalize="characters" autocorrect="off"
  spellcheck="false" autocomplete="off" inputmode="text"`;
- **[Paste Code]** (`navigator.clipboard.readText()`, where allowed);
- **[Scan Code]**: the camera (`getUserMedia({video:{facingMode:"environment"}})`)
  and `BarcodeDetector` where the browser has it, else the vendored `jsQR`
  (`public/assets/js/vendor/jsqr.js`, Apache-2.0) on canvas frames. A scanned
  sync link goes through the same parser. This is what makes the installed
  iPhone app linkable: on iOS the camera app always opens links in Safari, whose
  storage is not the Home Screen app's.

Extra line on the web Sync sheet (sync on or off), on Safari (not installed):
"Safari may clear this website's data if you don't visit for a week. Your
synced circuits are safe in the synced copy, but keep your code to link again,
or add CedarLogic to your Home Screen." On every web Link sheet: "On a shared or
lab computer? Turn off sync when you're done — anyone using this browser can see
your synced circuits."

**iPhone and iPad (Home Screen app).** A Home Screen web app keeps its own
storage, apart from Safari:

- In the installed app's Sync sheet, under I Have a Code: "Scan the code on
  your other device with [Scan Code], or paste it."
- The `/sync/` page, opened in iOS Safari (not standalone), shows the code
  large with [Copy Code] and: "Using CedarLogic from your Home Screen? Open it,
  tap Your Circuits › Sync › Scan Code (or paste)." plus [Use in Safari
  Instead].
- Android: an installed app shares storage with Chrome, so linking in Chrome
  links the app too; the page says "Linked. Your CedarLogic app on this phone
  syncs too."

**Headers** (in `netlify.toml`) for every page that loads `sim*.js` or
`sync-core.js` (the simulator, `/app/*`, `/sync/*`):
`Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self'; worker-src 'self'; manifest-src 'self'; media-src 'self' blob:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'`
(widened only for what the pages really load, e.g. a font host) and
`Permissions-Policy: camera=(self), microphone=()`. Ship it as
`Content-Security-Policy-Report-Only` on a deploy preview first and move any
inline script into files. The service worker (`public/app/sw.js`) adds
`sync-core.js`, `sim-library.js`, `qrcodegen.js` and `vendor/jsqr.js` to its
precache list with a version bump; it already never caches `/api/`.

### 5.5 The `/sync/` page (website)

Built by `scripts/build_pages.py` like the other pages, `noindex`, small.
Script (a file, not inline): read `#k=` (never the query string), then
`history.replaceState(null, "", "/sync/")` at once; validate the code (§1.2) —
invalid: "This link's code is damaged. Copy the code from your other device
instead." Valid:

> **Link to your synced circuits**
> [Open in the CedarLogic App] (desktop OSes only: `cedarlogic://sync#k=…`)
> [Use in This Browser] (opens CedarLogic Online at `…#k=<code>` — a fragment — which runs the preview and confirmation)
> the iOS note above, when it applies.

---

## 6. The shared C++ engine

### 6.1 Decision

**One engine for the three apps**, in `mac/CedarCore/` (already compiled into
all three: `mac/build.sh` and `linux/` + `windows/CMakeLists.txt` glob
`mac/CedarCore/*.cpp`, and they link the `format/` library). It holds
everything that must behave identically: codes, HKDF, envelope, JSON, text and
hashes, the structure digest (on `format/`'s readers and the core's gate
library), the HTTP protocol, the whole algorithm of §4, the library file rules
of §4.12, the state file, and a self-test (vectors + the §7.2 scenarios against
an in-process fake server). It calls nothing platform-specific: crypto, HTTP,
the UI thread and window actions come in through two hook interfaces. Plain
C++17 and `std::filesystem` (UTF-8 paths via `std::filesystem::u8path` on
Windows); no new third-party code except the QR encoder.

The Mac app uses it through a C API (Swift imports C; `CedarCore.h` is the
bridging header, so it `#include`s `CedarSync.h`). The web client is separate
JS (`sync-core.js`) checked against the same vectors and the same server.

Files (all new):

| File | What |
|---|---|
| `mac/CedarCore/Sync.h` | the C++ interface below (no platform headers) |
| `mac/CedarCore/SyncProtocol.cpp` | codes, HKDF, base64url, hex, envelope seal/open (`sealForTest` takes a nonce; the real seal doesn't), `fileText`, hashes |
| `mac/CedarCore/SyncStructure.cpp` | `structureText` (§2.4) on `cl::readCircuitFile`/`cl::readLegacyCdl` and the gate library's defaults |
| `mac/CedarCore/SyncJson.h/.cpp` | minimal strict JSON: parse (all escapes, surrogate pairs, unpaired surrogates refused, numbers as doubles with the integer rule of §2.2, depth ≤ 32) and write (escape `"` `\` and U+0000–U+001F) |
| `mac/CedarCore/SyncLibrary.cpp` | the library folder: list, hash cache (ns mtimes, file ids, racily-clean), read via `fileText`, write atomically, versions (arrival stamps, mtime, `.txt` notes, structure dedupe), create, trash, `.sync-library-id` / `.sync-library-gen` |
| `mac/CedarCore/Sync.cpp` | Engine: thread, triggers, backoff, lazy pushes, cycle, pull/push/merge/conflicts, preview, device record, state.json, status |
| `mac/CedarCore/SyncTest.cpp` | `clsync::selfTest`: vectors (from `ref/vectors.h`, copied in as `SyncVectors.h`, with `ref/fixtures/` as `SyncFixtures.h`) + scenarios on a FakeServer (a port of `ref/sim.py: Server`) |
| `mac/CedarCore/QrCodeGen.hpp/.cpp` | Nayuki "QR Code generator" C++ (MIT), vendored unchanged |
| `mac/CedarCore/SyncCApi.cpp`, `include/CedarSync.h` | the C API for Swift |

### 6.2 `Sync.h`

```cpp
// The CedarLogic sync engine (SYNC.md). Plain C++17; the platform comes in
// through Crypto and Host.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace clsync {

using Bytes = std::vector<uint8_t>;

// ---- What the platform provides -------------------------------------------------

// Thread-safe; called from the engine thread and from selfTest.
struct Crypto {
	virtual ~Crypto() = default;
	virtual bool random(uint8_t* out, size_t n) = 0;                       // a CSPRNG; false = stop, never use the buffer
	virtual void sha256(const uint8_t* p, size_t n, uint8_t out[32]) = 0;
	virtual void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* p, size_t n, uint8_t out[32]) = 0;
	// ctTag = ciphertext ‖ 16-byte tag. open() returns false on a tag mismatch.
	virtual bool aesGcmSeal(const uint8_t key[32], const uint8_t nonce[12], const Bytes& aad, const Bytes& plain, Bytes& ctTag) = 0;
	virtual bool aesGcmOpen(const uint8_t key[32], const uint8_t nonce[12], const Bytes& aad, const Bytes& ctTag, Bytes& plain) = 0;
	// Raw deflate (RFC 1951). deflateRaw may return false: the record goes uncompressed.
	virtual bool deflateRaw(const Bytes& in, Bytes& out) { (void)in; (void)out; return false; }
	virtual bool inflateRaw(const Bytes& in, size_t maxOut, Bytes& out) = 0;   // false past maxOut
};

struct HttpRequest {
	std::string method;                                      // GET PUT POST DELETE
	std::string url;                                         // absolute
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;                                        // JSON or empty
};
struct HttpResponse {
	bool sent = false;      // the whole request went out (the server may have acted)
	int status = 0;         // 0: no answer (offline, timeout, TLS failure)
	std::map<std::string, std::string> headers;   // names lower-case (date, retry-after)
	std::string body;
};

struct Status {
	enum Kind { Off, Synced, Syncing, Offline, Error, Full, Gone, Busy } kind = Off;
	std::string text;        // the §5.1 sentence, ready to show
	int64_t lastSyncAt = 0;  // ms
	int circuits = 0;
	int progressDone = 0, progressTotal = 0;   // "Bringing in 12 of 37…"
	std::vector<std::pair<std::string, std::string>> problems;   // (folder id, "Too big to sync (over 512 KB)")
};

struct Preview {            // what a code holds (§4.8), for the Link confirmation
	int circuits = 0;
	std::vector<std::string> devices;   // device names
	std::vector<std::string> names;     // up to 5 circuit names
	int64_t newestEdit = 0;             // ms, 0 if none
	std::string sentence;               // the §5.1 confirmation text, ready to show
};

struct WindowState { bool open = false, dirty = false; int64_t lastInputAt = 0; };

struct Host {
	virtual ~Host() = default;
	// Engine thread. Blocking; ~20 s connect, 60 s total; HTTPS only (http only for a localhost CL_SYNC_URL).
	virtual HttpResponse http(const HttpRequest& r) = 0;
	// Engine thread: run fn on the UI thread and wait for it. Never called after stop() returned, nor in quitting().
	virtual void onMain(const std::function<void()>& fn) = 0;
	// The secret (the 28-symbol code) at rest: §2.5. load returns "" if none.
	virtual std::string loadSecret() = 0;
	virtual bool saveSecret(const std::string& code) = 0;
	virtual void forgetSecret() = 0;
	// A lock on the sync folder held while the engine runs; false if another process has it.
	virtual bool tryLock(const std::string& lockPath) = 0;
	virtual void unlock() = 0;
	// UI thread (inside onMain):
	virtual void flushOpen(std::function<void()> done) = 0;                    // save every open library circuit with unsaved
	                                                                           // changes; call done when saved (any thread)
	virtual WindowState windowState(const std::string& folderId) = 0;          // §4.12 apply step
	virtual void circuitReplaced(const std::string& folderId, const std::string& fromDevice) = 0;   // reload its windows
	virtual void closeCircuit(const std::string& folderId) = 0;               // close its (clean) windows
	virtual void libraryChanged() = 0;                                         // refresh Your Circuits
	virtual void statusChanged(const Status& s) = 0;
	virtual void notice(const std::string& text) = 0;                          // a one-line note
	// Questions; answer on the UI thread whenever the person decides (the cycle waits, the cursor held).
	virtual void askMassDelete(int count, std::function<void(bool deleteEverywhere)> answer) = 0;
	virtual void askIncomingDeletes(int count, const std::string& fromDevices,
	                                std::function<void(bool moveToTrash)> answer) = 0;
};

struct Config {
	std::string libraryRoot;       // the apps' library folder
	std::string syncDir;           // §2.5 per-machine folder (state.json, lock)
	std::string serverBase = "https://cedarlogic.netlify.app/api/sync/v1";
	std::string appKey;            // x-cedarlogic-key
	std::string client;            // x-cedarlogic-client, e.g. "linux/0.4.0+812"
	std::string defaultDeviceName; // §5.1
	std::function<std::string(const std::string& lib, bool gui, const std::string& name)> gateDefault;
	                               // the gate library's default for a param, or "\x01" for none (§2.4)
};

// ---- Codes and text (any thread) -----------------------------------------------------

std::string newCode(Crypto&);                                  // 28 symbols ("" if the RNG failed)
// Text typed, pasted or scanned (or a link) -> the canonical code; false and why ("length", "symbol", "checksum").
bool parseCode(Crypto&, const std::string& text, std::string& code, std::string& why);
std::string whyText(const std::string& why, const std::string& text);   // the §1.2 sentences
std::string groupCode(const std::string& code);                // XXXX-XXXX-…
std::string webLink(const std::string& code);                  // https://cedarlogic.netlify.app/sync/#k=…
std::string appLink(const std::string& code);                  // cedarlogic://sync#k=…
// The QR code of webLink(code): size x size modules, row-major, true = dark (quiet zone not included).
std::vector<bool> qr(const std::string& text, int& size);

// ---- The engine (create, call and destroy on the UI thread) -------------------------

class Engine {
public:
	Engine(Config, Crypto&, Host&);
	~Engine();                                                 // stop(), joins the thread

	// Starts syncing if a secret is stored. Call once at launch.
	void start();
	void stop();                                               // returns at once; the thread finishes its step and exits

	bool enabled() const;
	std::string code() const;                                  // "" when off
	std::string deviceName() const;
	void setDeviceName(const std::string&);
	Status status() const;
	std::vector<std::pair<std::string, int64_t>> devices() const;   // (name, lastSyncAt) from the device records

	// Each finishes on the UI thread through `done` (ok, or a sentence for the person).
	void turnOn(std::function<void(bool, std::string)> done);                                   // new code, PUT space, first cycle
	void preview(const std::string& code, std::function<void(bool, std::string, Preview)> done); // §4.8 step 1: nothing stored
	void link(const std::string& code, std::function<void(bool, std::string)> done);            // step 2, after the confirmation
	void turnOff(bool removeSyncedCircuits);
	void deleteSyncedCopy(std::function<void(bool, std::string)> done);
	void startOver(std::function<void(bool, std::string)> done);                               // delete + turnOn

	// Triggers (§4.3).
	void syncNow();                     // a flush
	void noteLibraryChanged();          // after any save/rename/import/delete/restore in the library
	void appActivated();                // app or a window came to the front
	void appDeactivated();              // to the background: a flush
	void userActive();                  // input happened (keeps polling going for 10 min)
	// §4.12 Quitting: call after the host saved every open circuit. A push-only cycle on the engine
	// thread that never calls onMain; `done` runs on the engine thread within 5 s.
	void quitting(std::function<void()> done);

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};

// ---- Tests ---------------------------------------------------------------------------

// The vectors of SYNC.md §7.1 and the scenarios of §7.2 on an in-process
// FakeServer and temporary library folders (under tempDir). One PASS/FAIL line
// each in `report`; false if any failed. If serverBase is set (a mock server,
// §10 S), the scenarios that need only one device also run against it over http().
bool selfTest(Crypto&, const std::string& tempDir, std::string& report, Host* httpOnly = nullptr,
              const std::string& serverBase = "");

}  // namespace clsync
```

Threading rules: the engine owns one `std::thread`; all mutable engine state
is behind a mutex; `onMain` is the only way the engine touches the library or
the UI during a cycle; the UI thread never waits for the engine thread (the
`quitting()` call returns at once; its `done` comes later). `stop()` sets a flag;
an `onMain` that would run after `stop()` is dropped. `flushOpen`'s `done` may be
called from any thread; the engine waits for it on its own thread with a 10 s
limit (then it proceeds; the apply step's window check still protects edits).

### 6.3 The C API for Swift (`include/CedarSync.h`)

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
typedef void (*CLSyncQuitDone)(void *ctx);                // called on the engine thread
void cl_sync_quitting(CLSyncEngine *e, CLSyncQuitDone done, void *ctx);

// Codes (no engine needed; the hooks for SHA-256)
// Writes the canonical code (29 bytes with NUL) or returns false with why = "length" | "symbol" | "checksum".
bool cl_sync_parse_code(const CLSyncHooks *hooks, const char *text, char code[29], char why[16]);
const char *cl_sync_why_text(const char *why, const char *text);
void cl_sync_group_code(const char *code, char out[35]);
const char *cl_sync_web_link(const char *code);         // valid until the next call
const char *cl_sync_app_link(const char *code);
// QR modules of text: returns size (modules per side) and writes size*size bytes (1 = dark) into out (≥ 177*177).
int cl_sync_qr(const char *text, uint8_t *out);

// Tests: the report (malloc'd; caller frees); false if anything failed. serverBase may be NULL.
bool cl_sync_self_test(const CLSyncHooks *hooks, const char *tempDir, const char *serverBase, char **report);
```

### 6.4 The hooks on each platform

**Mac** (`mac/App/Sync.swift`): CryptoKit for SHA-256/HMAC/AES-GCM,
`SecRandomCopyBytes`, Compression `COMPRESSION_ZLIB` (as `ShareLinkCodec.swift`)
with the `MAX_PLAINTEXT + 1` buffer; HTTP with a `URLSession(configuration:
.ephemeral)` data task and a semaphore (we're on the engine thread), timeouts 20
s/60 s; `on_main` = `DispatchQueue.main.sync`; secret file 0600 in the Sync
folder; `flock`. The app is a SwiftUI `DocumentGroup` with `ReferenceFileDocument`
(`CedarLogicApp.swift`), so: `flush_open` asks each dirty library document to
autosave (`autosave(withImplicitCancellability:)`, as `CanvasView` does) and calls
`cl_sync_flush_done` from the last completion; `window_state` from the open
documents (dirty = `hasUnautosavedChanges`, last input from the canvas);
`circuit_replaced` → `revert(toContentsOf:ofType:)`, restoring page and camera
from a store keyed by file URL, then `canvas.note("Updated from …")`;
`close_circuit` → `doc.close()`; library writes through `NSFileCoordinator`;
`library_changed` → post `.clLibraryChanged`; `gate_default` from the core's gate
library. Calls `cl_sync_note_library_changed` from `Library.noteSaved`, `rename`,
`create`, `delete`, version restore; `cl_sync_app_activated` /
`_deactivated` on `NSApplication.didBecomeActive/didResignActiveNotification`;
quitting: `applicationShouldTerminate` returns `.terminateLater` (with the
existing `QuitConfirm`), saves the documents, calls `cl_sync_quitting`, and
replies on the main queue when it's done. The sync URL arrives in
`ShareLink.open` (host `sync`). Test: `mac/Tools/sync-check.sh` builds a tiny
Swift tool with the same hooks and runs `cl_sync_self_test` (also against the
mock server when `CL_SYNC_URL` is set).

**Linux** (`linux/App/SyncPlatform.cpp`): libcrypto (§1.4) (GLib's
`GChecksum`/`GHmac` may do the hashing; AES-GCM needs libcrypto), GIO raw zlib for
deflate. HTTP with the `curl` program, as Feedback does, but secrets never on the
command line (other users can read `/proc/*/cmdline`; today's `Feedback.cpp`
passes its key with `-H` — don't copy that): the request headers go in a 0600
temp file (`g_file_open_tmp`) passed as `-H @file` (curl ≥ 7.55; 22.04 has
7.81), the body as `--data-binary @file`, and `-sS --proto =https --max-time 60
--connect-timeout 20 -D <headers-out> -o <body-out> -w '%{http_code}'`
(`--proto =http,https` only when `CL_SYNC_URL` is `http://localhost…` or
`http://127.0.0.1…`); every temp file is deleted on every exit path; `sent` =
curl's exit code isn't 7 (couldn't connect) or 6 (couldn't resolve). (Linking
libcurl instead is fine.) `on_main` = `g_main_context_invoke` + a condition
variable. Secret file 0600; `flock`. Window hooks from `CircuitWindow` (save,
dirty state, last input, reload, close). Quitting: the last window's close saves
the windows, calls `quitting(done)`, and runs a nested `GMainLoop` until `done`
or 5 s. `cedarlogic://sync` through `sharelink::isLink` and the single-instance
forwarding. `thin()` in `Library.cpp` deletes the `.txt` beside a removed
version. CI: `linux/Tools/sync_check.cpp` (links CedarCore + SyncPlatform's
crypto) runs `clsync::selfTest` and the vectors, then again against the mock
server; the `.deb` is installed on Ubuntu 24.04 in CI.

**Windows** (`windows/App/SyncPlatform.cpp`): CNG/BCrypt (§1.4) with the
pseudo-handles `BCRYPT_SHA256_ALG_HANDLE`, `BCRYPT_HMAC_SHA256_ALG_HANDLE` on
MSVC; on the MinGW branch of `windows/CMakeLists.txt`, providers opened once with
`BCryptOpenAlgorithmProvider` and cached; AES through
`BCryptOpenAlgorithmProvider(BCRYPT_AES_ALGORITHM)` + `BCRYPT_CHAIN_MODE_GCM`;
`Deflate.h`. `target_link_libraries(... bcrypt crypt32)`. HTTP: generalise
`Feedback.cpp`'s `request()` (WinHTTP) to take a header list and return response
headers (`WinHttpQueryHeaders(WINHTTP_QUERY_RAW_HEADERS_CRLF)`), to read up to
6 MB, and to allow `http://localhost` for `CL_SYNC_URL`. `on_main` =
`PostMessage(hiddenWnd, WM_APP_SYNC, …)` + an event the engine waits on. Secret:
`CryptProtectData`/`CryptUnprotectData` (crypt32) in
`%LOCALAPPDATA%\CedarLogic\Sync\secret.dpapi`; lock with `CreateFileW(…, share 0)`
+ `LockFileEx`. Quitting: the last window's close saves, calls `quitting(done)`,
and pumps messages until `done` or 5 s. `thin()` deletes the `.txt` beside a
removed version. CI: `CedarLogic.exe --sync-test` runs `clsync::selfTest` (like
`--share-test`), x64 and ARM64, MSVC and the MinGW build.

---

## 7. Tests

### 7.1 Test vectors

Computed by `ref/vectors.py` (Python `cryptography`, the reference readers of
`ref/clsync.py`) and checked independently by:

- `ref/check_webcrypto.mjs` — WebCrypto, CompressionStream, TextDecoder and its
  own JS `.cdl` readers, as the website will;
- `ref/check_openssl.cpp` — libcrypto plus a C++ implementation of the shared
  byte-level code, with the structure digest built on the apps' **real** `.cdl`
  readers (`ref/format/`, copied from `format/` on `mac/native`) and a strict
  C++ JSON reader;
- `ref/check_cryptokit.swift` — CryptoKit and Compression, as the Mac hooks;
- `review/verify.mjs` — node:crypto and zlib, HKDF built by hand from HMAC,
  written from this document's text, which also checks every value below appears
  here.

`sh ref/run_all.sh` regenerates and checks everything (and runs the scenarios).
The same values are in machine-readable form in `ref/vectors.json` (and
`ref/vectors.h` for C++, with `ref/gate_defaults.json`'s defaults and
`ref/fixtures/`): **tests should load those files, not copy from this page**;
this page is for reading. `ref/fixtures/*-v1.cdl` were written by CedarLogic
Online's own `clToCdl` (`node ref/make_xml_fixtures.cjs`).

What each client must show:

- every code in 7.1.1 from its secret, and back;
- every parse case in 7.1.2 (canonical code or the error kind);
- every key in 7.1.3 (spaceId, authToken, authHash, deleteToken, deleteHash,
  recordKey) from its secret;
- the hashes and `fileText` cases in 7.1.4;
- every structure hash in 7.1.5 (and the structure texts shown);
- record 1 in 7.1.6 **byte for byte** (seal the given payload bytes with the
  given key, id, ver and nonce, flags 0, through the test-only seal); every
  record must open to its payload, read as the given `kind`, and records 3 and
  4's `name` and `cdl` must equal the given UTF-8;
- every payload in 7.1.7 opens but is refused with the given `why`;
- every case in 7.1.8 must fail to open.

#### 7.1.1 Codes (secret -> checksum -> code)

| label | secret (hex) | checksum12 | code | grouped |
|---|---|---|---|---|
| counting | `000102030405060708090a0b0c0d0e0f` | `be4` | `000G40R40M30E209185GR38E1YZ4` | `000G-40R4-0M30-E209-185G-R38E-1YZ4` |
| zeros | `00000000000000000000000000000000` | `374` | `00000000000000000000000000VM` | `0000-0000-0000-0000-0000-0000-00VM` |
| ones | `ffffffffffffffffffffffffffffffff` | `5ac` | `ZZZZZZZZZZZZZZZZZZZZZZZZZXDC` | `ZZZZ-ZZZZ-ZZZZ-ZZZZ-ZZZZ-ZZZZ-ZXDC` |
| random | `8e1f5a0c3b9d47e2a6c1f0d9b3e85274` | `1a3` | `HRFNM31VKN3Y59P1Y3CV7T2JEGD3` | `HRFN-M31V-KN3Y-59P1-Y3CV-7T2J-EGD3` |

Links for the `counting` secret:

```
https://cedarlogic.netlify.app/sync/#k=000G40R40M30E209185GR38E1YZ4
cedarlogic://sync#k=000G40R40M30E209185GR38E1YZ4
```

#### 7.1.2 Parsing what people type (input -> canonical code, or the error kind)

`⟨U+XXXX⟩` stands for that one character (the JSON has the real text).

| case | input | expect |
|---|---|---|
| canonical | `000G40R40M30E209185GR38E1YZ4` | `000G40R40M30E209185GR38E1YZ4` |
| grouped | `000G-40R4-0M30-E209-185G-R38E-1YZ4` | `000G40R40M30E209185GR38E1YZ4` |
| lowercase with spaces | ` 000g 40r4 0m30 e209 185g r38e 1yz4 ` | `000G40R40M30E209185GR38E1YZ4` |
| no-break spaces | `000G⟨U+00A0⟩40R4⟨U+00A0⟩0M30⟨U+00A0⟩E209⟨U+00A0⟩185G⟨U+00A0⟩R38E⟨U+00A0⟩1YZ4` | `000G40R40M30E209185GR38E1YZ4` |
| O for 0, l for 1 | `ooog-4or4-om3o-e2o9-l85g-r38e-lyz4` | `000G40R40M30E209185GR38E1YZ4` |
| I for 1 | `000G-40R4-0M30-E209-I85G-R38E-IYZ4` | `000G40R40M30E209185GR38E1YZ4` |
| web link | `https://cedarlogic.netlify.app/sync/#k=000G40R40M30E209185GR38E1YZ4` | `000G40R40M30E209185GR38E1YZ4` |
| app link | `cedarlogic://sync#k=000G40R40M30E209185GR38E1YZ4` | `000G40R40M30E209185GR38E1YZ4` |
| app link, query form | `cedarlogic://sync?k=000G-40R4-0M30-E209-185G-R38E-1YZ4` | `000G40R40M30E209185GR38E1YZ4` |
| web link, query form (never accepted) | `https://cedarlogic.netlify.app/sync/?k=000G40R40M30E209185GR38E1YZ4` | `error:symbol` |
| one symbol wrong | `000G4ZR40M30E209185GR38E1YZ4` | `error:checksum` |
| too short | `000G40R40M30E209185GR38E1YZ` | `error:length` |
| too long | `000G40R40M30E209185GR38E1YZ40` | `error:length` |
| U is never a symbol | `U00G40R40M30E209185GR38E1YZ4` | `error:symbol` |
| dotless i (U+0131) is not I | `000G40R40M30E209⟨U+0131⟩85GR38E⟨U+0131⟩YZ4` | `error:symbol` |
| ligature st (U+FB06) is not ST | `000G40R40M30E209185GR38E1YZ⟨U+FB06⟩` | `error:symbol` |
| full-width zero (U+FF10) | `⟨U+FF10⟩00G40R40M30E209185GR38E1YZ4` | `error:symbol` |

#### 7.1.3 Keys (HKDF-SHA256, salt `cedarlogic-sync-v1`)

**counting** — secret `000102030405060708090a0b0c0d0e0f`

```
PRK          00a5a575f839ae9e4617a77728e8212fc04ccdad4e7543e1ceae675514b5a929
spaceId      9d8d8b91b2ab7698e8b8e2b01513d9eb
authToken    z1wMKuEO0NfgDY6grGiQej05_UYkL3ePqDF4e0Gwb70
authHash     75b77eef59190c94960b586fbeefcb42a3dade0dd8a1606396a32a2e43542aac
deleteToken  -yX84OR04PUNepm51e0-QNAmxNIfov7YszBQaDD5nlo
deleteHash   18387b77ae3066323d89923c44117f54614d2f02ffdae127c937f2f1a09aa498
recordKey    e7333f3b411aec02c98d925aa938760f5ff075dbe21c1435332a57f5eeaec1d7
```

**zeros** — secret `00000000000000000000000000000000`

```
PRK          b23387f0fcc7fafee099cc90ae6f3e7e9ff8552351e04da3599006d2eca2468a
spaceId      a33dfde78832201b0c27bac7363053e9
authToken    cwrfGzVpH1yDet0jueSFandHf1JIgbdneMQDGf75cIU
authHash     0e579542bfbff1de7632df4dcc106dca469ead780fb15574668118ca4f03455a
deleteToken  cTqFVLdm_pI6pSTq1LbI3AC8TQt-nqCIKa6cB2Zd04Y
deleteHash   4334757758c4af926ec870d8e323f96ed1cd3a444b963e00252d51af2634cebc
recordKey    d149c9911677bf9d5364b8b20bf3f5d9cd1ee2a9937ffadbfac7f6d294193e2c
```

**ones** — secret `ffffffffffffffffffffffffffffffff`

```
PRK          9427ca23fc0ac44c491689fbb734e55a1cb7b959ca20e4a1386de597057d640d
spaceId      912f52be10996e158bb3d5427bab6fcb
authToken    FLUCeSHSwWmkVSmBpR6GMY3KUuZQ-m3YOXEuuJ4xVZI
authHash     699f2fff6902b8c48527b8bd52c884bdb4df10cb2068f3a3a6df771a32af7166
deleteToken  10qcgPeqqCUp2NAD9FOaBExDIjGDouxFyNSbCXMtFNU
deleteHash   6baa43b586d195c582d6e7253012e206f83fdceb9cd0c88b1ca5d962989e9436
recordKey    7365d2ef70a6d35bd1389ea2537b379f41b1696b94f5438290444e47490e0a5c
```

**random** — secret `8e1f5a0c3b9d47e2a6c1f0d9b3e85274`

```
PRK          7a73f7d62f160524c64077cc3e6ff6fdd40567f76d76052044bee7e2affc6f5b
spaceId      36cef329f46398a41af60bc09e94c67c
authToken    lsvSZZRjpveWdwMeJtbPNlKYf9nI66YZcBVT_5iw80Y
authHash     4a5a7796f98236047ae98887efddb602a1ceeb03004b89047cae4cce55944c48
deleteToken  pkIEN8iri1JTN9rWaF4dSF9rujhU4b6hjZICY5L2kr4
deleteHash   25302ecd0434b7e9ba5da850475dc3af62cf08b22f0b962e78a9cd81f0ee42a2
recordKey    bad741b7ac9415156d248c62c8366c828f7dcbb6ce301179c9e0a7a42c0d33b7
```

#### 7.1.4 Text and hashes

Name: `Half adder — lab 3 ✓` (UTF-8 `48616c6620616464657220e28094206c6162203320e29c93`)

cdl (exactly these bytes, LF line ends, final newline; `ref/fixtures/vector-v3.cdl`):

```
(cedarlogic
  (version 3)
  (generator "CedarLogic 0.3.5")
  (page 0
    (gate "AA_TOGGLE"
      (uuid "1")
      (at 10 20)
      (angle 0)
      (lparam "OUTPUT_NUM" "0"))
    (gate "GA_LED"
      (uuid "2")
      (at 18 20)
      (angle 0))
    (wire
      (ids "3")
      (seg "0" h
        (pts 11 20 17 20)
        (connect "1" "OUT_0")
        (connect "2" "N_in0")))))
```

```
nameHash(name)                                 16002a9defe5263b6e5c820741ddcb7612135ed53fd45acde5c50f54164e664b
cdlHash(cdl)                                   06c874731fa078530b826daece91b3e5997f558ab6a081aa0c0aad61d2aef92d
  same with CRLF line ends and a leading BOM   06c874731fa078530b826daece91b3e5997f558ab6a081aa0c0aad61d2aef92d
contentHash(name, cdl)                         0a94b77d3b84f3b171b04964c04fda6cf8df922fa876a3c73bb75c21c96d448b
  same with CRLF line ends                     0a94b77d3b84f3b171b04964c04fda6cf8df922fa876a3c73bb75c21c96d448b
  same with name "  <name>\n"                 0a94b77d3b84f3b171b04964c04fda6cf8df922fa876a3c73bb75c21c96d448b
cdl with OUTPUT_NUM "1" and generator 0.3.6:
  contentHash                                  2778c37bb1ce25cf734dac4681043a97f120c8f393755eafc210e414cd75a642
```

Stored bytes -> sync text (`fileText`):

| case | bytes (hex) | text (UTF-8 hex) |
|---|---|---|
| UTF-8 as is | `4c616220e2809c33e2809d20c3a9` | `4c616220e2809c33e2809d20c3a9` |
| windows-1252 (an old wx file) | `4c61622093339420e9` | `4c616220e2809c33e2809d20c3a9` |
| byte order mark dropped | `efbbbf4c6162` | `4c6162` |
| windows-1252 undefined byte 0x81 kept as U+0081 | `418142` | `41c28142` |

#### 7.1.5 Structure

Circuits in `ref/fixtures/` are named by file; `*-v1.cdl` were written by CedarLogic Online's own `clToCdl` (`ref/make_xml_fixtures.cjs`). Every implementation MUST produce every hash below; rows that share a hash are the same circuit written another way (format, switch state, app version, line ends).

| case | input | structureHash |
|---|---|---|
| switch and LED, v3 (the apps) | the cdl of 7.1.4 | `e045a4287db731864935fc23e23ab552c2d39cb323859b2be2ed75157568a8da` |
| the same, v1 XML (CedarLogic Online) | inline (vectors.json) | `e045a4287db731864935fc23e23ab552c2d39cb323859b2be2ed75157568a8da` |
| the same, switch on, generator 0.3.6 | inline (vectors.json) | `e045a4287db731864935fc23e23ab552c2d39cb323859b2be2ed75157568a8da` |
| the same, CRLF line ends | inline (vectors.json) | `e045a4287db731864935fc23e23ab552c2d39cb323859b2be2ed75157568a8da` |
| the LED moved to (22, 20) | inline (vectors.json) | `e63c9763100e246f3616f9c5835d7d9f2c1ba99e436439f685d739c2092889d0` |
| params, two pages, angles, a label, v3 | `fixtures/params-v3.cdl` | `60f31d97521c7cf8533197e1bdb0191e41cd7717e68cc44302b5f382c08baee8` |
| params: the same, v1 XML (CedarLogic Online) | `fixtures/params-v1.cdl` | `60f31d97521c7cf8533197e1bdb0191e41cd7717e68cc44302b5f382c08baee8` |
| 4-bit adders (a real app file), v3 | `fixtures/adder-v3.cdl` | `e28f006e7eaeecefa7c7fc263736cf60a35d01caf4e79076bdfbc685041f75a6` |
| adders: the same, v1 XML (CedarLogic Online) | `fixtures/adder-v1.cdl` | `e28f006e7eaeecefa7c7fc263736cf60a35d01caf4e79076bdfbc685041f75a6` |
| not a circuit file | inline (vectors.json) | `2214436a06f36d2e8661b55f05fd5bfc2397943ffa09d1816494db1de8ef1387` |
| v3 with trailing junk (unparsed) | inline (vectors.json) | `c99c28363fd4119727d6b1e8dbf0d06b710890275ef71acd09b1dcb2e7928703` |

structure text of *switch and LED, v3 (the apps)*:

```
cedarlogic-structure/1
G 0 AA_TOGGLE@10000,20000 0
G 0 GA_LED@18000,20000 0
W 0 AA_TOGGLE@10000,20000.OUT_0 GA_LED@18000,20000.N_in0
```

structure text of *the LED moved to (22, 20)*:

```
cedarlogic-structure/1
G 0 AA_TOGGLE@10000,20000 0
G 0 GA_LED@22000,20000 0
W 0 AA_TOGGLE@10000,20000.OUT_0 GA_LED@22000,20000.N_in0
```

structure text of *params, two pages, angles, a label, v3*:

```
cedarlogic-structure/1
G 0 AA_LABEL@-3250,7500 0 g:LABEL_TEXT=Count%20%3C%20100%20%22fast%22%20%C3%A9
G 0 AE_REGISTER8@20000,-4500 90 l:MAX_COUNT=%23100000
G 1 AA_INVERTER@6000,0 0
G 1 AA_TOGGLE@0,0 270
W 1 AA_INVERTER@6000,0.IN_0 AA_TOGGLE@0,0.OUT_0
```

structure text of *not a circuit file*:

```
cedarlogic-structure/1 unparsed
x
```

structure text of *v3 with trailing junk (unparsed)*:

```
cedarlogic-structure/1 unparsed
(cedarlogic
  (version 3)
  (generator "CedarLogic 0.3.5")
  (page 0
    (gate "AA_TOGGLE"
      (uuid "1")
      (at 10 20)
      (angle 0)
      (lparam "OUTPUT_NUM" "0"))
    (gate "GA_LED"
      (uuid "2")
      (at 18 20)
      (angle 0))
    (wire
      (ids "3")
      (seg "0" h
        (pts 11 20 17 20)
        (connect "1" "OUT_0")
        (connect "2" "N_in0")))))
)
```

#### 7.1.6 Records

**1. a circuit's first write: uncompressed, ver 1 (byte-exact: every client must produce this)**

```
recordKey    e7333f3b411aec02c98d925aa938760f5ff075dbe21c1435332a57f5eeaec1d7
recordId     3b241101-e2bb-4255-8caf-4136c566a962
ver          1
flags        0
nonce        c0c1c2c3c4c5c6c7c8c9cacb
AAD          cedarlogic-sync/1|3b241101-e2bb-4255-8caf-4136c566a962|1|0
payload      620 bytes, SHA-256 405114b06a056f3c897dbca04db31c779786f265a7eb65367fb2f079c814697b
envelope     650 bytes, h = 8fbbc50c32258eadfb4476ba513c9d59
kind         circuit
```

payload (UTF-8, exactly; the JSON has it as a string):

```json
{"v":1,"kind":"circuit","name":"Half adder — lab 3 ✓","cdl":"(cedarlogic\n  (version 3)\n  (generator \"CedarLogic 0.3.5\")\n  (page 0\n    (gate \"AA_TOGGLE\"\n      (uuid \"1\")\n      (at 10 20)\n      (angle 0)\n      (lparam \"OUTPUT_NUM\" \"0\"))\n    (gate \"GA_LED\"\n      (uuid \"2\")\n      (at 18 20)\n      (angle 0))\n    (wire\n      (ids \"3\")\n      (seg \"0\" h\n        (pts 11 20 17 20)\n        (connect \"1\" \"OUT_0\")\n        (connect \"2\" \"N_in0\")))))\n","createdAt":1759572000000,"modifiedAt":1759575600123,"device":"Levi’s MacBook Air","deviceId":"5f0c2a9e7d3b41c8a6e2f1b0c9d8e7a6"}
```

data (base64url of the envelope):

```
AQDAwcLDxMXGx8jJysu2m8nETA40NzXgIhu-uTBfIR81e9_kEqDlfsrJI382ekz4MBLHAGfNQ31Jny6KCKyDfOujN-7y6872Pe2J2Vu0iAh0SFY5al40KQrtGMt2naaCMEOxHIZ0lXV9mayI_8vAXYRXqMiUY58ks05MRl-v2O4nIZWZseoKFo-nNBye5djAqxLQiFvEYXTa9wik6o3mlKHMl38jiGD9kItdD52ASJyOFImro4mqhmkmuyhO2zg2IuSFSTmk8kS-BXRAhgGq89SeJL3CqyFAUhxx_MqLM7UyIENRjqUjRsfhzkboS2d96TfjL2E1UD0JTfjCr5SQsG0ieDOYrfGRmNZIKsZjKQycYn1k4G_Ruq1hI6O6IvRPb47KaKYJ40s9cVYU15sfKl2U0UMlWgD0QtqlOcukQTsPE6RWjNgGIhZ14nUsZ9_8JxmF4Jwga_0fl20K8tNndMW5cMPi4M7pMuyc_eEBUG_LseoD34LVMZVPljM-8YyhhmRzE6O5rnE0bQySu476oXX_9VX_1V6tQEqKnJHo12qy1-cvQYc60mpRXFPe58QVIB1gBOdtxpopZqjGwW0NmQJc8HpL704Lm5y7BeglU0GBTSoPd77RNatmSuUPX8tHlV4iZ9_SYz69JZLwmjVZnfWs-nm-se1saAd4Y5gjc2Lalit4Z4o6cP3FBMqyeLYR25gd7nCDBiqiXZ-JpsNPSO0YtrIQOmTA0n7WImvVN8UcgOlj1_YUt3krUAlS8BD5kXOUuavg5h_qfaDFjbcY4mXN0bGFra4clwguF5YB-c6uIOT6_S5F_ncxY-dPwdJ4QHu8iFuIlT9-5L5F24-pOSVpUwPP-fN6-MY
```

envelope (hex):

```
0100c0c1c2c3c4c5c6c7c8c9cacbb69bc9c44c0e343735e0221bbeb9305f211f357bdfe412a0e57ecac9237f367a4cf83012c70067cd437d499f2e8a08ac837ceba337eef2ebcef63ded89d95bb48808744856396a5e34290aed18cb769da6823043b11c867495757d99ac88ffcbc05d8457a8c894639f24b34e4c465fafd8ee27219599b1ea0a168fa7341c9ee5d8c0ab12d0885bc46174daf708a4ea8de694a1cc977f238860fd908b5d0f9d80489c8e1489aba389aa866926bb284edb383622e4854939a4f244be0574408601aaf3d49e24bdc2ab2140521c71fcca8b33b5322043518ea52346c7e1ce46e84b677de937e32f6135503d094df8c2af9490b06d22783398adf19198d6482ac663290c9c627d64e06fd1baad6123a3ba22f44f6f8eca68a609e34b3d715614d79b1f2a5d94d143255a00f442daa539cba4413b0f13a4568cd806221675e2752c67dffc271985e09c206bfd1f976d0af2d36774c5b970c3e2e0cee932ec9cfde101506fcbb1ea03df82d531954f96333ef18ca186647313a3b9ae71346d0c92bb8efaa175fff555ffd55ead404a8a9c91e8d76ab2d7e72f41873ad26a515c53dee7c415201d6004e76dc69a2966a8c6c16d0d99025cf07a4bef4e0b9b9cbb05e8255341814d2a0f77bed135ab664ae50f5fcb47955e2267dfd2633ebd2592f09a35599df5acfa79beb1ed6c6807786398237362da962b78678a3a70fdc504cab278b611db981dee7083062aa25d9f89a6c34f48ed18b6b2103a64c0d27ed6226bd537c51c80e963d7f614b7792b500952f010f9917394b9abe0e61fea7da0c58db718e265cdd1b185adae1c97082e179601f9ceae20e4fafd2e45fe773163e74fc1d278407bbc885b88953f7ee4be45db8fa93925695303cff9f37af8c6
```

**2. an edit, deflate-raw, ver 2, with base (decrypt-exact: their own deflate may differ)**

```
recordKey    e7333f3b411aec02c98d925aa938760f5ff075dbe21c1435332a57f5eeaec1d7
recordId     3b241101-e2bb-4255-8caf-4136c566a962
ver          2
flags        1
nonce        d0d1d2d3d4d5d6d7d8d9dadb
AAD          cedarlogic-sync/1|3b241101-e2bb-4255-8caf-4136c566a962|2|1
payload      772 bytes, SHA-256 56293d0547a8e60b2dcf8ce474c589615023ae79f76ad5f6384f19758c210263
envelope     482 bytes, h = 28289b1fe8a2f5625ac6cb7183f779da
kind         circuit
deflated     6d924d6edb301085af4270950046c01f9194b233dac02de026011aef041823ced0212a53812cbb8b2040efd02e7bba9ca4941d274e5baea8f73ebe2135f3c877fc524ef8b798905f721f7bbf8d039ff0046bcac22768030344ead9f38f5fac858669f6fcfb67263cb61938f384d0b7dd2afa3a3176b6a37e13bbc4f4f9fe7345897a18ba9ed5fcc348ce4792890b7d616b7e601e60454c8cdbf1000c94d9e9747977339bcdaf6a7e30b2b5dd46cc967c39b7d760605230254e94b46a73dc9bd03e400feb7cf0667177bbb85b5e2fbed4fc25e7fc7dd5d97439bffaf86f49f577c9f2bf258f69df634faf66c44d4ed0a7091b5a6549e45bdc1fb5f1370c1b26650e66d29dc667cb7729911f0e973ebc64294e12df216a8f5c2f631a9171d5696c574ff991381d72bf9da98c5362bf267cdd610cf1d432d68ac2d80947da453fcec15708d047961b1b6fefbb44fce87d1ec746806c94d7589009563859aa4a43d1186fd15119aa4c37b0c9398fc7b992560805155220a3ac6e2c195f2ae10a89e81b67a592da101a1db030e031db460453485b90b545f33a7fc2fad2154ecb00c295468ba65416813c55b2d164aaca05634a682c885202082f00d04a5440a152c89f9efe00
```

payload (UTF-8, exactly; the JSON has it as a string):

```json
{"v":1,"kind":"circuit","name":"Half adder — lab 3 ✓","cdl":"(cedarlogic\n  (version 3)\n  (generator \"CedarLogic 0.3.6\")\n  (page 0\n    (gate \"AA_TOGGLE\"\n      (uuid \"1\")\n      (at 10 20)\n      (angle 0)\n      (lparam \"OUTPUT_NUM\" \"1\"))\n    (gate \"GA_LED\"\n      (uuid \"2\")\n      (at 18 20)\n      (angle 0))\n    (wire\n      (ids \"3\")\n      (seg \"0\" h\n        (pts 11 20 17 20)\n        (connect \"1\" \"OUT_0\")\n        (connect \"2\" \"N_in0\")))))\n","createdAt":1759572000000,"modifiedAt":1759575660456,"device":"Safari on iPhone","deviceId":"0a1b2c3d4e5f60718293a4b5c6d7e8f9","base":{"name":"16002a9defe5263b6e5c820741ddcb7612135ed53fd45acde5c50f54164e664b","cdl":"06c874731fa078530b826daece91b3e5997f558ab6a081aa0c0aad61d2aef92d"}}
```

data (base64url of the envelope):

```
AQHQ0dLT1NXW19jZ2tvJnynLilIvP9R-Pw2vExgSIQsFtvJKitCfmCoNlI6dZteARsZYp6Cg-M97I1Cz8a24TwRYT1pjqDFiA8bC2qE3sGpd_yRtMexVLVw0ecfTxxUozqZ7vZveJVCNl9nJDh_oNvmCCxkRehz7Hr_1nY8B6ZA0FUVKJOGd5H3sLjzBEAjlyk-gmZ7nkkSIsG9a3eeG-z9O_kJkO9wknJKCf5-uslZUAItZSi91azwciRU-wv9ivv7fBT9896F4SdMVoPuBPZOanibfD9nYfXbYKzsR7rbUgtsB4fskgAZyPyaHJ09h5bPmEPNanCd_8gjVgSidtXXFO8glKut5908i_SBancjeHlqwCzRvEa1ZxfQBiE_AOMsIvjuf8NYS_8v6CRH9oJ2vWGEhjL3f4lKHklf2DPM0UyC1-i9dh1X0m901ZVE_P3yT-tk-PTZ4cXf7rMZVLsD85C0dnbI7LmrS5_Vns_jbiEuHEjeGp05mq0Fr4XaSaBxrCptIIKIN_Yj8hlJ8_v6wvRJ5tYj82mkG3Xt3tUzf5Tu13T0vXjiyeHI2r2PVdsqTe4q4__h_FOK6PzdeLCk2gcMs2I1ELu-ciWK9Oo4gLumIaH3jE6kUqNLvMjRlK28
```

**3. raw characters, ver 3 (decrypt, parse: name and cdl must equal the UTF-8 given in hex)**

```
recordKey    e7333f3b411aec02c98d925aa938760f5ff075dbe21c1435332a57f5eeaec1d7
recordId     3b241101-e2bb-4255-8caf-4136c566a962
ver          3
flags        0
nonce        e0e1e2e3e4e5e6e7e8e9eaeb
AAD          cedarlogic-sync/1|3b241101-e2bb-4255-8caf-4136c566a962|3|0
payload      818 bytes, SHA-256 8bb5bfcc684510af2ca35d5f1886ad22eaeca5817abec5ad275733f621a46bf1
envelope     848 bytes, h = 2b12c41e3811c1e89d8dc23096cf81c7
kind         circuit
name UTF-8   547269636b79202271756f74657322205c206261636be280a8736c61736820f09f9880
```

payload (UTF-8, exactly; the JSON has it as a string):

```json
{"v":1,"kind":"circuit","name":"Tricky \"quotes\" \\ back slash 😀","cdl":"(cedarlogic\n  (version 3)\n  (generator \"CedarLogic 0.3.5\")\n  (page 0\n    (gate \"AA_TOGGLE\"\n      (uuid \"1\")\n      (at 10 20)\n      (angle 0)\n      (lparam \"OUTPUT_NUM\" \"0\"))\n    (gate \"GA_LED\"\n      (uuid \"2\")\n      (at 18 20)\n      (gparam \"LABEL_TEXT\" \"a\u0007b \\\"c\\\"\")\n      (angle 0))\n    (wire\n      (ids \"3\")\n      (seg \"0\" h\n        (pts 11 20 17 20)\n        (connect \"1\" \"OUT_0\")\n        (connect \"2\" \"N_in0\")))))\n","modifiedAt":1759575700000,"device":"Levi’s MacBook Air","deviceId":"5f0c2a9e7d3b41c8a6e2f1b0c9d8e7a6","base":{"name":"16002a9defe5263b6e5c820741ddcb7612135ed53fd45acde5c50f54164e664b","cdl":"c3b7a36c43aaf25996aa58ceaa9b8d1e65d4c1a5311a348976b3657b87272d15"}}
```

data (base64url of the envelope):

```
AQDg4eLj5OXm5-jp6uv__L7HTJMCCJ7wDpM8g9BLKVzKL0oPfnFtD-JCZwSldZ6e50dhV66IzUiIGzNMkfxOfNtZomnSyTpMeLw-t28JWdNpkv1BnaftBx0gsChEkuU83FsQT5DD1hr6Jxpap-Y-LPl4dnw3ry5-dVRJu0QbPXtrEqzg3o8lLM2CiWPymhSqNV1FjxT8yv90UBR_JXE8z3R-Qd01FMMLUeV-YCK3Bx9HruDqQv_x2nP7y0o1inc-xO7rkhniN5BvTPeHsUrpJcrwcKrYlNEM0CgjTxX5bh5rJXfk6RtWj6gYnuqyh2dl37M4ic8XPg63qG7cs_tnjuij2g0f-OYuYdLJ-FZ4k8VV-fcxyT-MyBIdS53fkUp7QETj62lgGOtGTFFbP1JluiErEXs77biU5_2CYq51R1uCPRl6L_3_zv2wshwyP1vLRAo7IUKcY1PcAQIzwFPgspFHJGtzAoNu7X8kjQtdznBSMqDH6jQ-KU-IP7CxPeJQjXN89Vq882_G_gJuK5vrRzbb3THPRyRtH8YxZsYBl_LSJmG44v07FIaUIhrm3fFoIGt7EmaQGM6-aCZnP9Bpa1He2C7PrWiBiBV69vUTRVoFOK0WYsEY8h25w69O_HzKykWryX4ouuBUhFGURfDUbY49WwWbAyudoB6JgfYHo802NY9-XKqLZTRmltm1V407xjpXjaqLylv8L5Ny7vq1-gLEE6kc_1MXdnDaXs5CTa08ZbWTYbd-XnTP_O3UcvcGeJkP5qxPU1nkGcPbHXlkVywzo3WXcF5qcxTWy2G2SXkvqCnkpPJ0IJY7ICMnJrFBQMNLjBu5jp-98iz5v0V_nBMVQ1DkG22UyUPtaIFMFotcFG1UD7uK5WmGYSOEcqrYtkimZCqtkrC02eYrMBRRfE__yGofE2uxzSYbCy2bgsDgUQw3n2bLEicMmbGheVBEEL5cNZKYTCJCW32N86mV9xII9g7KK4ssbik6YwoTdoKLjD8RzrQDuLqYfo_-rIJSGTcaiYbkfEehjQbez3sx4rpMy0EtN_EvNupjQsU6CNBAVTjwT8VHZodYpwt7wp57My5fItRojrIT7z6nXzf7kciAHCNC8W8oy_2Nvp-hj-8
```

**4. escapes, ver 4 (decrypt, parse: name must equal the UTF-8 given in hex; unknown fields ignored)**

```
recordKey    e7333f3b411aec02c98d925aa938760f5ff075dbe21c1435332a57f5eeaec1d7
recordId     3b241101-e2bb-4255-8caf-4136c566a962
ver          4
flags        0
nonce        f0f1f2f3f4f5f6f7f8f9fafb
AAD          cedarlogic-sync/1|3b241101-e2bb-4255-8caf-4136c566a962|4|0
payload      814 bytes, SHA-256 28c5bca269221b4c5281e5986776e9d50eefb442c67e2482ce148bba3a112067
envelope     844 bytes, h = c63302d3a69968aacf68059c05236af7
kind         circuit
name UTF-8   456d6f6a6920f09f98802c20736570e280a820616e6420726177e280a82c20612f622c20c3a90907
```

payload (UTF-8, exactly; the JSON has it as a string):

```json
{"v":1,"kind":"circuit","name":"Emoji \ud83d\ude00, sep\u2028 and raw , a\/b, \u00e9\t\u0007","cdl":"(cedarlogic\n  (version 3)\n  (generator \"CedarLogic 0.3.5\")\n  (page 0\n    (gate \"AA_TOGGLE\"\n      (uuid \"1\")\n      (at 10 20)\n      (angle 0)\n      (lparam \"OUTPUT_NUM\" \"0\"))\n    (gate \"GA_LED\"\n      (uuid \"2\")\n      (at 22 20)\n      (angle 0))\n    (wire\n      (ids \"3\")\n      (seg \"0\" h\n        (pts 11 20 17 20)\n        (connect \"1\" \"OUT_0\")\n        (connect \"2\" \"N_in0\")))))\n","modifiedAt":1759575800000,"device":"Chrome on Android","deviceId":"9f8e7d6c5b4a39281706f5e4d3c2b1a0","base":{"name":"8f6c47aa56a354beba9d32389d02ad8323f3851cf373112f542005b2e4aba08a","cdl":"5bff4b6094303f829964261003e5c3318b2d637afdceb2727e647f7f33a9d199"},"futureField":[1,{"x":null}]}
```

data (base64url of the envelope):

```
AQDw8fLz9PX29_j5-vvtMe8zUiiCEUdOMPg90Xs-QFe5UoWYtbQJxplI06Z4-CHPdnghUHG3oq8Xg3-SDt4UZMHuU3OLX4T2zRwRf1sSvj0OqJNnXHcvJ6Yz4Xy9M3UAqCLZ1N2s-8bhwC-Ai4mYCX9k_vUL9nFxf9xnGm7DIsW865MaP1uUBUhHKz0nBEojGywFupSsBftQPEuym6j1TM_zOXjdIJLdbh4i72l4Xy4aG5LNYA4j_UA3o-waA7CauoNoBSi1UXJn6rvr0gnJYF1yY4ehQt3hV7n0qQTc8VkyCISh21Zs14fqiolWKQjsxq1xXE3_Pkwt5C2GUy6O-iISXO4TIRAFhJ1s40xvg4GWujvw4x5VoxkBo1p1ZG-SRa3u5fg6CWlG3L3BbwQBHg0Wy0tzC8xiA9nivsGBi9eZP0U1C7EEWoY25Kq2G4kF66i2lTLA55lJEbdRO9Ap3x4AuP_PkHAhMujXUh6e1a8un2iB_CnkusJJTAdBlnXFzX0AnL5uE9nll7SgK-2ISd7u1I415sC2b78fzggjPm60N7Jbe73-dpBveunEo0v_fnLVItx1gtBajiU7OlwJAqOMQbIVy2uRbUChts0GodSHN_3lYTzjmQaKGvc5ZfauNFeH7n2EnYY4ewI5xiM5ABSzoaN-xLdvnHnrPB8Zo--50ewHs4MIwzo3XlAuFkOZ2xq3kCQnRbUK72ADKxBjkeHJG4Yt7W01tM1UmpSnaEAkkKCqhyWkDcJMaIV-sxahnqGM7h6M2JSKt5M045f6e14RdRt4UyyfGZwS_zp03aOIGlEDBgX9NLLWaEhWgAQBnWs7SH4HaicOy5daK-CvArxS1jBQGSclfeDSf3p5jSxIbbOVpE-SxxAkJbr97VgBLTHM-A4LwMCnVgiFmZ-1OoVrgDvNSpCUhNG1TW06mrLPgrabqrbPy-0qsqdzvGTidU0RBRusWD9Hw1wLO-UZr5fmeqnHTJdDteI_6i-yPivTUlGVrVO_tFqGhKddJzAnUGiSYXl7wIxbAgv5JIfElMJD5xLBbOrZgUzvtdY3se6ebgkhtP2CPigXHStRcsNfOpe6hyQix01JOlifd7UhGx2wMDzvUyCoh-Zejw
```

**5. a deletion (sealed tombstone), ver 5**

```
recordKey    e7333f3b411aec02c98d925aa938760f5ff075dbe21c1435332a57f5eeaec1d7
recordId     3b241101-e2bb-4255-8caf-4136c566a962
ver          5
flags        0
nonce        a0a1a2a3a4a5a6a7a8a9aaab
AAD          cedarlogic-sync/1|3b241101-e2bb-4255-8caf-4136c566a962|5|0
payload      284 bytes, SHA-256 bd541ed61783ad06558588d94abeeaacbca287eaf4febe8bfab7a23b1edeebe8
envelope     314 bytes, h = 2a3074782be56aa6d0d0deae3984b3da
kind         deleted
```

payload (UTF-8, exactly; the JSON has it as a string):

```json
{"v":1,"kind":"deleted","deletedAt":1759575900000,"device":"Levi’s MacBook Air","deviceId":"5f0c2a9e7d3b41c8a6e2f1b0c9d8e7a6","base":{"name":"cfe837dbc3157db38fb96f2737ccf18fcc3a33f70888e94e00370d2bca7ec030","cdl":"751528219564c99883322c129466a65b8b884bfd3b193e9847c77219e509e7a3"}}
```

data (base64url of the envelope):

```
AQCgoaKjpKWmp6ipqqtQTqVTLc0J5BqT8QiWsOn3oZTPfprwS5B6lKw5_HCZ7K5D3zVYeyiZq_Bb5j1fLeJ2Q3HpgGFpg3vt7bFcf6Dgi9hJzX5ZYRSAy__RtpL97Ljg2zC4w8ZxeKoMVimvKrze76Nqq6_esXe1qjBD1r1j-ENNjwCGFcNjpWwhoTBN24kj_Q4TDfIfaUCOLi4uXA-_Aq9uwQIAgNoUW8XgIWeFMajuF50S6IwDkQEi9DmJJG5BDY60XAPwBz5pTY_yHD8LPQU5FUsOw0jrucj0uAQOluF8Hz0U06RhnUQs8BvfxrqVKWwWj-z-VJZCh1e7in9qhysEq5LBDNAJGG7rFKrdHVXKlTuu_lqJIWbjXJVPM94btNH1KLwWdYydqhJRgqaw0HmFP4bQ_q2TvVU
```

**6. a device record, ver 1**

```
recordKey    e7333f3b411aec02c98d925aa938760f5ff075dbe21c1435332a57f5eeaec1d7
recordId     d072d9d7-722d-472b-aa6b-e355ad92c15c
ver          1
flags        0
nonce        b0b1b2b3b4b5b6b7b8b9babb
AAD          cedarlogic-sync/1|d072d9d7-722d-472b-aa6b-e355ad92c15c|1|0
payload      153 bytes, SHA-256 94a52323ac1af9bce12b45cda13bfe3e462aa066e91f62334a405e01f6bc0382
envelope     183 bytes, h = d9e29ae49718885902c4f42d1f461dab
kind         device
```

payload (UTF-8, exactly; the JSON has it as a string):

```json
{"v":1,"kind":"device","device":"Levi’s MacBook Air","deviceId":"5f0c2a9e7d3b41c8a6e2f1b0c9d8e7a6","client":"mac/0.3.7+801","lastSyncAt":1759575960000}
```

data (base64url of the envelope):

```
AQCwsbKztLW2t7i5urvpsInzhanZC1-o40o2ie1M_9dbRU5O7OTFF4fp-FuWxEZJDh6Avd7xSzR5ADWCK-azOt5rSiuFKCu9qMMcchBBCQWo0ebY-9Ybu0O83AeAe3Mzw1d8FkAd8ZRnjqry1QNu2qyXhAu6Ti87OxqmszjU31aIWQTvFZk0tYI4rcJ8ZYI_aJDeltOvF92fLbchR0k3Wdjdt5CWln6J2zUHNe1PL-80NMH_8VQC
```

#### 7.1.7 Payloads that open but must be refused

Sealed with the `counting` record key under record id `bf1d2d5e-96a4-4f80-9235-bcaa2d14a882`, ver 1. `why`: `newer` = hands off (§4.10), `invalid` = treated as damaged. The full payloads and their envelopes (`data`) are in `vectors.json`.

| case | why | payload (hex) |
|---|---|---|
| payload v 2 | `newer` | `7b2276223a322c226b696e64223a2263697263756974222c226e616d65223a2248616c662061…` |
| kind 'folder' (unknown) | `newer` | `7b2276223a312c226b696e64223a22666f6c646572222c226e616d65223a2248616c66206164…` |
| unpaired surrogate escape in name | `invalid` | `7b2276223a312c226b696e64223a2263697263756974222c226e616d65223a225c7564383364…` |
| invalid UTF-8 byte in name | `invalid` | `7b2276223a312c226b696e64223a2263697263756974222c226e616d65223a22ff48616c6620…` |
| modifiedAt is not a whole number | `invalid` | `7b2276223a312c226b696e64223a2263697263756974222c226e616d65223a2248616c662061…` |
| modifiedAt is a string | `invalid` | `7b2276223a312c226b696e64223a2263697263756974222c226e616d65223a2248616c662061…` |
| modifiedAt past 2^53 | `invalid` | `7b2276223a312c226b696e64223a2263697263756974222c226e616d65223a2248616c662061…` |
| cdl missing | `invalid` | `7b2276223a312c226b696e64223a2263697263756974222c226e616d65223a2248616c662061…` |
| base is not two hashes | `invalid` | `7b2276223a312c226b696e64223a2263697263756974222c226e616d65223a2248616c662061…` |
| not an object | `invalid` | `5b312c325d` |
| not JSON | `invalid` | `7b2276223a312c` |

#### 7.1.8 Must not open

Each of these (with the `counting` record key unless another is given) must fail: tag mismatch, or the envelope refused before decrypting. Each is record 1's envelope, changed as named or opened with the wrong id, ver or key; the full `data` of each is in `vectors.json`.

| case | recordId | ver | data (first 24 chars) |
|---|---|---|---|
| ver 2 claimed for the ver 1 envelope | `3b241101-e2bb-4255-8caf-4136c566a962` | 2 | `AQDAwcLDxMXGx8jJysu2m8nE` |
| another record id | `9b2e7c1a-5d34-4f8e-b1a2-3c4d5e6f7a8b` | 1 | `AQDAwcLDxMXGx8jJysu2m8nE` |
| flags byte set to 1 | `3b241101-e2bb-4255-8caf-4136c566a962` | 1 | `AQHAwcLDxMXGx8jJysu2m8nE` |
| last tag byte flipped | `3b241101-e2bb-4255-8caf-4136c566a962` | 1 | `AQDAwcLDxMXGx8jJysu2m8nE` |
| envelope version 2 | `3b241101-e2bb-4255-8caf-4136c566a962` | 1 | `AgDAwcLDxMXGx8jJysu2m8nE` |
| unknown flag bit 0x02 | `3b241101-e2bb-4255-8caf-4136c566a962` | 1 | `AQLAwcLDxMXGx8jJysu2m8nE` |
| truncated to 29 bytes | `3b241101-e2bb-4255-8caf-4136c566a962` | 1 | `AQDAwcLDxMXGx8jJysu2m8nE` |
| the 'zeros' secret's key (key `d149c9911677bf9d5364b8b20bf3f5d9cd1ee2a9937ffadbfac7f6d294193e2c`) | `3b241101-e2bb-4255-8caf-4136c566a962` | 1 | `AQDAwcLDxMXGx8jJysu2m8nE` |

### 7.2 Protocol scenarios

Each client's suite runs these against the real server logic (web:
`netlify/lib/sync.mjs` in-process and the mock server; C++ engine: the
FakeServer in `SyncTest.cpp`, a port of `ref/sim.py: Server`, plus the mock
server over the platform's `http` hook). `ref/sim.py` passes every row marked
**sim** (`sNN_…`, names match); a client passes when the outcome column holds.
"App" = a C++ engine client (keeps versions, writes v3), "web" = the JS client
(copies, writes v1 XML). Times: each local edit is one tick later than the one
before, so "later" is well defined. "Same everywhere" = the same names and the
same structure hashes on every device (an app and the web may hold different
text for the same circuit).

| # | Scenario | Outcome that must hold | sim |
|---|---|---|---|
| 1 | App A has "Adder", turns on sync; web B previews (code typed in lower case with spaces), then links | the preview says 1 circuit from A's device name, "Adder"; B has exactly "Adder", the received text byte for byte | sim |
| 2 | Link with a valid code nobody turned on | GET space → 404; link refused with the §4.8 sentence; **no space and no auth record created** | sim |
| 3 | Same circuit edited on app A (earlier) and app B (later) while both offline; both sync, A syncs again | both: status was "offline" while down; afterwards one "Counter" with B's circuit everywhere; A's own edit is in A's Version History | sim |
| 4 | Same, but web W edits earlier and app A later | everywhere: "Counter" = A's circuit and "Counter (from W's device name)" = W's | sim |
| 5 | Same, but app A earlier and web W later | W keeps its edit as "Counter" and gets "Counter (from A)"; after A syncs, both devices have both; A's "Counter" = W's circuit | sim |
| 6 | A deletes "ALU" while B edits it; sync in either order | "ALU" exists on both with B's edit; a notice on the deleting device | sim |
| 7 | A deletes "Mux"; web B syncs | B: "Mux" in Recently Deleted, not in the list, with a notice; server: a sealed tombstone that decrypts to `kind:"deleted"` with no name or cdl | sim |
| 8 | A renames "Untitled" → "Traffic light"; B syncs. Then both rename (B later) | B gets the name, no new version; then both end "Lights B", no version, no copy | sim |
| 9 | App and web both flip only a switch (the web saves XML, the app v3, generators differ) | no copy, no version; both end with the newer text, byte for byte | sim |
| 10 | A has X, Y; B (app, and again as web) has X (identical), Z, Y (same structure, a switch flipped) and another Y (different circuit); B links | both devices: X, Y, Y, Z; the server holds 4 circuits (no duplicate X; the switch-flipped Y mapped onto A's Y, across formats for the web) | sim |
| 11 | `maxRecords` = 3; A has 4 circuits and turns on sync | 3 circuits on the server; A's status "full" with the 507 sentence; the device records don't take circuit room; after B deletes one, A's next sync uploads the fourth and A shows "synced" | sim |
| 12 | `maxRecordBytes` tiny; A has a big and a small circuit | small one synced; the big one shows "Too big to sync"; the next cycle makes **no** write attempt for it (one request: the pull); after it shrinks it syncs | sim |
| 13 | A pulls, then B writes the same circuit (later), then A pushes | A's write gets 412, A resolves (B's is newer), keeps its own as a version, ends equal to B | sim |
| 14 | A's first write lands but the answer is lost; A edits again; A syncs | one circuit on the server; no copy, no version; the record ends with A's second edit (the `sent` rule) | sim |
| 15 | The exact same write sent twice | the second gets 200 with the same ver (server replay rule) | sim |
| 16 | B offline for > 400 days while A deletes "Gone" (tombstone purged); B comes back (a) unchanged (b) having edited "Gone" | B does a full pull; in both cases "Gone" is sent again by B and is back on A (a forgotten record is never trashed); the write is accepted at a ver above the purged one (ghost) | sim |
| 17 | The bearer token alone tries `DELETE /spaces`; then device B deletes the synced copy; device A syncs | 403 without the delete token; then A: status "gone", sync off, circuits kept; PUT space with the same code → 410 | sim |
| 18 | Turn Off (keep), Turn On again, Turn Off with "remove synced circuits" | first: circuits stay; last: every synced circuit is in the trash | sim |
| 19 | A record's last envelope byte is flipped on the server (at a new ver) | B (linking) doesn't apply it, lists it as damaged once, doesn't refetch it next cycle, stays "synced"; A (which has the circuit) sends its copy over it; then B gets it | sim |
| 20 | Three devices (app, app, web) edit the same circuit and the web adds one; two rounds of syncing | all three: same everywhere | sim |
| 21 | **Network failure mid-push** — batches of one; the connection drops after 1 of 3 new circuits is written | next cycle writes the other 2; 3 circuits, no duplicates | sim |
| 22 | A server answer of `429` with `Retry-After: 120` | no request for 120 s; then the cycle completes | clients |
| 23 | (apps) A remote change arrives for a circuit whose window has unsaved edits | not applied; the cursor stays before it; once the window saves (later edit), the conflict rules run: the window's edit wins, the remote one is a version; nothing lost | sim |
| 24 | (apps) the circuit is open with no unsaved edits | the window reloads in place with "Updated from …"; undo is cleared for it; a runtime-only change to a window used in the last minute waits | clients |
| 25 | (apps) 10 of 12 synced folders vanish at once | nothing is deleted on the server; the mass-delete question is asked; "Bring Them Back" recreates them | sim |
| 26 | (apps) the library is restored from a backup (P older, Q deleted since, R made since; `.sync-library-gen` behind) | no deletes sent; R comes back; Q (deleted after the backup) goes to the trash; P ends with the newer edit (the older kept as a version); no duplicates | sim |
| 27 | (apps) a second process holds the lock | no requests from it; status "Syncing in another CedarLogic window." | clients |
| 28 | (web) two tabs open; one edits a circuit the other has open with unsaved edits | one cycle at a time (Web Locks); the other tab's list updates (BroadcastChannel); its refused save keeps both versions (§4.13) | clients |
| 29 | (web) opening a synced v3 circuit without touching it | no write; the record's `ver` stays | clients |
| 30 | A record whose payload has `"v":2`; then the device edits that circuit | not applied, not overwritten; "saved by a newer CedarLogic" shown; the next cycle makes no write attempt (one request: the pull) | sim |
| 31 | After linking, A and B each make an empty "Untitled"; both sync | two "Untitled" circuits on both (the join rule doesn't run outside a join) | sim |
| 32 | W's write lands but its answer is lost (a `pagehide` push); A pulls it, edits on top, pushes; W syncs (W web, and again W app) | no copy, no conflict version anywhere; W fast-forwards to A's edit (`base`) | sim |
| 33 | B renames "Untitled" → "Traffic light" (earlier); A moves a gate (later); both sync (app/app, web/web, app/web) | both: "Traffic light" with A's circuit; no copy | sim |
| 34 | B's clock is a day slow; A edits, then B edits later; both sync | B's (later) edit wins everywhere (server-corrected time) | sim |
| 35 | The server serves an older envelope of a record again as current | no device goes back; the device that has the newer copy sends it again at a higher ver; every device ends with the newer edit | sim |
| 36 | The server flags every record as deleted without sealed tombstones; then raises `purgedSeq` and lists nothing | nothing is trashed on any device; the circuits are sent again | sim |
| 37 | A deletes 10 of 12 synced circuits on purpose; B syncs and answers Keep Them | A was asked first (local guard); B is asked (incoming guard), keeps them; they come back on A too | sim |
| 38 | The space document is replaced by an empty one with a new epoch | A notices ("reset"), sends every circuit again; a device linking afterwards gets them all | sim |
| 39 | Bob's laptop has 3 circuits; I preview Bob's code | the preview says 3 circuits from "Bob’s laptop"; nothing of mine was sent; the server is unchanged | sim |
| 40 | Two web devices linked, no circuits, both syncing daily for 8 days; then the space document is lost | the space isn't removed while used; later a 404 makes the device re-create it with the same code; everything is sent again; both "synced" | sim |
| 41 | A switch is flipped in the app (runtime-only), then a gate moved | a non-flush cycle doesn't send the switch flip; it goes after 10 minutes; the move goes at once | sim |
| 42 | A long-offline device pages through changes (`limit` 1) while the daily cleanup purges tombstones mid-way | the pull restarts as a full pull; the devices converge | sim |
| 43 | A switch flipped online (earlier) and a real edit in the app | no conflict, no copy: the real edit wins quietly | sim |

### 7.3 Per-client suites

- **Server** — §3.11 (`scripts/test_sync_server.mjs`), then `ref/sim_http.py`
  runs every **sim** scenario of §7.2 with the reference clients talking HTTP to
  `scripts/sync-mock-server.mjs`.
- **Web** — `scripts/test_sync_client.mjs` (Node 20+, `globalThis.crypto`):
  the vectors from `scripts/fixtures/sync-vectors.json` (a copy of
  `ref/vectors.json`, with `ref/fixtures/` and `ref/gate_defaults.json`), then
  scenarios 1–22, 28–43 with two or three `sync-core.js` engines on in-memory
  libraries against `handle()` with the memory stub, and once more against the
  mock server. Then by hand (§10 W).
- **C++ engine** — `clsync::selfTest`: vectors (from `SyncVectors.h`) +
  scenarios 1–27, 30–43 on the FakeServer with temporary library folders.
  Run on all three CI systems with the real hooks: Linux `sync_check`,
  Windows `--sync-test`, Mac `mac/Tools/sync-check.sh`; and against the mock
  server.
- **Interop, once, before release**: a Mac, the Linux app (on the Pi), Windows,
  Online and a phone (installed app on iPhone, Chrome on Android) on one code
  against a deploy preview (`CL_SYNC_URL`, `SYNC_ORIGINS`), going through
  scenarios 1, 3, 4, 6, 8, 9, 10, 17, 32, 33, 39 by hand; then the owner's own
  checklist (build + tests + a manual checklist, no UI automation loops).

---

## 8. Privacy and security

### 8.1 What people are told (Settings › Sync › "How sync works", and the website)

> **How sync keeps your circuits private**
>
> There's no account. Turning on sync makes a secret code. Your devices use it
> to lock each circuit before it leaves the device, and only a device with the
> code can unlock it. CedarLogic's website stores the locked circuits so your
> devices can find them, but it can't read them: it never gets the code, so it
> doesn't know your circuits' names or what's in them.
>
> What the website (and its host) does see: that a set of synced circuits exists,
> how many there are and roughly how big, when they change, and the internet
> addresses your devices sync from — which can show, say, that they're used at
> the same school or home. CedarLogic uses addresses only to stop abuse and
> doesn't store them; the website's host keeps ordinary request logs.
>
> Keep the code private — anyone who has it can see and change your synced
> circuits. If someone else may have it, use Start Over with a New Code. Keep a
> copy somewhere safe: if you lose every device that has it, and the code, nobody
> can unlock the synced circuits. Not even us.
>
> Turn off sync any time; your circuits stay on your devices. "Delete Synced
> Copy" removes them from the website for good. If none of your devices syncs
> for a year, the synced copy is removed.

### 8.2 Threat model

| Who | Can | Can't |
|---|---|---|
| **The site operator, or anyone with a copy of the Blobs store** | See per space: id, epoch, creation and last-use time, record count, each record's random id, version, envelope size (compressed: a hint of circuit size), which records the writer called tombstones or device records, times of every change; request IPs and timing in Netlify's logs. Withhold records, delete the whole store, deny service. Link spaces to IPs. | Read names, contents, device names; tell which circuit is which; change a circuit (any change fails the tag: the record is "damaged" and the devices send their copy back); delete a circuit on a device (only a sealed tombstone does that, and many at once are asked about); roll a device back (the high-water mark ignores an older copy and the newer is sent again); move a record to another id or version (AAD); learn the code (only hashes are stored); write into a space without the token. |
| **A network attacker** | See that a device talks to `cedarlogic.netlify.app`, when, and roughly how much. | Anything else (HTTPS; and the payload is encrypted again inside). |
| **A TLS-inspecting proxy** (common on school and company networks) | See the bearer `authToken` in ordinary traffic; with it: read the encrypted envelopes and metadata, write garbage records or withhold them (devices treat garbage as damaged and send their copies back), fill the space up to its limits, serve stale envelopes (ignored by the high-water mark). | Read or change circuits; delete circuits on devices (no `recordKey`, so no sealed tombstone); delete the whole synced copy (needs the `deleteToken`, sent only for that one request) — unless it also sees that request. |
| **Someone who gets the code** (a screenshot, a shared computer, browser history, a shoulder) | Everything a linked device can: read, change, delete every synced circuit; link their own devices; delete the synced copy. They appear in the device list if they link a device that writes a device record. | Read circuits that were never synced. Keep access after Start Over with a New Code. |
| **Someone who sends you *their* code or link** ("scan this for the lab files") | If you press Link after the preview, your Your Circuits is added to a space they can read (and later delete from). | Anything without your two presses: links never link by themselves, and the preview shows whose circuits the code holds ("14 circuits from Bob’s laptop") before anything of yours is sent. |
| **Malware or another user account on a device** | Read the secret file / IndexedDB like any of the person's files (it's protected like the circuits themselves: 0600, DPAPI per user on Windows). | — |
| **A script injected into the website's pages** | Read the code from IndexedDB and every circuit, call the same-origin admin API. | — so it must not happen: names and other untrusted text only via `textContent`, a strict CSP on every sync page, no third-party scripts (§4.13, §5.4). |
| **Abusers of free storage** | Store ≤ 10 MB of encrypted data per space, ≤ 50 new spaces per IP per day and ≤ 50 MB written per IP per day, within the site budget; rotate IPs to get more; post a code publicly to share a file (≤ 200 MB served per space per day). | Run the site past its byte budget or its daily request budget: both stop new growth / pause sync for the day, keeping feedback and stats alive; inactivity expiry; `SYNC_CLOSED`. |

Notes:

- **Rotation**: Start Over with a New Code (§4.11) replaces a leaked code; the
  device list (device records) shows a device nobody recognises.
- **Rollback, withholding and deletion** by the server: a device never goes
  back below a version it verified, never trashes a circuit without a sealed
  tombstone, asks before trashing many, and treats a "forgotten" or damaged
  record by sending its own copy. What a malicious server *can* still do is keep
  a newer version from a device that has never seen it (a newly linked device can
  be served an older but authentic set), and deny service.
- **Proof of possession** (signing each request with a key instead of sending a
  bearer token) would close the TLS-proxy row; out of scope for v1.
- **Metadata**: envelope sizes aren't padded; a circuit's compressed size
  reveals little (circuits are many similar lines). Not worth the storage to
  hide.
- **Randomness**: secret, nonces and record ids come from the platform CSPRNG
  only; a failure stops the operation. A repeated (key, nonce) pair would break
  GCM; 96-bit random nonces make that negligible at these volumes (≪ 2³²
  encryptions per space).
- **Rate-limit records** key on an HMAC of the IP (IPv6: its /64) with a server
  secret, and are deleted daily.
- **Shared and lab computers**: a code in a link stays in browser history and a
  linked browser keeps the code; the Link sheet says to turn sync off on shared
  machines (§5.4).

---

## 9. Order of work, and the contract between packages

1. **S — server** first: `netlify/lib/sync.mjs` + memory stub + mock server +
   tests; deployed behind a deploy preview. `GET /health` answers `{ok:true,
   protocol:1}`. The mock server is what everyone else develops against.
2. **In parallel, from day one** (they need only §1–§2 and `ref/` to start):
   **W** (`sync-core.js` first: vectors, then scenarios against `handle()`), **E**
   (`SyncProtocol.cpp`, `SyncStructure.cpp`, `SyncJson.cpp` + vectors first, then
   `Sync.cpp` + FakeServer scenarios). **M**, **L**, **X** write their hooks and
   Settings pages against `Sync.h`, checking their crypto hooks with the vectors
   before the engine exists (a 30-line test like `ref/check_openssl.cpp`).
3. **UI** on every platform (§5), each against the mock server, then the deploy
   preview.
4. **Interop day** (§7.3), then release: site first (Your Circuits online works
   without sync; sync can't be turned on by anyone until the apps ship, if
   preferred — `SYNC_CLOSED=1`), then the apps' betas.

The contract, so nobody needs to talk to anyone else:

- Bytes: §1, §2.2–§2.4, proven by §7.1. Any disagreement → `ref/` wins.
- HTTP: §3.2–§3.4 exactly (paths, headers, bodies, status codes, `error` codes).
- Behaviour: §4, proven by §7.2. Any disagreement → `ref/sim.py` wins.
- Words: §5 (keep them; platform conventions for button order and capitals).

---

## 10. Implementation packages

Six packages. Each lists the files it owns (nobody else edits them), what it
needs from the others, and the tests that say it is done. Paths are in
`cedarlogic-site` (S, W) or `Cedarlogic` (E, M, L, X; branches `mac/native`,
`private/linux/native`, `w5/integrate`). Every package copies `ref/vectors.json`,
`ref/vectors.h`, `ref/gate_defaults.json` and `ref/fixtures/` as its test data
and MUST NOT edit them (a disagreement goes back to `ref/`).

### S — Server (Netlify Functions, the Blobs stub, the mock server)

Owns:
- `netlify/lib/sync.mjs` — `handle(req, context, deps)`: §3.2–§3.9.
- `netlify/lib/memory-blobs.mjs` — an in-memory stand-in for a `@netlify/blobs`
  store: `get`, `getWithMetadata` (`{data, etag}`), `set`, `setJSON` (with
  `onlyIfMatch` / `onlyIfNew` → `{modified, etag}`), `delete`, `list({prefix,
  paginate})`; deterministic etags; and fault injection for tests:
  `failNextConditional("trap")` (answers `{modified: true}` with no etag and
  doesn't write — the real library's behaviour on a lingering 5xx),
  `failNextConditional("lose")` (writes, then throws), `latency(ms)`,
  `snapshot()`/`restore()`.
- `netlify/functions/sync.mjs`, `netlify/functions/sync-cleanup.mjs`
  (`cleanup(deps)` exported from `netlify/lib/sync.mjs` so tests call it).
- `netlify/lib/common.mjs` changes: `allowIn(store, key, limit, windowSec,
  {cost, failOpen})`, `rateKey(ip, pepper)` (HMAC, IPv6 /64), and the
  `modified && etag` fix in `allow()` and `changeItem()`.
- `scripts/sync-mock-server.mjs` — `node scripts/sync-mock-server.mjs [--port
  8787] [--limits '{"maxRecords":3}']`: `handle()` on the memory stub at
  `http://localhost:8787/api/sync/v1/…`, `deps.now` = real time + an adjustable
  offset, `ip` = `127.0.0.1` or `x-mock-ip`. Test controls (only in this script,
  under `/__mock/`, JSON): `POST reset`, `POST clock {advanceMs}`, `POST cleanup`,
  `POST limits {…}`, `GET dump` (the documents), and misbehaviour for clients'
  tests — `POST tamper {space, op: "rollback"|"forgeTombstones"|"forgePurge"|"damage"|"resetSpace"|"loseSpace", id?}`,
  `POST dropNextAnswer {space}` (apply the next write, then close the
  connection without answering), `POST fail {status, retryAfter, count}`.
- `scripts/test_sync_server.mjs` (§3.11) and `ref/sim_http.py` (a `Server`
  stand-in for `ref/sim.py` that talks HTTP to the mock server, so the reference
  scenarios run end to end).
- `netlify.toml`: the function's path and rate-limit rule; nothing else of W's.
- Env (documented in the README/HANDOFF, set in Netlify): `SYNC_RATE_PEPPER`
  (required), `SYNC_ORIGINS` (empty in production), `SYNC_NEW_SPACES_PER_DAY`,
  `SYNC_MAX_TOTAL_BYTES`, `SYNC_MAX_REQUESTS_PER_DAY`, `SYNC_CLOSED`, `APP_KEY`
  (exists).

Needs: nothing (first). Reads `ref/` for the scenarios.

Acceptance:
- `node scripts/test_sync_server.mjs` passes every case of §3.11, including the
  Blobs trap, ghosts, the per-context store names and the gate table.
- `venv/bin/python ref/sim_http.py` passes every **sim** scenario of §7.2 against
  the mock server.
- On a deploy preview: `GET /health`; a PUT/write/changes/fetch round trip with
  `curl`; the store is `sync-preview`, not `sync`; the cleanup runs on schedule
  and logs its counts (no ids, no IPs).

### W — Web and phone client (CedarLogic Online and `/app/`)

Owns:
- `public/assets/js/sync-core.js` — protocol + engine, no DOM, no IndexedDB
  (§4.13): codes, HKDF, envelope, payload reader, `fileText`, hashes, the
  structure readers and digest, pull/push/merge, preview, lazy pushes.
- `public/assets/js/sim-library.js` — the IndexedDB library (`cl-library`, `rev`
  compare-and-set), the Your Circuits sheet, the Sync sheet (Turn On, Show Code,
  Paste, Scan, preview/confirm, devices, Turn Off, Start Over, Delete), notices.
- `public/assets/js/qrcodegen.js` (Nayuki, MIT) and `public/assets/js/vendor/jsqr.js`
  (Apache-2.0), vendored unchanged.
- The `/sync/` page (via `scripts/build_pages.py`) and its script file.
- Changes in `sim.js` / `sim-mobile.js` / `sim-files.js`: unlock Your Circuits,
  open/save through the library (no save on open), the editor's refused-save
  rule (§4.13), `#k=` handling, untrusted text only via `textContent`.
- `public/app/sw.js` precache list + version; `netlify.toml` headers for the sync
  pages (CSP, `Permissions-Policy`).
- `scripts/test_sync_client.mjs`, `scripts/fixtures/sync-vectors.json` (+ the
  fixtures and gate defaults, copied from `ref/`).

Needs: S's `netlify/lib/sync.mjs`, `memory-blobs.mjs` and the mock server for
the scenarios (vectors and the structure digest can be done before).

Acceptance:
- `node scripts/test_sync_client.mjs`: every vector of §7.1 (byte-exact record 1,
  all structure hashes on the fixtures, every refused payload's `why`), then
  scenarios 1–22, 28–43 in-process and against the mock server.
- Lighthouse/console on a deploy preview: no CSP violations with the policy
  enforced; the service worker serves the new files offline.
- By hand: Turn On in desktop Chrome, Firefox and Safari; link by Scan Code in
  the installed iPhone app (Home Screen), by QR in Safari on iPhone, by paste in
  Chrome on Android (installed and not); a conflict makes "Name (from Device)";
  a delete lands in Recently Deleted with a toast; two tabs on one circuit lose
  nothing; Safari's 7-day line shows on Safari only.

### E — The shared C++ engine

Owns (on `mac/native`, merged into the Linux and Windows branches like the rest
of `mac/CedarCore/`): `mac/CedarCore/Sync.h`, `SyncProtocol.cpp`,
`SyncStructure.cpp`, `SyncJson.h/.cpp`, `SyncLibrary.cpp`, `Sync.cpp`,
`SyncTest.cpp`, `SyncVectors.h` + `SyncFixtures.h` (generated from `ref/`),
`QrCodeGen.hpp/.cpp`, `SyncCApi.cpp`, `include/CedarSync.h` (§6); a small
`mac/Tools/sync-cli.cpp` driver (link/pull/push one library folder from the
command line, for interop day) is optional.

Needs: the vectors and fixtures (from `ref/`); S's mock server for the
over-HTTP part of the self-test. Uses `format/` (read only) and the gate
library through `Config::gateDefault`.

Acceptance:
- `clsync::selfTest` on the FakeServer with the OpenSSL test hooks (CI on
  macOS and Linux): every vector of §7.1 (including the structure hashes of the
  three fixture pairs through `cl::readCircuitFile`/`cl::readLegacyCdl`, and the
  refused payloads through `SyncJson`), and scenarios 1–27, 30–43.
- The same against the mock server (`serverBase` set) over a libcurl/`curl` test
  hook.
- Sanitizers clean (ASan/UBSan, TSan for the threading of §6.2) on the self-test.
- No platform header included by any `Sync*` file; `Sync.h` matches §6.2 (the
  hooks packages compile against it unchanged).

### M — Mac client

Owns: `mac/App/Sync.swift` (hooks, §6.4), `mac/App/SyncSettingsView.swift`
(§5.1 incl. the preview confirmation, devices, Start Over), the Settings page
entry in `SettingsView.swift`, the sync line and problem lines in
`LibraryView.swift`, `thin()`'s `.txt` deletion and version notes,
`ShareLink.swift` (`cedarlogic://sync`), the quit path in the app delegate, the
per-URL page/camera store for reloads, `mac/Tools/sync-check.sh`.

Needs: E (link against `libCedarCore.a` with the engine); S's mock server for
manual testing.

Acceptance:
- `mac/Tools/sync-check.sh` passes (vectors + scenarios with the CryptoKit hooks;
  also against the mock server).
- `mac/build.sh` builds with no new warnings; macOS 14.
- Manual checklist: Turn On shows the code and a scannable QR; I Have a Code →
  preview names the other device; a remote edit reloads an open clean window with
  "Updated from …"; a window with unsaved edits is never reverted (type, then
  sync from another device); quitting with an unsaved circuit sends it within 5 s
  and never hangs; a sync-made version survives the app's thinning a week later
  (set the clock); Start Over makes a new code and the other devices stop.

### L — Linux client

Owns: `linux/App/SyncPlatform.{h,cpp}` (hooks, §6.4), the Sync page in
`linux/App/Settings.cpp`, the sync line in `linux/App/LibraryWindow.cpp`,
`cedarlogic://sync` in `linux/App/ShareLink.cpp`, `thin()`'s `.txt` deletion in
`linux/App/Library.cpp`, the quit path, `linux/CMakeLists.txt` (OpenSSL),
`linux/Tools/sync_check.cpp`, CI (`sync_check`, the AppImage `ldd` check, the
`.deb` install test on Ubuntu 24.04).

Needs: E; S's mock server.

Acceptance:
- `sync_check` passes in CI on x86-64 and arm64 (vectors, scenarios, mock server).
- The `.deb` installs and starts on Ubuntu 24.04 and Raspberry Pi OS bookworm; the
  AppImage's `libcrypto.so.3` resolves inside the bundle.
- `ps`/`/proc/*/cmdline` never shows the token while syncing (a CI check greps
  the curl command lines); temp files are gone after every request, including
  failures.
- Manual checklist as M, on a Raspberry Pi with its clock wrong at boot
  (scenario 34 by hand).

### X — Windows client

Owns: `windows/App/SyncPlatform.{h,cpp}` (hooks, §6.4), the Sync page in
`windows/App/Settings.cpp`, the sync line in `windows/App/LibraryWindow.cpp`,
link routing in `windows/App/ShareLink.cpp`, the generalised WinHTTP `request()`
(`Feedback.cpp` or a new `Http.cpp`), `thin()`'s `.txt` deletion in
`windows/App/Library.cpp`, the quit path, `windows/CMakeLists.txt` (`bcrypt`,
`crypt32`), the `--sync-test` switch and CI.

Needs: E; S's mock server.

Acceptance:
- `CedarLogic.exe --sync-test` passes in CI for x64 and ARM64 (MSVC) and the
  MinGW build (vectors, scenarios, mock server).
- The secret file is DPAPI-protected (unreadable from another account) and in
  `%LOCALAPPDATA%`, not the roaming library folder.
- Manual checklist as M, plus: a lab PC with a roaming profile doesn't carry
  another PC's sync state; an old wx-saved circuit with windows-1252 label text
  syncs and shows the same text online.

---

### Open questions for the owner (none block building)

- The QR code points at the website (`/sync/`), which offers "Open in the
  CedarLogic App" on computers. On a phone it links the browser (or, on
  Android, the installed app); the installed iPhone app scans with its own
  Scan Code button. OK?
- 1,000 circuits / 10 MB / one year of inactivity — fine as the free limits?
  (The first draft said 25 MB; the review asked for less.)
- A circuit deleted on one device while another device is offline for more than
  400 days comes back when that device returns (the safe direction). OK?
- Page names are lost when a circuit is edited online (v1 export). A v3 writer
  for Online fixes it later; ship sync without it?
- Should sync start closed on the website (`SYNC_CLOSED=1`) until the apps'
  betas ship, so Online's Your Circuits launches first without sync?

---

## 11. Adding a device by scanning (pairing)

Added 2026-10-05 after the first phone test: typing a 28-symbol code on a new
computer is a pain, and many school computers can't paste from a phone. A device
that already syncs (usually a phone) can now **add** a new device by scanning a QR
code the new device shows; the new device then gets the sync code without anyone
typing it. The old direction (the new device scans or types the code shown by a
device that syncs) stays as it is.

Names: **D** is the device that wants to join (shows the QR code). **L** is the
device that already syncs (scans it). The website only carries sealed messages.

### 11.1 The flow on one page

1. D (sync off) opens **I Have a Code…**. Beside the text field it shows a QR
   code: "Scan with your phone". D made a fresh 16-byte **pairing secret** `P`,
   a **read token** `R`, and stored a sealed **hello** (its device name) on the
   website under `pairId` (derived from `P`). The QR code is the link
   `https://cedarlogic.netlify.app/sync/#p=<P as a 28-symbol code>`.
2. L scans it (in CedarLogic: Your Circuits › Sync › **Add a Device** › Scan; or
   with the phone's own camera app, which opens the link). L reads the hello
   and asks: **Add “Sam’s Windows PC”?** [Cancel] [Add].
3. L seals an **answer** holding its sync code and stores it on the website.
4. D, which has been polling, reads the answer (only D can: it needs `R`),
   opens it, deletes the slot, and goes straight to the usual **preview and
   Link** of §4.8 ("Link this PC? This code has 14 circuits from …").
   The person presses **Link**. Done.

If L doesn't sync yet, step 2 instead offers **Turn On Sync and Add** (11.7):
one scan sets up sync between two new devices.

### 11.2 The pairing secret and link

- `P` = 16 bytes from the platform CSPRNG (as §1.1). One per QR code shown;
  never stored on disk; forgotten when the sheet closes.
- Text: `P` encoded exactly like a sync code (§1.2: 28 Crockford symbols with
  the 12-bit checksum), the **pairing code**. It's never shown as text.
- Link: `https://cedarlogic.netlify.app/sync/#p=<pairing code>` (uppercase, no
  dashes). The QR code encodes this link (same encoder and settings as the sync
  code's QR, `cl_sync_qr` / `CedarQR`).
- Reading: any text with `#p=` followed by a pairing code (the https link, or a
  bare `#p=…`) is a pairing link. `normalizeCode` rules apply to the 28 symbols
  after `#p=`. Text with `#k=`, or a bare 28-symbol code, is a **sync code**,
  never a pairing link.
- `R` = 32 bytes from the CSPRNG; the **read token** is its base64url (no
  padding, 43 chars). Never in the QR code; never leaves D except as a bearer
  token to the website. The website keeps only `readHash` = lowercase hex
  SHA-256 of those 43 ASCII characters.

### 11.3 Keys

HKDF-SHA256, IKM = `P`, salt = the 18 ASCII bytes `cedarlogic-pair-v1`:

| Output | info | Length | Encoding |
|---|---|---|---|
| `pairId` | `pair-id` | 16 bytes | 32 lowercase hex chars; in URLs; not secret from the website |
| `pairKey` | `pair-key` | 32 bytes | raw AES-256-GCM key; never sent |

### 11.4 The two messages

Plaintext: UTF-8 JSON, keys in exactly this order, no whitespace, strings
escaped as `JSON.stringify` does (`"` `\` and U+0000–U+001F; nothing else):

- hello (D → L): `{"v":1,"kind":"hello","device":"<D's device name>"}`
- answer (L → D): `{"v":1,"kind":"answer","code":"<L's canonical 28-symbol sync code>","device":"<L's device name>"}`

Envelope: `0x01 ‖ nonce (12 bytes, fresh from the CSPRNG) ‖ AES-256-GCM(pairKey,
nonce, aad, plaintext)` (ciphertext ‖ 16-byte tag; no compression), sent as
base64url without padding. `aad` = the ASCII bytes
`cedarlogic-pair-v1|<kind>|<pairId>` (kind `hello` or `answer`). An envelope is
at most 2048 bytes.

Opening: wrong first byte, shorter than 29 bytes, a bad tag, JSON that isn't
an object, `v` ≠ 1, `kind` not the expected one, `device` not a string, or (for
an answer) a `code` that doesn't parse by §1.2 → **damaged**. `device` is shown
as plain text, cut to 64 characters, control characters dropped; empty →
"another device".

### 11.5 Server

Storage: the sync store, key `p/{pairId}` → `{ helloEnv, readHash, answerEnv,
createdAt, answeredAt }` (`answerEnv`/`answeredAt` null until answered). A slot
lives **10 minutes** from `createdAt` (`expiresAt` = `createdAt` + 600,000 ms;
at that instant and after it reads as gone, 404, whatever is stored). The daily cleanup (§3.10) deletes `p/` keys older than an hour.

The gate (origin / `x-cedarlogic-key`), the request breaker and Netlify's
per-address limit apply as to every other endpoint. `pairId` must match
`^[0-9a-f]{32}$`, bodies are JSON objects, envelopes base64url decoding to
29…2048 bytes, `readHash` 64 lowercase hex; anything else is `400
bad_request` (a bad `pairId` too, not 404). A missing or malformed bearer
token is `401 wrong_token`, like a wrong one. The checks run in this order:
gate, method (405), breaker, then `pairId`, body, slot.

| Request | Who | Answer |
|---|---|---|
| `PUT /pair/{pairId}` body `{"hello":env,"readHash":hex}` | D | 201 `{expiresAt}`; 409 `pair_exists` (make a new `P`); 429 `rate_limited` past **30 slots per address per hour** (counted with `allowIn` under the address's rate key) |
| `GET /pair/{pairId}` | L | 200 `{hello, answered: bool, expiresAt}`; 404 `pair_gone` |
| `POST /pair/{pairId}/answer` body `{"answer":env}` | L | 200 `{}`; 409 `pair_answered` (an answer is there already: first one wins, by a conditional write on the slot's etag); 404 `pair_gone` |
| `GET /pair/{pairId}/answer`, `Authorization: Bearer <readToken>` | D (polling) | 200 `{answer: env or null, expiresAt}`; 401 `wrong_token` (constant-time compare of SHA-256 of the token with `readHash`); 404 `pair_gone` |
| `DELETE /pair/{pairId}`, `Authorization: Bearer <readToken>` | D | 200 `{}` (also when already gone); 401 `wrong_token` |

Messages: `pair_gone` "That QR code has expired. Show a new one on the other
device."; `pair_answered` "Another device already answered this QR code. Show a
new one on the other device."; `pair_exists` "Try again."; `wrong_token` "Not
yours to read." `limits` (in `GET /health` and in every space status, §3.3: it
is one list) gains `pairSeconds: 600`.

Details that matter to clients: a second `POST …/answer` is `409` even with the
same envelope (only a write whose outcome was unknown and that did land is `200`);
`DELETE` of a slot that is gone is `200` whatever token it carries, and of a live
one needs the token; a `PUT` is counted against the 30 per hour only when it makes
a slot (a `409` or a `400` doesn't count); a `PUT` over an expired slot makes a
new one.

Logging as everywhere else: errors only; never a token, envelope or `pairId`.

### 11.6 D: showing the QR code

Where: the **I Have a Code…** sheet of the Mac, Linux and Windows apps, and of
CedarLogic Online / the phone app (sync off). Laid out as two halves:

> **Link to your synced circuits**
>
> **Scan with your phone** — [QR code]
> "On a phone that syncs, open CedarLogic › Your Circuits › Sync › Add a Device
> and scan this. Or scan it with the phone's camera."
> status line: "Waiting for your phone…" · "Can't reach the website. [Try Again]" ·
> "This QR code expired. [Show a New One]"
>
> **Or type the code** — the field, as before (§5.1) · [Continue]

On a phone (web, coarse pointer and narrow) the QR half starts folded: a link
"Show a QR code for another device to scan instead" unfolds it (and only then is
a slot made). Everywhere else the slot is made when the sheet opens.

Steps:
1. `P`, `R` ← CSPRNG; derive (11.3); hello = seal(D's device name);
   `PUT /pair/{pairId}`. 409 → new `P` (up to 3 times). 429 / 503 / no answer →
   the "Can't reach the website" line with [Try Again]. 201 → show the QR code.
2. Poll `GET /pair/{pairId}/answer` every **3 seconds** while the sheet is open
   (the web: also only while the page is visible; a poll is made at once when it
   becomes visible again). Errors while polling are retried at the next tick,
   silently, until the 10 minutes are up.
3. 404, or 10 minutes since the PUT → stop; "This QR code expired. [Show a New
   One]" (which starts again at 1).
4. `answer` not null → stop polling; open it (11.4). Damaged → `DELETE`, and the
   line says "An answer came that couldn't be read. [Show a New One]". Opened →
   `DELETE` (best effort; it expires anyway), then the preview of §4.8 with the
   code, exactly as if it had been typed. The confirmation's first line names who
   sent it: "Sent from “Sam’s phone”." (plain text). [Cancel] there forgets the
   code: nothing was stored.
5. The sheet closed, Cancel, the window closed, the app quitting → stop polling,
   `DELETE` (best effort).
   A `PUT` still out when this happens: the slot it makes is deleted as soon as it
   answers (a cancel generation, as in the engine), never shown. The web page
   going into the back/forward cache is the same (`pagehide`): the QR code is
   dead; coming back (`pageshow`, persisted) with the sheet still showing makes a
   new QR code.

### 11.7 L: adding a device

Entry points:
- Sync on, the Sync sheet: a new button **Add a Device…** (beside Show Code).
  It opens:
  > **Add a device**
  > On the other device, open Sync and choose I Have a Code (in the CedarLogic
  > app: Settings › Sync). It shows a QR code: scan it.
  > [Scan QR Code] (only where a camera can be used)
  > "Or link it with your code:" [Show Code]
  > [Done]
- Any in-app scan (Add a Device's, or I Have a Code's Scan Code) that reads a
  pairing link.
- A pairing link opened in a browser (a phone's camera app, or a link someone
  sent) is **never** taken as a pairing: the `/sync/` page (11.8) drops `P` and
  opens the app with `#add`, which shows the Add a device sheet, so the person
  scans the QR code with Scan QR Code. (A link can come from far away; a scan
  in the app means the device is in front of the person. See 11.9.) `#add` in
  the address (on load or on hashchange) comes off it at once and opens that
  sheet, whether or not this device syncs yet.
- A `#p=` in the address of CedarLogic Online or `/app/` (an old link, or typed)
  is treated the same way: dropped, and the Add a device sheet opens.

Steps:
1. Parse the link → `P` → derive → `GET /pair/{pairId}`. 404 → "That QR code has
   expired. Show a new one on the other device." Open the hello; damaged →
   "That QR code couldn't be read. Show a new one on the other device."
   `answered: true` → the `pair_answered` sentence.
2. L syncs → confirm:
   > **Add “Sam’s Windows PC”?**
   > It gets your sync code, so it can see and change all your synced circuits.
   > Only add a device that's yours and in front of you.
   > [Cancel] [Add]
3. [Add] → answer = seal({code: L's code, device: L's name}) →
   `POST /pair/{pairId}/answer` → 200:
   > **Almost done**
   > On “Sam’s Windows PC”, check what it shows and press Link.
   > [Done]
   409 / 404 → their sentences.
4. L doesn't sync yet → instead of 2:
   > **“Sam’s Windows PC” wants to sync**
   > This phone doesn't sync yet. Turn on sync here and send the new code to
   > “Sam’s Windows PC”? Circuits on both stay, kept together.
   > Already syncing on another device? Cancel, and link this phone to it first:
   > on that device, Sync › Show Code, then scan that code.
   > [Cancel] [Turn On Sync and Add]
   → Turn On (§4.8; the code sheet is not shown) → step 3 with the new code.

When this device doesn't sync yet, the Add a device sheet (opened by `#add`) says
so ("This browser doesn't sync yet. After you scan, you can turn on sync here and
add that device.") and has no Show Code; Scan QR Code leads to step 4.

Nothing is sent before [Add] but the `GET`.

If the preview D runs after an answer fails (offline, a damaged copy), D's I Have
a Code sheet comes back with the message and a new QR code, **never** with the
received code in its field (it's someone else's secret on a possibly shared
computer).

### 11.8 The `/sync/` page with `#p=`

Read `#p=` like `#k=`, off the address at once — and then **drop the pairing
code** (11.7): it's never passed on. Phones and tablets (iPhone, iPad, Android)
go to the phone app's Add a device sheet: `location.replace("/app/#add")` —
except Safari on an iPhone/iPad not running from the Home Screen, which shows:
"This QR code adds a device to your sync. Open CedarLogic from your Home
Screen, then Your Circuits › Sync › Add a Device, and scan it there." with
[Use in Safari Instead] (→ `/app/#add`). Computers: "This QR code adds a device
to your sync. Scan it in CedarLogic on a phone that syncs: Your Circuits › Sync
› Add a Device." with [Add a Device in This Browser] (→ CedarLogic Online
`#add`, for a computer with a camera). A damaged pairing code: "This QR code is
damaged. Show a new one on the other device."

The same page's `#k=` links on phones and tablets now also go to `/app/#k=…`
(the full-screen phone app; on Android it shares its storage with Chrome's
installed app), not to CedarLogic Online's page, whose window sits below the
page's introduction on a phone.

### 11.9 Security

- The website sees `pairId`, sizes, times and addresses. It can't read the hello
  or the answer (`pairKey` comes only from `P`, only in the QR code) and can't
  forge them (AES-GCM with `aad` binding kind and `pairId`).
- Someone who sees the QR code (over a shoulder, a photo) knows `P`: they can
  read the hello (a device name) and could post an answer of their own. They
  **can't read L's answer**: the website hands answers only to the holder of `R`,
  which never leaves D. (The website and a QR-code photographer would have to
  work together, which is the same trust the rest of sync already places in the
  website.) An answer posted by someone else reaches D as a code that isn't the
  person's: D's preview shows whose circuits they are, and nothing is linked
  without Link. L then gets "Another device already answered…", which tells the
  person something is off.
- **A link sent from far away.** Anyone can make a slot as D and send the
  `/sync/#p=…` link (chat, email, a QR code in a message) to someone whose phone
  syncs. If tapping it went straight to "Add …?", one press would give the
  sender the sync code. So a pairing is only ever started by a scan inside the
  app (11.7): a link only opens the Add a device sheet. To be fooled, the person
  would have to open Add a Device and point the camera at the sender's QR code
  on purpose — against the sheet's "Only add a device that's yours and in front
  of you."
- L confirms before sending its code, naming the device; D confirms (the
  preview) before linking.
- Slots are short-lived, small, and limited per address; `DELETE` needs `R`.

### 11.10 The engine (C++) and the C API

`SyncPair.cpp` in the shared engine: `pairKeys(P)`, `sealPair(key, pairId,
kind, json)`, `openPair(...)`, the link's parse/format, and the D side on the
engine thread (the polling loop, cancellable; requests through `Host::http`
with the usual headers but the read token as the bearer).

```c
// Pairing (SYNC.md §11): this device joins by showing a QR code that a device
// that syncs scans. Sync must be off. `show` gets the QR code's text (the
// pairing link) once the website has the request; `done` once:
//   CL_SYNC_PAIR_CODE    text = the sync code (then cl_sync_preview and
//                        cl_sync_link it), from = the sending device's name
//   CL_SYNC_PAIR_EXPIRED text = the sentence ("This QR code expired.")
//   CL_SYNC_PAIR_FAILED  text = the sentence (can't reach the website, damaged)
// Both on the main thread. cl_sync_pair_cancel (or a new start, or destroy)
// stops it: then done isn't called. Cancel deletes the slot (best effort).
enum { CL_SYNC_PAIR_CODE, CL_SYNC_PAIR_EXPIRED, CL_SYNC_PAIR_FAILED };
typedef void (*CLSyncPairShow)(void *ctx, const char *link);
typedef void (*CLSyncPairDone)(void *ctx, int result, const char *text, const char *from);
void cl_sync_pair_start(CLSyncEngine *e, CLSyncPairShow show, CLSyncPairDone done, void *ctx);
void cl_sync_pair_cancel(CLSyncEngine *e);
```

The apps don't scan (no camera code), so they never play L.

### 11.11 Test vectors

`P` = bytes `a0 a1 … af`:

| | |
|---|---|
| pairing code | `M2GT58X4MPKAFA59NANTSBDENX83` |
| link | `https://cedarlogic.netlify.app/sync/#p=M2GT58X4MPKAFA59NANTSBDENX83` |
| `pairId` | `2caadcde5f2aee160164bd13927d3637` |
| `pairKey` | `34ff905499658d1bd3842749d074ba1401b0816262af06abbbbc0735fedca39b` |

`R` = bytes `40 41 … 5f`: read token `QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl8`,
`readHash` `f45dc82a7523d14974cea050e3028e8bf06a8ee855a9d63034ebb4fb9ecc2305`.

Hello, nonce `10 11 … 1b`, plaintext
`{"v":1,"kind":"hello","device":"Sam’s MacBook \"Air\""}` (the ’ is U+2019,
three UTF-8 bytes):
`ARAREhMUFRYXGBkaG0vOHTp3q9Sc5tbSN42jE2irZhIUJNlUK6Zu-bHHIWnfkTRS3yJFkRLq1uekWAlpN6N7_r-5atwh5KPKqcA_y6zkuvoLcn7mCdI`

Answer, nonce `20 21 … 2b`, plaintext
`{"v":1,"kind":"answer","code":"000G40R40M30E209185GR38E1YZ4","device":"Chrome on Android"}`:
`ASAhIiMkJSYnKCkqK6XNj59CphJxJWWwjDJhjQBq0bFaTI6puMWrGLeOz0YQNUnuMszZCoWCMoG6nDWoj8d39E7TYfEgxBo0Q7mVazyKBMzOBRwf_CPwNfxOdNwvS5Ufp4EiL2LC6cxrjJODs2JjYJzi6r2CQ04`

Must not open: either envelope with any byte changed; the hello opened as an
answer (wrong `aad`); the answer under another `pairId`.

### 11.12 Tests

- Server (`test_sync_server.mjs`): each row of 11.5, expiry at 10 minutes, first
  answer wins under a race, the read token, the per-address limit, the cleanup.
- Web (`test_sync_client.mjs`): the vectors; D and L against the real server
  code; expiry; a damaged answer; cancel deletes. (`test_sync_ui.mjs`): a
  computer's I Have a Code shows the QR code; a phone scans it with the camera
  (fake camera showing that QR), confirms, and the computer reaches the preview
  and Link; Turn On Sync and Add; `/sync/#p=` on Android goes to `/app/`.
- Engine (`selfTest`): the vectors; the D loop against the in-process test
  server (answer arrives; expiry; damaged; cancel deletes; a 503 then success).
- Interop: the Mac `sync-check` tool (or Linux `sync_check`) as D against the
  site's mock server, answered by the web engine in node (L): D gets the code.
