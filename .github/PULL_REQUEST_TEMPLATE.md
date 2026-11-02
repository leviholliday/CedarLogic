<!-- Thanks for helping! Lines like this one are comments: they don't show up, so you can leave them. CONTRIBUTING.md has the build and check commands for each app. -->

## What and why

<!-- What does this change, and why? Link the issue if there is one (#123). -->

## Which apps

<!-- Tick what this touches. The engine (logic/, format/, src/gui, mac/CedarCore) reaches all three native apps. -->

- [ ] The shared engine
- [ ] Mac (`mac/`)
- [ ] Windows (`windows/`)
- [ ] Linux (`linux/`)
- [ ] Packaging, scripts or CI
- [ ] Docs only

## How I checked it

<!-- What you built and ran, and what you tried by hand. For example:
  - mac/build.sh, mac/Tools/build-tools.sh, cl_check and edit_check pass
  - opened the circuit from the issue, dragged the gate, the wire stays put
  No CI builds the Mac app yet, so say which Mac checks you ran.
  The Windows and Linux builds run only on windows/native-* and linux/native-* branches: link the green run. -->

## Before merging

- [ ] CI is green (and the Windows or Linux native run, if those apps are touched)
- [ ] Small commits, each with a message that says what and why
- [ ] Other apps that need the same change are noted above (or there are none)
- [ ] `.github/workflows/skia.yml` is untouched, or the owner agreed
