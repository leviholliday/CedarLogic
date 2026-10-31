# The Linux one-liner

```bash
curl -fsSL https://cedarlogic.netlify.app/install.sh | sh
```

`packaging/linux/install.sh` installs from the newest **full release** of
`leviholliday/CedarLogic-Releases`:

| System | What it installs | How |
|---|---|---|
| Debian, Ubuntu, Zorin, Mint, Pop!_OS (anything with `dpkg` + `apt-get`) | `cedarlogic_<v>_amd64.deb` | `apt-get install` on a root-only copy, so apt pulls in GTK and the rest |
| Raspberry Pi 4/5, 64-bit Raspberry Pi OS | `cedarlogic_<v>_arm64.deb` | same. The 32-bit OS (`armhf`) gets a clear "use the 64-bit OS" message |
| Fedora, openSUSE, Arch, anything without apt | `CedarLogic-Native-<v>-x86_64.AppImage` (or `-aarch64`) | to `~/Applications/CedarLogic.AppImage`, then started once, which adds it to the menu itself (`Updater_IntegrateAppImage`) |

The choice is by `dpkg --print-architecture` for the .deb (the userland's
architecture, not the kernel's: a Pi 4 with 32-bit Raspberry Pi OS runs a
64-bit kernel) and `uname -m` for the AppImage. `CEDARLOGIC_FORMAT=appimage`
forces the AppImage on a Debian system.

Without FUSE 2 (Fedora and Ubuntu 24.04 don't install it by default), the
AppImage is started with `APPIMAGE_EXTRACT_AND_RUN=1`, which the app notices
and writes into its own menu entry. The script prints the command to install
FUSE 2 for a faster start. If there's no display (SSH), it says to run the
AppImage once instead of starting it.

**Checks.** Before anything is installed:
- with a signed list on the release (`SHA256SUMS` + `SHA256SUMS.sig`;
  `packaging/sign-release.sh` makes them for a versioned release): the
  Ed25519 signature verifies with the update key (`openssl pkeyutl -rawin`,
  OpenSSL 3), the header is `CedarLogic preview manifest 1`, the `tag` line is
  the release's tag, and the file's size and SHA-256 are the list's;
- with no signed list: the SHA-256 GitHub's API records for the file
  (`digest`). With neither, it refuses.

The .deb is copied into a root-only folder, checked again as root, and apt
installs that copy (the same as `linux/install.sh`).

**Rate limits.** GitHub's API allows 60 unauthenticated requests an hour per
IP, and a classroom behind one address could use that up. When the API
refuses, the script reads the newest release's tag from
`github.com/<repo>/releases/latest` (a redirect, not the API) and takes the
file names from that release's signed list. That fallback needs the release
to be signed: another reason to run `sign-release.sh` for every release.

It's plain POSIX `sh`, because `curl ... | sh` on Debian and Ubuntu runs `dash`.

## Settings

Environment variables, for example `curl -fsSL .../install.sh | CEDARLOGIC_FORMAT=appimage sh`:

| Variable | What |
|---|---|
| `CEDARLOGIC_FORMAT=deb\|appimage` | instead of choosing by whether apt is there |
| `CEDARLOGIC_VERSION=0.4` | that release instead of the newest |
| `CEDARLOGIC_TAG=T`, `CEDARLOGIC_REPO=O/R` | a specific release tag (pre-releases too), another repo. The rolling test build: `CEDARLOGIC_REPO=leviholliday/CedarLogic CEDARLOGIC_TAG=linux-native-testing` |
| `CEDARLOGIC_DRY_RUN=1` | find, download and check, then stop |
| `CEDARLOGIC_NO_LAUNCH=1` | don't start it afterwards |
| `CEDARLOGIC_TEST_RELEASE`, `CEDARLOGIC_TEST_KEY` | the same test mode as `linux/install.sh` (a fake release over HTTP and a test key), so CI's test step can run this script unchanged |
| `CEDARLOGIC_NO_API=1` | skip the API (tests the rate-limited path) |

## Hosting it

The site should serve this file at **`https://cedarlogic.netlify.app/install.sh`**
(`Content-Type: text/plain; charset=utf-8`), copied in at release time like
`install.ps1`. Fallback URL once merged to `main`:
`https://raw.githubusercontent.com/leviholliday/CedarLogic/main/packaging/linux/install.sh`.

## linux/install.sh: the review

`linux/install.sh` (on `linux/native`; `scripts/sign-preview.sh` uploads it
to `linux-native-testing` at signing time) is the **preview** installer. It
stays as it is: testers use it, and it's what the rolling release carries.
Its signature handling is sound (signed list, tag line, size + SHA-256, root
re-check). The gaps for a public one-liner, all handled in this one:

1. **Bash only.** `set -euo pipefail` stops `dash` at once ("Illegal option
   -o pipefail"), so `curl ... | sh` fails on every Debian/Ubuntu; only
   `| bash`, as its comment says, works.
2. **Preview only.** It always installs from `linux-native-testing`, never a
   versioned release.
3. **Debian only.** Fedora and the rest get "use the AppImage" and nothing else.
4. **Root without sudo.** Run as root on a system with no `sudo` (a minimal
   Debian), it fails at the first `sudo`.
5. **apt not checked.** It looks for `dpkg` only.

If you'd rather have one script, this one can replace `linux/install.sh` on
`linux/native`: run as `CEDARLOGIC_REPO=leviholliday/CedarLogic
CEDARLOGIC_TAG=linux-native-testing`, it does what that one does, and it
accepts the same test variables. (Then `sign-preview.sh`, which uploads
`linux/install.sh` from the signed commit, carries on unchanged.)

## CI (not added)

Nothing here needs CI to make a release file, so no workflow was changed.
To test this script on Linux the way `linux/install.sh` is tested, add one
line to the "install.sh, against a signed fake release" step of
`linux-native.yml`, after the existing `bash linux/install.sh` lines:

```bash
CEDARLOGIC_NO_LAUNCH=1 sh packaging/linux/install.sh && cedarlogic --version && sudo apt-get remove -y cedarlogic
```

(The fake release is signed for `tag linux-native-testing`, which is what
test mode expects by default.)

## Tested (2026-10-31, on the Mac with `/bin/dash`, dry runs)

`shellcheck -s sh` and `-s dash`: clean. 18 cases, all passing:
- `CedarLogic-Releases` today: "no full release with a deb/appimage yet"
  (it has only Mac files), as expected.
- The signed `linux-native-testing` release, real key: deb amd64 and arm64,
  AppImage x86_64 and aarch64 all verify. armhf is refused with the
  64-bit-OS message.
- No API (`CEDARLOGIC_NO_API=1`): the file name comes from the signed list,
  and it verifies. On the unsigned releases repo it refuses.
- An old unsigned release (`linux-testing`): checked by GitHub's digest.
- Test mode against a local fake release signed by `sign-preview.sh --dry-run
  --test-key`: deb and AppImage pass. Refused, each one: the real key against
  the test signature, a changed .deb, a flipped signature bit, the wrong tag,
  an arch the release lacks, a missing `.sig`.

Not run here: the real `apt-get install` and the AppImage placement and
launch (they need Linux; the Docker daemon isn't running on this Mac). Try
them on the Zorin laptop and the Pi before the site links the command.
