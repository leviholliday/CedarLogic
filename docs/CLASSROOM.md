# CLASSROOM.md — CedarLogic Classroom: codes, no accounts

Status: design, protocol version 1, **revision 2** (2026-10-06: the review's
findings, each accepted or declined with its reason in §12). Written for
the work packages of §10 (server, web, the shared core, the Mac app; Windows
and Linux later), which must interoperate on the first try. Everything marked
MUST is checked by the test vectors (§7.1) or the scenarios (§7.2). The
reference implementation is `scripts/classroom-vectors.mjs` in the website
repo (WebCrypto only, as CedarLogic Online does it); it writes
`tests/classroom/vectors.json`, which two independent implementations check
again: `scripts/classroom-vectors-check.mjs` (node:crypto on OpenSSL, as the
C++ core will do it with libcrypto) and, in the app repo,
`mac/Tools/classroom-vectors-check.swift` (CryptoKit, CommonCrypto and
Compression: what the Mac hooks use). The node checker's `--doc` mode also
proves that every value this page quotes is in the file. The file is the
tie-breaker whenever this text and an implementation disagree — fix the text
or the code until all three checkers and every client's own suite agree.

It is built on `docs/SYNC.md`: the same code format, the same HKDF, the same
AES-GCM envelope, the same Netlify Functions + Blobs server pattern, the same
"the server keeps a hash of the token" rule, and the same people maintain both.
Where this document says "as Sync", the Sync section named is the rule.

## 0. The whole thing on one page

**What the owner chose.** Classrooms use codes, no accounts: nothing a school
has to sign up for, no passwords, no emails. Version 1 does three things — the
teacher posts assignments, collects what students hand in, and runs a live view
of a circuit on every student's screen — and nothing else (no roster
management, no grades). CedarLogic Online (the website) and the Mac app first;
Windows and Linux follow with the same core. Free, on the existing Netlify site.
The website can't read a student's name, a submission, an assignment or a
predict answer: everything is encrypted before it leaves a device.

**How it works.**

```
  teacher key    16 random bytes, shown as 28 symbols (000G-40R4-0M30-E209-185G-R38E-1YZ4): the teacher's secret
        │  HKDF ──► classId       (16 B, hex)      names the class on the website; in URLs; not secret
        │       ──► teacherToken  (32 B, b64url)   "Authorization: Bearer"; the server keeps its SHA-256
        │       ──► deleteToken   (32 B, b64url)   sent only to delete the class
        │       ──► backupKey     (32 B)           opens the teacher record on the website: the class's P-256 private key,
        │                                            the class key, the current join code, the class name
  join code      6 random bytes, shown as 12 symbols on the board (K7QM-4XPD-2FJ3); the teacher can change it or close joining
        │  PBKDF2 (600,000 rounds) ──► a stretched join secret (32 B): a copy of the server's records can't be searched for codes
        │  HKDF ──► joinId        (16 B, hex)      what a joining device looks up
        │       ──► joinToken     (32 B, b64url)   proof that the device knows the code (the server keeps an HMAC of it under its own secret)
        │       ──► joinKey       (32 B)           opens the join record: the class key, the class's public key, the class name
  class key      32 random bytes every member holds   ──► assignments, live records, class info  (AES-256-GCM, envelope 01)
  class public key (P-256; the private half only on the teacher's devices)
                                                      ──► names, submissions, predict answers, sealed answer keys
                                                          (ECDH P-256 + HKDF-SHA256 + AES-256-GCM, envelope 02)
  student token  32 random bytes per device, SHA-256 kept by the server; studentId a random UUID
  proof          32 random bytes per student, only ever inside the student's sealed records: the teacher's devices pin it,
                 so nobody without the student's device can pass a record off as theirs (§2.2)
  fetchKey       16 random bytes per class, made by the server and changed when a student is removed or the join code
                 changes: it is in the path of every record a student downloads, so only current members can (§3.3)
  move code      16 random bytes, 28 symbols, a 10-minute slot that carries a student's membership to another device
```

The server stores opaque envelopes and a few counters. The teacher's devices
write the class-side records (assignments, the live record, the join record)
with optimistic concurrency (a write names the `ver` it was based on, or it
gets **412**, as Sync). Students write only their own records (their name,
their hand-ins, their predict answers), each sealed to the class's public key,
so the server, other students and a school's TLS-inspecting proxy learn
nothing from them; the server never holds that public key, and each carries
the student's proof, so the server can't make one up either. Students poll
one tiny CDN-cached "pulse" per class to learn that something changed, and
fetch records by version (immutable, cached) under the class's current
`fetchKey`: a
class of 300 polling through a 75-minute lecture costs about 6,000 function
invocations (a third of them the pulse, half of them predict answers) and
320 MB (§3.10).

**Decisions in one table.**

| Question | Decision |
|---|---|
| Teacher key | 128-bit random; 28 Crockford symbols with Sync's 12-bit checksum; kept on the device (0600 file / IndexedDB); Show Teacher Key, Copy Link, QR code, printable recovery sheet; synced to the teacher's other devices as a `classroom` sync record when Sync is on (§2.5) |
| Join code | 48-bit random; 12 symbols in 3 groups of 4 with the same 12-bit checksum; stretched with PBKDF2-HMAC-SHA256 (600,000 rounds) before anything is derived from it, and the server indexes it only under an HMAC with a secret of its own, so a copy of the server's records holds nothing to search for codes (§1.3); online guesses are rate-limited per address (§3.5); the teacher can change it (the old code stops working) and close joining |
| Move code | 128-bit random; 28 symbols; a 10-minute slot on the website carrying the sealed membership, like Sync's pairing (§4.7) |
| KDF | HKDF-SHA256, salt `cedarlogic-classroom-v1`, infos `class-id` / `teacher-token` / `delete-token` / `backup-key` (teacher key), `join-id` / `join-token` / `join-key` (the stretched join secret), `move-id` / `move-key` (move code), `seal-key` ‖ E ‖ R (sealing); the join code is stretched first with PBKDF2-HMAC-SHA256, salt `cedarlogic-classroom-join-v1`, 600,000 rounds, 32 bytes |
| The class's key pair | P-256, made by the platform at Create Classroom (WebCrypto `generateKey`, CryptoKit `P256.KeyAgreement.PrivateKey()`), **not** derived from a code; the private scalar travels only inside the teacher record, sealed under `backupKey` (§1.4) |
| Sealing to the teacher | ephemeral P-256 key → ECDH with the class's public key → HKDF(X, salt, `seal-key` ‖ E ‖ R) → AES-256-GCM; envelope `02 ‖ flags ‖ E(65) ‖ nonce(12) ‖ ct ‖ tag(16)` |
| Class key | 32 random bytes; AES-256-GCM, envelope `01 ‖ flags ‖ nonce(12) ‖ ct ‖ tag(16)` (Sync's layout); handed to students inside the join record |
| AAD | `cedarlogic-classroom/1|<kind>|<classId>|<id>|<ver>|<flags>` for both envelopes |
| Records | class-side under the class key: `info`, `assignment`, `live`; the teacher's own: `teacher` (backupKey), `join` (joinKey); sealed to the teacher: `name`, `submission`, `answer`, `key`; `move` (moveKey) |
| Tokens | `teacherToken`, `deleteToken` (HKDF of the teacher key), `joinToken` (HKDF of the stretched join secret), `studentToken` (random); the server keeps `SHA-256` of each (of the join token: an HMAC under `CLASSROOM_JOIN_PEPPER`, §1.3) and compares in constant time |
| Who wrote it | a student's sealed records carry the student's `proof`, pinned per student by the teacher's devices ("Couldn't be verified" otherwise); the class's public key never reaches the server; signing the teacher's records is v1.1 (§8.2) |
| Who can download | the record paths carry the class's `fetchKey`, which the server changes when a student is removed or the join code changes; a removed student keeps the class key but can't fetch anything new (§3.3) |
| Concurrency | teacher records carry `base`/`ver` (412 on a stale base); a student's own records are last-writer-wins per device; nobody merges circuits |
| Live view | the teacher writes one `live` record per push (ver rises); students poll the pulse every 3 s while a session is on and the page is visible (60 s otherwise), fetch each version once; predict answers come back sealed and are counted on the teacher's device; reveal is a push with `reveal: true` |
| Answer keys | per assignment: **students can check their work** (the key is in the assignment, readable by the class) or **only I check** (the key is sealed to the teacher inside the assignment; results are computed on the teacher's device when it views submissions) |
| Limits | 300 students, 100 assignments, 512 KiB per assignment or submission, 4 MB inflated and 20,000 parts per received circuit, 100 MiB per class; per-address, per-class and per-student rate limits; site-wide budgets and a daily request breaker that pauses writes only and that one address can't trip alone (§3.5) |
| Retention | a class nobody touches for 400 days is removed (the id answers 410 forever); the teacher's devices warn from 60 days before; never-used classes after 7 days |
| Deletion | the teacher deletes the class (delete token; everything goes, the CDN's copies purged); a student leaves, or the teacher removes a student (roster entry and answers go; hand-ins stay with the teacher unless the teacher also ticks "delete everything they handed in"); Remove from This Device forgets a class on one computer and touches nothing on the website |
| Hosting | Netlify Functions + Netlify Blobs, stores `classroom` / `classroom-misc` per deploy context, as Sync; one function on `/api/classroom/*` (authenticated) and `/api/live/*` (ciphertext and counters behind the `fetchKey`, CDN-cached), carrying the Free plan's second and last code-based rate-limit rule (§3.1, §3.10) |
| Cost | a lecture of 300 students ≈ 5,800 function invocations and 320 MB; one such class at three lectures a week ≈ 60 % of the legacy Free plan's 125,000 invocations a month; on a credit plan ≈ 100 credits (about $0.67) a lecture (§3.10) |
| Engine | one C++ core (`mac/CedarCore/Classroom*.cpp`, P-256 behind hooks like Sync's crypto) for the Mac now and Linux/Windows later; `classroom-core.js` for the web; both checked against the same vectors |
| Keys at rest | Mac: 0600 files in `~/Library/Application Support/CedarLogic/Classroom/`; web: IndexedDB `cl-classroom` |

**Who builds what** — four packages now, with owned files, dependencies and
acceptance tests in §10: **S** server (+ the in-memory Blobs stub and a mock
server every other package tests against), **W** web client (core + UI, with
Group 6's UI), **E** the shared C++ core, **M** the Mac hooks and UI (Group 7).
**L** and **X** (Linux, Windows) later, as for Sync.

**Out of scope for v1** (nothing below blocks them): a roster with real names
from a school list, grades and comments; a message to one student (would be a
sealed record to the student's own key: students have none in v1); rotating the
class key (a removed student keeps it: what keeps them out is the server's
`fetchKey`, §3.3, so a dishonest server could still hand them later records;
rotation needs student key pairs, v1.1); signing the teacher's records and
requests instead of bearer tokens (§8.2, v1.1); a "class pass" that brings a
student back on a wiped shared computer (§11); websockets or long polling (§3.10); a second teacher
(share the teacher key); sections (make a class per section); an LMS link;
self-hosting.

### 0.1 Reading order

- **S** (server): §1.2–1.3 for the codes and tokens it never sees but whose
  hashes it stores, then §3 whole, §2.1 and §2.6 for what it stores, §7.2 for
  what the mock server must let clients do.
- **W**, **E**, **M** (clients): §1 and §2 first (the bytes, proven by §7.1),
  then §4 (what a client does), §5 (the words), §6 (the interfaces), §7.2.
- The owner: §0, §3.10 (cost), §8 (privacy, threats, failure modes), §11;
  §12 lists what the reviews of revision 1 found and what changed.

---

## 1. Codes, keys and envelopes

### 1.1 The three codes

| Code | Bytes | Shown as | Who has it | Lives |
|---|---|---|---|---|
| **teacher key** | 16 from the CSPRNG | 28 symbols in 7 groups of 4, e.g. `000G-40R4-0M30-E209-185G-R38E-1YZ4` | the teacher's devices | for the class's life; secret |
| **join code** | 6 from the CSPRNG | 12 symbols in 3 groups of 4, e.g. `K7QM-4XPD-2FJ3`; written on the board | the teacher, then every student who joined | until the teacher changes it; semi-secret (anyone who sees the board) |
| **move code** | 16 from the CSPRNG | 28 symbols, e.g. `M2GT-58X4-MPKA-FA59-NANT-SBDE-NX83`; also a QR code | one student, for ten minutes | 10 minutes |

Randomness as Sync §1.1: WebCrypto `crypto.getRandomValues`, Mac
`SecRandomCopyBytes`, later OpenSSL `RAND_bytes` and `BCryptGenRandom`. If the
RNG reports failure, the operation (Create Classroom, a seal, a new student id,
a move code) MUST stop with an error — never continue with a zeroed or partial
buffer. The teacher key, the class key and the key pair are made once per
class; nothing is ever derived from a class name or a date.

### 1.2 Encoding and parsing (byte-exact)

The teacher key and the move code are encoded exactly like a sync code (Sync
§1.2: 128 bits, a 12-bit checksum, 28 symbols). The join code is the same
construction over 48 bits:

```
checksum12 = (SHA256(secret)[0] << 4) | (SHA256(secret)[1] >> 4)         12 bits, both lengths
N          = secret (big-endian) << 12 | checksum12                       140 bits (16-byte secret) or 60 bits (6-byte secret)
code       = N as symbols of 5 bits, most significant first,              28 symbols or 12 symbols
             alphabet "0123456789ABCDEFGHJKMNPQRSTVWXYZ" (Crockford)
shown as   = groups of 4 joined with "-":  000G-40R4-0M30-E209-185G-R38E-1YZ4  /  000G-40R4-0MBY
```

C/C++ implementers: pack `secret ‖ (checksum12 << 4 as 2 bytes)` — 18 bytes for
a teacher or move code, 8 bytes for a join code — and read 140 or 60 bits five
at a time (`scripts/classroom-vectors-check.mjs` and the Swift checker do it
that way; no big integers). The sync engine's `encodeCode`/`decodeCode`
(`SyncProtocol.cpp`) handle the 16-byte case already; the 6-byte case is the
same loop with other lengths.

**Parsing input** (typing, pasting, a link, a scanned QR code) MUST behave
exactly as `scripts/classroom-vectors.mjs: normalizeCode/decodeCode`. A field
always knows which kind of code it expects (`teacher`, `join` or `move`):

1. If the text contains `#t=`, `#j=`, `#m=` or `#k=`, take the value after it,
   up to the first character that isn't an ASCII letter, digit, `-` or space.
   Otherwise, only if the text starts with `cedarlogic:` (ASCII, any case), do
   the same with `?t=`/`&t=` (and `j`, `m`, `k`). The letter names the kind:
   if it isn't the kind the field expects → error **kind** ("That's a join
   code, not a teacher key." / "That's a sync code, not a join code."). The
   website never puts a code in a query string.
2. Drop `-`, space, tab, CR, LF and U+00A0.
3. Look at each remaining character on its own (a Unicode scalar value; in C++
   a byte, where every byte ≥ 0x80 is already wrong): upper-case it **only if
   it is ASCII `a`–`z`**; then map `O`→`0`, `I`→`1`, `L`→`1`.
4. Anything not in the alphabet (that includes `U`) → error **symbol** ("‘U’
   can't be in a join code").
5. Not exactly 12 symbols (join) or 28 (teacher, move) → error **length** ("A
   join code has 12 letters and digits; this has 11").
6. Checksum mismatch → error **checksum** (join: "That code has a typo in it.
   Check it with your teacher."; teacher: "That key has a typo in it. Check it
   against your saved copy."; move: "That code has a typo in it. Check it on
   the other device.").

Canonical form for storage, links and the teacher record: the symbols, upper
case, no dashes. A bare 28-symbol code typed into a teacher-key field is a
teacher key, into a move-code field a move code: the field decides, never the
text.

**Links and QR codes.**

```
https://cedarlogic.netlify.app/classroom/#t=000G40R40M30E209185GR38E1YZ4     a teacher key (QR code, Copy Link, the recovery sheet)
https://cedarlogic.netlify.app/classroom/#j=000G40R40MBY                     a join code (QR code beside the big code on the projector)
https://cedarlogic.netlify.app/classroom/#m=M2GT58X4MPKAFA59NANTSBDENX83     a move code (the QR code the student's other device scans)
cedarlogic://classroom#t=…   cedarlogic://classroom#j=…   cedarlogic://classroom#m=…    "Open in the app"
```

The code is in the fragment so it never reaches a server, a log or the
`Referer`; the path has its trailing slash so there is no redirect. QR codes
encode the **https** link with Sync's settings (byte mode, error correction M,
quiet zone 4, dark on white also in dark mode, ≥ 160 px/pt; a join link is
56 bytes → QR version 4, a teacher or move link 72 bytes → version 5). Pages that receive a link MUST remove the
fragment from the address bar at once (`history.replaceState(null, "",
location.pathname)`) and MUST NOT create a class, join one or import a
membership without the person pressing a button after seeing what the code
holds (§4). A link in browser history, a camera app's history or a screenshot
is a code; the teacher-key sheet says so (§5.1).

### 1.3 Key derivation

HKDF-SHA256 (RFC 5869), salt = the 23 ASCII bytes `cedarlogic-classroom-v1`,
one derivation per output (web: `subtle.deriveBits` on an imported `HKDF` key;
C++: the sync engine's `hkdfSalted` on the `hmacSha256` hook; CryptoKit
`HKDF<SHA256>.deriveKey`; all three give the same bytes, §7.1.3). The join
code alone is **stretched first** (below): its HKDF runs on the stretched
secret, never on the 6 code bytes.

| Input keying material | Output | info (ASCII) | Length | Encoding | Use |
|---|---|---|---|---|---|
| teacher key (16 B) | `classId` | `class-id` | 16 B | 32 lowercase hex | the class's name on the website; in every URL; not secret |
| | `teacherToken` | `teacher-token` | 32 B | base64url, no padding (43 chars) | `Authorization: Bearer` on the teacher's requests |
| | `deleteToken` | `delete-token` | 32 B | base64url (43) | `x-cedarlogic-delete` on `DELETE /classes/{classId}` only |
| | `backupKey` | `backup-key` | 32 B | raw | AES-256-GCM key of the teacher record (§2.2); never sent |
| the stretched join secret (32 B) | `joinId` | `join-id` | 16 B | 32 hex | what `GET /join/{joinId}` looks up; the server maps it to the class |
| | `joinToken` | `join-token` | 32 B | base64url (43) | `Authorization: Bearer` on the two join requests: proof of knowing the code |
| | `joinKey` | `join-key` | 32 B | raw | AES-256-GCM key of the join record; never sent |
| move code (16 B) | `moveId` | `move-id` | 16 B | 32 hex | the slot's name |
| | `moveKey` | `move-key` | 32 B | raw | AES-256-GCM key of the move record |
| ECDH shared X (32 B) | `sealKey` | the 8 ASCII bytes `seal-key` ‖ E (65 B) ‖ R (65 B) | 32 B | raw | the AES-256-GCM key of one sealed envelope (§1.5) |

**Stretching the join code.** `stretched` = PBKDF2-HMAC-SHA256 (RFC 8018)
of password = the 6 secret bytes, salt = the 28 ASCII bytes
`cedarlogic-classroom-join-v1`, **600,000 rounds**, 32 bytes of output. A
join code has 48 bits because it has to fit on a whiteboard. Whoever can
compare a guess against something stored — a `joinId`, a token hash — can try
every code: with a plain HKDF, 2⁴⁸ guesses are about a day for one GPU, and a
found code opens the join record, so the class's assignments and live
records, and (for a dishonest server) lets it seal a join record of its own,
with its own public key, for the students who join next. The salt is the same
for every class (a student knows only the code, and any per-class value a
student could look up cheaply would itself be the thing to search against),
so the stretching doesn't make each class cost more: it makes **one table of
all 2⁴⁸ codes** cost 2⁴⁸ × 600,000 rounds — about 700 years of one top GPU
(≈ 12,000 guesses a second), once — and that table, built before or after any
breach, opens every class it can be matched against. Two things keep it from
being matched against anything a copy of the store holds:

- **The server keeps only HMACs.** The join index is `j/<joinIndex>` with
  `joinIndex` = lowercase hex HMAC-SHA256(`CLASSROOM_JOIN_PEPPER`, the 32
  ASCII hex characters of `joinId`), and the entry's `joinHash` =
  HMAC-SHA256(`CLASSROOM_JOIN_PEPPER`, the 43 ASCII characters of
  `joinToken`); the class document keeps `joinIndex`, never `joinId`. The
  teacher's device sends `joinId` and `joinToken` themselves (on create and
  on Change Join Code: a server that has them can only do what it already
  can to its own index). `CLASSROOM_JOIN_PEPPER` is 32 random bytes in the
  function's environment, never in Blobs, separate from Sync's rate pepper
  because it can't be rotated freely: a lookup tries it and then
  `CLASSROOM_JOIN_PEPPER_OLD` if set, and re-indexes a class found under the
  old one (it has the `joinId` from the path). §7.1.3 has a vector with a test
  pepper.
- **What's left needs the function's secrets too**: the operator, or someone
  who has both the store and the environment, still faces the 700-GPU-year
  table (and the class's join record, which they can then read, carries the
  class key, not names or hand-ins: those need the teacher's private key).

A device pays the 600,000 rounds once per join
(about 70 ms in node on a laptop, a few tenths of a second in a browser, a
second or two on a slow Chromebook, behind "Checking the code…"), the
teacher's device once per new code, and nobody else: the code's checksum is
checked before them, so a typo never waits, and the server never computes
them. The teacher key and the move code are 128 random bits and need no
stretching. Per platform: WebCrypto
`subtle.deriveBits({name:"PBKDF2", hash:"SHA-256", salt, iterations:600000}, key, 256)`
on a key imported as `"PBKDF2"`; Mac CommonCrypto
`CCKeyDerivationPBKDF(kCCPBKDF2, …, kCCPRFHmacAlgSHA256, 600000, …)` (CryptoKit has
no PBKDF2; `mac/Tools/classroom-vectors-check.swift` shows the call); OpenSSL
`PKCS5_PBKDF2_HMAC`; CNG `BCryptDeriveKeyPBKDF2`; or the core's own loop on
the `hmacSha256` hook (600,000 HMACs through a hook take about a second,
which is why a platform may override that one derivation, §6.2).

The server stores `teacherHash` and `deleteHash` = lowercase hex SHA-256 of
the 43 ASCII characters of the token, `tokenHash` of each student token
likewise (the student sends the hash when it joins, as Sync's `deleteHash`),
and the join code's HMACs above. It compares in constant time. HKDF outputs
are independent, so the website can't get a key from a hash it holds; the
join code's 48 bits are guessable online only against the rate limits of
§3.5 (the lookup answers 404 to a wrong code and is counted), and offline
only by someone holding the pepper, against the stretching.

The delete token is separate for the reason Sync gives: a TLS-inspecting
proxy sees the bearer token in ordinary traffic; it still can't delete the
class.

### 1.4 The class's key pair and the class key

Every class has one **P-256 key pair** (the "teacher's public key" of the
plan: it's per class, so a teacher with two classes has two). Students seal
things to its public half; only the teacher's devices hold the private half.

- **Made by the platform at Create Classroom**, not derived from the teacher
  key: WebCrypto can import a private key only as a JWK that already carries
  its public point, and no one wants to write P-256 point multiplication three
  times. Web: `subtle.generateKey({name:"ECDH", namedCurve:"P-256"}, true,
  ["deriveBits"])`, then `exportKey("jwk")` for `d`, `x`, `y`. Mac: CryptoKit
  `P256.KeyAgreement.PrivateKey()`, `rawRepresentation` (32 bytes, `d`) and
  `publicKey.x963Representation` (65 bytes). Later: OpenSSL `EVP_PKEY_keygen`
  with `EVP_PKEY_EC` / `NID_X9_62_prime256v1`; CNG `BCRYPT_ECDH_P256_ALGORITHM`.
- **Wire forms.** The public key is the X9.63 uncompressed point, 65 bytes
  `0x04 ‖ X ‖ Y` (what WebCrypto's `exportKey("raw")` gives and
  `importKey("raw")` takes; CryptoKit's `x963Representation`, *not* its 64-byte
  `rawRepresentation`), base64url without padding in JSON (87 characters). The
  private scalar `d` is 32 bytes big-endian (base64url, 43 characters). A
  reader MUST refuse a public key that isn't 65 bytes starting with `0x04`
  before doing any curve arithmetic, and MUST refuse a point that isn't on the
  curve (every platform's import does: WebCrypto throws, CryptoKit's
  `PublicKey(x963Representation:)` throws, OpenSSL's `EC_POINT_oct2point` /
  `EC_KEY_check_key` fail). The compressed forms (`0x02`/`0x03`, 33 bytes) are
  not accepted even where a library would take them (§7.1.4 `badPoints`).
- **Where the private key lives.** Only in the teacher record (§2.2), sealed
  under `backupKey`, on the website; and in the clear on the teacher's devices
  beside the teacher key (§2.4). A device that knows only the teacher key
  fetches the teacher record and opens it: that is how a second device, a
  recovery sheet and Sync all work (§4.1). There is no way to rebuild the key
  pair from the teacher key alone — if the website has forgotten the class,
  there is nothing left to decrypt anyway.
- **ECDH.** `shared` = the X coordinate of `d_A · Q_B`, 32 bytes: WebCrypto
  `deriveBits({name:"ECDH", public: Q_B}, d_A, 256)`; CryptoKit
  `sharedSecretFromKeyAgreement(with:)` whose raw bytes are exactly that X;
  OpenSSL `EVP_PKEY_derive`; CNG `BCryptSecretAgreement` + `BCryptDeriveKey`
  with `BCRYPT_KDF_RAW_SECRET` (which gives X little-endian: reverse it). Never
  the Y coordinate, never a hash of the point.
- **The class key** is 32 bytes from the CSPRNG, made at Create Classroom,
  kept in the teacher record and handed to every student inside the join
  record. It never changes in v1 (§0, out of scope): a student who leaves or
  is removed keeps it. What stops them from reading what comes later is the
  server's `fetchKey` (§3.3), not cryptography — an honest server won't hand
  them new records; a dishonest one could.
- **The class's public key never goes to the server** in the clear: it
  travels only inside the join record (under the join key), the teacher
  record and the move record. A sealed envelope proves nothing about who
  sealed it (it is confidential, not authenticated: anyone holding the public
  key can make one), so keeping the key from the server means a copy of the
  store gives nothing to seal with, and the student's `proof` (below) covers
  anyone who does have it.
- **A student's proof** is 32 bytes from the CSPRNG made at join, base64url
  (43 characters), kept with the membership and carried by the move record.
  It goes only inside the student's sealed records (`name`, `submission`,
  `answer`), so the server never sees it. Each teacher device pins the first
  proof it opens for a `studentId` and refuses later records whose proof
  differs (§4.3).
- **A student token** is 32 bytes from the CSPRNG per device, base64url (43
  characters); the device keeps it and sends it as the bearer; the server
  keeps its SHA-256.

### 1.5 The two envelopes

Every kind has exactly one envelope: **`0x01`** for `teacher`, `join`,
`info`, `assignment`, `live`, `move` (and the sync side record `classroom`),
**`0x02`** for `name`, `submission`, `answer`, `key`. A reader knows the kind
it is opening (the AAD needs it) and checks byte 0 against that kind's
envelope **before anything else** (`ENVELOPE_OF` in the generator, §7.1.7's
"envelope version 1 claimed" and "version 2 claimed" cases).

**The symmetric envelope (version byte `0x01`)** is Sync's (§1.4 there), used
with the class key, the backup key, the join key and the move key:

```
offset  size  field
0       1     0x01
1       1     flags: bit 0 = plaintext is deflate-raw (RFC 1951); other bits MUST be 0
2       12    nonce (random, fresh for every encryption)
14      n     AES-256-GCM ciphertext
14+n    16    GCM tag
```

**The sealed envelope (version byte `0x02`)** is for anything only the teacher
may read:

```
offset  size  field
0       1     0x02
1       1     flags: bit 0 = deflate-raw; other bits MUST be 0
2       65    E: the ephemeral public key, X9.63 uncompressed (0x04 ‖ X ‖ Y)
67      12    nonce (random, fresh)
79      n     AES-256-GCM ciphertext under sealKey = HKDF(X(e·R), salt, "seal-key" ‖ E ‖ R, 32)
79+n    16    GCM tag
```

- `R` is the class's public key (65 bytes), `e` a fresh P-256 private key the
  sealer makes for this one envelope and forgets (never stored, never reused,
  never written anywhere); `E` is its public key. Putting both public keys in
  the HKDF info binds the key to this sender and this recipient (as HPKE does
  with its `kem_context`), so an envelope re-addressed to another class fails.
- **AAD** for both envelopes = the ASCII bytes of
  `cedarlogic-classroom/1|<kind>|<classId>|<id>|<ver>|<flags>`, where `kind`
  is the payload kind (§2.2), `id` the record's id (an assignment or student
  UUID, a session UUID, `<assignmentId>/<studentId>` for a submission,
  `<session>/<studentId>` for an answer, the join or move id, or the literal
  `teacher` / `info`, see the table in §2.1), `ver` the decimal version this
  envelope will have on the server, `flags` the decimal flags byte. Example:
  `cedarlogic-classroom/1|name|9c89e40e9ea981bbfeaf61e93c91be46|5f3a1c2e-8b4d-4a6f-9c1d-2e3f4a5b6c7d|1|0`.
  Binding the kind stops a submission from being presented as a name, the ids
  stop a server from moving a record between classes, students or
  assignments, and `ver` stops it from presenting an old version as a new one.
  A Sync envelope (prefix `cedarlogic-sync/1`) can never open as a classroom
  one, even under the same key (§7.1.7).
- `plaintext` = the UTF-8 JSON payload (§2.2), deflated when flag bit 0 is set.
  Writers SHOULD deflate assignments, submissions and live records (circuits
  shrink 5–10×) and MUST NOT deflate the small kinds (`teacher`, `join`,
  `info`, `name`, `answer`, `key`, `move`). Readers MUST support both. Inflate
  MUST stop as soon as the output passes **4,000,000 bytes** (`MAX_PLAINTEXT`)
  and fail, with Sync's capped decoders (a streamed inflate counted as it
  goes, never "inflate it all, then look": §7.1.7 has a deflate bomb). That is
  lower than Sync's 20,000,000 because a classroom circuit comes from the
  other party — a student's hand-in to the teacher, a teacher's push to
  students — and §4.9 caps what is then built from it. The sync side record
  keeps Sync's rules.
- Sizes (envelope bytes, before base64url): assignment, submission and live
  records ≤ 524,288; `teacher`, `join`, `info`, `move` ≤ 4,096; `name` ≤ 640
  (a 64-character name in four-byte characters with its proof is about 450);
  `answer` ≤ 2,048; a sealed `key` inside an assignment ≤ 65,536 (it is part of
  the assignment's 512 KiB). In JSON an envelope travels as a base64url string
  without padding (`env` or `data`).
- A reader MUST treat as **damaged** (without trying to decrypt) an envelope
  whose byte 0 isn't its kind's envelope (checked first), shorter than 30
  bytes (symmetric) or 95 bytes (sealed), with flag bits other than bit 0, or whose `E` doesn't
  start with `0x04`; as damaged too when `E` isn't on the curve, the tag
  doesn't match, or the inflate fails. Damaged is a verdict about bytes; what a
  client does with a damaged record is in §4.9.
- Nonces: 96 bits from the CSPRNG for every seal (a sealed envelope could use
  a fixed nonce safely, since its key is fresh, but one rule is simpler and the
  test "two seals of the same payload differ" holds for both). The production
  seal functions MUST NOT take a nonce or an ephemeral key as parameters; the
  byte-exact vectors are made by test code only (`sealForTest`).

Primitives per platform (nothing to download):

| | random | SHA-256 / HMAC / HKDF | AES-256-GCM | P-256 key pair, ECDH | deflate-raw |
|---|---|---|---|---|---|
| Web | `getRandomValues` | `subtle.digest`, `subtle.deriveBits` (HKDF; PBKDF2 for the join code) | `subtle.encrypt/decrypt({name:"AES-GCM", iv, additionalData, tagLength:128})` — output is ciphertext‖tag | `subtle.generateKey/importKey/exportKey` (`raw` 65 B public, `jwk` private), `deriveBits({name:"ECDH", public}, priv, 256)` | `CompressionStream("deflate-raw")`, `DecompressionStream` counted to the cap |
| Mac | `SecRandomCopyBytes` | CryptoKit `SHA256`, `HMAC<SHA256>`, `HKDF<SHA256>` (or the core's HKDF on the HMAC hook); CommonCrypto `CCKeyDerivationPBKDF` for the join code | CryptoKit `AES.GCM.seal/open` with `authenticating:`; `ciphertext + tag`, not `.combined` | `P256.KeyAgreement.PrivateKey()`, `(rawRepresentation:)`, `PublicKey(x963Representation:)`, `sharedSecretFromKeyAgreement` | Compression `COMPRESSION_ZLIB` streamed with the cap (`SyncHooks.swift`) |
| Linux (later) | `RAND_bytes` | `EVP_Digest`, `HMAC`, `PKCS5_PBKDF2_HMAC` | `EVP_aes_256_gcm` as Sync §1.4 | `EVP_PKEY_keygen` (EC, prime256v1), `EC_POINT_point2oct` uncompressed, `EVP_PKEY_derive` | GIO raw zlib |
| Windows (later) | `BCryptGenRandom` | `BCryptHash`, `BCryptDeriveKeyPBKDF2` | CNG AES-GCM as Sync §1.4 | `BCRYPT_ECDH_P256_ALGORITHM`: `BCryptGenerateKeyPair`, `BCryptExportKey(BCRYPT_ECCPUBLIC_BLOB)` (X‖Y after the header: add `0x04`), `BCryptSecretAgreement`, `BCryptDeriveKey(BCRYPT_KDF_RAW_SECRET)` reversed | `Deflate.h` |

Browser support: ECDH P-256, HKDF and AES-GCM are in WebCrypto in Chrome (and
ChromeOS), Safari 11+, Firefox and Edge; `CompressionStream` is what Sync
already needs. A `CryptoKey` can be stored in IndexedDB; this design stores
the raw bytes instead (§2.4), because the teacher's devices must re-seal the
teacher record, which needs `d` in the clear.

### 1.6 Versioning

Every layer carries a version: the API paths (`/api/classroom/v1/…`,
`/api/live/v1/…`), the HKDF salt (`-v1`), the envelope bytes (`0x01`, `0x02`),
the AAD prefix (`cedarlogic-classroom/1`), the payload field `v` (1), and the
sync side-record kind (`classroom`, `v` 1). A reader that meets a payload `v`
above 1 or a `kind` it doesn't know MUST NOT apply, show or overwrite that
record: it marks it "needs a newer CedarLogic" (§4.9). A code that isn't 12 or
28 symbols isn't a v1 code.

---

## 2. Data model

### 2.1 The records of one class

| Record | Key | Written by | Read by | `id` in the AAD | `ver` | Size cap | Count |
|---|---|---|---|---|---|---|---|
| `teacher` | `backupKey` | the teacher's devices | the teacher's devices | the literal `teacher` | rises with every change (base/ver) | 4 KiB | 1 |
| `join` | `joinKey` of the **current** join code | the teacher's devices | a joining device | `joinId` | rises with every code change (base/ver) | 4 KiB | 1 (the server keeps only the current one) |
| `info` | class key | the teacher's devices | everyone in the class | the literal `info` | base/ver | 4 KiB | 1 |
| `assignment` | class key | the teacher's devices | everyone in the class | `assignmentId` (UUID) | base/ver | 512 KiB | ≤ 100 |
| `live` | class key | the teacher's devices | everyone in the class | `session` (UUID) | base/ver; one slot per class | 512 KiB | 1 (the latest) |
| `name` | sealed to the class's public key | the student's device | the teacher's devices | `studentId` (UUID) | rises with every rename; stored in the roster | 640 B | 1 per student |
| `submission` | sealed | the student's device | the teacher's devices (the student can fetch its own) | `assignmentId/studentId` | rises with every hand-in (`attempt`) | 512 KiB | 1 per student per assignment (the latest) |
| `answer` | sealed | the student's device | the teacher's devices | `session/studentId` | = the `live` ver it answers | 2 KiB | 1 per student per live version (the latest) |
| `key` | sealed | the teacher's device | the teacher's devices | `assignmentId` | = the assignment's ver | 64 KiB | inside an assignment |
| `move` | `moveKey` | the student's old device | the student's new device | `moveId` | 1 | 4 KiB | 1 per slot, 10 minutes |

Every record on the server has, in the clear, its `ver`, `size` (envelope
bytes), `updatedAt` (server time, ms UTC), `h` (first 32 hex of SHA-256 of the
envelope bytes, as Sync) and its envelope. `ver` only ever rises for a record;
a writer uses `max(the ver it has, the highest it has seen) + 1`; the server
refuses a write whose `base` isn't the current `ver` (412 with `current`).
A deleted teacher record kind simply disappears from the index (classroom
records have one writer each, so there are no sealed tombstones: a student's
assignment list is rebuilt from the server's index on every fetch).

Identifiers: `classId`, `joinId`, `moveId` are 32 lowercase hex (HKDF
outputs); `studentId`, `assignmentId`, `session` are random lowercase UUID v4
(regex `^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$`),
made by the device that first writes the record; never from a name.

### 2.2 The payloads (JSON, UTF-8)

Each is a JSON object with `"v":1` and `"kind"`. The examples are the exact
bytes of the vectors (§7.1.5): writers produce `JSON.stringify` of an object
with the keys in this order (so the byte-exact vectors reproduce); readers
accept any order and ignore unknown fields.

**teacher** (under `backupKey`; the teacher's whole state for the class):

```json
{"v":1,"kind":"teacher","name":"Digital Logic 101","d":"LjvF28Px1bDpuymMq9b-KL4ifWHytiYDoW080EfF_Oc","pub":"BDJOV2q24PP7ZmCxHXOmFiVEMLnY3eGlcRLuj3e2huFf1Whgvw-TSvdPbRrI8dEcRU_Z8fxFPRbBpwmNiwUlQhc","classKey":"EBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkqKywtLi8","joinCode":"000G40R40MBY","joinOpen":true,"createdAt":1759600000000,"modifiedAt":1759600000000}
```

**join** (under the current `joinKey`; what a joining device gets):

```json
{"v":1,"kind":"join","name":"Digital Logic 101","classKey":"EBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkqKywtLi8","pub":"BDJOV2q24PP7ZmCxHXOmFiVEMLnY3eGlcRLuj3e2huFf1Whgvw-TSvdPbRrI8dEcRU_Z8fxFPRbBpwmNiwUlQhc"}
```

The class's public key reaches students **inside** this record, authenticated
by the join key, which the website never has. That is what stops a dishonest
website from handing students a public key of its own and reading their names
and hand-ins: the root of trust is the code the teacher wrote on the board.

**info** (class key): `{"v":1,"kind":"info","name":"Digital Logic 101","modifiedAt":1759600000000}` — the class name as members see it (a rename re-writes `info`, `join` and `teacher`).

**assignment** (class key):

```json
{"v":1,"kind":"assignment","title":"Lab 3: a switch and a light","instructions":"Wire the switch to the light. Hand in when the light follows the switch.","dueAt":1760140800000,"closeAfterDue":false,"cdl":"(cedarlogic\n  (version 3)\n…","key":{"text":"LED = A"},"createdAt":1759600000000,"modifiedAt":1759600000000}
```

with `key` either `null`, `{"text": T, "names"?: N}` (students can check
their work) or `{"sealed": "<base64url sealed envelope of a key payload>"}`
(only the teacher checks). `T` is the answer key exactly as the teacher typed
or pasted it into Check My Circuit — a formula, a truth table, a count, a
state table or a timing table — and its kind is told from the text alone
(`docs/CHECK-SEQUENTIAL.md` §2: `cl_check_key_kind` on the Mac,
`CedarLogicCheck.detect` on the web), so the protocol carries no kind; a
client whose checker can't read the text says so through the checker's own
"couldn't check" messages, never by guessing. `N` is the by-hand name matching
in `cl_check_expected`'s form (a line per name, `asked<TAB>circuit's`), absent
when the teacher set none.

**key** (sealed to the class's public key; the `id` in its AAD is the
assignment's id and `ver` the assignment's ver, so a key can't be moved to
another assignment or version): `{"v":1,"kind":"key","text":"LED = A"}`, plus
`"names"` when the teacher set any.

**live** (class key; one per push):

```json
{"v":1,"kind":"live","session":"2b1d0a9c-3e4f-4a5b-8c6d-7e8f9a0b1c2d","step":3,"cdl":"(cedarlogic\n…","predict":{"prompt":"What will the light show when the switch is on?","lights":["LED"]},"reveal":false,"at":1759603600000}
{"v":1,"kind":"live","session":"2b1d0a9c-3e4f-4a5b-8c6d-7e8f9a0b1c2d","ended":true,"at":1759607200000}
```

`session` names one live session (a new UUID each time the teacher goes
live); `step` counts pushes within it (for the teacher's own display);
`predict` is `null` or the question (the prompt and the names of the lights
students guess); `reveal` true means the lights are shown and guesses scored;
`ended` ends the session (no `cdl`).

**name** (sealed): `{"v":1,"kind":"name","name":"Sam Lee","joinedAt":1759600000000,"proof":"YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn8"}`.

**submission** (sealed, deflated):

```json
{"v":1,"kind":"submission","name":"Sam Lee","cdl":"(cedarlogic\n…","handedInAt":1759690000000,"client":"web/2026-10-06","attempt":2,"proof":"YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn8"}
```

The name travels inside too, so a submission is readable on its own (Download
All) even if the roster entry is gone.

**answer** (sealed): `{"v":1,"kind":"answer","session":"2b1d0a9c-…","ver":57,"lights":{"LED":1},"at":1759603660000,"proof":"YGFi…fn8"}` — the guesses for one live version.

**The proof** (`name`, `submission`, `answer`, `move`) is the student's 32
random bytes of §1.4, the same in every record the student writes (a rename
keeps it; a move carries it; a new device that joins afresh is a new
`studentId` with a new proof). A teacher device keeps `proofs[studentId]`
(§2.4): the first proof it opens for a student is pinned — normally the name
record written at join, read with the roster — and any later `name`,
`submission` or `answer` of that student whose proof differs is listed as
"Couldn't be verified" (the time it arrived, the name the teacher already
knows), never opened as content, never counted, never checked. The server
can't make a record that passes: it never sees a proof, and it doesn't hold
the class's public key. Each teacher device pins on its own; two that pinned
different values both flag the other's records, so a substitution shows up
either way.

**move** (under `moveKey`): the whole membership —

```json
{"v":1,"kind":"move","classId":"9c89e40e9ea981bbfeaf61e93c91be46","studentId":"5f3a1c2e-8b4d-4a6f-9c1d-2e3f4a5b6c7d","token":"QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl8","proof":"YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn8","classKey":"EBES…","pub":"BDJO…","name":"Sam Lee","className":"Digital Logic 101"}
```

**The order of the checks** is part of the rule, so every reader gives the
same verdict (§7.1.6 has a case for each step): the bytes are UTF-8 with no
unpaired surrogate escape, and JSON, and an object → else invalid; `v` is an
integer ≥ 1 → else invalid; `v` > 1 → **newer**; `kind` is a string → else
invalid; `kind` is one this client knows → else **newer**; `kind` equals the
kind the record was opened as (its AAD's) → else invalid; then the fields
below → else invalid.

**Field rules** (readers MUST refuse as **invalid** anything else):

| Field | Kinds | Type | Rules |
|---|---|---|---|
| `v` | all | integer | 1. Larger → "newer"; missing or not an integer → invalid. |
| `kind` | all | string | one of the ten above, or `classroom` for the sync side record of §2.5; any other string → "newer". A record read under one kind's AAD whose payload says another kind → invalid. |
| `name` | teacher, join, info, name, submission, move (`className` too) | string | writers trim ASCII whitespace at both ends, drop control characters (U+0000–U+001F, U+007F) and cut to 64 scalar values (a person's name) or 100 (a class name); empty → "a student" / "Untitled class". Readers trim and cut the same way and show it only as plain text. |
| `title`, `instructions`, `prompt` | assignment, live | string | ≤ 200 / 20,000 / 500 scalar values; writers trim; readers cut. |
| `d`, `pub`, `classKey` | teacher, join, move | string | base64url of exactly 32 / 65 / 32 bytes (43 / 87 / 43 characters); `pub` must start with `0x04` and be on the curve. |
| `joinCode` | teacher | string | 12 canonical symbols with a valid checksum. |
| `joinOpen`, `closeAfterDue`, `reveal`, `ended` | teacher, assignment, live | boolean | `ended` optional (absent = false); present, it must be a boolean. |
| `proof` | name, submission, answer, move | string | base64url of exactly 32 bytes (43 characters). |
| `cdl` | assignment, live (unless ended), submission | string | the circuit file's text as Sync §2.2 (v1/v2 XML or v3); may be empty for a blank starter. |
| `key` | assignment | object or null | exactly one of `text` (string) or `sealed` (base64url) present; optional `names` string. |
| `text`, `names` | key | string | `text` required, `names` optional. |
| `teacherKey` | classroom | string | 28 canonical symbols (§2.5). |
| `dueAt` | assignment | integer or null | ms since 1970, UTC; shown, not enforced by clients. |
| `createdAt`, `modifiedAt`, `joinedAt`, `handedInAt`, `at` | various | integer | server-corrected time (Sync §4.6: the `Date` header's offset). |
| `session` | live, answer | string | a UUID v4. |
| `step`, `attempt`, `ver` | live, submission, answer | integer | |
| `predict` | live | object or null | `prompt` string, `lights` array of strings (light names as the circuit's truth table names them; ≤ 32). |
| `lights` | answer | object | light name → 0 or 1 (JSON numbers; `true`/`false` are invalid). |
| `classId`, `studentId`, `token` | move | string | 32 hex / UUID v4 / base64url of exactly 32 bytes (43 characters). |

"Integer" means a JSON number whose value is a whole number from 0 to 2⁵³−1
(`5`, `5.0` and `5e0` are all 5). Readers MUST refuse a payload that isn't
valid UTF-8, isn't a JSON object, has a required field missing or of the wrong
type, or has an unpaired surrogate escape anywhere (§7.1.6 has every case).
JSON writers escape `"`, `\` and U+0000–U+001F and may write everything else
as raw UTF-8; readers accept every JSON escape, surrogate pairs and raw
U+2028/U+2029 (the sync vectors' records 3 and 4 cover the same reader).

### 2.3 Hashes and change detection

```
h              = first 32 hex of SHA-256(envelope bytes)                      (the server's entry; the clients' high-water marks)
tokenHash      = lowercase hex SHA-256( the 43 ASCII characters of a token )  (what the server stores for every token)
cdlHash        = Sync §2.3's cdlHash of a submission's or assignment's cdl    (the teacher's "changed since I checked it" cache; the student's "handed in what I have now")
```

Nothing is merged, so there is no structure digest here: a student's hand-in
replaces the previous one, and an assignment edit replaces the assignment.
Clients MAY use Sync's `structureHash` to tell "the student only flipped a
switch since the last hand-in" (then Hand In Again is offered quietly), but
it is not part of the protocol.

### 2.4 Local storage

**Mac** (and Linux/Windows later, with Sync's per-machine folders): a folder
`~/Library/Application Support/CedarLogic/Classroom/` (mode 0700; Linux
`$XDG_DATA_HOME/CedarLogic/Classroom/`, Windows `%LOCALAPPDATA%\CedarLogic\Classroom\`
DPAPI-protected like the sync secret), with files written atomically (temp +
rename), mode 0600:

- `teaching.json` — the classes this device teaches:
  `{"v":1,"classes":{"<classId>":{"teacherKey":"<28>","record":{…the opened teacher payload…},"ver":2,"h":"…","join":{"ver":3,"h":"…"},"seq":12,"assignments":{"<aid>":{"ver":1,"h":"…"}},"liveVer":57,"proofs":{"<sid>":"<b64u>"},"checked":{"<aid>/<sid>":{"h":"…","verdict":0,"summary":"…"}},"expiresWarnedAt":0}}}`
  (`join`: the join record this device last checked, §4.1; `proofs`: the pinned proofs, §2.2).
- `memberships.json` — the classes this device is a student in:
  `{"v":1,"classes":{"<classId>":{"studentId":"…","token":"…","proof":"<b64u>","classKey":"<b64u>","pub":"<b64u>","fetchKey":"<32 hex>","name":"Sam Lee","className":"…","joinedAt":…,"seq":12,"seen":{"live":57,"assignments":{"<aid>":1}},"pending":{"<aid>":{"cdl":"…","attempt":3,"at":…}}}}}`
  (`pending`: a hand-in made offline, sent at the next chance, §4.8;
  `fetchKey`: from the last status, §3.3).
- `cache/<classId>/assignments/<aid>.json` — the opened assignment payload,
  so the list and the starter open offline; `cache/<classId>/live.cdl` the
  last pushed circuit; `cache/<classId>/submissions/<aid>/<sid>.json` opened
  submissions (teacher), cleared when the class is deleted.
- a `lock` file (`flock`) held by the running app, as Sync, so two copies
  don't poll and hand in at once.

A student's circuit made from an assignment lives in the library like any
circuit, with one extra file in its folder, `assignment.json` =
`{"classId":"…","assignmentId":"…","title":"…"}`, which the Hand In button
reads (the wx app ignores the file; Sync ignores it too: it isn't `circuit.cdl`
or `name.txt`).

**Web** (CedarLogic Online and `/app/`): IndexedDB database `cl-classroom`,
version 1 (its own database, so `cl-library`'s version never moves):

| Store | Key | Value |
|---|---|---|
| `teaching` | `classId` | the `teaching.json` entry above |
| `memberships` | `classId` | the `memberships.json` entry above |
| `cache` | `"<classId>/assignment/<aid>"`, `"<classId>/live"`, `"<classId>/submission/<aid>/<sid>"` | the opened payloads (and their `ver`, `h`) |
| `meta` | string keys | `"poll"` → the poller's backoff state |

A circuit opened from an assignment gets `from: {classId, assignmentId, title}`
on its `cl-library` record (Sync carries the record; the field is ignored by
other devices, which is fine: Hand In is offered wherever the membership is).
The page asks for `navigator.storage.persist()` when a class is created or
joined (Sync §2.5's Safari note applies: the teacher-key sheet tells Safari
users to save the key).

Tabs: a `BroadcastChannel("cl-classroom")` message after every local change
(`{type:"changed", classId}`) and `navigator.locks.request("cl-classroom-poll")`
so one tab polls per class.

### 2.5 The teacher key on the teacher's other devices (Sync)

When Sync is on, the teacher key is a **sync side record** of kind
`classroom`: one record per class, random UUID id, sealed under the sync
record key exactly like a circuit (Sync §1.4, AAD `cedarlogic-sync/1|…`),
payload

```json
{"v":1,"kind":"classroom","classId":"9c89e40e9ea981bbfeaf61e93c91be46","teacherKey":"000G40R40M30E209185GR38E1YZ4","name":"Digital Logic 101","createdAt":1759600000000,"modifiedAt":1759600000000,"device":"Levi’s MacBook Air","deviceId":"5f0c2a9e7d3b41c8a6e2f1b0c9d8e7a6"}
```

written at Create Classroom (and when the class is renamed), ≤ 4,096 bytes,
sent with `deleted: false, device: false`; deleting the class writes Sync's
sealed tombstone over it. Only the key and the name are synced: everything
else a device needs comes from the website with the key (§4.1). (A student's
membership is not synced in v1; the move code does that. It would be the same
mechanism, kind `membership`, if wanted later.) Record 12 of §7.1.5 is such a
record, sealed with the sync vectors' own record key, so the sync engines can
test it. Readers apply the field rules of §2.2 to it: `classId` 32 hex,
`teacherKey` 28 canonical symbols, `name` a string, the times integers;
`device` and `deviceId` as Sync §2.2.

**Older clients are not hurt.** By Sync §1.5/§4.10 a reader that meets a kind
it doesn't know marks the record `unreadable[rid] = [ver, "newer"]`, never
applies or overwrites it, doesn't fetch it again until its `ver` changes,
skips it in the join rule and the preview (both count `kind:"circuit"` only),
and never writes over it. Checked in the shipped code (2026-10-06):
`sync-core.js`'s `readPayload` says "newer" for a kind other than `circuit`,
`deleted` or `device`; the pull puts such a record only into `unreadable` and
`seen` (no `records` entry, so no local circuit); `unreadableCount()` counts
only "damaged"; the "Saved by a newer CedarLogic" problem line is raised only
in the push's loop over *local circuits* mapped to such a record, which a
`classroom` record never is; and `preview()` counts `kind === "circuit"`
only. The C++ engine does the same: `SyncProtocol.cpp: readPayload` → "newer";
`Sync.cpp`'s pull stores it in `unreadable`/`seen` only, its push raises
`kNewer` only for a mapped local circuit, the damaged count counts "damaged"
only, the preview counts `kind == "circuit"`, and `Core::circuitCount()`
counts mapped records — so Settings' "42 circuits" doesn't move either. An
old device shows nothing at all; the server counts one ≤ 4 KiB record against
the space's 1,000 circuits.

**Classroom-aware clients** need one small extension of the sync engine
(package E and W, Sync's files): records of a kind listed in a `sideKinds`
option are not "newer"; the engine keeps their opened payloads in its state
(`side[rid] = {ver, h, kind, payload}`), reports changes through
`Host::sideRecordsChanged(kind)` / the web engine's `onSide(kind)`, and
offers `sideRecords(kind)`, `putSideRecord(kind, payloadJson, rid?)` (a write
with `base` = the record's ver, 412 → refetch and apply the newer payload,
then write again only if this device's value still differs) and
`deleteSideRecord(rid)` (a sealed tombstone). The classroom module registers
`classroom`; a key that arrives this way is handled as "Add a class with a
teacher key" (§4.1), quietly. Delete Class tombstones the record, and the
other devices forget the class. Remove from This Device (§4.1) leaves the
record alone, so the teacher's other devices keep the class; the device
notes the record's id in `teaching.json`'s `removed` list so its own sync
doesn't add the class back.

### 2.6 What the server can see

Per class: that it exists, when it was created and last used, an HMAC of
the current `joinId` and whether joining is open, how many
students have joined and when each did (and roughly when each was last seen,
to the day), the class's `fetchKey` (its own random value), how many
assignments there are, their sizes, versions and times,
which assignments close hand-ins and when, each student's hand-in times,
attempt counts and sizes, when a live session is on and how often the teacher
pushes, how many students answered a predict question and when, and the IP
addresses requests come from (used for rate limits through an HMAC with a
server secret, as Sync; and in Netlify's own request logs). It cannot see
names, class names, instructions, circuits, answer keys, guesses or results,
and it never holds the class's public key or a student's proof, so it can't
make a record a teacher's device would accept as a student's. §8 says what that does and doesn't
reveal, in the words a school will read.

---

## 3. Server API (Netlify Functions)

### 3.1 Files

- `netlify/lib/classroom.mjs` — all the logic: `handle(req, context, deps)`
  for `/api/classroom/v1/…`, `pulse(req, context, deps)` for the cached
  `/api/live/v1/…` endpoints, `cleanup(deps)` for the daily job, with
  `deps = { store, misc, now, env, ip, state?, limits? }` exactly as
  `netlify/lib/sync.mjs`, so the tests and the mock server run it on the
  in-memory stub (`netlify/lib/memory-blobs.mjs`, unchanged).
- `netlify/functions/classroom.mjs` — one function for both prefixes, so
  one rule covers the pulse too:
  `export const config = { path: ["/api/classroom/*", "/api/live/*"], rateLimit: { windowLimit: 1200, windowSize: 60, aggregateBy: ["ip", "domain"] } }`.
  Netlify allows **2 code-based rate-limit rules per site on the Free plan**
  (5 on Pro, 100 on Enterprise), and `sync.mjs` holds one, so this is the
  site's last: nothing else may add one without moving to Pro. 1,200 a minute
  is 20 a second from one address: a class of 300 behind one school address
  joining, fetching and handing in at once stays under it. Whether CDN cache
  hits on `/api/live/*` count against the rule isn't documented: the
  deploy-preview check of §3.10 measures it, and if they do, the limit goes to
  7,200 (300 students polling every 3 s are 6,000 a minute), leaving the
  per-address allowance of §3.5 to do the fine work.
  `export default (req, context) => (new URL(req.url).pathname.startsWith("/api/live/") ? pulse : handle)(req, context, { state, store: getStore({ name: storeName("classroom", context), consistency: "strong" }), misc: getStore({ name: storeName("classroom-misc", context), consistency: "strong" }), now: Date.now, env, ip: context.ip, purge: tags => purgeCache({ tags }) })`
  (`purgeCache` from `@netlify/functions`), with Sync's `storeName`
  (production / `-preview` / `-dev`), so previews and local runs never touch
  real classes. Every thrown error becomes `500 {"error":"server_error"}`,
  never a stack.
- `netlify/functions/classroom-cleanup.mjs` — `config = { schedule: "@daily" }` (§3.11).
- `scripts/classroom-mock-server.mjs` — a plain Node HTTP server serving
  `handle()` and `pulse()` on the memory stub (with a `purge` that records the
  tags it was given) at
  `http://localhost:8788/api/classroom/v1/…` and `/api/live/v1/…`, with
  test-only controls under `/__mock/` (§10 S). Every other package's tests
  run against it.
- Shared with Sync: `netlify/lib/common.mjs` (`sameSecret`, `rateKey`,
  `allowIn`, `bump`, `done`, `env`); the gate, the request breaker, the site
  byte budget, `readJSONBody`, `bearer` and the error reply helpers move from
  `sync.mjs` into `netlify/lib/api.mjs` and both import them (behaviour
  unchanged; `test_sync_server.mjs` still passes). The rate pepper and the
  allowed origins are Sync's (`SYNC_RATE_PEPPER`, `SYNC_ORIGINS`): one secret,
  one list. The join pepper is the classroom's own (`CLASSROOM_JOIN_PEPPER`,
  §1.3): the function refuses to start a join or a create without it
  (`503 busy`), so a deploy that forgot it fails loudly, not openly.

### 3.2 Requests

Base URLs `https://cedarlogic.netlify.app/api/classroom/v1` and
`https://cedarlogic.netlify.app/api/live/v1`. Apps read overrides from
`CL_CLASSROOM_URL` and `CL_LIVE_URL` (as `CL_SYNC_URL`); `http://` only for
`localhost` / `127.0.0.1`.

| Header | Value |
|---|---|
| `Authorization` | `Bearer <teacherToken>` (teacher endpoints), `Bearer <studentToken>` (student endpoints), `Bearer <joinToken>` (the two join endpoints); none on `/health`, `/api/live/*` and `GET /move/{moveId}` |
| `x-cedarlogic-student` | student endpoints: the `studentId` the token belongs to (the server looks the hash up by it) |
| `x-cedarlogic-client` | `mac/0.4.0+830`, `web/<build>`; logged nowhere |
| `x-cedarlogic-key` | apps only: `APP_KEY`, as Sync |
| `x-cedarlogic-delete` | `DELETE /classes/{classId}` only: the `deleteToken` |
| `If-None-Match` | status, the pulse, the submissions index and the answers: the `ETag` last seen → `304` |
| `content-type` | `application/json` with a body |

Bodies are JSON (≤ 4,000,000 bytes; larger → `413 too_large`). Every
`/api/classroom/` response is JSON with `cache-control: no-store` and a `Date`
header (clients keep Sync's clock offset from it); errors are
`{"error":"<code>","message":"<a sentence a person can read>"}` plus
`retryAfter` and a `Retry-After` header with 429/503. `/api/live/` responses
carry cache headers instead (§3.10).

**Who may call**: Sync §3.2's gate (an `Origin` of the site or of
`SYNC_ORIGINS`, a same-origin `Sec-Fetch-Site`, or the app key), with the
same CORS answer for the extra origins and `Access-Control-Allow-Headers:
authorization, content-type, if-none-match, x-cedarlogic-client,
x-cedarlogic-delete, x-cedarlogic-student`. The `/api/live/` endpoints skip
the gate: they are cacheable, need the class's `fetchKey` in the path, and
hold nothing but ciphertext and two counters.

### 3.3 Endpoints

`{classId}`, `{joinId}`, `{moveId}`, `{fetchKey}` = 32 lowercase hex;
`{aid}`, `{sid}` = lowercase UUID v4. Anything else → `404 not_found`. "T" = teacher token,
"S" = student token (+ `x-cedarlogic-student`), "J" = join token, "–" = none.

| Method | Path | Who | Does | Success |
|---|---|---|---|---|
| GET | `/health` | – | `{ok:true, protocol:1, limits}` | 200 |
| PUT | `/classes/{classId}` | T | create the class (first device), re-create a never-used one, or confirm it | 201 / 200 |
| GET | `/classes/{classId}` | T, S | status (the index of everything; the student view is smaller) | 200 / 304 |
| DELETE | `/classes/{classId}` | T + delete token | delete the class; the id answers 410 forever | 200 |
| PUT | `/classes/{classId}/teacher` | T | the teacher record `{base, ver, env}` | 200 / 412 |
| PUT | `/classes/{classId}/join` | T | a new join code and/or open/closed: `{joinId, joinToken, open, ver, env, teacher:{base, ver, env}}` | 200 / 412 |
| PUT | `/classes/{classId}/info` | T | the class info `{base, ver, env}` | 200 / 412 |
| PUT | `/classes/{classId}/assignments/{aid}` | T | post or edit `{base, ver, env, closesAt}` | 201 / 200 / 412 |
| DELETE | `/classes/{classId}/assignments/{aid}` | T | remove it and its submissions | 200 |
| GET | `/classes/{classId}/students` | T | the roster: `{students:[{studentId, joinedAt, seenAt, name:{ver, env}}]}` | 200 |
| DELETE | `/classes/{classId}/students/{sid}` | T, or S for its own `sid` | remove a student / leave: the roster entry and answers; hand-ins stay, unless the teacher adds `?submissions=delete` | 200 |
| POST | `/classes/{classId}/students/remove` | T | remove several: `{ids:[≤ 300 sids], submissions:"keep"\|"delete"}` | 200 |
| GET | `/classes/{classId}/assignments/{aid}/submissions` | T | the index `{subs:{sid:{ver, size, at, h, attempts, firstAt}}}` | 200 / 304 |
| POST | `/classes/{classId}/assignments/{aid}/submissions/fetch` | T | `{ids:[≤ 50 sids]}` → `{records:[{studentId, ver, size, at, h, attempts, env}], missing, deferred}` | 200 |
| GET | `/classes/{classId}/assignments/{aid}/submissions/{sid}` | T, or S for its own | one submission record | 200 |
| PUT | `/classes/{classId}/assignments/{aid}/submissions/{sid}` | S (own) | hand in `{base, ver, env}` | 201 / 200 / 409 / 412 |
| PUT | `/classes/{classId}/live` | T | go live, push, predict, reveal, end: `{base, ver, session, on, predict, env}` | 200 / 412 |
| GET | `/classes/{classId}/live/answers?session=…` | T | `{session, answers:[{studentId, ver, h, env}]}` | 200 / 304 |
| PUT | `/classes/{classId}/live/answers/{sid}` | S (own) | `{session, ver, env}` (the latest wins) | 200 |
| GET | `/join/{joinId}` | J | what the code holds: `{classId, ver, env, open}` | 200 |
| POST | `/classes/{classId}/students` | J | join: `{studentId, tokenHash, name:{ver:1, env}}` → `{joinedAt, fetchKey}` | 201 |
| PUT | `/classes/{classId}/students/{sid}` | S (own) | rename: `{name:{ver, env}}` | 200 |
| PUT | `/move/{moveId}` | S | leave a move slot: `{classId, studentId, env}` | 201 |
| GET | `/move/{moveId}` | – | `{classId, env, expiresAt}` | 200 |
| DELETE | `/move/{moveId}` | S (the slot's student), – after expiry | | 200 |
| GET | `/api/live/v1/{classId}/{fetchKey}` | – (the `fetchKey`) | **the pulse**: `{"seq":12,"live":57,"p":3}` (`live` 0 when no session is on; `p` the poll interval in seconds), `ETag: "12.57"` | 200 / 304 / 404 |
| GET | `/api/live/v1/{classId}/{fetchKey}/live/{ver}` | – (the `fetchKey`) | the live record `{ver, session, env}` of exactly that version | 200 / 404 |
| GET | `/api/live/v1/{classId}/{fetchKey}/assignment/{aid}/{ver}` | – (the `fetchKey`) | the assignment record `{ver, env}` of exactly that version | 200 / 404 |
| GET | `/api/live/v1/{classId}/{fetchKey}/info/{ver}` | – (the `fetchKey`) | the info record of exactly that version | 200 / 404 |

`limits` everywhere =
`{"maxStudents":300,"maxAssignments":100,"maxRecordBytes":524288,"maxSmallRecordBytes":4096,"maxNameBytes":640,"maxAnswerBytes":2048,"maxClassBytes":104857600,"maxStudentBytes":10485760,"maxFetchIds":50,"pulseSeconds":3,"idleSeconds":60,"moveSeconds":600,"expiryDays":400}`.

**Entry** shapes: a record index entry is `{ "ver": 3, "size": 2048, "at": ms, "h": "<32 hex>" }`
(a submission's adds `attempts`, `firstAt`; an assignment's adds `closesAt`:
ms or null); a **record** is an entry plus `"env": "<base64url>"`.

**Class status** (`GET /classes/{classId}`), teacher view:

```json
{ "seq": 12, "createdAt": ms, "activeAt": ms, "expiresAt": ms, "fetchKey": "<32 hex>",
  "join": {"ver": 3, "h": "…", "open": true, "at": ms},
  "teacher": {"ver": 2, "h": "…", "env": "<b64u: the teacher record itself, ≤ 4 KiB>"}, "info": {"ver": 1, "h": "…"},
  "assignments": { "<aid>": {"ver": 1, "size": 770, "at": ms, "h": "…", "closesAt": null} },
  "live": {"ver": 57, "session": "<uuid>", "on": true, "predict": true, "at": ms},
  "students": 212, "bytes": 1234567, "limits": {…} }
```

The teacher record travels inside the teacher's status (it is small and
under `backupKey`, which only the teacher has), so a second device needs one
request to get everything. `join.h` is the `h` of the join record on the
server, which the teacher's devices compare with the one they wrote (§4.1).
The student view has `seq`, `fetchKey`, `info`, `assignments`, `live`,
`expiresAt` and `limits` only. Both carry `ETag: "<seq>"`; `If-None-Match` →
`304`. There is no public key in either: the server has none (§1.4).

#### `PUT /classes/{classId}` — create, re-create or confirm

Body `{"deleteHash":"<64 hex>","teacher":{"ver":1,"env":"…"},"join":{"joinId":"<32 hex>","joinToken":"<43 b64url>","open":true,"ver":1,"env":"…"},"info":{"ver":1,"env":"…"}}`
(all required to create; ignored when confirming).

1. A `gone/<classId>` marker → `410 class_deleted` / `410 class_expired`.
2. `a/<classId>` exists with another `teacherHash` → `401 wrong_key`.
3. `a/<classId>` missing: a new class. `CLASSROOM_CLOSED=1` → `503 busy`.
   Validate the body (envelopes base64url decoding to 30…4,096 bytes with
   byte 0 = 1 and byte 1 ∈ {0, 1}; `deleteHash` 64 hex; `joinId` 32 hex;
   `joinToken` 43 base64url characters; an unknown field such as an old
   client's `pub` is ignored and never stored) → else `400 bad_request`. Rate checks: `create/<ipkey>` (10 per
   address per day), the site's daily creation brake (env
   `CLASSROOM_NEW_CLASSES_PER_DAY`, default 200) and the site byte budget →
   `503 classroom_busy` when over. Write `a/<classId>` =
   `{teacherHash, deleteHash, createdAt}` with `onlyIfNew` (if that loses,
   re-read and go to step 2).
4. `c/<classId>` exists → `200 {created:false, …status}`.
5. Else store the three envelopes as blobs (`t/`, `i/`, and `j/<joinIndex>`
   = `{classId, joinHash, open, ver, env, at}` with the HMACs of §1.3, with
   `onlyIfNew`: a `joinIndex` already taken by another class — 1 in 2⁴⁸ per
   pair of codes — or retired (a changed code, a deleted class: below) makes
   the client pick another join code: `409 join_exists`; only the class's own
   code, kept while it was idle, is taken back), make the class's `fetchKey` (16 bytes from the CSPRNG,
   hex), then write the class document (§3.6) with `onlyIfNew` →
   `201 {created:true, …status}`.

**Create Classroom** uses PUT with brand-new codes and keys (must be 201; on
200 — practically impossible — start again with new secrets). A device that
has the teacher key and meets `404 no_class` on a never-used class (removed
after 7 idle days, §3.11) re-creates it the same way from what it holds.

#### `GET /join/{joinId}` and `POST /classes/{classId}/students` — joining

`GET /join/{joinId}` with `Authorization: Bearer <joinToken>`: an address
already past its 60 failed lookups this hour → `429`, before anything is
looked up, so past the limit a right code and a wrong one look the same. The
server computes `joinIndex` (§1.3); `j/<joinIndex>` missing → `404 no_class`
(counted as a failed lookup); the class gone → `410`; `joinHash` ≠ HMAC of
the token → `401 wrong_token` (counted; `404 no_class` for a retired code).
The whole code right but changed since → `404 no_class` with "This code was
changed. Ask your teacher for the new one." and `changed: true` (not
counted). Else
`200 {classId, ver, env, open}`. Nothing is written.

**A join code is never freed.** Change Join Code, Delete Class, expiry and the
7-day removal of a never-used class turn its `j/` entry into a retired one,
`{retired:true, reason, classId, joinHash, at}` (`reason`: `changed`,
`deleted`, `expired` or `idle`), kept 400 days, so whoever knows an old code
(a leak, an old handout) can't register a class under it for students to
join. Only the same class can take back an `idle` one (its `a/` is kept, so
only its own teacher token gets that far). The server never tells how many students a class has to someone who
hasn't joined.

`POST /classes/{classId}/students` with the join token, body
`{"studentId":"<uuid>","tokenHash":"<64 hex>","name":{"ver":1,"env":"<sealed, ≤ 640 bytes>"}}`:
the class's **current** join entry's `joinHash` must match the token (an old
code → `401 wrong_token`); `open` false → `403 join_closed`; roster at
`maxStudents` → `507 class_full` (an address past its failed lookups → `429`
first, as above); `studentId` already in the roster →
`409 student_exists`; a name envelope over 640 bytes → `413
record_too_large`; counted per address (300 joins an hour: a lab; and 500
into one class a day: a whole lecture behind one school address) and per
class (1,000 a day). Then
a conditional write of the roster document (§3.7) adding `{hash: tokenHash,
joinedAt: now, seenAt: now, name: {ver, env, size}}` →
`201 {joinedAt, fetchKey}`. A lost answer: the client repeats the POST; `409`
on its own `studentId` → it checks with `GET /classes/{classId}` under its
token: `200` means it is in; `401`/`403` means the id belongs to someone else
(impossible in practice) — make a new one and join again.

#### `PUT /classes/{classId}/join` — change the code, open or close joining

Body `{"joinId":"…","joinToken":"…","open":true,"ver":4,"env":"…","teacher":{"base":2,"ver":3,"env":"…"}}`.
The teacher record travels in the same request so the join code and the
record that remembers it change together: `teacher.base` ≠ the current
teacher ver → `412 conflict` (nothing written). Else: write the new
`j/<joinIndex>` (`onlyIfNew` unless it is the current one; a taken index →
`409 join_exists`) and the teacher blob, then one conditional write of the
class document (`joinIndex`, `join: {ver, h, open, at}`, `teacher`, `seq +
1`, and — when the code changed — a new `fetchKey`), then retire the old
`j/<oldJoinIndex>` (best effort; the cleanup retires a `j/` entry no class
names; a retired code → `409 join_exists`) and purge the tag `class-<classId>` (§3.3, the pulse). Closing or
re-opening with the same code sends the same `joinId`, `joinToken` and `env`
with `open` changed, and keeps the `fetchKey`. A new code is a new
`fetchKey` because the old code may be the leak: whoever joined with it and
was then removed, or only ever read the join record, loses the record paths
with it; current members get the new value from their next status.

#### `PUT /classes/{classId}/assignments/{aid}` — post or edit

Body `{"base":0,"ver":1,"env":"<class-key envelope ≤ 512 KiB>","closesAt":null}`
(`closesAt`: ms, or null; the client sets it to `dueAt` when
`closeAfterDue` is on). `base` ≠ the current ver (0 for a new id) →
`412 conflict` with `current`; the same `ver` and `h` again → `200` (replay);
`maxAssignments` reached by a new id → `507 too_many_assignments`; the
class's bytes over `maxClassBytes` by a growing write → `507 class_full`
(edits that shrink and deletes always go through); the site budget → `507
site_full`. Store the blob, then the class document (`assignments[aid]`, `seq
+ 1`) → `201` (new) / `200`. Every student's next pulse shows the new `seq`.

#### `PUT …/submissions/{sid}` — hand in

Student auth; `sid` must be the header's student. Body `{"base":1,"ver":2,"env":"<sealed envelope ≤ 512 KiB>"}`.
The assignment must exist (`404 no_assignment`); `closesAt` set and `now >
closesAt` → `409 assignment_closed`; the envelope must be a sealed one (byte 0
= 2); counted: 60 hand-ins per student per hour, 600 per class per hour, and
the student's bytes in the class ≤ `maxStudentBytes` → else `429` /
`507 student_full`. Replay and `base` as above (the index entry is the
record). Store the blob, then the submissions index document of that
assignment (`subs[sid] = {ver, size, at, h, b, attempts+1, firstAt}`) with a
conditional write → `201` the first time, `200` after. The previous blob is
garbage. A student can `GET` its own record back (the Mac app uses that to
show "Handed in 4 Oct 10:31 · attempt 2" after a reinstall).

#### `PUT /classes/{classId}/live` — the live session

Body `{"base":56,"ver":57,"session":"<uuid>","on":true,"predict":true,"env":"<class-key envelope>"}`.
`base`/`ver` against `live.ver` in the class document (the slot starts at 0);
a second teacher device that is behind gets `412` with `current` and asks
the teacher (§4.4). `on: false` ends the session (`env` is the ended
record; `live` in the pulse becomes 0). Store the blob, write the document
(`live = {ver, session, on, predict, at, size, h, b}`, `seq + 1`). Answers of
sessions other than the current one are deleted by the cleanup after a day.

#### `PUT /classes/{classId}/live/answers/{sid}` — a predict answer

Student auth. Body `{"session":"<uuid>","ver":57,"env":"<sealed ≤ 2 KiB>"}`.
The session must be the live one and `ver` ≤ its ver (an answer to the
version just replaced is kept and labelled by its ver), else
`409 not_live`; counted 10 per student per minute and 6,000 per class per
day. Writes `la/<classId>/<session>/<sid>` = `{ver, h, at, env}` (a plain
overwrite: no document, no contention when 300 answer in the same ten
seconds) → `200 {at}`.

#### `GET /classes/{classId}/live/answers?session=S`

Teacher. Lists `la/<classId>/<S>/` (keys and etags only), `ETag` = first 32
hex of SHA-256 of the sorted `key:etag` lines; `If-None-Match` equal → `304`
without reading a blob. Else reads them 16 at a time (≤ 500) →
`200 {session, answers:[{studentId, ver, h, env}]}`.

#### `GET …/submissions` and `POST …/submissions/fetch`

Teacher. The index document with `ETag: "<its etag>"` (`304` when
unchanged). `fetch` takes ≤ 50 student ids and answers like Sync's fetch
(records in the order asked until 4,000,000 characters or ~6 s, the rest in
`deferred`; ids not handed in in `missing`); bytes served count against the
class's daily serve budget (200 MB).

#### `DELETE /classes/{classId}/students/{sid}`

With the teacher token: the roster entry and the student's answers
(`la/<classId>/*/<sid>`); the hand-ins **stay** (each is readable on its own,
with the name inside: the teacher's list shows them under that name, marked
"left the class") unless the request says `?submissions=delete`, which also
removes every submission of theirs (each assignment's index document, then
the blobs; `todo/<classId>` lists what the cleanup must finish) →
`200 {removed:true}`. With the student's own token: the roster entry and the
answers; hand-ins stay (§4.7) → `200 {left:true}`. Either way the class gets a
new `fetchKey` (one conditional write of the class document, `seq + 1`, then
a purge of `class-<classId>`), and the student's next request answers
`403 not_a_member`.

`POST /classes/{classId}/students/remove` (teacher; `{ids, submissions}`):
the same for up to 300 students in one roster write and one `fetchKey`
change — what "Remove everyone who joined after 10:05" and a multi-select in
the Students tab (§5.2) send after someone posted the code online. Counted as
one teacher write; the submissions, when deleted, go through `todo/`.

#### `DELETE /classes/{classId}/assignments/{aid}`

Teacher. Conditional write of the class document (the entry removed, `seq +
1`), purge the tag `asg-<aid>`, then its index document and blobs as time
allows (`todo/`). `200`.

#### `DELETE /classes/{classId}`

As Sync's `DELETE /spaces`: the teacher token plus `x-cedarlogic-delete`
whose SHA-256 is `deleteHash` (`403 wrong_delete_token`); 5 per class per
day. Marks `a/` deleted, writes `gone/<classId>` (`{at, reason:"deleted"}`,
kept forever), `todo/<classId>`, retires `j/<joinIndex>`, purges the tag
`class-<classId>` (so the CDN's copies of its records go too), then the
documents and the blobs in batches as time allows → `200 {deleted:true}`. Every later request
on the id, from anyone, gets `410 class_deleted`.

#### The move slot

`PUT /move/{moveId}` (student auth; body `{"classId", "studentId", "env"}`,
`studentId` must be the caller): a live slot with this id → `409 move_exists`
(make a new code); counted 10 per student per hour and 30 per address per
hour; writes `mv/<moveId>` = `{classId, studentId, env, createdAt}` →
`201 {expiresAt}`. `GET /move/{moveId}` (no auth: knowing the id is knowing
the code): `200 {classId, env, expiresAt}` or `404 move_gone` (at or after
`createdAt + 600,000 ms`, whatever is stored). `DELETE /move/{moveId}`: the
slot's student's token, or anyone once it has expired → `200`. The cleanup
deletes slots older than an hour.

#### The pulse (`/api/live/v1/…`)

`GET /{classId}/{fetchKey}`: reads the class document (one strong read of a
few KB): `200 {"seq":12,"live":57,"p":3}` with `ETag: "12.57"`,
`Cache-Control: no-store` (for browsers), `Netlify-CDN-Cache-Control: public,
durable, s-maxage=2, stale-while-revalidate=5` and `Netlify-Cache-Tag:
class-<classId>`; an `If-None-Match` that matches → `304` with the same cache
headers; no such class (or gone) → `404 {"error":"no_class"}` cached 10 s; a
`fetchKey` that isn't the class's current one → `404
{"error":"wrong_fetch_key"}` cached 10 s (a member then reads its status,
which has the new one; a removed member gets `403 not_a_member` there).
`p` is `limits.pulseSeconds` (env `CLASSROOM_PULSE_SECONDS`), so the owner
can slow a whole site's polling without a deploy.

`GET /{classId}/{fetchKey}/live/{ver}`, `…/assignment/{aid}/{ver}`,
`…/info/{ver}`: the class document is read first (the `fetchKey` must be the
current one, else `404 wrong_fetch_key` as above; a cache miss happens about
once per version, so the extra read is cheap), then the record of exactly
that version — `200 {ver, session?, env}` with `Netlify-CDN-Cache-Control:
public, durable, max-age=31536000, immutable` (a version's bytes never
change) and `Netlify-Cache-Tag: class-<classId>` (an assignment's also
`asg-<aid>`) — or `404 {"error":"no_version"}` cached 10 s when the class has
moved on (the client reads the pulse again). Every `/api/live/` response
carries the class's cache tag, so a purge (a deleted class or assignment, a
new `fetchKey`, an expired class) removes the CDN's copies at once; the
purge is best effort (a failure is logged as a count and retried by the
cleanup), and the deletion texts promise only what it does. The version is in the path, never in a query
string: Netlify's CDN keys a function's response on the whole URL, query
string included, so a `?v=` would work but would also let any `?x=…` make a
fresh cache miss; these endpoints ignore query strings, clients never send
one, and no `Netlify-Vary` header is needed. They hold only ciphertext under
the class key, and only someone who has the current `fetchKey` — a member,
from an authenticated status — can download them. A student who leaves or is
removed, or who only ever had an old join code, keeps the class key but not
the path: an honest server stops serving them anything new. (A dishonest
server could, and a current member could pass the path on; that is the
limit of a class key that never changes, §0.)

### 3.4 Status codes and errors (complete list)

| Status | `error` | `message` (shown as is) | Client does |
|---|---|---|---|
| 400 | `bad_request` | "That request wasn't understood." | bug: log, back off |
| 401 | `wrong_key` | "This teacher key doesn't match this class." | teacher: stop; show it |
| 401 | `wrong_token` | "This device isn't known to the class any more. Join again with the class code." | student: forget the membership after one retry (§4.11) |
| 403 | `forbidden` | "Not from CedarLogic." | stop until restart |
| 403 | `wrong_delete_token` | "That key can't delete the class." | bug |
| 403 | `join_closed` | "Joining this class is closed. Ask your teacher to open it." | show |
| 403 | `not_a_member` | "You're not in this class any more." | student: forget the membership; keep the circuits |
| 404 | `no_class` | join: "No class has this code. Check it with your teacher." (a changed code: "This code was changed. Ask your teacher for the new one.") / teacher: "This class isn't on the website." | join: show; teacher: re-create (§4.1) |
| 404 | `no_assignment` | "That assignment was removed." | drop it from the list |
| 404 | `no_version` | — (the pulse's record fetch; never shown) | read the pulse again |
| 404 | `wrong_fetch_key` | — (`/api/live/` with an old `fetchKey`; never shown) | read the status (authenticated), take its `fetchKey`, try again once |
| 404 | `move_gone` | "That move code has expired. Make a new one on the other device." | show |
| 404 | `not_found` | "Not here." | bug |
| 405 | `method_not_allowed` | "That can't be done here." | bug |
| 409 | `student_exists`, `join_exists`, `move_exists` | "Try again." | new id / code, once more |
| 409 | `assignment_closed` | "Hand-ins for this assignment are closed." | show, with the due date the client knows |
| 409 | `not_live` | "The teacher isn't showing this question any more." | drop the answer |
| 410 | `class_deleted` | "This class was deleted by the teacher." | forget the class (§4.11) |
| 410 | `class_expired` | "This class was removed after 400 days without use." | forget the class |
| 412 | `conflict` | "This was changed on another device." (+ `current`) | teacher: refetch, show, let the person decide (§4.1); student hand-in: take `current.ver` as base and send again |
| 413 | `too_large` | "That's too much to send at once." | bug |
| 413 | `record_too_large` | "Too big to send (over 512 KB)." | show; the circuit stays local |
| 429 | `rate_limited` | "Lots going on just now. Trying again in a minute." | wait `Retry-After`, ≥ 30 s |
| 500 | `server_error` | "Something went wrong on the website." | back off |
| 503 | `busy` | "The classroom service is busy. Trying again shortly." | back off (`Retry-After`) |
| 503 | `classroom_busy` | "Classrooms can't be made today. Try again tomorrow." | show; don't retry |
| 503 | `classroom_paused` | "Classrooms are resting on the website for today. Your work is safe on this device." | writes only (§3.5): back off until `Retry-After`; reading goes on |
| 507 | `class_full` | "This class is full." | show |
| 507 | `too_many_assignments` | "This class has as many assignments as it can hold (100). Delete one to post another." | show |
| 507 | `student_full` | "You've handed in as much as this class allows. Ask your teacher." | show |
| 507 | `site_full` | "The website's classroom storage is full just now." | show; keep the work local |

### 3.5 Limits and abuse policy (stated plainly)

Free encrypted storage with public write endpoints invites abuse, and the
server can't look inside. The budget is the site's Netlify plan, shared with
Sync, feedback and the stats function (§3.10 has the plan and the numbers).

- **Caps per class**: 300 students (`maxStudents`; 500 as a hard ceiling in
  code), 100 assignments, 512 KiB per assignment, submission or live record,
  4 KiB per small record, 2 KiB per answer, 100 MiB in all, 10 MiB of hand-ins
  per student.
- **Per address** (`ipkey` = Sync's HMAC of the address, IPv6 /64): 10 new
  classes a day; 60 failed lookups or sign-ins an hour (wrong join code, wrong
  token, unknown class, wrong `fetchKey`), then `429` for that hour — for a
  join lookup or a join, even with the right code; 300 joins an hour, and 500
  into any one class a day; 30 move slots an hour; 50 MB of
  envelopes written a day.
- **Per address, every invocation** (`CLASSROOM_ADDRESS_PER_HOUR`, default
  6,000, and `CLASSROOM_ADDRESS_PER_DAY`, default 12,000: a 300-student school
  behind one address in a predict-heavy lecture makes about 4,700 function
  invocations — pulse misses, answers, joins — in its 75 minutes). Counted on
  every request that reaches the function, 4xx and 5xx included, before
  anything else is read: each instance counts in memory per `ipkey` and adds
  its count to `rate/<hour>/addr/<ipkey>` with Sync's `allowIn` every 50
  requests (so a busy address costs one counter write per 50 requests, not
  one each); an address over either allowance gets `429 rate_limited` (with
  `Retry-After` to the window's end) and is remembered in that instance's
  memory until then. Over-allowance requests are **not** counted by the
  site's breaker below.
- **Per class, only after the token checks out**: 1,000 joins a day; 600
  hand-ins an hour; 6,000 answers a day; 200 MB served by `fetch` a day; 5
  deletions a day; 60 teacher writes a minute.
- **Per student**: 60 hand-ins an hour; 10 answers a minute; 5 renames an
  hour; 10 move slots an hour.
- **Site-wide**: a byte budget (env `CLASSROOM_MAX_TOTAL_BYTES`, default 4 GiB)
  summed by the cleanup; a daily creation brake (`CLASSROOM_NEW_CLASSES_PER_DAY`,
  200); a daily request breaker (`CLASSROOM_MAX_REQUESTS_PER_DAY`, default
  20,000 invocations that were within their address's allowance — Netlify
  counts invocations, not cached hits, so the pulse's cache hits are free).
  **Past the breaker only writes stop**: every `PUT`, `POST` and `DELETE`
  except `DELETE /classes/{classId}` gets `503 classroom_paused` until 00:00
  UTC; the status, the pulse and the record fetches keep being served, so a
  lecture in progress goes on (students' hand-ins and answers queue or drop
  as §4.8 says). This is the one place that says so; §3.4 and §8.3 follow it.
  `CLASSROOM_CLOSED=1` refuses new classes and every write with `503 busy`
  (reads and the pulse keep working).
- **Before the function runs**: the Netlify rate-limit rule of §3.1, on both
  `/api/classroom/*` and `/api/live/*` (one function, one rule).
- **A flood.** Every request that reaches a function is an invocation, and
  the legacy Free plan has 125,000 a month, so what stops a flood from
  spending them is the edge rule (a request it refuses never runs the
  function), not anything inside the function: a `429` or a `503` is still an
  invocation. One address is held to the rule's 1,200 a minute by the edge
  and to 6,000 an hour and 12,000 a day by the allowance; the allowance keeps
  it from tripping the breaker for everyone (it can spend at most 12,000 of
  the breaker's 20,000; tripping it takes two addresses at full allowance in
  one day), and the pulse's made-up class ids and stale `fetchKey`s are
  failed lookups, `429` after 60. What no rule here prevents is a flood from
  many addresses spending the month's invocations: that is the same exposure
  `/api/sync` and `/api/feedback` have today, and the answer is by hand — a
  Netlify firewall traffic rule blocking the source (two per site on the Free
  plan), raising the pulse interval, or the owner's choice of §11.4. Netlify's
  suspension of the whole site (feedback and sync with it) is what the
  breaker exists to make come second, not what it can always prevent.
- **Counters** live in `classroom-misc` with Sync's `allowIn` (fixed windows,
  conditional writes, fail open after two lost tries, deleted daily).
- **Expiry** (in the UI and on the website, in these words): "A class nobody
  opens for 400 days is removed from the website. Everyone keeps the circuits
  on their own devices." A class with no students and no assignments that
  nobody touched for 7 days is removed quietly (the teacher's device
  re-creates it from the key at the next open).
- **Logging**: no bodies, tokens, codes, ids or addresses; counts only.

### 3.6 Storage layout (Netlify Blobs)

Store `classroom` (per deploy context), `consistency: "strong"`:

| Key | Value |
|---|---|
| `a/<classId>` | `{"teacherHash","deleteHash","createdAt","deleted"?,"reason"?}` — read first on every teacher request |
| `c/<classId>` | the class document (below): teacher-written; `activeAt` touched daily |
| `r/<classId>` | the roster document: `{"v":1,"students":{"<sid>":{"hash":"<64 hex>","joinedAt":ms,"seenAt":ms,"name":{"ver":1,"size":212,"env":"<b64u>"}}},"count":212,"bytes":…}` — read on every student request |
| `si/<classId>/<aid>` | the submissions index: `{"v":1,"subs":{"<sid>":{"ver":2,"size":392,"at":ms,"h":"…","attempts":2,"firstAt":ms,"b":"<blob name>"}},"bytes":…}` |
| `t/<classId>/<ver>-<ms>-<rand8hex>` | the teacher record envelope (raw bytes) |
| `i/<classId>/<ver>-<ms>-<rand>` | the info envelope |
| `as/<classId>/<aid>/<ver>-<ms>-<rand>` | an assignment envelope |
| `lv/<classId>/<ver>-<ms>-<rand>` | a live record envelope |
| `s/<classId>/<aid>/<sid>/<ver>-<ms>-<rand>` | a submission envelope |
| `la/<classId>/<session>/<sid>` | `{"ver":57,"h":"…","at":ms,"env":"<b64u>"}` |
| `j/<joinIndex>` | `{"classId","joinHash","open","ver","env","at"}` — `joinIndex` and `joinHash` are HMACs under `CLASSROOM_JOIN_PEPPER` (§1.3); neither `joinId` nor a plain hash of the join token is stored anywhere. A retired code: `{"retired":true,"reason","classId","joinHash","at"}`, kept 400 days (§3.3) |
| `mv/<moveId>` | `{"classId","studentId","env","createdAt"}` |
| `gone/<classId>` | `{"at": ms, "reason": "deleted" \| "expired"}` — kept forever |
| `todo/<classId>` | unfinished deletions: `{"at", "all"?: true, "students"?: [sid], "assignments"?: [aid]}` |

Store `classroom-misc`: `rate/<end>/<key>` counters (`rate/<hour>/addr/<ipkey>` among them), `day/<yyyymmdd>/creates`,
`day/<yyyymmdd>/req/<instance>`, `budget`, `cleanup/cursor`.

The class document:

```json
{
  "v": 1, "createdAt": 1759600000000, "activeAt": 1759690000000, "seq": 12,
  "fetchKey": "4f6b0c2d…c7d9",
  "joinIndex": "3bd699d5b6c129e47eb1c224c07e596f47f62cbf51fd9214fc9d96ebaa5fed2a",
  "join": { "ver": 1, "h": "91896986016f65d61c8caff95f203d5e", "open": true, "at": 1759600000000 },
  "teacher": { "ver": 1, "size": 379, "at": 1759600000000, "h": "570aeb55…", "b": "1-1759600000000-9f2c4e1a" },
  "info":    { "ver": 1, "size": 105, "at": 1759600000000, "h": "9c0cc08b…", "b": "1-1759600000000-1c2d3e4f" },
  "assignments": {
    "7c9e6679-7425-40de-944b-e07fc1f90ae7": { "ver": 1, "size": 738, "at": 1759600000000, "h": "4e60b267…", "b": "1-1759600000000-77aa0012", "closesAt": null }
  },
  "live": { "ver": 57, "session": "2b1d0a9c-3e4f-4a5b-8c6d-7e8f9a0b1c2d", "on": true, "predict": true, "at": 1759603600000, "size": 662, "h": "750b6b99…", "b": "57-1759603600000-0a0b0c0d" },
  "bytes": 1234567
}
```

`seq` rises on every teacher write (not on joins, hand-ins or answers, which
live in their own documents and blobs — so the pulse changes only when there
is something new for students). `bytes` counts every envelope of the class
(the roster's and the indexes' envelopes included, kept up to date by their
writes). With 100 assignments the document is ~20 KB; the roster at 300
students ~110 KB (≤ 640 bytes of name envelope each, so never much more); an
index at 300 hand-ins ~40 KB.

### 3.7 Consistency: every change is one conditional write

As Sync §3.7, document by document: a request reads the document it needs
with `getWithMetadata`, decides, stores new envelopes as blobs nobody refers
to yet, then writes the document once with `onlyIfMatch: etag` (or
`onlyIfNew`); a write is done **only if `w.modified && w.etag`** (the
`@netlify/blobs` trap); anything else means "unknown": re-read and decide
again (the replay rule recognises a write that did land); five tries, then
`503 busy` (`retryAfter: 2`). After a definite loss the request waits a
random moment (`Math.random() * 100 * (attempt + 1)` ms) so 300 hand-ins at
the deadline minute don't lose the same rounds together. Blobs are never
deleted inline — not a record's previous blob, not a lost write's — and a
blob no document names is collected by the cleanup an hour later. Answers and
move slots are plain blobs with no document, so they never contend.

The documents a request may write, and in which order when it writes two:
blobs first, then `c/` (class) **or** `r/` (roster) **or** `si/` (one
assignment's index) — never two documents in one request, except `PUT /join`
(the class document only; the `j/` entries are blobs). Deleting a student or
an assignment touches several documents one after another and leaves a
`todo/` for the cleanup, so a half-done removal is finished, never left
inconsistent for long.

### 3.8 Auth and activity

Teacher: read `a/<classId>` (missing → `gone/` → `410`, else `404 no_class`,
counted); `deleted` → `410`; compare the token's SHA-256 with `teacherHash`
in constant time → `401 wrong_key` (counted). Student: read `r/<classId>`;
the `x-cedarlogic-student` id missing from it → `403 not_a_member`; its hash
≠ the token's → `401 wrong_token` (counted). Join: `j/<joinIndex>` as §3.3.
Only after the token checks out are per-class counters touched.

`activeAt` on the class document is updated when it is more than 24 hours old
(a conditional write whose failure is ignored), by teacher **and** student
requests: a class students still use is active. A student's `seenAt` in the
roster likewise, daily. `expiresAt` in every status = `activeAt + 400 days`.

### 3.9 Idempotency, in one place

- `PUT /classes/{classId}`: idempotent (200 the second time).
- Teacher record writes, assignments, live, hand-ins: a lost answer followed
  by the same request is answered `200` by the replay rule (same `ver`, same
  `h`); a different write of the same record gets `412` with `current`.
- Join: `409 student_exists` on one's own id → confirm with a status read
  (§3.3).
- Answers, renames: plain overwrites; repeating is harmless (a rename is
  counted, §3.5, and a forged one fails the teacher's proof check, §2.2).
- `DELETE` class: a second call gets `410`, treated as done; `DELETE`
  student/assignment: a second call gets `200`.

### 3.10 The live view's load, and what it costs

**The plan, as read on 2026-10-06** (`netlify api getSite` for
`4021c3de-8c03-48e0-80dc-90fcb5faf0c8`, `listAccountsForUser`): site
`cedarlogic`, account "Levi" (`leviholliday7`), account type **Free**, site
plan `nf_team_dev`, `credit_features: false` — a **legacy Free plan**, not one
of the credit-based plans Netlify has sold since September 2025. Its included
monthly amounts, from the account's capabilities: **125,000 function
invocations**, 360,000 function-seconds (100 GB-hours), **100 GB bandwidth**,
1,000,000 edge function invocations, 300 build minutes, 100 form
submissions; `accumulate_overages: false` and
`block_builds_when_usage_exceeded: true` — past a limit the site "will be
suspended for the remaining days in the calendar month" (Netlify's Free plan
announcement), with notices at 50/75/90/100 %. Functions (Netlify's
configuration docs, read the same day): 60 s per synchronous call, 30 s for a
scheduled one, 15 min for a background one, 6 MB buffered request or
response, 1,024 MB memory. Blobs: included, with no per-operation price
published for this plan, so a Blobs read or write costs the invocation and
runtime of the function that does it, nothing more; site-wide stores are
kept in `us-east-2` (Ohio) whatever the functions' region. Rate limiting: 2
code-based rules per site on Free, 5 on Pro, 100 on Enterprise (§3.1). If the
account is ever moved to a credit-based plan — the move is permanent —
Netlify's pricing page says today: Free 300 credits a month, Personal 1,000
for $9, Pro 3,000 for $20 a member; extra credits 500 for $5 (Personal) or
1,500 for $10 (Pro); **web requests 2 credits per 10,000 — every request,
cache hits included**; bandwidth 20 credits per GB; compute 10 credits per
GB-hour; production deploys 15 each.

**The design that keeps a lecture cheap.** Students never poll an
authenticated endpoint. They poll the pulse, which is one URL per class,
public, 40 bytes, cached at Netlify's edge for 2 s and in the durable cache
shared by every edge node, so the function behind it runs about once every
2 seconds per class however many students poll; everything a student then
fetches is a record addressed by its version in the path (immutable, cached
for a year). Polling runs only while the class page is visible: every `p`
seconds (3) while a session is on, every 60 s otherwise, and stops when the
page is hidden or after two hours without input. The teacher's device polls
answers every 3 s only while a predict question is open. The pulse carries
`p`, so the interval can be raised for the whole site from an environment
variable if a month gets tight. The pulse sets an `ETag` and clients send
`If-None-Match`; whether Netlify's edge answers such a poll with a `304`
itself or hands it to the function isn't documented and is measured before
launch — the numbers below don't depend on it (a `304` and a 40-byte `200`
are the same invocation and nearly the same bytes).

**One lecture** — 300 students, 75 minutes, a session on throughout, the
teacher pushes 30 times and asks 10 predict questions:

| What | Requests | Function invocations | Bandwidth |
|---|---|---|---|
| pulse polls, every 3 s | 450,000 | ≈ 2,250 (one per 2 s, the cache serves the rest) | ≈ 225 MB (≈ 0.5 KB each with headers, `304` or a 40-byte body) |
| live record fetches | 30 × 300 = 9,000 | 30 (cached, immutable) | 9,000 × ~10 KB ≈ 90 MB |
| predict answers | 3,000 PUTs | 3,000 | ≈ 3 MB |
| the teacher: pushes, answer polls (3 s during 10 two-minute windows), status | ≈ 500 | ≈ 500 | ≈ 5 MB |
| **per lecture** | **≈ 462,000** | **≈ 5,800** | **≈ 320 MB** |
| **per month, 3 lectures a week (13)** | ≈ 6,000,000 | **≈ 75,000** | **≈ 4.2 GB** |

Outside lectures, the same class adds about 3,000–6,000 invocations a month
(joins, status fetches when an assignment is posted, hand-ins, the teacher's
submission views) and under 1 GB.

So on the legacy Free plan one such class uses ≈ 60 % of the month's function
invocations (predict answers are half of them: every answer is a write that
can't be cached) and 4 % of the bandwidth, beside Sync's and feedback's
use. Two classes of 300 with ten predict questions every lecture would pass
125,000 — the request breaker (§3.5) would pause classroom writes for the
rest of that day rather than let Netlify suspend the whole site. If that
happens, in order: ask fewer predict questions per lecture (five instead of
ten halves the answers); move `PUT …/answers` and the pulse to Edge Functions
(their 1,000,000 a month are a separate budget; `@netlify/blobs` works there;
the durable cache doesn't apply to edge responses, but the per-node edge
cache does, a class sits behind one or two nodes, and Netlify doesn't count
cached edge responses as invocations; an edge function gets 50 ms of CPU per
request and 40 s to start its response, enough for a Blobs read or write),
which the API shapes here allow without a client change; or Netlify Pro.

On a credit-based plan every request counts, cache hits included, so the
interval matters: 3 s is 90 credits of requests a lecture, plus ≈ 6 for
bandwidth and ≈ 5 for compute — **≈ 100 credits, about $0.67, per lecture of
300** (1,500 credits for $10), ≈ 1,300 credits a month at three lectures a
week: more than the Free (300) and Personal (1,000) allowances, well within
Pro's 3,000 ($20 a month). Each second added to the interval saves in
proportion (5 s: 54 credits; 10 s: 27).

What the design does **not** do, and why: long polling (a function may run
60 s, but each held request is an invocation per student per minute —
22,500 a lecture) and websockets (no Netlify runtime holds one). Server-sent
events from an Edge Function (40 s response-header limit, invocations per
student per 40 s) would cost 1,700 edge invocations a lecture per class and
is the upgrade path if sub-second latency is ever wanted.

**Checks before launch** (package S, on a deploy preview): `Cache-Status`
shows `"Netlify Edge"; hit` and `"Netlify Durable"; hit` on the pulse; what an
`If-None-Match` poll gets (`304` from the edge, or from the function); a
request for a record with a stray query string is served from the cache all
the same; **whether cache hits count against the rate-limit rule** (poll a
cached pulse 1,300 times in a minute from one address: a `429` from the edge
means they do, and the rule goes to 7,200, §3.1); after a purge of
`class-<classId>`, the next fetch of a cached record shows `Cache-Status`
miss (and `404` once the class is gone); the usage dashboard after a
rehearsal lecture matches the table within 2×.

### 3.11 The daily cleanup (`classroom-cleanup.mjs`)

Walks `c/` with `list({prefix:"c/", paginate:true})` from `cleanup/cursor`,
20 s at a time:

1. A class with no students and no assignments whose `activeAt` is older than
   7 days → delete its documents and blobs; no marker; `a/` stays and `j/` is
   retired as `idle`, so only its teacher can make it again, with its code.
2. `activeAt` older than 400 days → write `gone/<classId>` `{reason:"expired"}`
   and `todo/`, mark `a/` deleted, remove everything.
3. Blobs under `t/`, `i/`, `as/`, `lv/`, `s/` not named by a document and
   older than an hour → delete; `la/` sessions other than the current one
   older than a day → delete; `j/` entries no class names, older than an hour
   → retired; retired ones older than 400 days → deleted (an `idle` one whose
   class was never made again takes its `a/` with it and leaves
   `gone/<classId>` `{reason:"expired"}`); `mv/` older than an hour → delete.
4. Add the class's `bytes` to the day's total.

Then: finish every `todo/` (purging `class-<classId>` / `asg-<aid>` again
as each finishes, in case the request's purge failed); write `budget`;
delete `rate/` and `day/` keys from past windows. An expired class (2.) is
purged like a deleted one. Logs counts only.

### 3.12 Server tests (`scripts/test_classroom_server.mjs`)

On the memory stub, in the style of `test_sync_server.mjs`: every row of §3.4
at least once; create/confirm/re-create; the join flow (a wrong code 404 and
counted, a closed class, a full class, a stale code after a change, the
`409` own-id rule); hand-ins (replay, `412`, `closesAt` with the server clock,
quotas at the edge: the 300th student accepted and the 301st `507`, the 100th
assignment and the 101st, a shrinking edit in a full class accepted); the
live slot (`412` for a second device, `on: false`, answers to the current and
the previous ver, `not_live`, the answers `ETag`/`304` without blob reads);
`DELETE` student/assignment/class finishing through `todo/`; the pulse's
headers, `304`, `404` for a gone class, the immutable record fetches by path
(a query string is ignored) and `no_version`; the Blobs trap on every
document; two concurrent hand-ins to
the same assignment (both land, no lost update); the cleanup rules; the gate
and CORS headers; the per-context store names; the breaker (writes only) and
`CLOSED`; a dump of the store after create, join and Change Join Code
contains no `joinId`, no SHA-256 of a join token and no public key (the
`joinIndex`/`joinHash` HMACs of §7.1.3's server vector instead), and a
missing `CLASSROOM_JOIN_PEPPER` answers `503 busy`; `fetchKey`: a new one
after a removal and after a code change (not after closing joining), the
old path `404 wrong_fetch_key`, the purge called with the class's tag;
removal keeps hand-ins, `?submissions=delete` and the bulk remove don't;
the per-address-per-class join cap (61st join from one address into one
class `429`), the 640-byte name cap, the rename cap; one address at full
speed: `429` from its allowance before the site's breaker moves (the breaker
counts only allowed requests); `DELETE` class and assignment and the
expiry purge their tags;
then the mock server over HTTP with its `/__mock/` controls (clock, limits,
`tamper {rollback|damage|forgePulse}`, `dropNextAnswer`, `fail`).

---

## 4. The clients (web and Mac, from the same rules)

The web client implements this in JS (`classroom-core.js`), the Mac through
the shared C++ core (§6). Both are "a device that teaches classes" and "a
device that is a student in classes" at once.

### 4.1 Teacher: the class and its keys

**Create Classroom** (name typed first, ≤ 100 characters): make the teacher
key, the join code, the class key and the key pair; derive the keys; seal the
teacher record (ver 1), the join record (ver 1) and the info record (ver 1);
`PUT /classes/{classId}` (must be 201). Save `teaching[classId]` (§2.4) only
after 201 — nothing is kept for a class the website didn't take. Write the
Sync side record if Sync is on (§2.5). Then the **teacher-key sheet** (§5.1):
the key large, as a QR code of the `#t=` link, [Copy Key] [Copy Link] [Print
Recovery Sheet] [Done]. The join code is shown next, on the class page, as the
big code for the projector with its own QR code (`#j=`).

**Keys at rest**: the teacher key, `d`, the class key and the join code are in
`teaching.json` / the `teaching` store, as §2.4; the file is 0600 in a 0700
folder on the Mac (Sync's secret rules); IndexedDB on the web. "Show Teacher
Key" on the class page shows the sheet again. The key is also visible in the
teacher record on the website to anyone who has it, which is the point.
**Remove from This Device** (the class's Settings) forgets the class on this
computer only — `teaching[classId]` and its cache; nothing on the website
changes, and the class's sync side record is left alone (so the teacher's
other devices keep it) — for the projector PC or a lab computer the teacher
used once: "Remove Digital Logic 101 from this computer? The class stays on
the website and on your other devices. To open it here again you'll need
your teacher key." The teacher-key sheet says to do this on a shared
computer (§5.1).

**Another device** (I Have a Teacher Key…, a `#t=` link, a scanned recovery
sheet, or a `classroom` sync record): parse → derive → `GET /classes/{classId}`
with the teacher token → `404` "This class isn't on the website." / `410` the
sentence / `401` "This teacher key doesn't match this class." On `200`: open
the teacher record that came inside the status (`teacher.env`) with
`backupKey` → the class name, `d`, the class key, the join code. Show "Add
“Digital Logic 101” to this device?" (the name from the record) → [Add] →
save `teaching[classId]`. Nothing is written to the website. A device that
already has the class says so. A record that won't open under this key's
`backupKey` can only mean a wrong key with a colliding `classId` (1 in 2¹²⁸)
or a damaged record: "This class's record on the website couldn't be read."

**Re-creating a never-used class** (status `404 no_class` on a class this
device teaches): PUT it again from what the device holds (the record, ver 1
again); quietly.

**Is the join record ours?** Every teacher status carries `join: {ver, h}`,
and the device keeps the pair it last checked in `teaching[classId].join`.
When the status shows another pair, the device fetches the join record
(`GET /join/{joinId}`, with the code from its own teacher record — after a
code change on another device, the re-sealed teacher record names the new
code), opens it with that code's join key, and checks that it holds this
class's public key and class key. If it does, the new pair is kept quietly;
if it doesn't open or holds anything else, the class page says: "The join
record on the website isn't the one your devices wrote. Change the join
code." [Change Join Code]. That is what a dishonest server that learned the
code would have to do to hand later joiners a public key of its own (§1.3);
the check costs one field on the server and, rarely, one request here.

**Class settings** (the class page): rename (writes `info`, `join` and
`teacher` with their bases; one request each, in that order; a `412` on any
→ refetch the status, open the newer records, show "This class was changed on
another device" and the fresh values, and let the person try again — nothing
is merged); **Change Join Code** (a new code; `PUT /join` with the new join
record and the re-sealed teacher record; the old code stops working at once;
students who joined keep working — their devices pick up the class's new
`fetchKey` from their next status — while removed students and anyone who
only had the old code lose the record paths); **Close Joining** / **Open
Joining**; **Show Teacher Key**; **Remove from This Device**; **Delete
Class…** (`DELETE` with the delete token; then forget the class locally,
tombstone the sync record; students' devices learn of it at their next
request).

**Expiry warning**: from 60 days before `expiresAt` the class page and the
class list say "This class will be removed from the website on 14 Jan 2028
unless someone opens it. Opening it, or a student handing in, keeps it." Any
authenticated request resets the clock.

**Two teacher devices disagreeing** is always resolved by `412`: the device
that wrote second refetches, shows what the other wrote, and the teacher
decides (post again, or keep the other's). For the live session see §4.4.

### 4.2 Teacher: assignments

**Post an assignment** from the circuit on screen: title, instructions, due
date (local time, shown to students in theirs), "Close hand-ins after the due
date" (sets `closeAfterDue` and the server's `closesAt`), the starter circuit
(the open document's `.cdl` text, with any locked parts Group 3.5 saved in
it), and the answer key: none; **Students can check their work** (the key's
text goes into the assignment under the class key); **Only I check** (the
key is sealed to the class's public key and the assignment carries the sealed
envelope). The sheet explains the choice in one line each (§5.2). The key is
whatever the Check sheet accepts, verbatim — a formula, a truth table, a
count, a state table or a timing table (`docs/CHECK-SEQUENTIAL.md`); its kind
is told from the text, so nothing else is stored; the `names` lines go with
it when the teacher matched names by hand.

Writes: `PUT /classes/{classId}/assignments/{aid}` with a new UUID, `base: 0`,
`ver: 1` (edits: the current ver as base). `507` sentences shown; `413` →
"Too big to send (over 512 KB). A starter circuit is usually a few KB; check
for a huge RAM image." Deleting: `DELETE` after "Delete “Lab 3”? Hand-ins for
it are deleted too." Students' lists follow the server's index.

Drafts are kept locally while the sheet is open (a lost connection loses
nothing typed): "Can't reach the website. [Try Again]".

### 4.3 Teacher: submissions

The submissions view of an assignment: `GET …/submissions` (index, `304`
when unchanged; polled every 30 s while the view is open), the roster (names,
opened once per session and cached by `name.ver`), then `fetch` for every
entry whose `(ver, h)` isn't in the local cache, 50 at a time. Each record is
opened with `d`; a damaged or refused one is listed as "Couldn't be read" with
the student's name from the roster and the hand-in time, never applied; a
"newer" one as "Needs a newer CedarLogic"; one whose `proof` isn't the one
pinned for that student (§2.2) as "Couldn't be verified", never opened as a
circuit, checked or counted. A hand-in whose student has left or been removed
stays in the list under the name inside it, with "(left the class)".

**Check results** are computed on the teacher's device, never trusted from a
student: for each opened submission, run Check My Circuit (Mac:
`cl_check_key_kind`, then `cl_check_expected` / `cl_check_table` on the truth
table or `cl_check_clocked` on a document opened from the submission's `cdl`;
web: `CedarLogicCheck.check` through the WASM engine's worker;
`docs/CHECK-SEQUENTIAL.md` §11–§12) with the assignment's key (its text, or
the sealed key opened with `d`); cache the verdict and summary per `(aid, sid, h)` in `checked` (§2.4) so
reopening the view doesn't recompute 300 circuits; each check runs with the
budget of §4.9 ("Couldn't check: too big"); show them as the Check
sheet does (matches / wrong rows / couldn't check). No key → no column.

The table: student (name as plain text), handed in (local time), attempts,
check result, [Open]. **Open** shows the submission read-only in a new
document/circuit titled "Sam Lee — Lab 3" that is never autosaved into the
library (Mac: a read-only `CircuitDocument` from a temporary file; web: a
circuit with `readOnly` that Your Circuits doesn't list) with [Keep a Copy].
**Download All**: a zip `<class> – <assignment>.zip` with `<student
name>.cdl` for each (file-system-safe names: `/ \ : * ? " < > |` → `-`,
duplicates `(2)`), plus `hand-ins.csv` (name, handed in, attempts, check
result); the web zips in JS with `CompressionStream("deflate-raw")` entries,
the Mac writes a folder and zips it with `ditto -c -k` (or offers the folder).

**High-water marks**: `checked[aid/sid].h` and the index entry's `ver` are
the marks; an index that lists a lower `ver` than seen, or the same ver with
another `h`, is ignored for that student with one quiet log line (a stale
cache or a server that went back). Nothing a student handed in is ever
deleted by the teacher's client.

### 4.4 Teacher: the live view

**Go Live** (from the class page, with a circuit open): a new `session` UUID,
`PUT /live` with `base` = the current live ver, `on: true`, `predict: false`
and a `live` record holding the open document's `.cdl` (step 1). The class
page's live bar (§5.3) shows "Live · 212 students · step 1". **Push** sends
the circuit as it is now (step + 1): switches, register contents and clock
state as the document holds them, so students see what the projector shows.
A pushed circuit with manual clocks (the shared Step Clock decision) stays
where the teacher stepped it; students may press K locally.

**Predict**: type the question, pick the lights (the names Group 3's Predict
mode lists); push with `predict: {prompt, lights}` and `reveal: false`
(students' lights hide); the live bar shows "23 of 212 answered · LED: 1 (18)
0 (5)" from the answers endpoint polled every 3 s (`304` most of the time;
an answer whose proof isn't the student's pinned one isn't counted);
counts, not names — the answers carry `studentId`, and the UI shows only
totals. **Reveal**: push the circuit (usually after stepping the clock) with
`reveal: true` and the same `predict`; the bar adds "18 right, 5 wrong" by
comparing each answer's guesses with the pushed circuit's lights as this
device simulates them. A new push without `predict` clears the question.
**End Live**: `PUT /live` with `on: false` and an `ended` record; the bar
goes away; students' screens say "The live view ended."

**A second teacher device**: its `PUT /live` gets `412` → "You're live from
Levi’s MacBook Air. Take over here? The other device stops pushing." [Take
Over] → the same write with `current.ver` as base. (The first device sees
its next push refused the same way and offers the same.) The first device's
own pushes never conflict with anything else: students only read.

### 4.5 Student: joining and assignments

**Join a Class** (the title menu on the web, File › Classroom › Join a Class…
on the Mac, or a `#j=` link / QR code): the code field (the checksum checks
as you type, the §1.2 sentences) → [Continue] → "Checking the code…" (the
stretching of §1.3: a moment, once) → `GET /join/{joinId}` → open the join
record → "Join **Digital Logic 101**? Your name, as your teacher should see
it: [____]" (closed: "Joining “Digital Logic 101” is closed. Ask your teacher
to open it.") → [Join] → a new `studentId`, a token, a proof (§1.4), the
name (with the proof) sealed to the class's public key from the join record →
`POST /classes/{classId}/students` → save `memberships[classId]` with the
`fetchKey` from the answer. Then the class page. A device already in the
class says so and opens it. The name can be changed later on the class page
(`PUT …/students/{sid}` with the next `ver`).

**The class page** lists assignments newest first: title, due date ("Due
Fri 10 Oct, 11:59 PM"; past: "Was due …"; closed: "Hand-ins closed"), and
the hand-in state ("Not handed in" / "Handed in 4 Oct 10:31" / "Changed since
you handed in"). It is kept from the status (`GET /classes/{classId}` with
`If-None-Match`, fetched when the pulse's `seq` moves) and the cached
records (`/api/live/v1/{classId}/{fetchKey}/assignment/{aid}/{ver}`), opened
with the class key; a `404 wrong_fetch_key` means the class's `fetchKey`
changed: read the status, take the new one, try once more. The page works
offline from the cache.

**Open** makes the student's own copy: a new circuit in Your Circuits (web)
or the library (Mac) named after the assignment, with `from`/`assignment.json`
pointing at `{classId, assignmentId}`; opening again opens that copy. The
instructions show in a panel beside the canvas (plain text; links aren't
clickable). Locked parts behave as Group 3.5 says.

**Check My Circuit** (only with a readable key): the Check sheet with the
key filled in; results stay on the device. With a sealed key the button says
"Your teacher checks this one."

**Hand In**: the toolbar button (and the class page's row) seals
`{name, cdl, handedInAt, client, attempt, proof}` to the class's public key and
`PUT`s it with `base` = the ver of the last hand-in this device knows
(`memberships[classId].seen.submissions[aid]`), `ver` + 1. `201`/`200` →
"Handed in." and the row updates; `409 assignment_closed` → the sentence with
the due date; `412` → take `current.ver` (handed in from another device) and
send again, once; `413` → the sentence; offline → the hand-in is kept in
`pending` and sent at the next chance (§4.8), the row says "Will hand in when
you're online." **Hand In Again** is the same button once something was
handed in; it is offered quietly when the circuit's `cdlHash` differs from the
last hand-in's, and says "Nothing changed since you handed in" otherwise
(still allowed).

### 4.6 Student: the live view

While the pulse says `live > seen.live`, the client fetches that version
(`/api/live/v1/{classId}/{fetchKey}/live/{ver}`), opens it and, if the live view is open or
the student accepts the banner "Your teacher is live. [Join Live View]",
loads the circuit into the **live view**: a read-only-by-default circuit slot
"Live: Digital Logic 101" outside Your Circuits (Mac: a window with the live
bar; web: the simulator with a live bar). Switches, Step Clock (K) and the
predict mode work locally; a new push replaces the circuit (local tinkering
is the teacher's circuit, not the student's: it isn't kept, and the bar says
"Updated · step 4" when it happens); [Keep a Copy] saves the current one to
Your Circuits. `predict` present and `reveal` false → the named lights hide
(Group 3's Predict mode), the prompt shows, the student guesses, [Send My
Guess] → `PUT …/answers/{sid}` with `{session, ver, lights, at, proof}` sealed
(`409 not_live` → dropped quietly: the teacher moved on). `reveal` true →
lights show, the student's own guesses are scored locally ("2 of 2 right").
`ended` → "The live view ended." and the fast polling stops. A `live` record
that won't open is reported once ("Couldn't read what the teacher sent.
Trying again.") and the view keeps the last good one.

### 4.7 Student: another device, leaving, being removed

**Move to Another Device** (the class page): a move code (new 16 bytes), the
membership sealed under `moveKey` → `PUT /move/{moveId}` → the code shown
large with a QR code of the `#m=` link and "On your other device: Join a
Class › I Have a Move Code, or scan this. It works for 10 minutes." On the
other device: the code field (or the link) → `GET /move/{moveId}` → open →
"Join “Digital Logic 101” as Sam Lee on this device?" → [Join] → save the
membership, `DELETE /move/{moveId}` (best effort). Both devices now hand in
as the same student (the same token); the second doesn't need the join code.
A move code is a secret for ten minutes: whoever gets it is that student; the
sheet says so.

**Leave Class**: "Leave “Digital Logic 101”? Your teacher won't see you in the
class any more. Work you handed in stays with your teacher until the class is
deleted. Your circuits stay on this device." → `DELETE …/students/{sid}` with
the student token → forget the membership; the circuits made from assignments
stay in Your Circuits (their `from` link is cleared).

**Remove from This Device** (the class page, for a shared or borrowed
computer): forgets the membership here (token, proof, class key, cache) and
calls nothing on the website: "Remove Digital Logic 101 from this computer?
You stay in the class. To use it here again, make a move code on another
device that's in the class, or join again (your teacher will then see you
twice)." A Chromebook in guest or ephemeral mode forgets everything at
sign-out by itself; the join sheet and the `/classroom/` page's school notes
say so (§5.3, §8.1).

**Removed by the teacher**, the class deleted or expired: the next request
answers `403 not_a_member` / `410` → forget the membership, keep the circuits,
one notice: "You were removed from “Digital Logic 101”." / "“Digital Logic
101” was deleted by the teacher." / "…was removed after 400 days without use."

### 4.8 Polling, backoff and offline

One poller per device per class (web: `navigator.locks.request`, tabs
coordinated by the `BroadcastChannel`; Mac: the engine thread), and nothing
polls while the page is hidden or the app is in the background.

| Trigger | Student | Teacher |
|---|---|---|
| The class page or live view opened; visibility → visible; the `online` event; app activated | pulse at once | status at once (`If-None-Match`) |
| While a live session is on (pulse `live` ≠ 0) and the live view or class page is visible | pulse every `p` s (3), fetch new versions | answers every 3 s while a predict question is open |
| Otherwise, while the class page is visible | pulse every 60 s, rising to 120 s after 10 minutes without change | status every 30 s while the submissions view is open, else 60 s |
| Two hours without input | stop until the next input | stop |
| `pending` hand-ins | sent at the next pulse or `online`, oldest first | — |

Failures back off as Sync §4.3 (30 s, 1 min, 2 min, 5 min, 10 min, then every
15 min, ±20 %); `429`/`503` wait at least `Retry-After`; a success resets.
The pulse never backs off past the idle interval while a session is on: a
lecture mustn't go quiet because the website hiccupped once.

Offline: the student's class page, assignments and the last live circuit open
from the cache; Hand In queues (one pending hand-in per assignment, the newest
replacing the older); the teacher's posting, pushing and deleting need the
connection and say "Can't reach the website. [Try Again]" (the sheet keeps
what was typed); the submissions view shows what it has with "Last updated
10:31".

### 4.9 Never going back, and records that can't be read

- Every client keeps **high-water marks**: `seq`, `live` and each
  assignment's `ver` (student); the live ver, each assignment's ver and each
  submission's `(ver, h)` (teacher). A pulse, status or index that shows a
  lower value is ignored quietly — the CDN can serve a copy that is a second
  old, which is normal, not an attack; a lower value after a higher one is
  never applied either way, which is what stops a dishonest server from
  rolling a class back. A record fetched at a version the mark already passed
  isn't fetched again.
- **Damaged** (§1.5) or **invalid** (§2.2) records are never applied: an
  assignment that won't open shows in the list as "Couldn't be read (the
  teacher may need to post it again)", a live record keeps the last good one,
  a submission shows "Couldn't be read" with the hand-in time; the record is
  fetched again only when its `ver` or `h` changes. The teacher's device, on
  seeing its own assignment reported damaged by a student, re-posts it from
  the local draft cache (it keeps the last posted payload per assignment).
- **Newer** (`v` > 1, or a kind this client doesn't know): "Needs a newer
  CedarLogic"; never overwritten, never shown as content. (An answer key
  whose text the checker can't read isn't "newer": the checker's own
  "couldn't check" message is shown, §2.2.)
- The teacher's public key a student holds comes from the join record and
  never changes for the class; a status or info record can't replace it (there
  is no field for it).
- **Circuits from the other party get a budget.** A hand-in reaches the
  teacher from a student, a push or an assignment reaches students from
  whoever holds the class key; either may be hostile. Besides the 4,000,000-
  byte inflate cap (§1.5), a client counts parts before it builds anything:
  more than 20,000 gates or 50,000 wire segments in a received `cdl` →
  "Couldn't be read (too big)", never loaded. The web parses received
  circuits in the worker, never on the page's thread, and gives up after 5 s
  of parsing; the automatic check of a hand-in (§4.3) gets 5 s of simulation
  per submission ("Couldn't check: too big"). The Mac core's readers
  (`readLegacyCdl`, `readCircuitFile`) now read input from other people, so
  package E fuzzes them (libFuzzer, sanitizers on) before the Mac classroom
  ships.

### 4.10 Untrusted text and the page's policy

Names, class names, titles, instructions, prompts and light names go into the
page only with `textContent` (or as DOM attribute values), never into
`innerHTML`; instructions are shown as plain text with line breaks. The
`/classroom/` page and the simulator pages keep Sync's Content-Security-Policy
(`connect-src 'self'`, no inline scripts); `qrcodegen.js` draws the QR codes
as SVG. On the Mac, every string lands in an `NSTextField`/SwiftUI `Text`.

### 4.11 Errors as the person sees them

The §3.4 sentences, shown where the action was taken (a sheet's status line,
the class page's banner, the live bar), with the backoff sentence "Trying again
in a minute." where the client will retry. `401 wrong_token` once → one
retry after refetching nothing (it can only be stale storage) → then "This
device isn't known to the class any more…" with [Join Again] (a new join)
and [Leave]. `410`/`403 not_a_member` → the membership is forgotten and the
notice of §4.7 shown once.

---

## 5. What people see

Plain words, the apps' own styles, the same strings on every platform.
"Class" and "classroom" are the nouns; "teacher key", "join code", "move
code"; "hand in" is the verb; never "account", "login", "submit", "upload".

### 5.1 Teacher: Create Classroom and the keys

**Create Classroom** (web: the title menu › Classroom › Create Classroom…;
Mac: File › Classroom › Create Classroom…):

> **Create a classroom**
> Class name: [Digital Logic 101]
> Students join with a short code you write on the board. Their names and
> what they hand in are encrypted for you before they leave their devices;
> CedarLogic's website can't read them. There's no account: a teacher key is
> made for you now — keep it.
> [Create]  [Cancel]

**The teacher-key sheet** (after Create, and from Show Teacher Key):

> **Your teacher key for Digital Logic 101**
> `000G-40R4-0M30-E209-185G-R38E-1YZ4`  (large, monospaced, selectable)
> [QR code of the #t= link]
> This key opens the class on any of your devices: CedarLogic Online, the
> CedarLogic app, your phone. It is the only way back in. Keep a copy
> somewhere safe — print the recovery sheet or save the link. If you lose
> every device that has it and this key, nobody can read the hand-ins, not
> even CedarLogic's website.
> Keep it private: anyone with it can read everything in the class and change
> it. (Students never need it.) On a shared computer, use Remove from This
> Device in the class's settings when you're done.
> [Copy Key]  [Copy Link]  [Print Recovery Sheet]  [Done]

With Sync on, one more line: "It's synced to your other devices with your
circuits, so your sync code opens this class too." On Safari (not installed): Sync's "Safari may clear this
website's data…" line, ending "keep this key".

**The recovery sheet** (print / PDF; web `window.print()` of a print-styled
page, Mac `NSPrintOperation`): the class name, the date, the key in large
type, the QR code, and the two paragraphs above, plus: "To use it: CedarLogic
Online › Classroom › I Have a Teacher Key, or scan the code. Store this sheet
like a password."

**I Have a Teacher Key…**: a field ("Type or paste the key, or a link") with
the §1.2 sentences, [Continue] → "Checking…" → "Add **Digital Logic 101** to
this device? You'll see its assignments, hand-ins and live view here too."
[Add] [Cancel].

### 5.2 Teacher: the class page

The class list (web: Classroom in the title menu; Mac: the Classroom window):
each class with its name, "212 students", "Live" when a session is on, and
the expiry line when due.

The class page: the **join code card** —

> **Join code**  `K7QM-4XPD-2FJ3`  (very large: for the projector)   [QR code]
> Students: CedarLogic Online › Classroom › Join a Class, or scan.
> Joining is open · 212 students   [Close Joining]  [Change Code…]

(Change Code… → "Make a new join code? The old one stops working at once.
Students who already joined stay in the class; anyone you removed can't
see anything new." [Change] [Cancel].)

Then tabs or sections: **Assignments** (the list, [Post Assignment…] from the
open circuit), **Students** (names, joined, last seen; a row's ••• › Remove
from Class…, or several rows selected › Remove…, or [Remove Everyone Who
Joined After…] with a time →

> **Remove Sam Lee from the class?**
> They can't see the class any more. What they handed in stays in your
> hand-ins.
> [ ] Also delete everything they handed in
> [Remove]  [Cancel]

— "Remove 14 students…" for several), **Live** (§5.4), and **Settings**:
rename, [Show Teacher Key], [Remove from This Device…], [Delete Class…] →

> **Delete Digital Logic 101?**
> Every assignment, hand-in and student in it is deleted from the website.
> Students keep the circuits on their own devices. This can't be undone.
> [Delete]  [Cancel]

Expiry line: "This class will be removed from the website on 14 Jan 2028
unless someone opens it."

**Post Assignment** (from a circuit on screen):

> **Post an assignment to Digital Logic 101**
> Title: [Lab 3: half adder]   Due: [Fri 10 Oct 2026] [11:59 PM]   [ ] No hand-ins after the due date
> Instructions: [ multi-line ]
> Starter circuit: the circuit on screen (3 parts, 2 pages) · [ ] Lock the parts I selected (Group 3.5)
> Answer key: (•) None  ( ) Students can check their work  ( ) Only I check
>   Students can check: the key is sent to students' devices, encrypted for
>   the class, so Check My Circuit works for them — and a curious student
>   can find the answer in it. Good for practice.
>   Only I check: the key is encrypted for you alone; you see each hand-in's
>   result when you open the hand-ins.
>   [the key, as in Check My Circuit: a formula, a pasted table, a count sequence…]
> [Post]  [Cancel]

Editing an assignment shows the same sheet with [Save]; "Students see the
change the next time they look."

**Hand-ins** (an assignment's view): "18 of 212 handed in" · a table — Name ·
Handed in · Attempts · Check · [Open] — [Download All] [Refresh]. Check
column: "Matches" / "2 rows wrong" / "Couldn't check: no light named S" /
"—" (no key). A row that can't be read: "Couldn't be read" in the Check
column; one whose proof doesn't match: "Couldn't be verified" (with a short
explanation on hover: "This hand-in doesn't carry the code Sam Lee's device
puts in everything it sends. It may not be from them."). A hand-in of a
student who left: the name, then "(left the class)". An opened hand-in: a window/circuit titled "Sam Lee — Lab 3:
half adder" with a bar "Handed in 4 Oct 10:31 · attempt 2 · read-only
[Keep a Copy]".

### 5.3 Student: joining, the class page, handing in

**Join a Class** (web: the title menu › Classroom › Join a Class…; Mac: File
› Classroom › Join a Class…; a `#j=` link):

> **Join a class**
> Code from the board: [K7QM-4XPD-2FJ3]   (checks as you type)   [Scan Code] (phones)
> [Continue]
> → **Join Digital Logic 101?**
> Your name, as your teacher should see it: [Sam Lee]
> Your name and what you hand in are encrypted for your teacher; the website
> can't read them. Nothing else about you is sent. On a shared computer, use
> Remove from This Device on the class page when you're done.
> [Join]  [Cancel]
> (also) I have a move code from my other device…

**The class page**: the class name, "You're in this class as Sam Lee
[Change]", the assignments (each: title, due line, state, [Open] / [Hand In]
/ [Hand In Again] / [Check My Circuit]), the live banner when a session is
on ("Your teacher is live. [Join Live View]"), and at the end [Move to
Another Device…], [Remove from This Device…] and [Leave Class…] (§4.7's
sentences).

**Hand In** (the toolbar button in a circuit that came from an assignment,
and the class page):

> **Hand in “Lab 3: half adder”?**
> Your circuit (5 parts, 1 page) goes to your teacher, encrypted for them.
> You can hand in again until the due date (Fri 10 Oct, 11:59 PM).
> [Hand In]  [Cancel]

Afterwards: "Handed in 4 Oct 10:31." in the class page and a quiet note in
the window. Offline: "Will hand in when you're online."

**Move to Another Device**:

> **Move to another device**
> `M2GT-58X4-MPKA-FA59-NANT-SBDE-NX83`   [QR code]
> On your other device: Classroom › Join a Class › I have a move code — or scan
> this. It works for 10 minutes. Anyone who gets this code can hand in as you,
> so don't share it.
> [Copy Code]  [Done]

### 5.4 The live view

**Teacher** (the class page's Live tab, and a bar in the circuit window):

> ● Live · Digital Logic 101 · 212 students · step 4
> [Push This Circuit]  [Ask a Prediction…]  [Reveal]  [End Live]
> Prediction: "What will Q be after the next clock?" · 23 answered · Q: 1 (18) 0 (5)
> after Reveal: … · 18 right, 5 wrong

Go Live from the class page: "Go live with the circuit on screen? Every
student who opens the class sees it; push again whenever you change it."
[Go Live]. Ask a Prediction…: the prompt and the lights (checkboxes over the
page's lights' names).

**Student** (the live view):

> ● Live · Digital Logic 101 · step 4   [Keep a Copy]
> Your teacher asks: What will Q be after the next clock?  — click each hidden light to guess, then [Send My Guess]
> after Reveal: 2 of 2 right  /  Q: you guessed 1, it's 0
> ended: The live view ended. [Keep a Copy] [Close]

### 5.5 Links to the apps and the `/classroom/` page

`cedarlogic://classroom#t=…`, `#j=…`, `#m=…` arrive as `cedarlogic://sync`
does (Mac `ShareLink.swift`, host `classroom`) and open the matching sheet
with the code filled in — never creating, joining or importing by
themselves. `https://cedarlogic.netlify.app/classroom/` (built by
`scripts/build_pages.py`, `noindex`, Sync's CSP) reads the fragment, removes
it at once, and offers: `#j=` → [Join in This Browser] (CedarLogic Online
with `#j=` in its fragment) / [Open in the CedarLogic App]; phones and
tablets go to `/app/#j=…` as the sync page does; `#t=` → the same with "Add
this class on this device"; `#m=` → "Move your class to this device". With
no fragment: a short page on what Classroom is, with the privacy text of
§8.1 ("Privacy for schools") — Group 6 builds it.

---

## 6. The shared C++ core and the web core

### 6.1 Decision

**One core for the apps** in `mac/CedarCore/` (compiled into all three apps
like `Sync*.cpp`), holding everything that must behave identically: the
codes, the key derivations, both envelopes, the payload reader and writers,
the HTTP protocol, the polling and backoff, the state files, the high-water
marks, Check My Circuit on hand-ins (through CedarCore's own document and
check API, in process), and a self-test. It reuses the sync engine's
building blocks directly (`SyncInternal.h`: `hkdfSalted`, `encodeCode`'s
loop, `b64u`, `hex`, the JSON reader/writer, `fileText`, the HTTP date
parser) and its `Crypto` and HTTP hooks; it adds one hook interface for
P-256. Plain C++17, no platform headers. The web core is separate JS
(`classroom-core.js`) checked against the same vectors and the same server.

Files (all new):

| File | What |
|---|---|
| `mac/CedarCore/Classroom.h` | the C++ interface below |
| `mac/CedarCore/ClassroomProtocol.cpp` | codes (three kinds), keys, both envelopes (`sealForTest` takes a nonce and an ephemeral scalar; the real seals don't), payload reader and writers, AAD |
| `mac/CedarCore/ClassroomEngine.cpp` | the engine: thread, triggers, polling, teacher and student flows of §4, state files, checks, high-water marks |
| `mac/CedarCore/ClassroomTest.cpp` | `clclass::selfTest`: the vectors (`ClassroomVectors.h`, generated from `tests/classroom/vectors.json`) and the §7.2 scenarios on a FakeServer (a port of the server's rules) |
| `mac/CedarCore/ClassroomCApi.cpp`, `include/CedarClassroom.h` | the C API for Swift |
| `mac/CedarCore/Sync*.cpp` (changed) | side records (§2.5): `sideKinds`, `sideRecords`, `putSideRecord`, `deleteSideRecord`, `Host::sideRecordsChanged` |

### 6.2 `Classroom.h`

```cpp
// The CedarLogic classroom core (CLASSROOM.md). Plain C++17; crypto, P-256,
// HTTP, files and the UI thread come in through the hooks.
#pragma once
#include "Sync.h"          // clsync::Crypto, HttpRequest, HttpResponse, Bytes
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace clclass {

using clsync::Bytes;
using Done = std::function<void(bool ok, const std::string& message)>;   // on the UI thread

// ---- What the platform provides ----------------------------------------------------

// P-256 (thread-safe), beside clsync::Crypto's random / sha256 / hmac / AES-GCM / deflate.
struct Curve {
	virtual ~Curve() = default;
	virtual bool p256Generate(uint8_t d[32], uint8_t pub[65]) = 0;                       // a fresh key pair; false = stop
	virtual bool p256Public(const uint8_t d[32], uint8_t pub[65]) = 0;                    // false if d is 0 or ≥ n
	virtual bool p256Ecdh(const uint8_t d[32], const uint8_t peer[65], uint8_t x[32]) = 0;   // false: peer isn't 04‖X‖Y on the curve
	// PBKDF2-HMAC-SHA256 for the join code (1.3). The default runs the rounds on clsync::Crypto's hmacSha256
	// hook (about a second); a platform with a native PBKDF2 (CommonCrypto, OpenSSL, CNG) overrides it.
	virtual bool pbkdf2Sha256(clsync::Crypto&, const uint8_t* pw, size_t pwLen, const uint8_t* salt, size_t saltLen,
	                          uint32_t rounds, uint8_t out[32]);
};

struct Assignment {
	std::string id, title, instructions, cdl;
	int64_t dueAt = -1;                 // -1: none
	bool closeAfterDue = false;
	int64_t ver = 0;
	std::string keyText, keyNames;      // the key as typed (students can check), or the teacher's own copy
	bool keySealed = false;             // the key is sealed to the teacher (students see only that there is one)
	// a student's view:
	int64_t handedInAt = 0; int attempts = 0; bool changedSince = false; bool pending = false; bool closed = false;
	bool unreadable = false;            // damaged, or needs a newer CedarLogic (message says which)
	std::string problem;
};
struct Submission {
	std::string studentId, name, cdl;
	int64_t handedInAt = 0; int attempts = 0;
	int checkVerdict = -1;              // -1 not checked, 0 matches, 1 wrong rows, 2 couldn't check
	std::string checkSummary;
	bool unreadable = false;
};
struct Student { std::string studentId, name; int64_t joinedAt = 0, seenAt = 0; bool unreadable = false; };
struct Live {
	bool on = false, ended = false, reveal = false, hasPredict = false;
	std::string session, cdl, prompt;
	std::vector<std::string> lights;
	int64_t ver = 0; int step = 0;
};
struct AnswerCounts { int answered = 0, students = 0, right = 0, wrong = 0; std::map<std::string, std::pair<int, int>> perLight; };   // light -> (ones, zeros)
struct ClassInfo {
	std::string classId, name;
	bool teaching = false;              // else a membership
	std::string teacherKey, joinCode;   // teaching
	bool joinOpen = true;
	std::string studentName;            // membership
	int64_t expiresAt = 0;
	bool live = false;
};
struct Status { enum Kind { Idle, Working, Offline, Error, Gone } kind = Idle; std::string text; };

struct Host {
	virtual ~Host() = default;
	// Engine thread. Blocking, as Sync's; HTTPS only (http only for localhost overrides).
	virtual clsync::HttpResponse http(const clsync::HttpRequest&) = 0;
	virtual void onMain(const std::function<void()>&) = 0;
	// The Classroom folder (2.4): "teaching.json", "memberships.json", "cache/<classId>/<name>". "" = none. Atomic, 0600.
	virtual std::string loadFile(const std::string& name) = 0;
	virtual bool saveFile(const std::string& name, const std::string& text) = 0;
	virtual void removeTree(const std::string& name) = 0;
	virtual bool tryLock(const std::string& lockPath) = 0;
	virtual void unlock() = 0;
	// UI thread:
	virtual void classesChanged() = 0;                                          // lists, assignments, roster, hand-ins
	virtual void liveChanged(const std::string& classId, const Live&) = 0;      // a new version for a student following
	virtual void answersChanged(const std::string& classId, const AnswerCounts&) = 0;
	virtual void statusChanged(const std::string& classId, const Status&) = 0;
	virtual void notice(const std::string& text) = 0;
	// Sync's side records (2.5); a host without Sync returns nothing and ignores writes.
	virtual std::vector<std::pair<std::string, std::string>> syncSideRecords() = 0;   // (rid, payload JSON) of kind "classroom"
	virtual void syncPutSide(const std::string& ridOrEmpty, const std::string& payloadJson) = 0;
	virtual void syncDeleteSide(const std::string& rid) = 0;
};

struct Config {
	std::string dir;                    // the Classroom folder
	std::string serverBase = "https://cedarlogic.netlify.app/api/classroom/v1";
	std::string liveBase = "https://cedarlogic.netlify.app/api/live/v1";
	std::string appKey, client;         // x-cedarlogic-key, x-cedarlogic-client
};

// ---- Codes and text (any thread) --------------------------------------------------------

enum class CodeKind { Teacher, Join, Move };
std::string newCode(clsync::Crypto&, CodeKind);                              // "" if the RNG failed
bool parseCode(clsync::Crypto&, CodeKind, const std::string& text, std::string& code, std::string& why);   // why: length | symbol | checksum | kind
std::string whyText(CodeKind, const std::string& why, const std::string& text);
std::string groupCode(const std::string& code);
std::string webLink(CodeKind, const std::string& code);
std::string appLink(CodeKind, const std::string& code);
// (QR modules: clsync::qr of the web link)

// ---- The engine (create, call and destroy on the UI thread) -------------------------

class Engine {
public:
	Engine(Config, clsync::Crypto&, Curve&, Host&);
	~Engine();
	void start();                                        // loads the files, starts polling for open pages
	void stop();

	std::vector<ClassInfo> classes() const;
	Status status(const std::string& classId) const;

	// Teacher (4.1-4.4). `done` on the UI thread.
	void createClass(const std::string& name, std::function<void(bool, std::string message, std::string classId)> done);
	void previewTeacherKey(const std::string& text, std::function<void(bool, std::string message, std::string className)> done);
	void addTeacherKey(const std::string& text, Done done);
	void renameClass(const std::string& classId, const std::string& name, Done);
	void setJoinOpen(const std::string& classId, bool open, Done);
	void newJoinCode(const std::string& classId, Done);
	void forgetClass(const std::string& classId);                                   // Remove from This Device: this device only
	void deleteClass(const std::string& classId, Done);                             // the website too
	std::vector<Assignment> assignments(const std::string& classId) const;
	void postAssignment(const std::string& classId, const Assignment& draft, bool studentsCanCheck, Done);   // draft.id "" = new
	void deleteAssignment(const std::string& classId, const std::string& aid, Done);
	std::vector<Student> students(const std::string& classId) const;
	void refreshStudents(const std::string& classId, Done);
	void removeStudents(const std::string& classId, const std::vector<std::string>& sids, bool deleteHandIns, Done);   // one or many
	std::vector<Submission> submissions(const std::string& classId, const std::string& aid) const;
	void refreshSubmissions(const std::string& classId, const std::string& aid, Done);  // index, fetch, open, check
	void goLive(const std::string& classId, const std::string& cdl, Done);
	void push(const std::string& classId, const std::string& cdl, const std::string* prompt,
	          const std::vector<std::string>* lights, bool reveal, Done);
	void endLive(const std::string& classId, Done);
	void takeOverLive(const std::string& classId, Done);                             // after a 412
	Live live(const std::string& classId) const;
	AnswerCounts answers(const std::string& classId) const;

	// Student (4.5-4.7).
	void previewJoinCode(const std::string& text, std::function<void(bool, std::string message, std::string className, bool open)> done);
	void join(const std::string& text, const std::string& name, std::function<void(bool, std::string message, std::string classId)> done);
	void rename(const std::string& classId, const std::string& name, Done);
	void handIn(const std::string& classId, const std::string& aid, const std::string& cdl, Done);   // queues when offline
	void follow(const std::string& classId, bool following);                        // the live view is open
	void sendAnswer(const std::string& classId, const std::map<std::string, int>& lights, Done);
	void makeMoveCode(const std::string& classId, std::function<void(bool, std::string message, std::string code)> done);
	void previewMoveCode(const std::string& text, std::function<void(bool, std::string message, std::string className, std::string studentName)> done);
	void importMoveCode(const std::string& text, Done);
	void leaveClass(const std::string& classId, Done);
	void forgetMembership(const std::string& classId);                              // Remove from This Device: nothing on the website

	// Triggers (4.8).
	void pageOpen(const std::string& classId, bool open);                            // the class page is showing
	void appActivated();
	void appDeactivated();
	void userActive();
	void syncSideChanged();                                                          // Sync applied a classroom record

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};

// The vectors of 7.1 and the scenarios of 7.2 on an in-process FakeServer (and, with serverBase, the mock server).
bool selfTest(clsync::Crypto&, Curve&, const std::string& tempDir, std::string& report, Host* httpOnly = nullptr,
              const std::string& serverBase = "", const std::string& liveBase = "");

}  // namespace clclass
```

Threading as Sync §6.2: one engine thread, state behind a mutex, `onMain`
the only way to the UI, the UI never waits for the engine. Hand-in and
answer seals run on the engine thread (an ephemeral key pair per seal from
`Curve`); Check My Circuit runs on the engine thread too (CedarCore's
document API is thread-safe for a document nobody else holds).

### 6.3 The C API for Swift (`include/CedarClassroom.h`)

The hooks struct carries Sync's crypto/http pointers (a `CLSyncHooks*` to
reuse `SyncHooks.swift` as it is) plus the three P-256 functions, the file
hooks and the UI callbacks; every call that finishes later takes a `done`
with `ctx`, called on the main thread; every list is read through
`count`/`item` getters valid until the next `classes_changed`:

```c
typedef struct CLClassroom CLClassroom;
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
} CLClassroomHooks;

CLClassroom *cl_classroom_create(const CLClassroomHooks *, const char *dir, const char *appKey, const char *client);
void cl_classroom_destroy(CLClassroom *);
void cl_classroom_start(CLClassroom *);

typedef void (*CLClassroomDone)(void *ctx, bool ok, const char *message, const char *result);   // result: a classId, a code, a name… per call
int cl_classroom_class_count(CLClassroom *);
// classes: id, name, teaching, teacherKey, joinCode, joinOpen, studentName, expiresAt, live
const char *cl_classroom_class_id(CLClassroom *, int i); /* … one getter per ClassInfo field … */
void cl_classroom_create_class(CLClassroom *, const char *name, CLClassroomDone, void *ctx);
void cl_classroom_preview_teacher_key(CLClassroom *, const char *text, CLClassroomDone, void *ctx);   // result = the class name
void cl_classroom_add_teacher_key(CLClassroom *, const char *text, CLClassroomDone, void *ctx);
void cl_classroom_rename_class(…); void cl_classroom_set_join_open(…); void cl_classroom_new_join_code(…);
void cl_classroom_forget_class(…); void cl_classroom_delete_class(…);
int cl_classroom_assignment_count(CLClassroom *, const char *classId); /* … getters per Assignment field … */
void cl_classroom_post_assignment(CLClassroom *, const char *classId, const char *aidOrNull, const char *title, const char *instructions,
                                  int64_t dueAt, bool closeAfterDue, const char *cdl, const char *keyText, const char *keyNames,
                                  bool studentsCanCheck, CLClassroomDone, void *ctx);
void cl_classroom_delete_assignment(…);
int cl_classroom_student_count(…); /* getters */ void cl_classroom_refresh_students(…); void cl_classroom_remove_student(…);
int cl_classroom_submission_count(CLClassroom *, const char *classId, const char *aid); /* getters incl. cdl, check verdict and summary */
void cl_classroom_refresh_submissions(…);
void cl_classroom_go_live(CLClassroom *, const char *classId, const char *cdl, CLClassroomDone, void *ctx);
void cl_classroom_push(CLClassroom *, const char *classId, const char *cdl, const char *promptOrNull, const char *const *lights, int lightCount,
                       bool reveal, CLClassroomDone, void *ctx);
void cl_classroom_end_live(…); void cl_classroom_take_over_live(…);
bool cl_classroom_live_on(…); const char *cl_classroom_live_cdl(…); /* session, ver, step, prompt, lights, reveal, ended */
int cl_classroom_answers_answered(…); /* students, right, wrong, per-light counts */
void cl_classroom_preview_join_code(CLClassroom *, const char *text, CLClassroomDone, void *ctx);   // result = "<name>\n<open>"
void cl_classroom_join(CLClassroom *, const char *text, const char *name, CLClassroomDone, void *ctx);   // result = classId
void cl_classroom_rename(…); void cl_classroom_hand_in(CLClassroom *, const char *classId, const char *aid, const char *cdl, CLClassroomDone, void *ctx);
void cl_classroom_follow(CLClassroom *, const char *classId, bool following);
void cl_classroom_send_answer(CLClassroom *, const char *classId, const char *const *lights, const int *values, int n, CLClassroomDone, void *ctx);
void cl_classroom_make_move_code(…);   // result = the code
void cl_classroom_preview_move_code(…); void cl_classroom_import_move_code(…); void cl_classroom_leave_class(…);
void cl_classroom_page_open(CLClassroom *, const char *classId, bool open);
void cl_classroom_app_activated(…); void cl_classroom_app_deactivated(…); void cl_classroom_user_active(…); void cl_classroom_sync_side_changed(…);
// Codes (no engine; the sync hooks for SHA-256): kind 0 teacher, 1 join, 2 move.
bool cl_classroom_parse_code(const CLSyncHooks *, int kind, const char *text, char code[29], char why[16]);
const char *cl_classroom_why_text(int kind, const char *why, const char *text);
void cl_classroom_group_code(const char *code, char out[35]);
const char *cl_classroom_web_link(int kind, const char *code);
const char *cl_classroom_app_link(int kind, const char *code);
// Tests
bool cl_classroom_self_test(const CLClassroomHooks *, const char *tempDir, const char *serverBase, const char *liveBase, char **report);
```

### 6.4 The hooks on each platform

**Mac** (`mac/App/ClassroomHooks.swift`, on top of `SyncHooks.swift`):
`p256_generate` = `P256.KeyAgreement.PrivateKey()` → `rawRepresentation`,
`publicKey.x963Representation`; `p256_public` = `PrivateKey(rawRepresentation:)`
(throws for an invalid scalar) → `x963Representation`; `p256_ecdh` =
`PublicKey(x963Representation:)` (throws off the curve; the hook also refuses
anything not 65 bytes starting `0x04`) + `sharedSecretFromKeyAgreement` →
the 32 raw bytes; `pbkdf2_sha256` = CommonCrypto `CCKeyDerivationPBKDF`
with `kCCPRFHmacAlgSHA256` (about 70 ms for the 600,000 rounds). Files: 0600
in `~/Library/Application Support/CedarLogic/Classroom/`
(0700), atomic as `saveSecret`; `flock`. The UI (`mac/App/Classroom.swift`,
`ClassroomWindow.swift`, the Hand In toolbar item, the live bar in
`CircuitWindow`, `ShareLink.swift`'s `classroom` host, `Templates`-style
sheets) is Group 7's; `mac/Tools/classroom-check.sh` builds a tool with
these hooks and runs `cl_classroom_self_test` (vectors, scenarios, the mock
server when `CL_CLASSROOM_URL` is set), as `sync-check.sh` does;
`mac/Tools/classroom-vectors-check.sh` builds and runs the CryptoKit vector
checker alone (no core needed).

**Linux / Windows (later)**: OpenSSL `EVP_PKEY` P-256 (keygen,
`EC_POINT_point2oct` uncompressed, `EVP_PKEY_derive`); CNG
`BCRYPT_ECDH_P256_ALGORITHM` (`BCryptExportKey` gives X‖Y after a 8-byte
header — prepend `0x04`; `BCryptSecretAgreement` + `BCryptDeriveKey`
`BCRYPT_KDF_RAW_SECRET` returns X little-endian — reverse it). Files in the
per-machine folders of §2.4.

### 6.5 The web core (`public/assets/js/classroom-core.js`)

No DOM, no IndexedDB (as `sync-core.js`): the store, `fetch`, the clock and
the host come in:

```
CedarClassroom.parseCode(text, kind)          -> { ok, code } | { ok:false, why, message }          (1.2)
CedarClassroom.groupCode / webLink(kind, code) / appLink(kind, code)
CedarClassroom.teacherKeys(secret) / joinKeys(secret) / moveKeys(secret)    -> the 1.3 outputs (all async; joinKeys is the slow one: the 600,000 PBKDF2 rounds)
CedarClassroom.generateKeyPair()               -> { d (Uint8Array 32), pub (Uint8Array 65) }         (1.4)
CedarClassroom.seal(key, kind, classId, id, ver, payloadBytes, { compress })      -> Uint8Array    (1.5, 0x01)
CedarClassroom.sealTo(pub, kind, classId, id, ver, payloadBytes, { compress })    -> Uint8Array    (1.5, 0x02)
CedarClassroom.open(env, kind, classId, id, ver, key | { d, pub })           -> payload bytes, or throws Damaged
CedarClassroom.readPayload(bytes, expectedKind) -> object, or throws Unreadable("newer" | "invalid")   (2.2)
new CedarClassroom.Engine({ store, fetch, now, host, base, liveBase, client, check, sync })        (4)
   the methods of 6.2's Engine with the same names, returning promises; `host` has classesChanged / liveChanged /
   answersChanged / statusChanged / notice; `check(cdl, text, names)` -> { verdict, summary } runs Check My Circuit
   through the WASM engine's worker (`CedarLogicCheck`, docs/CHECK-SEQUENTIAL.md 12); `sync` is the CedarSync engine,
   for side records (2.5), or null.
new CedarClassroom.Poller(engine, { lock, visible, timers })                                        (4.8)
```

`scripts/test_classroom_client.mjs` runs it in node on Maps against
`netlify/lib/classroom.mjs`'s `handle()`/`pulse()` in-process and against
the mock server; `sim-classroom.js` (Group 6) is the UI on top, plus the
`/classroom/` page's script.

---

## 7. Tests

### 7.1 Test vectors

Made by `scripts/classroom-vectors.mjs` (WebCrypto, CompressionStream) and
checked by `scripts/classroom-vectors-check.mjs` (node:crypto, zlib: codes
packed byte-wise, `pbkdf2Sync`, `hkdfSync`, `createECDH`, `aes-256-gcm`) and
`mac/Tools/classroom-vectors-check.swift` (CryptoKit, CommonCrypto's PBKDF2,
Compression; built by `mac/Tools/classroom-vectors-check.sh` with the
Command Line Tools alone). The values are in `tests/classroom/vectors.json`
(the site's copy is the original; the app repo carries a copy at the same
path): **tests load the file, never this page**; this page is for reading,
and `classroom-vectors-check.mjs --doc docs/CLASSROOM.md` proves every value
it quotes is in the file. The fixed P-256 keys came out of one `generateKey`
run and are pasted into the generator, so the file never changes.

What each client must show:

- every code in 7.1.1 from its secret, and back;
- every parse case in 7.1.2 (the canonical code, or the error kind);
- every key in 7.1.3, the join code's stretched secret first, and the
  server's join index and join hash under the test pepper (package S);
- the public keys from their scalars, the shared secret both ways, the seal
  key, and every bad point refused (7.1.4);
- every record in 7.1.5 opened to its payload bytes and read as its kind;
  the byte-exact ones sealed again to the same bytes (through the test-only
  seal that takes a nonce and an ephemeral scalar); record 3's inner key
  opened; the deflated bytes given inflating to the payload; record 12 under
  the sync rules (a sync engine's own `open`, SYNC.md §1.4);
- every payload in 7.1.6 opening (in its kind's own envelope) but refused
  with the given `why`, read with its kind expected and the checks in §2.2's
  order;
- every case in 7.1.7 failing to open — the envelope byte checked against the
  kind first, and the deflate bomb stopped by a streamed inflate at
  `maxPlaintext` (the file also carries `envelopeOf` and `maxPlaintext`);
- and that two seals of one payload differ (a property, not a vector).

#### 7.1.1 Codes

Teacher keys (identical to the sync codes of the same secrets):

| label | secret (hex) | checksum12 | code | grouped |
|---|---|---|---|---|
| counting | `000102030405060708090a0b0c0d0e0f` | `be4` | `000G40R40M30E209185GR38E1YZ4` | `000G-40R4-0M30-E209-185G-R38E-1YZ4` |
| zeros | `00000000000000000000000000000000` | `374` | `00000000000000000000000000VM` | `0000-0000-0000-0000-0000-0000-00VM` |
| ones | `ffffffffffffffffffffffffffffffff` | `5ac` | `ZZZZZZZZZZZZZZZZZZZZZZZZZXDC` | `ZZZZ-ZZZZ-ZZZZ-ZZZZ-ZZZZ-ZZZZ-ZXDC` |
| random | `8e1f5a0c3b9d47e2a6c1f0d9b3e85274` | `1a3` | `HRFNM31VKN3Y59P1Y3CV7T2JEGD3` | `HRFN-M31V-KN3Y-59P1-Y3CV-7T2J-EGD3` |

Join codes:

| label | secret (hex) | checksum12 | code | grouped |
|---|---|---|---|---|
| counting | `000102030405` | `17e` | `000G40R40MBY` | `000G-40R4-0MBY` |
| zeros | `000000000000` | `b0f` | `0000000002RF` | `0000-0000-02RF` |
| ones | `ffffffffffff` | `ce8` | `ZZZZZZZZZZ78` | `ZZZZ-ZZZZ-ZZ78` |
| random | `8e1f5a0c3b9d` | `853` | `HRFNM31VKP2K` | `HRFN-M31V-KP2K` |

Move code: secret `a0a1a2a3a4a5a6a7a8a9aaabacadaeaf` → checksum `503` →
`M2GT58X4MPKAFA59NANTSBDENX83` (`M2GT-58X4-MPKA-FA59-NANT-SBDE-NX83`; the
same bytes as Sync's pairing vector, so it is the same symbols).

Links for `counting`:

```
https://cedarlogic.netlify.app/classroom/#t=000G40R40M30E209185GR38E1YZ4     cedarlogic://classroom#t=000G40R40M30E209185GR38E1YZ4
https://cedarlogic.netlify.app/classroom/#j=000G40R40MBY                     cedarlogic://classroom#j=000G40R40MBY
https://cedarlogic.netlify.app/classroom/#m=M2GT58X4MPKAFA59NANTSBDENX83     cedarlogic://classroom#m=M2GT58X4MPKAFA59NANTSBDENX83
```

#### 7.1.2 Parsing what people type (input -> canonical code, or the error kind)

| field | case | input | expect |
|---|---|---|---|
| teacher | canonical | `000G40R40M30E209185GR38E1YZ4` | `000G40R40M30E209185GR38E1YZ4` |
| teacher | grouped | `000G-40R4-0M30-E209-185G-R38E-1YZ4` | the same |
| teacher | lower case with spaces | ` 000g 40r4 0m30 e209 185g r38e 1yz4 ` | the same |
| teacher | web link | `https://cedarlogic.netlify.app/classroom/#t=000G40R40M30E209185GR38E1YZ4` | the same |
| teacher | app link | `cedarlogic://classroom#t=000G40R40M30E209185GR38E1YZ4` | the same |
| teacher | app link, query form | `cedarlogic://classroom?t=000G-40R4-0M30-E209-185G-R38E-1YZ4` | the same |
| teacher | web link, query form (never accepted) | `https://cedarlogic.netlify.app/classroom/?t=000G…` | `error:symbol` |
| teacher | a join code where a teacher key is expected | `000G40R40MBY` | `error:length` |
| teacher | a join link where a teacher key is expected | `…/classroom/#j=000G40R40MBY` | `error:kind` |
| teacher | a sync link where a teacher key is expected | `…/sync/#k=000G40R40M30E209185GR38E1YZ4` | `error:kind` |
| teacher | one symbol wrong | `000G4ZR40M30E209185GR38E1YZ4` | `error:checksum` |
| teacher | too short | `000G40R40M30E209185GR38E1YZ` | `error:length` |
| teacher | U is never a symbol | `U00G40R40M30E209185GR38E1YZ4` | `error:symbol` |
| join | canonical | `000G40R40MBY` | `000G40R40MBY` |
| join | grouped | `000G-40R4-0MBY` | the same |
| join | lower case, O for 0, l for 1 | `ooog-4or4-omby` | the same |
| join | no-break spaces | `000G⟨U+00A0⟩40R4⟨U+00A0⟩0MBY` | the same |
| join | web link / app link | `…/classroom/#j=000G40R40MBY`, `cedarlogic://classroom#j=000G40R40MBY` | the same |
| join | a teacher key where a join code is expected | `000G40R40M30E209185GR38E1YZ4` | `error:length` |
| join | a move link where a join code is expected | `…/classroom/#m=M2GT…` | `error:kind` |
| join | one symbol wrong | `000Z40R40MBY` | `error:checksum` |
| join | too long | `000G40R40MBY0` | `error:length` |
| join | dotless i (U+0131) | `000G40ı40MBY` | `error:symbol` |
| join | full-width zero (U+FF10) | `０00G40R40MBY` | `error:symbol` |
| move | canonical / web link | `M2GT58X4MPKAFA59NANTSBDENX83`, `…/classroom/#m=M2GT…` | `M2GT58X4MPKAFA59NANTSBDENX83` |
| move | a teacher link where a move code is expected | `…/classroom/#t=000G…` | `error:kind` |

#### 7.1.3 Keys (HKDF-SHA256, salt `cedarlogic-classroom-v1`)

**counting** teacher key — secret `000102030405060708090a0b0c0d0e0f`

```
PRK           43472b9cdc411fb6cd145b915789086adb10b15ed03270f29f8892f0bbf9d4f1
classId       9c89e40e9ea981bbfeaf61e93c91be46
teacherToken  GnAkibV2czlE1zhADeTN5d3BGYEWLd_sZuCmWd7LO7U
teacherHash   9ac81bbe5eb87f494d7ade493499fd7468b19b1cd6e62016169eb8edeab2f9be
deleteToken   OsNx84G_qLn1qGSgyZUvTsTdz5vSylCZCDdj8Ykh_QA
deleteHash    1986da0c8ccb16c252144cae76e8165b2177132050489b45df9d640aa964082a
backupKey     47abb2dc45d5dec8c85370f119b51a78de57a4f38c4c66ca8ab6cb3bd6d5dd1d
```

**zeros**: classId `3b8d1a072bc2a3da51e62d6ba1e226ac`, teacherToken
`SRREBiwXfX94GwpRXRse-GgoPqqU5eNraL0qoH2EYlU`, backupKey
`dc9d595160721c649c34eee6bdbc472d49736d769fc0cbadefca978638cdbcee`.
**ones**: classId `c83c9b04c33160b70fa57bbb6a69e97a`, teacherToken
`4qJsTeiKo-iPrQ9XoXdX5nZx1n2NUtHJxKTu_ECxI0s`, backupKey
`c2068e858fdaca514647a00e2c8f4bf465aef2798314ec41cfcee83adb3de3ef`.
**random**: classId `1190cc035a12bc0ca0a32c3954689c1f`, teacherToken
`jZx9b2Nz3FFmZiRdz5E1X_HuQk0oqlu87o49xsffcwU`, backupKey
`0b5bbe8a9d6d5a4ac7c9b2f906ba2d6cbde8b74d91cfafb860383b6b2df1a3a7` (the
rest in the file).

**counting** join code — secret `000102030405`, stretched first (PBKDF2-HMAC-SHA256,
salt `cedarlogic-classroom-join-v1`, 600,000 rounds), then HKDF of the
stretched secret:

```
stretched     5bfd32af2d66869db77c9544e4b984d817b20b341f023909bc5cb0f39c27917c
PRK           7b7c47d5b1cd77e9533294fa790cdae3edbd7d699c59db07a41372069cd0aa5c     (HMAC(salt, stretched))
joinId        cdd55457c09fc79b540e6d3cfe4c49b4
joinToken     4XCpmP0JSSv1bd6VPDMlH5xuwMhLG-p3GGO7JJGOm_U
joinKey       764d5999b9351ed00327388a8d954527e83707e1cef958ddae6b2857bdececef
```

What the server stores for it (§1.3), with the **test** pepper
`e0e1…ff` (the 32 bytes 0xe0–0xff; production's is a secret):

```
joinIndex     3bd699d5b6c129e47eb1c224c07e596f47f62cbf51fd9214fc9d96ebaa5fed2a     HMAC-SHA256(pepper, joinId as 32 ASCII hex)
joinHash      74277fb6351b2dd56c0eea6b0a1bb9b198d26ee0c1e6f2af350d5fcca423669c     HMAC-SHA256(pepper, joinToken as 43 ASCII)
```

(zeros: stretched `7f602300c15a2c68ad1ce6b7267687487a2a75080363e6a3ae5548b0a1f65ef8`,
joinId `13269e21d4303681da142796fb08ac2c`; ones: joinId
`5595c635eecd88467f7f5ce71818d360`; random: joinId
`5344db56825cb06154e777866d92e987`; the rest in the file.)

**move** — secret `a0a1…af`: moveId `fb490c53b8af36af281afb2f5064d1fb`,
moveKey `04a3145f07d26ae375750e00c55ec4f693e387f988efa6f65cbc973bb110a7f2`.

**A student token** — bytes `40 41 … 5f`: token
`QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl8`, tokenHash
`f45dc82a7523d14974cea050e3028e8bf06a8ee855a9d63034ebb4fb9ecc2305`.
**A student's proof** — bytes `60 61 … 7f`: `YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn8`.

#### 7.1.4 P-256

```
the class's key pair ("teacher")
  d    2e3bc5dbc3f1d5b0e9bb298cabd6fe28be227d61f2b62603a16d3cd047c5fce7     (JWK d: LjvF28Px1bDpuymMq9b-KL4ifWHytiYDoW080EfF_Oc)
  pub  04324e576ab6e0f3fb6660b11d73a616254430b9d8dde1a57112ee8f77b686e15fd56860bf0f934af74f6d1ac8f1d11c454fd9f1fc453d16c1a7098d8b05254217
       (b64u BDJOV2q24PP7ZmCxHXOmFiVEMLnY3eGlcRLuj3e2huFf1Whgvw-TSvdPbRrI8dEcRU_Z8fxFPRbBpwmNiwUlQhc)
the ephemeral key of records 1 and 10 ("ephemeral")
  d    1d10aee5f2f99a4f15abaeed5e72abb0c7241876b33c46f8134f400b42e58787
  pub  045984605780b06108a14464479339e2f38b970e29a01d98f509e18fd779a0e39a88cc1462c88360a05978d73777c91abe5642b7711ab39591496a0739e88a1dd9
another key pair ("other": the ephemeral key of records 3 and 9, and "another teacher" in 7.1.7)
  d    7bf546b501de8621b3384352980855890f87399ae84df390186b9dae4b1ae9f2
  pub  0433dc5eb66636c81f7cc62ddfcee4bfd6ab242d76893b83486366c4d3be35f720ce346a0d88c1fa493d05104c975d5670836b43a8e63eba6007429007d1fe6ac0
shared X of ephemeral·teacher (and teacher·ephemeral)
       d946dce2baf1963492089a5356f301f35386b12b4483d111a4eccf9c0198b06c
HKDF info = "seal-key" ‖ E ‖ R (138 bytes), sealKey
       a00683a5ca1aa894982e109abdf827ee7752d5fec8c1eda095a91ebc80ea705d
```

Bad points, each refused before or at import: the teacher's public key with
its last byte changed (not on the curve); the compressed form `02 ‖ X` (33
bytes; refused by the format rule even where a library would accept it); the
64 bytes without the `0x04` prefix; the single byte `00`.

#### 7.1.5 Records

All under classId `9c89e40e9ea981bbfeaf61e93c91be46` (the `counting`
teacher key), class key `101112…2f` (the 32 bytes 0x10–0x2f), student
`5f3a1c2e-8b4d-4a6f-9c1d-2e3f4a5b6c7d`, assignment
`7c9e6679-7425-40de-944b-e07fc1f90ae7`, session
`2b1d0a9c-3e4f-4a5b-8c6d-7e8f9a0b1c2d`; payloads as in §2.2 (the file has
each one exactly). "byte-exact" = every client must produce these bytes from
the payload, key, nonce (and ephemeral scalar); "decrypt-exact" = their own
deflate may differ, the opened payload must not.

| # | record | envelope | key | id | ver | flags | nonce | payload bytes | envelope bytes | `h` |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | a student's name, sealed (byte-exact) | 02 | class public key, ephemeral "ephemeral" | the student | 1 | 0 | `c0c1…cb` | 117 | 212 | `894017f82fe1539b76726d869068a003` |
| 2 | an assignment with a readable key (byte-exact) | 01 | class key | the assignment | 1 | 0 | `d0d1…db` | 708 | 738 | `4e60b267fd5ea590055afa47caf279fe` |
| 3 | the same at ver 2 with the key sealed to the teacher, deflated (decrypt-exact) | 01 | class key | the assignment | 2 | 1 | `d1d2…dc` | 835 | 542 | `ad09aff943793c071cf9c3766967186b` |
| 4 | the teacher record (byte-exact) | 01 | backupKey (counting) | `teacher` | 1 | 0 | `e0e1…eb` | 349 | 379 | `570aeb55247a2d0a320295baa8790f7b` |
| 5 | the join record (byte-exact) | 01 | joinKey (counting) | `cdd55457…49b4` | 1 | 0 | `f0f1…fb` | 201 | 231 | `91896986016f65d61c8caff95f203d5e` |
| 6 | the class info (byte-exact) | 01 | class key | `info` | 1 | 0 | `a0a1…ab` | 75 | 105 | `9c0cc08b5be06c15a70858b2af8653e3` |
| 7 | a live record with a predict question (byte-exact) | 01 | class key | the session | 57 | 0 | `b0b1…bb` | 632 | 662 | `750b6b99380a9c7dd28433d7f40a66ba` |
| 8 | the live session ended (byte-exact) | 01 | class key | the session | 58 | 0 | `b1b2…bc` | 102 | 132 | `cf155f7ecd44efc0f8b26c00019212a8` |
| 9 | a submission, sealed, deflated (decrypt-exact) | 02 | class public key, ephemeral "other" | `<aid>/<sid>` | 2 | 1 | `8081…8b` | 594 | 437 | `cb2574c02ba74fe4fc0365b0429f5800` |
| 10 | a predict answer, sealed (byte-exact) | 02 | class public key, ephemeral "ephemeral" | `<session>/<sid>` | 57 | 0 | `7071…7b` | 173 | 268 | `1916a4363ee4d4be367f4a1e4c3fa99f` |
| 11 | a move record (byte-exact) | 01 | moveKey | `fb490c53…d1fb` | 1 | 0 | `6061…6b` | 427 | 457 | `5edd6a3916be57a939d8532172f643c4` |
| 12 | the teacher key as a sync side record (byte-exact; a **sync** envelope, AAD `cedarlogic-sync/1\|<id>\|1\|0`, §2.5) | 01 | the sync vectors' `recordKey` (SYNC.md 7.1.3, counting) | `6f1e2d3c-4b5a-4968-8778-695a4b3c2d1e` | 1 | 0 | `5051…5b` | 273 | 303 | `e38769fb156d3661620ac926165a78da` |

Record 1 in full — AAD
`cedarlogic-classroom/1|name|9c89e40e9ea981bbfeaf61e93c91be46|5f3a1c2e-8b4d-4a6f-9c1d-2e3f4a5b6c7d|1|0`,
payload `{"v":1,"kind":"name","name":"Sam Lee","joinedAt":1759600000000,"proof":"YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn8"}`
(SHA-256 `14d7f8e4396c048eeb17908dcdf89bdc8e0ae09cfabf2e357d60d11f32e0c26a`):

```
envelope (hex)
0200045984605780b06108a14464479339e2f38b970e29a01d98f509e18fd779a0e39a88cc1462c88360a05978d73777c91abe5642b7711ab39591496a0739e88a1dd9c0c1c2c3c4c5c6c7c8c9cacb11843da5a1a0bf999ddd35db578adf53ccaeae6ea283afae83a74ace1f4582748cc9c6daa2f140b63a5af66e6cef0b17b183e852a89e389fbf77856d191e446a70e53f12e540951c2e3d3067b569296c9e7cf1a50af6f5ec07b8816ecdc2fe2d6a77f28494fed1a2121a760925f13c8be9e9e6a445dff28fbe4601b48c4a058d425291e6e1
data (base64url)
AgAEWYRgV4CwYQihRGRHkzni84uXDimgHZj1CeGP13mg45qIzBRiyINgoFl41zd3yRq-VkK3cRqzlZFJagc56Iod2cDBwsPExcbHyMnKyxGEPaWhoL-Znd0121eK31PMrq5uooOvroOnSs4fRYJ0jMnG2qLxQLY6WvZubO8LF7GD6FKonjifv3eFbRkeRGpw5T8S5UCVHC49MGe1aSlsnnzxpQr29ewHuIFuzcL-LWp38oSU_tGiEhp2CSXxPIvp6eakRd_yj75GAbSMSgWNQlKR5uE
```

(byte 0 `02`, flags `00`, then E = the ephemeral public key, the nonce, 117
bytes of ciphertext and the 16-byte tag.)

Record 4 (the teacher record, AAD
`cedarlogic-classroom/1|teacher|9c89e40e9ea981bbfeaf61e93c91be46|teacher|1|0`,
payload SHA-256 `b4ad6ded377b349fdabc33f5cedab24586e99b186155480d7f2434c6cfba930d`):

```
AQDg4eLj5OXm5-jp6uvBVrmDUCYgF8e5LU59-4wJHQ5GBncL9KAwD_RQZBBBCOVFqCqz-SO6uw3BW76Sh_r_KaHHNqisPe4X6O3A64cxtnd_O6IAWgd4F4r6DVbCtcCeVZPBCe5yyx43kO4sV3zlIYe_P1w76CUX2m_UFT-mzXWfc9uwkblfcxcF3pFupsGRW-0A-kFf3AN2fiW54TR2XHNJyDl-uqXF21a3cIvX87HmKE-TU_0DTbZu-q7dEfT9_UAJ5jBQQqeEHC5DLz8B64Ue2iaYpKLq5n_FDU3GmKJLGdcro8CTDRZ7LIf-eqTCnRR5NrVJTDlzVZiQd0L3fmgrkRz_YcpRik9J1xvtmkHIwUNs7VFB5mY5kvhH3mb8kARyFQlt800AW-rf3nWXGxvnuEVT6rGBYa5Pb63gaGBLiE_wlwO6bpPv2NAazUYUaj0U8ZKQPKwcw93Zzj8yhXk1vpbASV0e8fFl_I4KFUs-2nOvgqtMVTsaVA
```

Record 5 (the join record, AAD `…|join|9c89e40e…|cdd55457c09fc79b540e6d3cfe4c49b4|1|0`):

```
AQDw8fLz9PX29_j5-vsQW_H-GQr8QewNq6-cgmYH2pKe8Wh40oM3U5jYcWYzYAeDRs263p2Iml09ef-DNoSiuMXugC6Cdm4OOT1Rxo7vbjOJiv16d0kzQa8jOfiEaYPPAqKLk-Zf7-CkFMlpPQI6woJoWgIm2hrOi1Ygasu7b9wPKvopaZpB1pfQSJjUesBC8d-yPFtXqfbNBqey3QT0QgNzzLT4RxJJPYE29724hl-Cq8Iwt0zZrlstgT_T760kkT4n0-hhshtnWasg31USqdXHgn4tviatXra75LqVrZqClKLxgU62
```

Record 10 (an answer, AAD
`…|answer|9c89e40e…|2b1d0a9c-3e4f-4a5b-8c6d-7e8f9a0b1c2d/5f3a1c2e-8b4d-4a6f-9c1d-2e3f4a5b6c7d|57|0`,
payload `{"v":1,"kind":"answer","session":"2b1d0a9c-3e4f-4a5b-8c6d-7e8f9a0b1c2d","ver":57,"lights":{"LED":1},"at":1759603660000,"proof":"YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn8"}`):

```
AgAEWYRgV4CwYQihRGRHkzni84uXDimgHZj1CeGP13mg45qIzBRiyINgoFl41zd3yRq-VkK3cRqzlZFJagc56Iod2XBxcnN0dXZ3eHl6e8eIAvPZIvz06wmWDtAYrFGVjc2isdo8212T4Fx0aT4qOJMuW12H9PZFB0H6IJjybudQLDDR0ln7oAji3AEafqkJasEzJ2_W7WXQkSd5NsTnESaHHvFDhuoiaV4whZkpEeYaNZhrklVJZSitbRvg-59Ru4u9P76KhtQr-F7NNWFsn8UzRkf8rsK4OB5J_SOY6cdA4yeJkwxGcYsltq3ybt7j2nslueVPUqDffKo-uRczfPLciPBMQIulUNm2wA
```

Record 3's inner sealed key (AAD `…|key|9c89e40e…|7c9e6679-7425-40de-944b-e07fc1f90ae7|2|0`,
nonce `9091…9b`, ephemeral "other", payload
`{"v":1,"kind":"key","text":"LED = A"}`), hex:

```
02000433dc5eb66636c81f7cc62ddfcee4bfd6ab242d76893b83486366c4d3be35f720ce346a0d88c1fa493d05104c975d5670836b43a8e63eba6007429007d1fe6ac0909192939495969798999a9b095b7db024c9407d6f34d9db7349e187d4693934c95a76da40a541a7d0000196cc037cf443961601cb815c1836958832d4775d456f
```

Record 12 (the sync side record of §2.5, sealed with SYNC.md's `counting`
record key `e7333f3b…c1d7` under its own record id, AAD
`cedarlogic-sync/1|6f1e2d3c-4b5a-4968-8778-695a4b3c2d1e|1|0`, payload
`{"v":1,"kind":"classroom","classId":"9c89e40e…","teacherKey":"000G40R40M30E209185GR38E1YZ4","name":"Digital Logic 101","createdAt":1759600000000,"modifiedAt":1759600000000,"device":"Levi’s MacBook Air","deviceId":"5f0c2a9e7d3b41c8a6e2f1b0c9d8e7a6"}`):

```
AQBQUVJTVFVWV1hZWlsSA3blCkRlxaPP5PDfzoV8w-deYMb35wF90VU_8Xjobwg32z_GKT-XxuAc9I2XnZFsLqFAJr0Jrc-vLVh6YNCfej0apfZ4bQSjv11Sdsp5OOcb9KT9_D-QNaeXpDFtipT3GfoMak9ZDOIE4AHDSckrmmXAnMXHA2Y4gy_ZI-mV8ucHj3Nnsg71quZx-D6-JhBXqh0E3tbyzXNadcHqLA5VbwX45MBu-TWCTKhQzL7ERMaokYYMmxg34FanvrjtvW2D7DTZoRrShkFg36hTm07XhBjikYLFH_yPc-reNYKHtzb7WC6NTU8IpMMc1vGO5FNJhNu24rGevOc0jlwxJd_hcKbimmbRFTxd-DKasyk8R_5rHSLuw38xoeyduetS30qu
```

Records 2, 3, 6–9 and 11: in the file (`data`, `envelopeHex` where short,
`deflatedHex` for 3 and 9 — 512 and 342 bytes of deflate that every reader
must inflate to the payload).

#### 7.1.6 Payloads that open but must be refused

Each is sealed in **its own kind's** envelope under its own kind's AAD (id
`bf1d2d5e-96a4-4f80-9235-bcaa2d14a882`, ver 1, nonces `30…3b`, `31…3c`, … in
order; sealed ones with the "ephemeral" key to the class's public key,
symmetric ones under the class key, the teacher one under `backupKey`, the
move one under `moveKey`) and read with that kind expected, so each fails for
the reason its label gives, in §2.2's order. `why`: `newer` = hands off,
`invalid` = treated as damaged.

| kind read as | case | why |
|---|---|---|
| `name` | payload v 2 | `newer` |
| `name` | kind `grade` (unknown) — unknown beats mismatched | `newer` |
| `name` | kind `submission` (known, but not the AAD's kind) | `invalid` |
| `name` | name is a number | `invalid` |
| `name` | unpaired surrogate escape in name (`"\ud83d"`) | `invalid` |
| `name` | invalid UTF-8 byte (0xff) in name | `invalid` |
| `name` | joinedAt is a string | `invalid` |
| `name` | joinedAt past 2⁵³ | `invalid` |
| `name` | name without a proof | `invalid` |
| `assignment` | assignment without cdl | `invalid` |
| `assignment` | assignment key with both text and sealed | `invalid` |
| `assignment` | assignment key with neither text nor sealed (only `names`) | `invalid` |
| `teacher` | teacher record whose join code isn't 12 canonical symbols (`000G-40R4-0MBY`) | `invalid` |
| `answer` | answer with a light that isn't 0 or 1 | `invalid` |
| `live` | live without a session | `invalid` |
| `live` | live whose `ended` is the string `"yes"` | `invalid` |
| `move` | move whose token isn't 43 base64url characters (`not-a-token`) | `invalid` |
| `name` | v missing | `invalid` |
| `name` | not an object (`[1,2]`) | `invalid` |
| `name` | not JSON (`{"v":1,`) | `invalid` |

#### 7.1.7 Must not open

Each (opened with the class's private key for the sealed ones, the class key
for the symmetric ones unless another is given) must fail: the kind's
envelope byte, a format rule, a point off the curve, a tag mismatch, or the
inflate cap. The full `data` of each is in the file.

| case | envelope | data (first 24 chars) |
|---|---|---|
| ver 2 claimed for the ver 1 name | record 1 | `AgAEWYRgV4CwYQihRGRHkzni` |
| another student's id (`9b2e7c1a-5d34-4f8e-b1a2-3c4d5e6f7a8b`) | record 1 | the same |
| another kind (`submission`) claimed | record 1 | the same |
| last tag byte flipped | record 1 | the same |
| a byte of the ephemeral key flipped | record 1 | the same |
| flags byte set to 1 | record 1 | `AgEEWYRgV4CwYQihRGRHkzni` |
| unknown flag bit 0x02 | record 1 | `AgIEWYRgV4CwYQihRGRHkzni` |
| envelope version 3 | record 1 | `AwAEWYRgV4CwYQihRGRHkzni` |
| envelope version 1 claimed (a name's envelope is 0x02: refused before anything else) | record 1 | `AQAEWYRgV4CwYQihRGRHkzni` |
| truncated to 94 bytes | record 1 | `AgAEWYRgV4CwYQihRGRHkzni` |
| opened with another teacher's key ("other") | record 1 | the same |
| ver 2 claimed for the ver 1 assignment | record 2 | `AQDQ0dLT1NXW19jZ2tsHTu0X` |
| another class id (`9d8d8b91b2ab7698e8b8e2b01513d9eb`) | record 2 | the same |
| last tag byte flipped | record 2 | the same |
| the join key instead of the class key | record 2 | the same |
| envelope version 2 claimed (an assignment's envelope is 0x01: refused before anything else) | record 2 | `AgDQ0dLT1NXW19jZ2tsHTu0X` |
| truncated to 29 bytes | record 2 | `AQDQ0dLT1NXW19jZ2tsHTu0X` |
| a sync envelope (AAD `cedarlogic-sync/1\|<aid>\|1\|0`, same key, same nonce) opened as a classroom one | — | `AQDQ0dLT1NXW19jZ2tsHTu0X` |
| a deflate bomb: a well-sealed assignment (ver 3, flags 1) whose deflate inflates to 4,000,001 spaces, one byte past the cap | — | `AQHS09TV1tfY2drb3N34KmwH` |
| the sync side record opened as a classroom envelope (the classroom AAD) | record 12 | `AQBQUVJTVFVWV1hZWlsSA3bl` |
| the sync side record under the class key instead of the sync record key | record 12 | the same |

### 7.2 Protocol scenarios

Each client's suite runs these against the real server logic (web:
`netlify/lib/classroom.mjs` in-process and the mock server; C++ core: the
FakeServer in `ClassroomTest.cpp`, plus the mock server over the platform's
`http` hook). "T1", "T2" are the teacher's devices; "S1", "S2" a student's;
"web" and "app" either kind.

| # | Scenario | Outcome that must hold |
|---|---|---|
| 1 | T1 creates "Digital Logic 101" | 201; `teaching` holds the key, `d`, the class key and the join code; the status's `teacher.env` opens with `backupKey` to the same record; a sync side record is written when Sync is on |
| 2 | T2 adds the class with the teacher key (typed lower case with spaces; a `#t=` link; a scanned sheet) | the preview names the class; T2 has everything T1 has; nothing was written to the server |
| 3 | A wrong teacher key (one symbol) / the key of another class | `error:checksum` / `401 wrong_key`; nothing stored |
| 4 | S1 joins with the code (grouped, lower case) and a name | the preview names the class; 201; the roster has a sealed name T1 opens to "Sam Lee"; S1's status lists no assignments yet |
| 5 | A wrong join code / a closed class / a full class (`maxStudents` = 2) | `404 no_class` and counted / `403 join_closed` / `507 class_full`; the sentences |
| 6 | T1 changes the join code; S2 tries the old one, then the new | old: `404 no_class`; new: joins; the class's `fetchKey` changed: S1's next record fetch gets `404 wrong_fetch_key`, S1 reads its status and keeps working; the old path stays refused |
| 7 | T1 posts an assignment with a readable key; S1 polls | the pulse's `seq` moves; S1 fetches the status and the record from the cached path; the list shows it; Check My Circuit is offered; `attempt` 0 |
| 8 | T1 posts with "only I check" | S1 sees the title and "Your teacher checks this one"; S1's device holds no key text; T1 opens the sealed key from the assignment |
| 9 | T1 and T2 edit the same assignment; T2 saves second | T2 gets 412, refetches, shows T1's edit; nothing merged |
| 10 | S1 hands in; hands in again after a change; T1 views | two attempts, the latest content, `firstAt` kept; T1's table shows "attempt 2", the check result from T1's own run |
| 11 | S1 hands in from S2 (moved) while S1's base is stale | 412 → S1 takes `current.ver` and sends again, once |
| 12 | "Close hand-ins after the due date"; the server clock passes `closesAt` | `409 assignment_closed`; the sentence with the due date; the row says "Hand-ins closed" |
| 13 | S1 hands in offline (fetch fails), then goes online | `pending` kept; sent at the next pulse; the row updates |
| 14 | A hand-in over 512 KiB | `413 record_too_large`; nothing stored; the sentence |
| 15 | A hand-in whose envelope was damaged on the server (a byte flipped) | T1 lists "Couldn't be read" with the time; nothing else changes; fetched again only when `h` changes |
| 16 | A submission payload with `v: 2` | "Needs a newer CedarLogic"; never shown as content |
| 17 | T1 goes live, pushes 3 times; S1 follows | S1's live view shows each version once, in order; the pulse's `live` is the ver; local switch flips are replaced by a push |
| 18 | Predict: T1 asks; S1 and S2 answer; T1 reveals | T1's counts 2 answered, per-light counts; after reveal right/wrong against the pushed lights; S1's own score shown |
| 19 | An answer sent after the next push (stale ver), and after End Live | the first is accepted and labelled by its ver; the second `409 not_live`, dropped quietly |
| 20 | T2 pushes while T1 is live | T2 gets 412 → "Take over" → T2 is live; T1's next push gets 412 and the same offer |
| 21 | End Live | the pulse's `live` is 0; S1 stops fast polling; "The live view ended." |
| 22 | The pulse answers a lower `seq`/`live` than seen (a stale cache) | ignored quietly; nothing refetched; no notice |
| 23 | The server lists an older `ver` for a submission T1 already opened | ignored for that student with a log line; the opened copy stays |
| 24 | S1 makes a move code; S2 imports it; the slot is deleted; S2 imports again | S2 is the same student (same token); the second import `404 move_gone` |
| 25 | A move code expires (10 minutes) | `404 move_gone` on the other device; a new code works |
| 26 | S1 leaves | roster entry and answers gone; hand-ins stay and T1 still opens them with the name inside, marked "(left the class)"; S1's circuits stay |
| 27 | T1 removes S2 (the box unticked), then S3 with "Also delete everything they handed in" | S2's roster entry and answers gone, S2's hand-ins still listed and readable; S3's hand-ins gone too; the `fetchKey` changed; S2's next request `403 not_a_member` → membership forgotten, circuits kept, one notice; S2's old record paths `404 wrong_fetch_key` |
| 28 | T1 deletes the class | 410 for everyone; T2 and S1 forget it with the notice; the sync record is tombstoned; blobs gone after the cleanup |
| 29 | A class idle 400 days (clock advanced); the cleanup runs | `class_expired` for everyone; the warning line appeared on T1 from 60 days before |
| 30 | A never-used class idle 8 days; the cleanup runs; T1 opens it | 404 → T1 re-creates it quietly from its files; the same key and code work |
| 31 | Two devices run an old sync client beside a new one; the new creates a class | the old device shows nothing (no notice, no damaged count), keeps syncing circuits; the new device's record is intact |
| 32 | The request breaker trips during a lecture | `503 classroom_paused` on writes only; the status, the pulse and record fetches keep being served (cache hits and misses); S1's live view keeps following |
| 33 | `429` with `Retry-After: 120` on a hand-in | no request for 120 s; then it goes |
| 34 | The server answers the join lookup with a record under another key, or a `pub` not on the curve | "couldn't be read"; nothing joined |
| 35 | 300 students join and hand in within one minute (the mock server with latency) | every write lands or is retried to success; no lost update in the index |
| 36 | `CLASSROOM_CLOSED=1` | create and writes `503 busy`; reads and the pulse work |
| 37 | The mock server swaps the join record (`tamper {swapJoin}`: one sealed under the same join key with another public key) | T1's next status shows another `join.h`; T1 opens it, finds another key, and shows "The join record on the website isn't the one your devices wrote. Change the join code." |
| 38 | A hand-in, a rename and an answer for S1 made through the store stub with S1's `studentId` and the class's public key but another proof | T1 lists the hand-in as "Couldn't be verified", keeps the pinned name, doesn't count the answer |
| 39 | A store dump after create, join and a code change (`GET /__mock/dump`) | no `joinId`, no SHA-256 of a join token, no public key anywhere; the `j/` key and `joinHash` equal §7.1.3's server vector under the test pepper |
| 40 | Someone posts the code; 40 names join from one address in a minute; T1 removes everyone who joined after 10:05 | the 61st join from that address into the class would be `429`; one request removes the 40; their record paths stop working |
| 41 | One address sends requests as fast as the edge allows | its allowance answers `429` before the site's breaker moves; other classes keep handing in |
| 42 | T1 deletes an assignment, then the class | the mock's purge log shows `asg-<aid>`, then `class-<classId>` |
| 43 | A hand-in whose `cdl` has 25,000 gates; one whose deflate inflates past 4,000,000 bytes | "Couldn't be read (too big)" for both; nothing built; the rest of the table fills |
| 44 | T1 uses Remove from This Device on a second computer (Sync on); S1 does the same on a lab PC | the class is gone from that computer only; T1's other devices and the website unchanged; the sync side record intact and not re-added there; S1 is still in the class on the website |

### 7.3 Per-client suites

- **Server** — §3.12 (`scripts/test_classroom_server.mjs`), then scenarios
  1–44 driven by the web core against the mock server.
- **Web** — `scripts/test_classroom_client.mjs` (Node 20+): the vectors
  (every item of §7.1), then scenarios 1–44 with web engines on Maps against
  `handle()`/`pulse()` in-process and once more against the mock server;
  `scripts/test_classroom_ui.mjs` (Group 6, headless Chrome): a whole class
  through the real pages, and the phone-width checks.
- **C++ core** — `clclass::selfTest`: the vectors (`ClassroomVectors.h`) and
  scenarios 1–44 on the FakeServer; `mac/Tools/classroom-check.sh` with the
  CryptoKit hooks, also against the mock server; sanitizers clean.
- **Interop, once, before release**: a Mac teacher with website students
  (Chrome, Safari, a Chromebook, a phone) and a website teacher with a Mac
  student through scenarios 1, 4, 7, 8, 10, 17, 18, 24, 27, 28 by hand against a
  deploy preview; then the owner's checklist.

---

## 8. Privacy and security

### 8.1 What people are told

In the Create Classroom sheet, Settings › Classroom › "How Classroom keeps
things private", and the `/classroom/` page's **Privacy for schools**
section (plain words, no legal advice):

> **How CedarLogic Classroom keeps a class private**
>
> There are no accounts. A teacher makes a class and gets a secret teacher
> key; students join with a short code from the board and type a name — the
> name their teacher should see, which can be a first name or a nickname.
>
> **What leaves a device, and how.** A student's name, every circuit they
> hand in and every answer to a live question are encrypted *for the teacher*
> on the student's device before they are sent: only a device holding the
> teacher key can read them. Assignments and the live view are encrypted for
> the class: only its members can read them. CedarLogic's website stores
> these encrypted records so the class can find them; it never has the keys.
> A student who leaves or is removed keeps what they already saw, but can't
> download anything new from the class.
>
> **What the website does see.** That a class exists, when it was made and
> last used, how many students joined and when, how many assignments there
> are and how big they are, when hand-ins and answers arrive, and the
> internet addresses devices connect from (used only to stop abuse, kept as a
> keyed hash for at most a day, not stored as addresses). It doesn't see names, class names,
> instructions, circuits, answer keys, guesses or results. It sets no
> cookies, runs no analytics and loads nothing from third parties on
> classroom pages.
>
> **Where.** The records live in Netlify's storage for this site, in the
> United States (the site's functions and storage run in Netlify's `us-east-2`
> region, in Ohio). Netlify, the website's host, keeps ordinary request logs
> for a short time.
>
> **How long.** A class nobody opens for 400 days is removed from the website
> (the teacher's devices warn beforehand). A teacher can delete a class at any
> time, and everything in it is deleted from the website, along with the
> copies the website's content network keeps; a student can leave a class at
> any time. Everyone keeps the circuits on their own devices.
>
> **For schools.** CedarLogic Classroom asks for no email, login or account.
> Names, circuits and answers are encrypted for the teacher on the student's
> device, so the website can't read them. What it does handle: the internet
> addresses devices connect from (which some rules, COPPA among them, count
> as personal information; used only against abuse, as above, and in
> Netlify's ordinary request logs) and a random id per student per class,
> tied to a person only by the name the teacher sees. If your school's policy
> treats student names or coursework as protected records (FERPA-style rules
> in the US, or similar elsewhere), note that the operator of this website
> can't read them; the teacher's devices can, and the teacher key is the
> only way in — or the teacher's sync code, when Sync is on — so teachers
> should keep both as they would a password. Removing a student keeps what
> they handed in unless the teacher chooses to delete it. On shared
> computers, use Remove from This Device when you're done; a Chromebook in
> guest or ephemeral mode forgets the class at sign-out, and a student then
> has to join again (the teacher sees them twice). For students under 13
> (COPPA-style rules), the teacher decides what students type as a name;
> "first name only" or a nickname works. The code is open (the apps and the
> protocol), and the test vectors in `tests/classroom/` let anyone check the
> encryption.

The teacher-key sheet's two paragraphs are in §5.1; the join sheet's "Your
name and what you hand in are encrypted for your teacher" in §5.3.

### 8.2 Threat model

| Who | Can | Can't |
|---|---|---|
| **Anyone with a copy of the Blobs store** (a leaked store token, a backup) | See what §2.6 lists: class ids, counts, sizes, times, which student id handed in when, how often the teacher pushes; with write access, replace or delete records and deny service | Read names, class names, instructions, circuits, keys, guesses; alter a record (any change fails the tag); make a student record a teacher's device accepts (the store has no public key to seal to and no proof: a made-up one is "Couldn't be verified"); make a class-key record (no class key); roll a class back (high-water marks); present a record as another class's, student's, assignment's or version (AAD); search the store for join codes (only HMACs under a pepper that isn't in the store, §1.3) |
| **The site operator** (the store and the function's environment, the pepper included) | All of the above; withhold records, delete a class; link classes to addresses; hand a removed student new records (the `fetchKey` is the server's own rule); and, with one table of all 2⁴⁸ join codes built once for about 700 GPU-years (§1.3), or with a join code a student passes on: open that class's join record, read its assignments and live view, and seal records to the class — but still no proof, so a forged hand-in or answer is "Couldn't be verified" | Read names, hand-ins, answers or sealed keys (the teacher's private key never leaves the teacher's devices); swap the join record without the teacher's devices noticing (§4.1); forge the teacher's assignments or pushes without a member's class key, and even with it only until v1.1's signatures (§0, below) |
| **A network attacker** | See that a device talks to `cedarlogic.netlify.app`, when, and roughly how much | Anything else |
| **A TLS-inspecting proxy** (school networks) | See the teacher's and students' bearer tokens; with a student's: hand in garbage (it fails the proof check) or leave as that student; with the teacher's: post garbage or delete assignments, remove students, push a garbage live record, read encrypted records (useless) — vandalism the teacher sees at once; with the join token (sent on the two join requests): join under any name, though it can't read the join record without the join key | Read or forge anything (no keys, no proofs); delete the class (the delete token is sent only for that one request); learn the join code or the teacher key (never sent) |
| **Someone who sees the join code** (the board, a photo, a student who shares it) | Join the class under any name (60 a day from one address) and read the assignments and the live view while a member; hand in as that made-up student; keep what they downloaded, and the class key, after being removed | Read anyone's name, hand-ins or answers; download anything new from an honest server once removed or once the code changes (the `fetchKey` changes); act as a real student (no proof) |
| **Someone who gets the teacher key** (a screenshot, the recovery sheet, a shared computer, or the teacher's sync code when Sync is on) | Everything the teacher can: read every name and hand-in, post, push, delete the class | Keep access after… there is no rotation of the teacher key in v1: the teacher makes a new class (open question §11) |
| **Someone who gets a move code** (within its ten minutes) | Become that student on their device | Anything after the ten minutes, or after the student leaves and joins again |
| **Another student** | What any member can: the assignments, the live view, the class name; after leaving or being removed, what they already downloaded | Other students' names, hand-ins or answers (sealed to the teacher); the roster; pass work off as another student's (no proof); anything new after leaving, from an honest server |
| **A student's own device** | Read the student's own records | — (they're the student's) |
| **A malicious student client** | Hand in huge, malformed or deflate-bomb envelopes within the caps; answer many times a minute; join many times under many names (counted per address and per class; the teacher removes them in one go) | Pass the caps (512 KiB sent, 4 MB inflated, 20,000 gates built, 5 s checked, §4.9); make the teacher's client apply anything (damaged records are listed, never applied); read anything beyond the class key's records |
| **Malware, or the next person at a shared computer** | Read the key files / IndexedDB like any of the person's files: every class left on that computer | — so: Remove from This Device (§4.1, §4.7), which the teacher-key and join sheets point to |
| **A script injected into the website** | Read the keys in IndexedDB | — so it must not happen: text only via `textContent`, the CSP, no third-party scripts |
| **Abusers of free storage** | 100 MiB per class, 10 new classes per address per day, within the site budget | Pass the budgets and the request breaker; a lecture in progress keeps its cached pulse and records even when writes are paused |
| **A flood of requests** | From one address: run the function up to the edge rule's rate and its allowance (§3.5), each refused request still an invocation. From many addresses: spend the plan's monthly invocations, and trip the daily breaker (writes pause until 00:00 UTC) | From one address: trip the breaker for everyone (its allowance runs out first); take a lecture's live view down (reads go on past the breaker); read anything. Stopping a many-address flood before it spends the month is by hand (§3.5) |

Notes:

- **Sealed means confidential, not authenticated.** Anyone with the class's
  public key can seal a record to it, which is why the key stays off the
  server and why each student record carries the student's proof. Class-key
  records (assignments, live, info) are authenticated only by the class key,
  which every member holds: a member who also gets write access to the store
  could forge teacher content. **v1.1 signs it**: the teacher's devices sign
  each envelope they write with `d` (ECDSA P-256 over the envelope bytes:
  WebCrypto imports the same JWK as ECDSA, CryptoKit as `P256.Signing`;
  using one scalar for ECDH and ECDSA is acceptable here, or the teacher
  record can carry a second key pair), `sig` beside `env`, and students
  verify with the public key from the join record. The same signature on
  requests instead of the bearer token would close the proxy row. Neither
  changes a v1 byte.
- **Metadata**: sizes aren't padded; a submission's size says roughly how big
  a circuit is, which the operator may also infer from the assignment's size.
  Not worth hiding.
- **Randomness**: every key, nonce, id and code comes from the platform
  CSPRNG; a failure stops the operation.
- **Rate-limit records** key on an HMAC of the address with the site's pepper
  and are deleted daily.
- **The sync code** opens every `classroom` side record (§2.5), so with Sync
  on it is as good as every teacher key of that person. SYNC.md §8.1's text
  and the `/sync/` page must say so when Classroom ships ("your circuits and,
  if you teach, your classes") — a change to Sync's files, listed for package
  W.

### 8.3 Failure modes

| What happened | What the design does | What the person does |
|---|---|---|
| **The teacher loses the teacher key** (no device has it, no sheet, Sync off) | nothing can read the class's records; the server can't help (hashes only); the class keeps working for students until it expires | make a new class; students join it; the old one expires after 400 days (or an admin deletes it on request — there is no admin path in v1: open question) |
| **The teacher loses one device** | the key is on the others (Sync) or on the sheet; add it to a new device | I Have a Teacher Key |
| **A student loses the device** (no move code) | the membership is gone; the teacher sees the old entry go quiet | join again with the join code under the same name; the teacher sees two "Sam Lee" entries and removes the old one, leaving its box unticked; hand-ins made from the old device stay in the list under the name inside them |
| **The join code leaks** (shared beyond the class) | strangers can join under any name (60 a day from one address); what they download while members stays with them | the teacher changes the code and removes the names they don't recognise (several at once, or everyone who joined after a time); the removed can't download anything new from the website; nothing of other students was ever readable |
| **The teacher key leaks** | whoever has it is the teacher | delete the class and make a new one (students re-join); v1 has no key rotation |
| **The server is compromised or its storage copied** | ciphertext and metadata only (§2.6), no public key, no proofs, the join index only under the pepper; a thief with write access can replace records — a student's made-up one is "Couldn't be verified", a tampered one "damaged", a rolled-back one ignored | the operator rotates the rate pepper and the app key, and the join pepper through `CLASSROOM_JOIN_PEPPER_OLD` (§1.3) if the environment leaked too |
| **Replay**: an old record or pulse served again | high-water marks ignore lower versions; the AAD stops re-labelling | nothing |
| **A malicious student sends huge or malformed hand-ins** | 512 KiB and per-student caps; a malformed one is "Couldn't be read" with the name and time; the teacher removes the student | — |
| **The teacher's two devices disagree** (edit, join-code change, live) | 412 on the second write; the second device refetches and shows the first's change; live: Take Over | choose |
| **The website is down mid-lecture** | the pulse's cached answers survive short outages; the student keeps the last circuit; the teacher's pushes fail with "Can't reach the website"; hand-ins queue | wait; nothing is lost |
| **Netlify's monthly budget runs out** (legacy plan) | the breaker pauses writes before Netlify suspends the site; the live view keeps its cached state | the owner raises the limits, slows the pulse (`CLASSROOM_PULSE_SECONDS`) or upgrades (§3.10) |
| **A device's clock is wrong** | times are server-corrected (the `Date` header); `closesAt` is enforced by the server's clock; due dates are shown from the payload | nothing |
| **The teacher posts an assignment that won't open on a student's device** (damaged in transit) | the student's list says so; the teacher's device re-posts from its draft cache on the next status (the client compares `h`) | post again if it persists |
| **Two students pick the same name** | the teacher sees two rows with the same name and different join times | ask them to rename ("Sam L." / "Sam Lee 2") |
| **A student joins twice from one device** | the device already has the membership and opens it instead | — |
| **The class expires while the teacher still wants it** | the warning showed for 60 days on every teacher device | open the class once a year |
| **A flood of requests trips the daily breaker** (it takes more than one address, §3.5) | writes answer `503 classroom_paused` until 00:00 UTC; reading goes on — the status, the pulse and the record fetches keep every lecture's live view going; hand-ins queue on the students' devices | the owner blocks the source with a Netlify traffic rule, or raises `CLASSROOM_MAX_REQUESTS_PER_DAY`; students hand in later |
| **A copy of the server's records is stolen and searched for join codes** | nothing to search: the store holds only HMACs under a pepper kept in the function's environment (§1.3). With the environment too, one table of all codes (about 700 GPU-years, once, for every class) would give a class's assignments and live records, not names or hand-ins; a new join code makes the old index worthless | the owner rotates the join pepper (§1.3); teachers can change their join codes |
| **A teacher or student leaves a class on a shared computer** | everything of that class on it is readable to the next person (§8.2) | Remove from This Device; the sheets say so |
| **Chromebooks in guest or ephemeral mode** | IndexedDB is wiped at sign-out: a student's membership is gone and they join again as a new student, every lesson | the teacher removes the old entries (several at once); hand-ins stay under the name; the school can allow CedarLogic's site data to persist, or students can use a move code from a device that keeps it; a lasting "class pass" is an open question (§11) |

---

## 9. Order of work, and the contract between packages

1. **S — server** first: `netlify/lib/classroom.mjs` + the stub + the mock
   server + tests; deployed behind a deploy preview. `GET /health` answers
   `{ok:true, protocol:1}`; the pulse's `Cache-Status` is checked there.
2. **In parallel from day one** (they need only §1–§2 and the vectors): **W**
   (`classroom-core.js`: vectors first, then the engine against `handle()`),
   **E** (`ClassroomProtocol.cpp` + vectors, then the engine + FakeServer
   scenarios), **M** (the P-256 hooks, checked with
   `mac/Tools/classroom-vectors-check.sh` before the core exists).
3. **UI**: Group 6 (web) and Group 7 (Mac) against the mock server, then the
   deploy preview.
4. **Interop day** (§7.3), then release: the site first with
   `CLASSROOM_CLOSED=1` until the Mac beta ships, if preferred.

The contract, so nobody needs to talk to anyone else:

- Bytes: §1, §2.2, proven by §7.1. Any disagreement → the vectors and the
  three checkers win.
- HTTP: §3.2–§3.4 exactly (paths, headers, bodies, status codes, `error`
  codes, cache headers).
- Behaviour: §4, proven by §7.2.
- Words: §5 (keep them; platform conventions for button order and capitals).

---

## 10. Implementation packages

### S — Server

Owns: `netlify/lib/classroom.mjs`, `netlify/lib/api.mjs` (the gate, breaker,
budget, body reader and reply helpers lifted from `sync.mjs`, which keeps
passing its tests), `netlify/functions/classroom.mjs` (both prefixes, §3.1),
`classroom-cleanup.mjs`, `scripts/classroom-mock-server.mjs` (`--port 8788`;
controls `POST /__mock/reset|clock|cleanup|limits|tamper|dropNextAnswer|fail`
— `tamper` also `swapJoin` and `forgeAs {sid}` for scenarios 37–38 —
`GET /__mock/dump` and `GET /__mock/purges`), `scripts/test_classroom_server.mjs`, the `netlify.toml`
rate-limit rules, the env names of §1.3 and §3.5 (`CLASSROOM_JOIN_PEPPER`,
`CLASSROOM_ADDRESS_PER_HOUR`, `CLASSROOM_ADDRESS_PER_DAY`, …) in the
README/HANDOFF.

Needs: nothing. Acceptance: `node scripts/test_classroom_server.mjs` passes
every case of §3.12; on a deploy preview a create/join/post/hand-in/live round
trip with `curl`, the store names end in `-preview`, the pulse shows
`Cache-Status` hits and a `304`, the cleanup runs on schedule and logs counts.

### W — Web core (and, with Group 6, the UI)

Owns: `public/assets/js/classroom-core.js`, `scripts/test_classroom_client.mjs`,
the `tests/classroom/vectors.json` copy under `scripts/fixtures/` if the test
layout wants it, the side-record extension of `sync-core.js` (§2.5, with
`test_sync_client.mjs` still passing), and with Group 6: `sim-classroom.js`,
the `/classroom/` page and its script, the `#j=`/`#t=`/`#m=` handling in
`sim.js`, the service worker's precache list, `netlify.toml` headers, and
the Sync wording of §8.2's last note (SYNC.md §8.1 and the `/sync/` page).

Needs: S's lib and mock server. Acceptance: `node scripts/test_classroom_client.mjs`
(vectors byte-exact, scenarios in-process and against the mock server);
Group 6's `test_classroom_ui.mjs`; no CSP violations on a deploy preview; by
hand: Chrome, Safari, a Chromebook and a phone through §7.3's interop list.

### E — The shared C++ core

Owns: `mac/CedarCore/Classroom.h`, `ClassroomProtocol.cpp`,
`ClassroomEngine.cpp`, `ClassroomTest.cpp`, `ClassroomVectors.h` (generated
from the JSON by a small script beside `make-sync-vectors.py`),
`ClassroomCApi.cpp`, `include/CedarClassroom.h`, the side-record extension
of `Sync*.cpp` (with `selfTest` still passing).

Needs: the vectors; S's mock server for the over-HTTP part. Acceptance:
`clclass::selfTest` with the OpenSSL test hooks on macOS and Linux CI (every
vector, scenarios 1–44), the same against the mock server; sanitizers clean;
no platform header in any `Classroom*` file; `Classroom.h` matches §6.2;
`readLegacyCdl` / `readCircuitFile` fuzzed (libFuzzer, sanitizers on) with no
finding left open, and the part counts of §4.9 enforced before a received
circuit is built.

### M — Mac hooks and (Group 7) UI

Owns: `mac/App/ClassroomHooks.swift`, `mac/Tools/classroom-vectors-check.swift`
+ `.sh` (in the repo now), `mac/Tools/classroom-check.sh`, and with Group 7:
`mac/App/Classroom.swift`, `ClassroomWindow.swift`, the Hand In item, the live
bar, `ShareLink.swift`'s `classroom` host, the Settings page entry.

Needs: E. Acceptance: `mac/Tools/classroom-vectors-check.sh` passes (it does,
2026-10-06, revision 2: 124 checks, built with the Command Line Tools alone,
no Xcode);
`mac/Tools/classroom-check.sh` passes with the hooks
(vectors, scenarios, the mock server); `mac/build.sh` builds with no new
warnings; the manual checklist of §7.3.

---

## 11. Open questions for the owner (none block building)

1. **Teacher-key rotation.** v1 has none: a leaked teacher key means "delete
   the class, make a new one". Adding "Change Teacher Key" (a new key, the
   same class: re-seal the teacher record, swap `teacherHash`/`deleteHash` on
   the server) is a small server change later. Fine for v1?
2. **A student who leaves or is removed.** The design keeps their hand-ins
   with the teacher (coursework) and removes their roster entry and answers;
   the teacher can tick "Also delete everything they handed in" when
   removing. Fine?
3. **Earlier attempts.** Only the latest hand-in is kept (with the attempt
   count and the first time). Should the previous one be kept too, so the
   teacher can see what changed?
4. **Cost on the legacy Free plan.** One class of 300 with heavy predict use
   is about 60 % of the month's function invocations (§3.10). OK to launch
   with the breaker at 20,000 a day and watch the dashboard, or move answers
   and the pulse to Edge Functions before launch?
5. **Sync's breaker.** `SYNC_MAX_REQUESTS_PER_DAY` defaults to 100,000 a day,
   above the plan's 125,000 a month. Lower it to the same 20,000?
6. **The 400-day expiry and the 60-day warning**, and the 7-day removal of
   never-used classes: fine?
7. **Student memberships via Sync** (kind `membership`): would make a student's
   phone and laptop one student without a move code. v1.1?
8. **Lost teacher key, class still live for students**: should there be an
   out-of-band way for the owner to delete a class on request (an admin
   function behind the feedback admin's password)? Today there is none.
9. **Names**: should the join sheet suggest "first name only" by default for
   younger classes (a hint line), or leave that to the teacher's instructions?
10. **The join code's stretching** (§1.3) — answered in revision 2: keep
    600,000 rounds (OWASP's 2023 figure for PBKDF2-SHA256; a second or two
    once per join on the slowest Chromebooks). It prices one table of all
    codes at about 700 GPU-years, and the pepper keeps a copy of the store
    from being searched at all; trading down would halve the one and change
    nothing about the other.
11. **Where the data is.** Netlify keeps site-wide Blobs in `us-east-2`
    (Ohio), so the privacy text says "in the United States". A school outside
    the US may ask about transfers; say so on the page, or leave it?
12. **A longer join code.** 15 symbols (`K7QM-4XPD-2FJ3-X9W`, 60 secret bits)
    would make the table of all codes 4,096 times dearer (§12, finding 1.3).
    Revision 2 keeps 12 symbols, since the pepper already keeps the store from
    being searched and a code on a whiteboard is typed by every student.
    Switch to 15?
13. **A class pass for wiped computers.** Chromebooks in guest or ephemeral
    mode forget a class at every sign-out, so students re-join as new
    students (§8.3). A pass — a move slot that never expires, shown once as a
    28-symbol code / link / QR ("Save this to come back on another
    computer") — would carry them back. v1.1, or before launch for schools
    that run Chromebooks that way?
14. **"Encrypted for your teacher", not "end to end".** Revision 2 words the
    school text that way (§8.1), since the server's records are confidential
    but the teacher's own records aren't signed until v1.1. Fine?
15. **A removed student keeps the class key.** The `fetchKey` stops an honest
    server from serving them anything new; real rotation (student key pairs,
    the class key re-sealed to each remaining student) is v1.1. Acceptable
    for launch?

---

## 12. Review notes (revision 2)

Two reviews of revision 1 (2026-10-06) reported the findings below; each is
accepted, accepted in part, or declined, with the reason and where the
change is. The second review's list reached this revision cut off after its
eighth finding: anything it said after that is not addressed here.

**Review 1**

1. **The join code's stretching was priced per class** (medium). *Accepted.*
   The salt is fixed, so the cost is one table for every class, and the
   stored `joinId`/`joinHash` were the oracle. §1.3 now says so (about 700
   GPU-years, once, for all classes) and the store keeps only HMACs under
   `CLASSROOM_JOIN_PEPPER` (`j/<joinIndex>`, `joinHash`); the teacher sends
   `joinToken` instead of its hash (§3.3); a test-pepper vector is in §7.1.3;
   §8.2, §8.3 and §11.10 reworded (keep 600,000 rounds). *Declined in part*:
   the longer join code (60 bits). With the pepper, a copy of the store holds
   nothing to search, and what remains needs the function's secrets too; a
   code every student types from a whiteboard is a real cost. Left to the
   owner (§11.12); the change is confined to the code length.
2. **"Removed members can't fetch anything" was false** (medium). *Accepted*
   as proposed: a per-class `fetchKey` in every `/api/live/` path, checked
   against the class document, handed out in the authenticated statuses and
   the join answer, changed by the server on Change Join Code and on every
   removal (§3.3); §0, §1.4, §8.1–§8.3 now say what a removed member keeps
   (what they downloaded, and the class key). Scenarios 6, 27.
3. **Nothing was origin-authenticated** (medium). *Accepted:* (1) the class's
   public key is gone from the server (create body, class document,
   statuses, §2.6); (2) a per-student `proof` inside `name`, `submission`,
   `answer` and `move`, pinned per student by each teacher device, "Couldn't
   be verified" otherwise (§1.4, §2.2, §4.3, vectors 1, 9, 10, 11 and 7.1.6);
   (4) §8.1–§8.3 reworded ("encrypted for the teacher", what a store writer
   can and can't do). *Deferred:* (3) signing the teacher's envelopes, to
   v1.1 (§8.2's notes), because it adds a signature to every class-key
   record and a verification path to three clients, and the remaining gap
   needs both store write access and a member's class key.
4. **The teacher's devices never checked the join record** (low).
   *Accepted:* `join: {ver, h}` in the teacher status, kept per device; a
   change makes the device open the server's join record and check its keys
   (§4.1, §2.4); scenario 37.
5. **Two threat rows understated a token and the sync code** (low).
   *Accepted:* the proxy row covers the join token; the teacher-key row, the
   teacher-key sheet and the school text cover the sync code (§8.2, §5.1,
   §8.1). The change to Sync's own text is listed for package W (§8.2's
   notes, §10 W) rather than made in SYNC.md here.
6. **The reference script and the vectors under-pinned four rules** (low).
   *Accepted:* each kind's envelope byte is fixed and checked first
   (`ENVELOPE_OF`, §1.5, two 7.1.7 cases); the payload check order is stated
   (§2.2) and every 7.1.6 case is sealed in its own kind's envelope and AAD and
   read with that kind expected (with new cases for a known-but-wrong kind,
   a missing proof, `ended` as a string and a short move token); the
   reference inflate streams and stops at the cap (a deflate-bomb case in
   7.1.7); `ended` and `move.token` are type-checked. Both checkers follow.

**Review 2**

1. **Former and removed members could read the class forever** (high).
   *Accepted in part:* the `fetchKey` of finding 1.2 stops an honest server
   from serving them anything new, and the claims are corrected. *Declined
   for v1:* student key pairs and re-keying on removal. They add a key pair
   to every membership, a re-key record per removal written by the teacher's
   device (up to 300 envelopes) and a key history to every reader; the
   remaining exposure needs a dishonest server or a current member passing
   records on. Adding a student public key later needs no change to the join
   request (a v1.1 client can register one with an authenticated call), so
   nothing is lost by waiting. Owner question §11.15.
2. **One address could pause classroom writes for every class** (high).
   *Accepted:* one function now serves `/api/classroom/*` and `/api/live/*`
   with the site's last rate-limit rule (§3.1; the deploy-preview check
   decides between 1,200 and 7,200 a minute); a per-address allowance of
   every invocation (6,000 an hour, 12,000 a day, counted in memory and
   flushed every 50) answers `429` first, and the breaker counts only
   allowed requests, so one address can't trip it; past the breaker only
   writes stop, said once in §3.5 and followed by §3.4, §7.2 (32, 41) and
   §8.3. *Declined in part:* lowering the rule toward Sync's 300 a minute —
   a 300-student class behind one school address needs more, and the
   allowance does the fine work. The §8.2 flood rows now say that a
   many-address flood can still spend the month and that stopping it is by
   hand.
3. **Removing a student deleted their hand-ins** (high). *Accepted:* removal
   keeps them by default; "Also delete everything they handed in"
   (`?submissions=delete`) is an explicit choice (§3.3, §5.2, §8.3, Q2;
   scenario 27).
4. **Shared computers** (medium). *Accepted:* Remove from This Device for
   teachers and students, local only (§4.1, §4.7, §5, §6.2), a line on the
   teacher-key and join sheets, the Chromebook ephemeral-mode note in the
   school text and §8.3. *Deferred:* the durable "class pass", to the owner
   (§11.13): it is a new code kind and a never-expiring server slot, and the
   move code covers a student who still has another device.
5. **Fake joins could fill a class** (medium). *Accepted:* bulk removal
   (`POST …/students/remove`, multi-select and "Remove everyone who joined
   after…"), 60 joins per address per class a day, a 640-byte name envelope
   (the reviewer's 512 doesn't fit a 64-character name in four-byte
   characters with its proof), 5 renames an hour (§3.3, §3.5, §5.2; scenario
   40).
6. **Deletion didn't purge the CDN** (medium). *Accepted:* every `/api/live/`
   response carries `class-<classId>` (and `asg-<aid>`), purged on class and
   assignment deletion, expiry, and every `fetchKey` change, re-tried by the
   cleanup; the privacy text says what it does (§3.3, §3.10, §3.11, §8.1;
   scenario 42).
7. **Circuits from the other party had no budget** (medium). *Accepted:* a
   4,000,000-byte inflate cap for classroom envelopes (Sync's side record
   keeps 20,000,000), 20,000 gates / 50,000 wire segments counted before
   building, worker parsing and a 5 s budget on the web, a 5 s budget per
   automatic check, fuzzing of the C++ readers before the Mac ships (§1.5,
   §4.9, §10 E; a deflate-bomb vector; scenario 43).
8. **The school privacy text overclaimed** (medium; the finding was cut off
   mid-text). *Accepted* for what it showed: §8.1 no longer says "collects
   no personal information" or "no device identifier", names internet
   addresses and Netlify's logs, and says "encrypted for the teacher" rather
   than "end to end".
