# Releasing the Mac version

## Version numbers

The version lives in one place: `project(CedarLogic VERSION x.y.z)` at the top
of `CMakeLists.txt` (also mirror it in `flake.nix`). It flows into the title bar,
the About window, and the app bundle.

This version started at **4.0.0**, a major version above upstream's 3.x, so
it always counts as newer than upstream builds.

- **Patch** (4.0.0 → 4.0.1): bug fixes only
- **Minor** (4.0.0 → 4.1.0): new features
- **Major** (4.x → 5.0.0): something that changes how people work, or breaks old files

## Making a build to share

```bash
scripts/package-mac.sh
```

This produces `build/CedarLogic-<version>-mac.dmg`. It is ad-hoc signed, not
notarized, so on another Mac the first launch needs right-click → Open.

## Updates

The app's updater (Help → Check for Updates…) is Sparkle. Its feed is set in
`CMakeLists.txt` (`CEDARLOGIC_APPCAST_URL`) and points at this version's own
feed, never upstream's. That keeps upstream's updater from swapping this app
for its own builds.

The updater is **off** until a build sets `SPARKLE_ED_PUBLIC_KEY`. To turn it
on when you are ready to share:

1. Create a **public** repo `leviholliday/CedarLogic-Releases`. The source can
   stay private; only the DMGs and `appcast.xml` go there.
2. Generate a signing key once with Sparkle's `bin/generate_keys` (in
   `build/_deps/sparkle-src`). Keep the private key in your Keychain.
3. Configure with the public key:
   `cmake -S . -B build -DSPARKLE_ED_PUBLIC_KEY=<key>`
4. For each release, build the DMG, sign it with `bin/sign_update`, upload it
   to a GitHub release in the releases repo, and add an item to `appcast.xml`
   there (`scripts/update-appcast.sh` writes the format).

The upstream GitHub release workflow no longer runs on tags here. It needs
upstream's Apple, Sparkle and npm secrets.
