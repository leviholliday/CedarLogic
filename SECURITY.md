# Security

## Reporting a problem

If you find a security problem in CedarLogic, please tell us privately first.

- **Best:** email me at cedarlogic@proton.me
- **Another Option:** open a private report on GitHub: [Report a vulnerability](https://github.com/leviholliday/CedarLogic/security/advisories/new) (the repository's **Security** tab, then **Report a vulnerability**). Only the maintainer sees it.
- **If it isn't sensitive** (for example, an out-of-date library or a hardening idea), a normal issue is fine. The website's [`security.txt`](https://cedarlogic.netlify.app/.well-known/security.txt) points to the issues of [leviholliday/CedarLogic-Releases](https://github.com/leviholliday/CedarLogic-Releases/issues); issues on this repository work too.

Please don't put details of an unfixed problem in a public issue, a feedback report or a pull request.

Tell us what you found, which app or page and which version, and the steps to see it. A small `.cdl` file or a request that shows it helps a lot.

## In scope

- **The apps:** CedarLogic for Mac, Windows and Linux (the folders `mac/`, `windows/`, `linux/` and the shared engine they run).
- **Updates and installers:** the update checks in each app, `update-signing/`, `scripts/sign-preview.sh`, `packaging/` (the Homebrew cask, winget manifests, `install.ps1`, `linux/install.sh`).
- **The sync server:** the `/api/sync/` service on cedarlogic.netlify.app that stores Your Circuits for Sync.
- **The website:** cedarlogic.netlify.app, including CedarLogic Online and the feedback service (`/api/feedback`).
- **Classroom:** the class service the apps and the website talk to (`cedarlogic-classroom.leviholliday7.workers.dev`).
- **Opening files:** a `.cdl` file or a pasted circuit that crashes an app, hangs it, or reaches outside the circuit (for example, reading or writing other files).

## Not in scope

- The legacy wxWidgets app (CedarLogic Classic 4.0.x). It is retired. Tell us anyway if it affects the shared engine.
- Problems in GitHub, Netlify, Cloudflare or other services themselves.
- Reports that only say a page lacks a header, with no way to misuse it.
- Flooding a service with traffic.

## How updates are signed

The apps check a signature before they install an update. The Mac app uses Sparkle 2, which checks each update's EdDSA signature against the public key built into the app. The Windows and Linux apps trust only files listed in a signed manifest: after CI publishes a build, `scripts/sign-preview.sh` (or `packaging/sign-release.sh` for a versioned release) checks the release against the CI run that made it, writes `SHA256SUMS` (the tag, the commit, a build number and each file's size and SHA-256) and signs it with an Ed25519 key that lives only in the owner's Keychain, never on GitHub. The apps check that signature with Monocypher (`update-signing/`) against the public key built into them, then check the tag, the commit, that the build is newer than the one installed, and the downloaded file's size and SHA-256. If anything doesn't match, no update is offered. `update-signing/UpdateSignature.h` has the details. The one-line installers (`linux/install.sh`, `install.ps1`) check the same signed list when the release has one, and otherwise the SHA-256 GitHub records for the file; a signed list that doesn't verify stops the install.
