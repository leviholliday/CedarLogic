# The Linux one-liner

```bash
curl -fsSL https://cedarlogic.netlify.app/install.sh | sh                  # the newest release
curl -fsSL https://cedarlogic.netlify.app/install.sh | sh -s -- --preview  # the signed rolling test build
```

**One script: `linux/install.sh`.** It is the release installer and the
test-build installer. `packaging/linux/install.sh` is only a symlink to it.
The path is the one CI and `scripts/sign-preview.sh` already use:
- `linux-native.yml` checks its public key against the app's, and runs it
  against a signed fake release (`CEDARLOGIC_TEST_RELEASE`/`_KEY`, `bash
  linux/install.sh`);
- `sign-preview.sh` takes `linux/install.sh` from the signed commit, signs it
  with the rest and uploads it to `linux-native-testing`.

So the link testers, the updater docs and today's site already use,
`curl -fsSL https://github.com/leviholliday/CedarLogic/releases/download/linux-native-testing/install.sh | bash`,
runs this same script.

## Which build it installs

| How it's run | Installs |
|---|---|
| no option (the site's command, and the old link) | the newest **full release** of `leviholliday/CedarLogic-Releases` with Linux files. **While there is none (before 0.4)**, it says so and installs the signed test build, exactly what the old link always did |
| `--preview` (or `CEDARLOGIC_CHANNEL=preview`) | always the signed test build, `linux-native-testing` in `leviholliday/CedarLogic` |
| `--version 0.4`, `--tag T`, `--repo O/R` | that release, with no fallback |

The fallback only happens in the plain no-option mode, and only to the
test build, which is always signed: an unsigned test build is refused ("hasn't
been signed yet"). Once v0.4 has its .debs and AppImages, the old link
installs 0.4 too. Its .deb then updates itself from the test build, as the
updater always does.

## What it installs where

| System | File | How |
|---|---|---|
| Debian, Ubuntu, Zorin, Mint, Pop!_OS (anything with `dpkg` + `apt-get`) | `cedarlogic_<v>_amd64.deb` (`cedarlogic_amd64.deb` on the test build) | `apt-get install` on a root-only copy, so apt pulls in GTK and the rest |
| Raspberry Pi 4/5, 64-bit Raspberry Pi OS | `..._arm64.deb` | same. The 32-bit OS (`armhf`) gets "use the 64-bit OS" |
| Fedora, openSUSE, Arch, anything without apt | `CedarLogic-Native-<v>-x86_64.AppImage` (or `-aarch64`) | to `~/Applications/CedarLogic.AppImage`, then started once, which adds it to the menu itself (`Updater_IntegrateAppImage`) |

The architecture comes from `dpkg --print-architecture` for the .deb (the
userland's, not the kernel's: a Pi 4 running the 32-bit OS has a 64-bit
kernel), and from `uname -m` for the AppImage. `--format appimage` forces
the AppImage on a Debian system. Without FUSE 2 (Fedora and Ubuntu 24.04
don't install it), the AppImage starts with `APPIMAGE_EXTRACT_AND_RUN=1`,
which the app notices and writes into its menu entry. The script prints the
command that installs FUSE 2. With no display (SSH), it says to run it once
instead of starting it. Running as root works without `sudo`.

**Checks**, before anything is installed:
- with a signed list on the release (`SHA256SUMS` + `.sig`; the test build
  always has one, and `packaging/sign-release.sh` makes one for a versioned
  release): the Ed25519 signature verifies with the update key (`openssl
  pkeyutl -rawin`, OpenSSL 3), the header is `CedarLogic preview manifest 1`,
  the `tag` line is the release's tag, and the file's size and SHA-256 are
  the list's;
- with no signed list (only for a versioned release, never the test build):
  the SHA-256 GitHub's API records for the file. With neither, it refuses.

The .deb is copied into a root-only folder, checked again as root, and apt
installs that copy, as before.

**Rate limits.** GitHub's API allows 60 unauthenticated requests an hour per
IP, and a classroom behind one address could use that up. When the API
refuses, the script takes the newest release's tag from
`github.com/<repo>/releases/latest` (a redirect, not the API) and the file
name from that release's signed list. If that release has no Linux files,
it falls back to the test build as above.

It's plain POSIX `sh`, because `curl ... | sh` on Debian and Ubuntu runs
`dash`. It also runs under `bash`.

## Options

After `sh -s --` when piped (`curl ... | sh -s -- --preview --format appimage`),
or as arguments (`sh linux/install.sh --dry-run`), or as environment variables:

| Option | Variable | What |
|---|---|---|
| `--preview` | `CEDARLOGIC_CHANNEL=preview` | the signed test build |
| `--format deb\|appimage` | `CEDARLOGIC_FORMAT` | instead of choosing by whether apt is there |
| `--version 0.4` | `CEDARLOGIC_VERSION` | that release instead of the newest |
| `--tag T`, `--repo O/R` | `CEDARLOGIC_TAG`, `CEDARLOGIC_REPO` | a specific release (pre-releases too), another repo |
| `--dry-run` | `CEDARLOGIC_DRY_RUN=1` | find, download and check, then stop |
| `--no-launch` | `CEDARLOGIC_NO_LAUNCH=1` | don't start it afterwards |
| | `CEDARLOGIC_TEST_RELEASE`, `CEDARLOGIC_TEST_KEY` | CI's test mode (a fake release over HTTP, a test key; expects `tag linux-native-testing` unless `CEDARLOGIC_TAG` says otherwise) |
| | `CEDARLOGIC_NO_API=1` | skip the API (tests the rate-limited path) |

## Hosting

The site serves a copy at **`https://cedarlogic.netlify.app/install.sh`**
(`Content-Type: text/plain; charset=utf-8`), refreshed at each release like
`install.ps1`. The copy on `linux-native-testing` keeps coming from
`sign-preview.sh`. Raw fallback once merged to `main`:
`https://raw.githubusercontent.com/leviholliday/CedarLogic/main/linux/install.sh`.

## Merging into linux/native

This branch (`pkg/installers`, from `v4/mac`) has `linux/install.sh` and
nothing else under `linux/`. Merging it into `linux/native` gives a conflict
on that one file (both sides changed it): take this branch's version
(`git checkout --theirs linux/install.sh` while merging into `linux/native`).
Nothing else in `linux/` is touched. After the merge, the existing CI step
("install.sh, against a signed fake release") tests this script unchanged,
and the next `sign-preview.sh linux` uploads it.

## What the old linux/install.sh lacked (now fixed)

1. **Bash only.** `set -euo pipefail` stops `dash` at once, so `curl ... | sh`
   failed on every Debian/Ubuntu; only `| bash` worked.
2. **Test build only.** It always installed `linux-native-testing`, never a
   versioned release.
3. **Debian only.** Fedora and the rest got "use the AppImage" and nothing else.
4. **Root without sudo.** As root on a system without `sudo`, it failed at the
   first `sudo`.
5. **apt not checked.** It looked for `dpkg` only.

Its signature handling (signed list, tag line, size and SHA-256, root
re-check) is kept as it was.

## Tested (2026-10-31, on the Mac, dry runs)

`shellcheck -s sh` and `-s dash`: clean; `dash -n` and `bash -n`: parse.
28 cases, all passing (`/bin/dash` unless noted):
- **No option**: the releases repo has no Linux files yet, so it falls back
  to the signed test build, which verifies (deb and AppImage). The same with
  no API, where the latest release is Mac-only. `--repo` set to the releases
  repo, or `--version 0.3.4`: refused, with no fallback.
- **Test build**: env vars, `CEDARLOGIC_CHANNEL=preview`, `--preview` as an
  argument, and piped `cat install.sh | sh -s -- --preview`. deb amd64 and
  arm64, AppImage x86_64 and aarch64 verify against the real key. armhf
  refused. No-API path verifies. Unknown option refused.
- **The old link's way**: `bash linux/install.sh` and piped `| dash` with no
  arguments. Both install the test build, verified.
- **Unsigned release** (`linux-testing`): checked by GitHub's digest.
- **CI's test mode**: a local fake release signed by `sign-preview.sh
  --dry-run --test-key`. deb and AppImage pass, under dash and bash. Refused:
  the real key against the test signature, a changed .deb, a flipped
  signature bit, the wrong tag, an arch the release lacks, a missing `.sig`.
- The public key block extracts the way CI extracts it and equals `PUBHEX`.

Not run here: the real `apt-get install` and the AppImage placement and
launch (they need Linux; Docker isn't running on this Mac). CI's existing
step covers the apt install once this is on `linux/native`. Try the AppImage
on Fedora and the .deb on the Pi before the site links the command.
