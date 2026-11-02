# CedarLogic repo tidy plan (phase 3 of 0.4)

Read-only survey, 2026-10-31. Nothing in this document has been executed: no branch was deleted, no tag created, no PR opened or closed. Everything below was measured from `private` (github.com/leviholliday/CedarLogic) as fetched on 2026-10-31, from the CI logs of the latest green runs, and from `git merge-tree` simulations. The three finals are:

| Final | Tip | Date |
|---|---|---|
| `v4/mac` (Mac app, shared engine, packaging, docs) | `5e08852` | 2026-10-31 |
| `windows/native-v4-int3` | `56ccf0e` | 2026-10-31 |
| `linux/native-v4-int3` | `53b67db` | 2026-10-31 |

## 0. Read this first

1. **`private` has no `master`. Its default branch is `main`.** `master` exists only on `origin` (taciturnaxolotl/CedarLogic, the upstream wxWidgets app, never to be pushed to). `private/main` is the old wx-based Mac fork: it is 1 commit ahead of `v4/mac`'s base (`4ca1a2c`, a README edit) and 315 commits behind `v4/mac`. The workflows still say `master` (`ci.yml:6`, `nix.yml:14`, `release.yml:517`, `skia-app.yml:50`) because they were inherited from upstream, so today `ci.yml` never runs on a push to `main`. The install one-liners in `packaging/*/README.md` fetch from `.../CedarLogic/main/...`. **Recommendation: the tidy targets `main`** and the four `master` words in the workflows become `main`. Creating a new `master` on `private` would break those URLs and leave two default-ish branches. Needs your decision before PR 1.
2. **The three finals merge into each other with zero textual conflicts**, in either order (simulated with `git merge-tree`; result tree `828c7c9`). `windows/` of the result is byte-identical to the Windows final's, `linux/` to the Linux final's, `mac/` to the Mac final's, and `logic/ format/ src/ include/ tests/ tools/ cmake/ wasm/ packaging/` are identical in all three, so the shared engine has not forked. The one semantic collision is `res/templates/`, which both platforms fill with the same 50 templates under two different naming schemes (section 2.3).
3. **`origin/master` is 114 commits ahead of the fork point** (Kieran's wx app, v3.2.2 to v3.4.2, up to 2026-10-29) and touches 37 shared-engine files (2,210 lines), 28 of which `v4/mac` also changed. Merging it for real gives 29 conflicted files. Keep it out of the tidy; treat it as a separate "upstream engine sync" job (section 2.2).
4. **Branch hygiene is mostly safe**: 53 of 66 remote branches are fully contained in a final (or have only patch-equivalent commits) and can be archive-tagged and deleted once the three PRs land. Two names must survive until publishing moves (`windows/native`, `linux/native`). Seven branches hold work that is in no final and should be merged or consciously dropped first.
5. **There is unpushed work only in this clone** (`pub/v4-mac` 5 commits, `tools/demo-mode` 6, `rel/mac-0.4` 6, a few older `claude/*`). Push or archive it before cleaning anything (section 1.3).

## Summary numbers

| Item | Number |
|---|---|
| Remote branches on `private` | 66 (65 + `main`) |
| Keep (3 finals, `main`, `windows/native`, `linux/native`) | 6 |
| Needs a look (unmerged work) | 7 |
| Delete after `archive/<name>` tag | 53 (47 fully merged, 5 superseded old wx ports with unique commits, 1 patch-equivalent) |
| Local branches without a remote counterpart / worktrees | about 120 of about 170 / 83 and growing (every `worktree-agent-*` branch, 33+, has no commits of its own) |
| PRs to land | 3 required (engine+Mac, Windows, Linux) + 1 fix-up PR, 2 optional |
| Dead files (used by nothing) | about 88 KB in 24 files, plus about 2.7 MB of byte-identical duplicates (0.5 MB more once the two template sets meet) |
| Legacy wx app (built only by the root CMake and 6 workflows) | about 3.8 MB, 66 source files, 20k lines of C++ |
| Unpinned `uses:` lines / workflows without `permissions:` | 55 in 7 workflows / 3 (they are the 11 open code-scanning alerts) |
| Open PRs / issues on `private` | 1 (#1, stale, conflicting) / 0 |


## 1. Branch inventory of `private`

`private` has **66 branches**: **6 keep**, **7 need a look**, **53 delete after tagging**. Tips and merge state are as of 2026-10-31 (`git fetch private`). "Merged" means the tip is an ancestor of the named final (`git merge-base --is-ancestor`); "unique" is the number of commits reachable from the branch but from none of the three finals.

Tag recipe for every DELETE row (run only after the three finals are in `main`, section 2): `git tag archive/<name> private/<name> && git push private archive/<name>` and only then `git push private --delete <name>`. Nothing in any workflow triggers on a tag push (`release.yml` is `workflow_dispatch` only), so archive tags are inert.

### 1.1 What CI publishes from, and so must NOT be deleted or renamed

| Branch (exact name) | Why | Where it is hard-coded |
|---|---|---|
| `windows/native` | The `publish` job runs only on this ref and replaces the `windows-native-testing` pre-release that installed Windows apps update from | `.github/workflows/windows-native.yml:776` (`if: github.ref == 'refs/heads/windows/native'`); `scripts/sign-preview.sh:228` (`BRANCH=windows/native`) and `:268-276` (refuses to sign unless the branch tip equals the release's tag); `packaging/sign-release.sh:91-92` (`git show private/windows/native:scripts/sign-preview.sh`) |
| `linux/native` | Same for `linux-native-testing` | `.github/workflows/linux-native.yml:593`; `scripts/sign-preview.sh:223` |
| `main` | The repo default, and the install one-liners fetch from it | `packaging/README.md:63`, `packaging/linux/README.md:101`, `packaging/windows/README.md:49` (`raw.githubusercontent.com/leviholliday/CedarLogic/main/...`), winget `LicenseUrl` (`.../blob/main/LICENSE`) |
| `windows/native-**`, `linux/native-**` | Not required, but a push to any branch of this name starts the full native build (no publish): `windows-native.yml:13-15`, `linux-native.yml:11-13` | n/a |
| `windows/**` except `windows/native*` | A push starts the legacy wx "Windows build" (`windows.yml`, triggers `'windows/**'`, `'!windows/native'`, `'!windows/native-**'`) | n/a |

Rolling release tags that updaters depend on (all exist on `private` already, none may be moved or deleted): `windows-native-testing` (0baf7c0 = tip of `windows/native`), `linux-native-testing` (12f3944 = tip of `linux/native`), `windows-native-design-1`, `linux-testing` (01de922 = tip of `linux/port`), `v4.0.0-beta.1` (a8bc39f), `before-overnight-linux-native`, `before-overnight-windows-native`.
Three of those tags (`linux-native-testing`, `linux-testing`, `windows-native-testing`) are **out of date in this clone** (`git fetch` refuses to clobber them). Do not run `git push --tags` or `--force` from here: it would move the release channels. `git fetch private --tags --force` fixes the clone.

### 1.2 Remote branches by group


#### `main`

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `main` | 4ca1a2c | 2026-09-30 | none | 1 | **KEEP** | default branch; install one-liners and README links point at it |

#### `v4/*`  (4: 1 keep, 0 look, 3 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `v4/mac-app` | f00a63a | 2026-10-20 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `v4/mac-core` | e307059 | 2026-10-16 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `v4/mac-sweep` | 5716748 | 2026-10-26 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `v4/mac` | 5e08852 | 2026-10-31 | mac | 0 | **KEEP** | FINAL: Mac app + engine + packaging + docs (PR 1) |

#### `windows/*`  (19: 2 keep, 1 look, 16 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `windows/feedback` | 5acd906 | 2026-09-29 | none | 23 | **DELETE (tag first)** | 23 commits of the superseded wx-based port; the v4.0.0-beta.1 / linux-testing tags already pin part of it; archive tag keeps all |
| `windows/native-04` | a621793 | 2026-10-05 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-channel` | 59a3ccc | 2026-10-31 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-features` | deb1a96 | 2026-10-05 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-pair` | a2c792c | 2026-10-05 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-polish` | 47b6656 | 2026-10-31 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-v4` | 89d397b | 2026-10-16 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-v4-core` | 98e8c3d | 2026-10-16 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-v4-int` | c10511d | 2026-10-19 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-v4-int2` | b37c917 | 2026-10-20 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-v4-pin` | cbedc20 | 2026-10-27 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-v4-session` | cbedc20 | 2026-10-27 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-v4-signed` | 0baf7c0 | 2026-10-27 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/native-v4-ui` | 1899368 | 2026-10-19 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `windows/phase1` | 4709f91 | 2026-09-30 | none | 24 | **DELETE (tag first)** | 24 commits of the superseded wx-based port; the v4.0.0-beta.1 / linux-testing tags already pin part of it; archive tag keeps all |
| `windows/port` | 7a33996 | 2026-09-24 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `windows/native` | 0baf7c0 | 2026-10-27 | win | 0 | **KEEP** | CI publishes windows-native-testing from this exact name (windows-native.yml:776; sign-preview.sh:228) |
| `windows/native-v4-int3` | 56ccf0e | 2026-10-31 | win | 0 | **KEEP** | FINAL: Windows (PR 2) |
| `windows/pc-test` | 223a17f | 2026-10-29 | none | 2 | **LOOK** | 2 commits, clean into Windows final: adds windows/PC-TEST.md (186 lines); ran 'Windows build' CI green 2026-10-29 |

#### `linux/*`  (20: 2 keep, 2 look, 16 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `linux/feedback` | 393b789 | 2026-09-29 | none | 39 | **DELETE (tag first)** | 39 commits of the superseded wx-based port; the v4.0.0-beta.1 / linux-testing tags already pin part of it; archive tag keeps all |
| `linux/native-04` | 0729b9b | 2026-10-05 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-channel` | 7ecee4c | 2026-10-31 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-features` | bbdf875 | 2026-10-05 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-pair` | d5da9bc | 2026-10-05 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-polish` | d562e4b | 2026-10-31 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-v4` | d25890e | 2026-10-16 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-v4-core` | 5666fe7 | 2026-10-16 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-v4-int` | a588310 | 2026-10-19 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-v4-int2` | d3b2513 | 2026-10-20 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-v4-perf` | 7904751 | 2026-10-27 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-v4-pin` | 7904751 | 2026-10-27 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-v4-session` | fb458cb | 2026-10-26 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-v4-signed` | 12f3944 | 2026-10-28 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/native-v4-ui` | 212344b | 2026-10-16 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `linux/port` | 01de922 | 2026-09-30 | none | 40 | **DELETE (tag first)** | 40 commits of the superseded wx-based port; the v4.0.0-beta.1 / linux-testing tags already pin part of it; archive tag keeps all |
| `linux/native` | 12f3944 | 2026-10-28 | lin | 0 | **KEEP** | CI publishes linux-native-testing from this exact name (linux-native.yml:593; sign-preview.sh:223) |
| `linux/native-v4-int3` | 53b67db | 2026-10-31 | lin | 0 | **KEEP** | FINAL: Linux (PR 3) |
| `linux/native-v4-desktop` | f5fb814 | 2026-10-27 | none | 8 | **LOOK** | 8 commits; conflicts in Canvas.cpp/TabSwitcher.cpp with Linux final, which re-did A/C-key and Ctrl+Tab work itself (c5d4155, 011095b). Probably superseded; only the DESKTOP-TEST.md notes may be worth taking |
| `linux/native-v4-simbench` | 9280d2e | 2026-10-27 | none | 2 | **LOOK** | 2 commits, clean into Linux final: bench rows with the simulation running (linux/App/Bench.cpp) + SMOOTHNESS.md numbers |

#### `mac/*`  (4: 0 keep, 0 look, 4 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `mac/feedback-cap` | 1f7b5bd | 2026-10-31 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `mac/native` | 5029f4c | 2026-10-09 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `mac/parity-gaps` | 7fa7dff | 2026-10-31 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `mac/whats-new-0.4` | 64b2520 | 2026-10-31 | mac | 0 | **DELETE (tag first)** | fully merged (v4/mac) |

#### `ui/*`  (6: 0 keep, 0 look, 6 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `ui/linux-channel` | 48c5ba9 | 2026-10-31 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `ui/linux-menubar` | 46d32ba | 2026-10-28 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `ui/linux-polish` | d562e4b | 2026-10-31 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `ui/windows-channel` | 59a3ccc | 2026-10-31 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `ui/windows-menubar` | 54f1236 | 2026-10-28 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |
| `ui/windows-polish` | 47b6656 | 2026-10-31 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |

#### `sec/*`  (3: 0 keep, 0 look, 3 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `sec/linux-apps` | a3c261c | 2026-10-28 | lin | 0 | **DELETE (tag first)** | fully merged (linux int3) |
| `sec/mac-apps` | 25769f8 | 2026-10-28 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `sec/windows-apps` | 8901759 | 2026-10-28 | win | 0 | **DELETE (tag first)** | fully merged (windows int3) |

#### `core/*`  (1: 0 keep, 0 look, 1 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `core/hardening` | ca3237b | 2026-10-31 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |

#### `pkg/*`  (2: 0 keep, 1 look, 1 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `pkg/installers` | 3eb9899 | 2026-10-31 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `pkg/tap-name` | c8a9a82 | 2026-10-31 | none | 1 | **LOOK** | 1 commit, clean merge into v4/mac: Homebrew cask now in leviholliday/homebrew-tap (packaging/homebrew/*). Also local pub/v4-mac has a merge of it plus 4 more unpushed commits. |

#### `docs/*`  (3: 0 keep, 2 look, 1 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `docs/parity-2026-10-31` | 5f1292f | 2026-10-31 | all 3 | 0 | **DELETE (tag first)** | fully merged (v4/mac,windows int3,linux int3) |
| `docs/soak-2026-10-31` | aff72b9 | 2026-10-31 | none | 1 | **LOOK** | 1 commit, clean: adds docs/SOAK-2026-10-31.md (a dated log; merge or drop) |
| `docs/whats-new-0.4` | f08c448 | 2026-10-31 | none | 1 | **LOOK** | 1 commit, clean: adds docs/RELEASE-0.4-notes.md (246 lines) |

#### `tools/*`  (1: 0 keep, 1 look, 0 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `tools/signing-keys` | b4b1406 | 2026-10-28 | none | 2 | **LOOK** | 2 commits: adds scripts/signing-keys.sh (135 lines). Clean into Windows final, conflicts scripts/sign-preview.sh against Linux final |

#### `claude/*`  (1: 0 keep, 0 look, 1 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `claude/quirky-keller-1jc29u` | 52f960a | 2026-09-28 | none | 31 | **DELETE (tag first)** | 31 commits of the superseded wx-based port; the v4.0.0-beta.1 / linux-testing tags already pin part of it; archive tag keeps all |

#### `codex/*`  (1: 0 keep, 0 look, 1 delete)

| Branch | Tip | Date | Merged into | Unique | Recommendation | Note |
|---|---|---|---|---|---|---|
| `codex/stability-fixes` | 4a5fa0c | 2026-09-23 | none | 1 | **DELETE (tag first)** | its 1 commit has a patch-equivalent in a final |

There are no `rel/*`, `cls/*`, `worktree-agent-*`, `beta/*`, `sync/*`, `pair/*`, `w5/*` branches on `private`; those exist only in this machine's clone (next section).

### 1.3 Branches that exist only in the local clone

The local repository had **172 branches and 83 worktrees** when this was measured (agents add more while they run); 123 branches have no counterpart on `private`. None of them is protected by a tag or a push, so they are the only copies of whatever they hold.

**Rescue first (unpushed commits that are in no final):**

| Local branch | Unique commits | What is in them | Suggested action |
|---|---|---|---|
| `pub/v4-mac` (cab7813, in a worktree) | 5 | merge of `pkg/tap-name`, "packaging: the publisher is CedarLogic, not Cedarville University", "Mac: copyright line names CedarLogic as the publisher", "docs: parity matrix re-checked on the 0.4 int3 tips" | Push as a branch and fold into PR 1; these look like release-blocking edits |
| `tools/demo-mode` (918fb15, in a worktree) | 6 | Demo mode for screen recordings (half adder, key badges) | Decide: merge into `main` behind its flag, or archive |
| `rel/mac-0.4` (72dc9ef) | 6 | 0.4 What's New tour, release steps and notes, quit fix | Compare with `mac/whats-new-0.4` + `docs/whats-new-0.4` (both on `private`); probably superseded, archive |
| `claude/flip-flop-components-templates-973400` (8738cee) | 6 (5 shared with `rel/mac-0.4`) | "Templates: every flip-flop and latch, ready to run" | Check that `res/templates/*ff*` and `mac/App/Templates*.swift` in the finals already carry it, then archive |
| `claude/beta-updates` (3168fe5) and `claude/beta-updates-linux` (47127a2) | 19 and 9 | wx-era "Normal/Beta tester" updater setting, "Version 4.0.1" | Superseded by the native updaters (`sign-preview.sh` testing groups); archive |
| `claude/heuristic-jackson-eda769` (09c9abf) | 1 | "Put back wires attached to a moved part when a move is cancelled" | An engine fix: check whether the finals' `src/gui/command/cmdMoveGate`/`cmdMoveWire` already do this; else port |

Everything else local-only is fully contained in a final, or its only unique commits are patch-equivalent to final commits. Local-only counts: `worktree-agent-*` 33, `claude/*` 16, `cls/*` 11, `rel/*` 9, `f/*` 8, `ov*/` 14, `sync/*` 4, `pair/*` 4, `w5/*` 4, plus about 20 unprefixed or one-off names (`queue-mac`, `more-examples`, `pagelinks`, `straighten-*`, `classroom-*`, `menu-export-mac`, ...).

Specifics:

- **`worktree-agent-*` (33 branches)**: every one sits on a commit of upstream history (`d36179c`, `c096da3`, `b4b8d2f`, ...) with **zero commits of its own** (`git rev-list --count <b> --not origin/master` is 0 for all 33). Remove the worktree (`git worktree remove`), then `git branch -D`. Do this last: this plan was written in worktree `a6c19a86...`, now on branch `docs/tidy-plan`; remove it only after the plan is pushed.
- **`cls/*` (11)**, **`rel/*` (9, except `rel/mac-0.4`)**, **`beta/mac-0.3.11`**, **`sync/*`, `pair/*`, `w5/*`, `ov*/`, `f/*`, `queue-*`, `more-examples`, `straighten-*`, `pagelinks`, `menu-export-mac`, `classroom-*`**: all fully merged into a final. Tag locally (`archive/<name>`), push the tags if you want them on GitHub (they cost nothing), delete.
- **Local `v4/mac` (163affe) is 66 commits behind `private/v4/mac`** and has no upstream set. Anyone building from it builds an old app; reset it to the remote or delete it.
- Local `master` is `origin/master` (upstream's wx app, d36179c). It is not a `private` branch.
- Run `git worktree prune` after removing the agent worktrees.


## 2. How `main` / `master` relate to the finals, and the path to one tree

### 2.1 The branches side by side

| Ref | Tip | What it is | Ahead of `v4/mac` | Behind `v4/mac` |
|---|---|---|---|---|
| `origin/master` | `d36179c` (2026-10-29) | Upstream wxWidgets app, v3.4.2, Kieran Klukas. Root CMake app only: no `mac/`, `windows/`, `linux/`, `packaging/`, `tests/` | 114 | 340 |
| `private/main` | `4ca1a2c` (2026-09-30) | The wx-based "CedarLogic 4.0.x for Mac" fork: upstream at v3.2.1 plus 26 commits, the last one a README edit. Same top-level folders as `origin/master` | 1 | 315 |
| `private/v4/mac` | `5e08852` | Native Mac app + shared engine + packaging + docs. Keeps the whole wx tree too | 0 | 0 |
| `private/windows/native-v4-int3` | `56ccf0e` | Final Windows. Merged `v4/mac` at `ecbd547`, not the current tip | 397 | 2 |
| `private/linux/native-v4-int3` | `53b67db` | Final Linux. Same base `ecbd547` | 385 | 2 |

`git merge-base` of all of `v4/mac`, `windows/native-v4-int3`, `linux/native-v4-int3` is `ecbd547`; of `v4/mac` and `main` is `dd0ac21` (2026-09-23); of `v4/mac` and `origin/master` is `9d18536`.

So: **yes, `master` (and `private/main`) is the old wx app**. `main` is a strict ancestor of `v4/mac` except for one README commit, so PR 1 is as close to a fast-forward as it gets: one conflict, `README.md`.

### 2.2 What is on `origin/master` and not on `v4/mac`

114 commits since the fork point (`9d18536`, v3.2.1, 2026-09-21), 111 files, +6,734/-2,336 lines: 107 by Kieran (v3.2.2 through v3.4.2) and 7 appcast bot commits. Nothing here is Levi's. By kind:

- **Shared-engine files: 37 files, 2,210 lines** (`src/gui/guiWire.cpp` 698, `CircuitParse.cpp` 344, `logic/tests/test_circuit.cpp` 283, `test_fsm_junctions.cpp` 204, `cmdDeleteGate.cpp` 71, `guiGate.cpp` 55, `logic_gate_sequential.cpp` 47, ...). These are real bug fixes ("wires to a turned gate no longer vanish when connected", "stop deleting a gate from crashing", "a pin claimed by two wires no longer flips drivers on reopen", ...). **28 of the 37 files were also changed on `v4/mac`**.
- wx-only `src/` and `include/` files: 61 files, 5,909 lines. Not wanted by the native apps.
- CI, release, docs: 13 files.

`git merge-tree v4/mac origin/master` reports **29 conflicted files**: 11 engine files (`logic_gate_memory.cpp`, `CircuitParse.cpp`, `guiGate.cpp`, `guiWire.cpp`, `klsCollisionChecker.cpp`, `gl_defs.cpp`, `cmdMoveWire.cpp`, `cmdSetParams.cpp`, `guiWire.h`, `wireSegment.h`, `gl_defs.h`) and 18 wx/build files (`CMakeLists.txt`, `flake.nix`, `MainFrame.*`, `GUICanvas.cpp`, `MainApp.*`, `SettingsDialog.*`, ...).

Recommendation: **do not merge `origin/master` as part of the tidy.** It would drag 29 hand conflicts into the review of PR 1 and change engine behaviour under the Windows/Linux apps that have just passed their long session checks. Instead open a separate job after the tidy: replay the 37 engine-file fixes onto `main` one at a time with their tests (the new `logic/tests/*` files come along for free) and let `core-hardening.yml` judge each. Never push to `origin`.

### 2.3 What the merged tree looks like (simulated, not pushed)

`git merge-tree --write-tree` of `v4/mac` + `windows/native-v4-int3` + `linux/native-v4-int3`, in both orders: **0 conflicts**. Result tree `828c7c9`: 35 MB, `windows/` equals the Windows final's, `linux/` the Linux final's, `mac/` the Mac final's.

The three finals changed 3 (Mac), 265 (Windows) and 257 (Linux) files since `ecbd547`; 178 paths are touched by both Windows and Linux, but only because each final carries an older copy of the other platform's directory (Windows has 86 stale `linux/` files, Linux has 79 stale `windows/` files), and the merge keeps the newer owner's version. The Mac side overlaps neither (0 shared paths).

Hotspots found, in order of risk:

1. **`res/templates/` is filled twice.** Windows ships `builtin-*.cdl` (50 files) + `templates.txt` (written by `windows/make-templates.py`, read by `windows/App/Collections.cpp:208`). Linux ships `template-builtin-*.cdl` (50) + `index.tsv` (read by `linux/App/Collections.cpp:243`). The `.cdl` pairs are byte-identical apart from the file name. In the merged tree both sets sit in one folder (about 1 MB; 0.5 MB wasted). Each app keeps working, but `windows-native.yml:72` (`for f in res/templates/*.cdl; do cp "$f" "tpl/template-$(basename "$f")"`) would also copy Linux's already-prefixed files, as `template-template-builtin-*.cdl`, into the folder the clock/register/mistakes/examples checks scan (`:73-77`). I did not run it; a surprise in the Windows checks after PR 3 is possible and the fix in PR 4 by moving to a single naming scheme (Linux's `template-builtin-*` + `index.tsv`, since the Windows CI already prefixes with `template-` and the Linux CI refers to `res/templates/template-builtin-count-3bit.cdl` at `linux-native.yml:305`) and having `make-templates.py` write it.
2. **Workflow files exist twice with different content.** `windows-native.yml` is 857 lines on the Windows final and 406 on the Linux final; `linux-native.yml` is 251 on the Windows final and 666 on the Linux final. A textual merge takes the right one (verified: the result equals the owning final's file). Hand-resolving in a PR UI would not be safe; do not.
3. **Stale cross copies between PR 2 and PR 3**: after PR 2, `main` carries Windows's older `linux/` (86 files) until PR 3 replaces it. Harmless (the Linux workflows only fire on `linux/native*` branches) but do not cut a Linux build from `main` in between.
4. **Two files a branch needs from outside its folder**: `scripts/sign-preview.sh` (identical on both finals, but `tools/signing-keys` conflicts with the Linux copy: it touches that file), `update-signing/*` (9 files, identical on both).
5. **Triggers keyed to branch names** (section 1.1): `windows-native.yml`/`linux-native.yml` have no `pull_request` trigger, so none of the Windows or Linux build/check/session jobs will run for PR 2 or PR 3 or for `main` itself unless a trigger is added. Add it in the PRs, not afterwards.
6. **Nothing builds the Mac app in CI.** `mac/build.sh` (swiftc) is not referenced by any workflow; `core-hardening.yml` only builds the headless engine. The Swift app is verified by hand. A `macos-14` job running `mac/build.sh` is the missing safety net for PR 1.
7. **`core-hardening.yml` triggers on every push of every branch** (`on: push:` with no filter). Narrow to `main` + `pull_request`.

### 2.4 Proposed layout of `main`

Stage A (what PRs 1 to 3 produce, no file moves):

```
/                       README.md (repo map, section 3.1), LICENSE
mac/                    Native Mac app: Swift/AppKit in App/, build.sh, sign/DMG scripts
  CedarCore/            the C++ engine glue and C API the three native apps call (63 files)
  Tools/                headless checks, fuzzers, generators the three CIs run (80 files)
windows/                Win32 + Direct2D app, Inno Setup installer
linux/                  GTK3 app, AppImage/deb scripts, install.sh
logic/                  gate/wire/event simulator (no UI)
format/                 .cdl reader/writer, legacy reader, migrations
src/gui, include/gui    circuit model, routing, edit commands (engine) and the wx UI (legacy)
update-signing/         Ed25519 verification of signed updates (monocypher)
packaging/              Homebrew cask, winget manifests, install.ps1, install.sh link, sign-release.sh
scripts/                package-mac.sh, update-appcast.sh, sign-preview.sh, DMG background
res/                    gate library (cl_gatedefs.xml), samples, templates, help, icons
tests/, tools/, wasm/, docs/, .github/
CMakeLists.txt cmake/ flake.nix Taskfile.yaml Doxyfile ...   the legacy wx build
```

Stage C (optional PR 5, pure `git mv` plus path edits): move the shared engine out of `mac/`, because Windows and Linux compile and run it too (`windows/CMakeLists.txt:32` and `linux/CMakeLists.txt:36` glob `mac/CedarCore/*.cpp`; both CMake files add `mac/Tools/<tool>.cpp`):

```
core/CedarCore  (from mac/CedarCore)     35 files / 77 lines mention the old path
core/Tools      (from mac/Tools)         91 files / 196 lines mention the old path
```

The 35 + 91 files include 3 workflows, 9 docs, `mac/build.sh`, both platform CMake files and ~60 scripts/tools. It is mechanical but touches every CI, so do it alone, after the three platform PRs, with all three native workflows required green. Call it low value, medium risk: skip it unless the name `mac/CedarCore` bothers you.

Stage D (optional, owner decision): fence or retire the wx app (section 3.3).

### 2.5 PR sequence

Use **merge commits** for all PRs (not squash, not rebase) and do not tick "delete branch on merge": the tag-then-delete routine in 2.6 relies on every branch tip being an ancestor of `main`.

**Pre-flight (no PR)**
- Decide `main` vs `master` (section 0.1).
- Push or archive the unpushed local work (section 1.3). `pub/v4-mac` in particular holds the "publisher is CedarLogic, not Cedarville University" edits.
- `git fetch private --tags --force`.
- Close PR #1 (section 5).

**PR 1: `v4/mac` -> `main`** (engine + Mac + packaging + docs; 315 commits; 1 conflict, `README.md`)
1. Fold into `v4/mac` first (small PRs into it, all verified clean by `merge-tree`): `pkg/tap-name` (3 files), `docs/whats-new-0.4` (+246 lines), optionally `docs/soak-2026-10-31`.
2. Same branch, one commit: `master` -> `main` at `ci.yml:6`, `nix.yml:14`, `release.yml:517`, `skia-app.yml:50`; `permissions: contents: read` on `ci.yml`, `core-hardening.yml`, `release.yml` (section 4.6); narrow `core-hardening.yml` triggers.
3. Resolve `README.md` by writing the repo map (section 3.1) rather than choosing a side; `main`'s version leads with Mac downloads and the website, keep that paragraph.
4. Checks that should be green: `CI` (this is the wx build on Linux and Windows plus wasm; its first run needs Skia built by `skia.yml`, 20 to 40 minutes cold per the comment at the top of `skia.yml`), `Core hardening`, `Nix`. Add the macOS build job if you want PR 1 to prove the Swift app.
5. After merge: `git push private main` is the only push. Tag nothing yet.

**PR 2: `windows/native-v4-int3` -> `main`** (397 commits; clean)
1. Fold in `windows/pc-test` (adds `windows/PC-TEST.md`) and `tools/signing-keys` (adds `scripts/signing-keys.sh`; both clean into this branch). Do not fold `signing-keys` into the Linux PR (it conflicts with the Linux copy of `scripts/sign-preview.sh`).
2. In `windows-native.yml` add `pull_request` and `push: branches: [main]` to `on:` so the build/check/session/installer jobs run for the PR and for `main`. Leave the publish `if` (`:776`) pointing at `windows/native` for now.
3. Expect the stale `linux/` copy (see 2.3.3).

**PR 3: `linux/native-v4-int3` -> `main`** (385 commits; clean after PR 2)
1. Fold in `linux/native-v4-simbench` (clean; adds `Bench.cpp` rows and `SMOOTHNESS.md`). Look at `linux/native-v4-desktop` first: its 8 commits conflict in `Canvas.cpp` and `TabSwitcher.cpp` because the final redid the same behaviour (`c5d4155` "C connects a moving part wherever the keyboard is", `011095b` "Ctrl+Tab's panel looks like the Mac's"); most likely take only `linux/DESKTOP-TEST.md`.
2. Same trigger change in `linux-native.yml`; publish `if` (`:593`) stays on `linux/native`.
3. In this PR or immediately after: resolve `res/templates` (2.3.1) and watch `windows-native.yml:72`.

**PR 4: post-merge fix-up** (small, reviewed alone)
- One template naming scheme.
- Publish from `main`: `windows-native.yml:776` and `linux-native.yml:593` to `refs/heads/main` (+ `push: branches: [main]`), `scripts/sign-preview.sh:223,228` `BRANCH=main`, `packaging/sign-release.sh:91-92`, the `Updater` comments that name the branches (`linux/App/Updater.h:7`), `packaging/README.md:66,87`, `packaging/linux/README.md:103-108`. This is the only step that changes what users receive. Ship one test build from `main` and run `sign-preview.sh --check-only` before deleting `windows/native` and `linux/native`.
- Duplicate assets (section 3.2), shared `Formula.cpp` (section 4.3).
- Pin actions, drop the `taciturnaxolotl`-era `release.yml` bits if wx is retired.

**PR 5 (optional)**: `mac/CedarCore`, `mac/Tools` -> `core/` (2.4). **PR 6 (optional)**: wx fence/retire (3.3).

### 2.6 Tag-then-delete routine (after PR 3 is merged)

```sh
git fetch private --tags --force
# 1. every candidate must already be contained in main
for b in $(cat delete-list.txt); do
  git merge-base --is-ancestor "private/$b" private/main || echo "NOT MERGED: $b"
done
# 2. tag (same sha as the branch), push the tags
for b in $(cat delete-list.txt); do git tag "archive/$b" "private/$b"; done
git push private $(sed 's|^|refs/tags/archive/|' delete-list.txt)
# 3. only then delete the branches
git push private $(sed 's|^|:refs/heads/|' delete-list.txt)
```

`delete-list.txt` = the 53 DELETE rows in section 1.2, minus the 5 superseded wx ports and `codex/stability-fixes` if you want an explicit yes first (they fail step 1 by design: they hold commits in no final; tagging still preserves them). The finals themselves, `windows/native` and `linux/native` follow in a second pass after PR 4 has published a build from `main`. Archive tag names are safe from git's directory/file ref clash (no branch is a path prefix of another). Check the three rolling tags are fresh first, or the `archive/` push is the only tag push you make.

## 3. Repo map and dead code

### 3.1 Repo map (draft for the README)

```markdown
## Where things are

| Folder | What it is |
|---|---|
| `mac/` | The native Mac app (Swift/AppKit, `App/`), `build.sh`, signing and DMG scripts. Also holds the shared engine glue `CedarCore/` and the shared headless checks `Tools/` that the Windows and Linux builds use too. |
| `windows/` | The native Windows app (Win32 + Direct2D), its Inno Setup installer and tools. |
| `linux/` | The native Linux app (GTK3 + Cairo), AppImage/deb packaging and `install.sh`. |
| `logic/` | The simulator: gates, wires, events. No UI. Own unit tests. |
| `format/` | The `.cdl` file format: reader, writer, old-format reader, migrations. Own unit tests. |
| `src/gui/`, `include/gui/` | The circuit model, wire routing and edit/undo commands that every native app shares, and (legacy) the wxWidgets UI. |
| `update-signing/` | Ed25519 check of signed updates, used by the Windows and Linux updaters. |
| `packaging/` | Homebrew cask, winget manifests, `install.ps1`, `install.sh`, `sign-release.sh`. |
| `scripts/` | Mac packaging, appcast, DMG background, `sign-preview.sh`. |
| `res/` | The gate library (`cl_gatedefs.xml`), sample circuit, built-in templates, classic help, icons. |
| `tests/`, `tools/` | Fixtures and cases for the checks; small generators. |
| `wasm/` | Browser build of the engine (Emscripten). |
| `docs/` | Format spec, release runbooks, parity matrix, design notes. |
| `CMakeLists.txt`, `cmake/`, `flake.nix`, `Taskfile.yaml`, `Doxyfile` | The original wxWidgets app's build (legacy; see below). |
```

Where the engine lives: `logic/` (simulation), `format/` (files), 36 files of `src/gui/` named in `mac/build.sh` (model, routing under `src/gui/route/`, edit commands under `src/gui/command/`), and `mac/CedarCore/` (document, editor, C API, check/sync/classroom engines). That set is about 374 KB and 9.9k lines in `src/gui` plus the folders above. The native apps add only their windowing, drawing (CoreGraphics / Direct2D / Cairo scene classes) and updater.

### 3.2 Used by nothing (delete now, about 88 KB in 24 files)

Method: file basenames and paths searched across the merged tree, build lists read from `mac/build.sh`, `windows/CMakeLists.txt`, `linux/CMakeLists.txt`, the root `CMakeLists.txt` globs and all workflows.

| Path | Size | Why it is dead |
|---|---|---|
| `res/bitmaps/` (17 `.bmp`) | 53,176 B | Only copied into the macOS wx bundle (`CMakeLists.txt:529-530`); no source opens them (`include/gui/MainFrame.h:436`: "Bundles, not bitmaps") |
| `tools/make_icon.py` | 6,921 B | Named nowhere; icons come from `mac/Tools/make-icons.sh` |
| `tools/wire-golden/wire-golden.ps1`, `tools/gate-golden/gate-golden.ps1` | 2,530 + 9,320 B | Windows-wx-era render diffs; reference only each other and `.gitignore` |
| `mac/try-first-launch.sh` | 1,043 B | Named nowhere |
| `docs/Deploying.md`, `docs/Releasing.md`, `docs/History.md` | 9,772 + 2,885 + 1,961 B | Linked from no other file; wx-era (the Mac runbook is `docs/Releasing-Mac.md`) |

Byte-identical duplicates (keep one, reference it): `res/macos/CedarLogic.icns` = `mac/App/CedarLogic.icns` (1,489,805 B); `linux/res/FirstLaunch.wav` = `windows/res/FirstLaunch.wav` (1,164,284 B); `linux/App/Formula.cpp` = `windows/App/Formula.cpp` (36,815 B, 1,030 lines); plus the 50 template pairs after the merge (about 0.5 MB).

### 3.3 The legacy wx app (compiled only by the root CMake and 6 workflows)

Nothing in `mac/build.sh`, `windows/CMakeLists.txt` or `linux/CMakeLists.txt` compiles these. They are built by the root `CMakeLists.txt` (which globs all of `src/` and `include/`) and exercised by `ci.yml`, `release.yml` (disabled in Actions), `windows.yml`, `skia.yml`, `skia-app.yml`, `nix.yml`. They are not "compiled by nothing", so this is a decision, not a cleanup. It also keeps the door open to upstream merges (2.2).

| Part | Size |
|---|---|
| 43 wx-only `src/` files (`MainFrame.cpp` 163 KB, `GUICanvas.cpp` 117 KB, `MainApp.cpp` 54 KB, `Welcome.cpp` 52 KB, ...) | 748,341 B, 19.5k lines |
| 23 wx-only headers in `include/gui/` | 45,398 B |
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/` (Skia args/shim, NSIS, modules), `Doxyfile`, `flake.nix`/`flake.lock`, `Taskfile.yaml`, `CedarLogic.h.in`, `icon.rc.in`, `wasm/` | 411,860 B |
| `res/KLS_Logic.chm` 2,398,160; `res/arial.glf` 133,400; `res/icons/*.svg` 7,830; small wx files (`icon.png`, `mime.xml`, `.desktop`, manifest, `LICENSE.txt`) | 2,550,809 B in all |
| Workflows `ci.yml`, `release.yml`, `windows.yml`, `skia.yml`, `skia-app.yml`, `nix.yml`, `release-footer.md`; `docs/appcast.xml`; `scripts/update-appcast.sh` | 60,115 B |
| **Total** | **about 3.8 MB** |

Not part of it although it looks like it: `res/help/` (8.9 MB, the Mac copies it as Classic Help, `mac/build.sh`), `res/macos/` (Mac packaging), `res/icon.ico` (used by `windows/CMakeLists.txt:78`), `src/gui/tests/` and `src/gui/route/tests/` (52 KB of engine tests that the root CMake runs), `cmdAddTab/cmdDeleteTab` aside the 36 engine files.

Options: (a) leave it and say so in the README (cheapest, keeps upstream sync possible); (b) move it under `legacy/wx/` (touches `ci.yml`, `nix.yml`, `Taskfile.yaml`, `flake.nix`, CMake presets); (c) delete after the upstream sync of 2.2 is done. I would do (a) for 0.4.

## 4. Code-quality pass candidates

All measured on the simulated merged tree (`828c7c9`).

### 4.1 The 15 largest source files

| Lines | Bytes | File | Note |
|---|---|---|---|
| 4,044 | 127,161 | `mac/CedarCore/SyncFixtures.h` | data: sync test vectors as a header |
| 3,944 | 163,198 | `src/gui/MainFrame.cpp` | wx, legacy |
| 3,110 | 129,126 | `mac/CedarCore/ClassroomClient.cpp` | |
| 3,075 | 117,630 | `windows/App/Window.cpp` | |
| 2,977 | 116,637 | `src/gui/GUICanvas.cpp` | wx, legacy |
| 2,750 | 113,904 | `linux/App/Window.cpp` | |
| 2,548 | 110,382 | `windows/App/Dialogs.cpp` | |
| 2,384 | 111,422 | `mac/App/CLWindow.swift` | |
| 2,220 | 106,971 | `mac/Tools/make_check_cases.cpp` | generator |
| 2,195 | 82,726 | `mac/CedarCore/SyncTestScenarios.cpp` | test |
| 2,089 | 77,784 | `mac/CedarCore/Sync.cpp` | |
| 1,953 | 101,376 | `mac/App/ClassroomView.swift` | |
| 1,948 | 91,462 | `mac/App/CanvasView.swift` | |
| 1,945 | 76,781 | `linux/App/Canvas.cpp` | |
| 1,807 | 74,255 | `windows/App/Feedback.cpp` | |

Splitting candidates worth doing: the two `Window.cpp` files (mirror each other: split the same way, tabs / menus / drop / session), `ClassroomClient.cpp`, `Dialogs.cpp`. `SyncFixtures.h` should become data files loaded by the test.

### 4.2 TODO / FIXME / XXX / HACK

30 occurrences in 14 files, **none in `mac/`, `windows/`, `linux/` or `update-signing/`**. All in engine or wx code: `logic/src/logic_circuit.cpp` 7, `include/gui/guiWire.h` 4, `logic/src/logic_gate_memory.cpp` 4, `src/gui/MainFrame.cpp` 3, `src/gui/klsGLCanvas.cpp` 2, `src/gui/GUICanvas.cpp` 2, one each in `klsGLCanvas.h`, `klsCommand.h`, `logic/tests/test.cpp`, `logic_gate_connectivity.cpp`, `logic_wire.cpp`, `.github/workflows/release.yml`, `guiGate.cpp`, `cmdDeleteTab.cpp`. Triage the 15 in `logic/`/`guiWire.h`/`guiGate.cpp` (engine); the rest go with the wx app.

### 4.3 Duplicated helpers across windows / linux / core

Whole file: `Formula.cpp` is identical in `windows/App` and `linux/App` (1,030 lines, 36.8 KB) and `Formula.h` is 97% the same. Move both (and `Formula`'s check, `linux/Tools/formula_check.cpp` vs `mac/Tools/formula-check`) into `mac/CedarCore` (later `core/`), the way the Mac already has `FormulaCircuit.swift` over the engine.

Near-duplicates (same basename in `windows/App` and `linux/App`, line similarity): `Predict.cpp` 86%, `LibraryWindow.cpp` 61%, `Collections.cpp` 55%, `Help.cpp` 53%, `LabReport.cpp` 51%, `Scope.cpp` 47%. Only 1 of 68 shared basenames is identical; these are the ones worth putting behind a shared core API (templates loading in `Collections.cpp`, lab report text in `LabReport.cpp`, prediction logic in `Predict.cpp`).

Small helpers re-implemented per file (function body identical after whitespace normalisation; "n" = copies of the best identical group):

| Helper | Where | Copies |
|---|---|---|
| `trimmed()` | `windows/App/Alert.cpp`, `Dialogs.cpp`, `Export.cpp`, ...; `linux/App/ExportImage.cpp`, `Feedback.cpp`, `LabReport.cpp`, `SyncPlatform.cpp`, `TruthTableWindow.cpp`, ... | 10 of 11 identical |
| `trim()` | `Collections.cpp`, `Formula.cpp`, `Library.cpp` on both platforms, `mac/CedarCore/CheckImpl.h`, `mac/Tools/make_check_cases.cpp` | 4 of 8 |
| `readFile()` | `linux/App/Collections.cpp`, `Library.cpp`, `SessionCheck.cpp`, `mac/CedarCore/Document.cpp`, six `mac/Tools/*_check.cpp` | 3+ of 14 |
| `values()` | `LabReport.cpp` and `TruthTableWindow.cpp` on both platforms | 4 of 4 |
| `reduceMotion()` | `Splash.cpp`, `Welcome.cpp` (both), `windows/App/Alert.cpp`, `TabStrip.cpp`, `Window.cpp` | 3 of 7 |
| `lower()` | `Help.cpp`, `Palette.cpp`, `ShortcutsSheet.cpp`, `CheckImpl.h`, `Find.cpp` | 3 of 7 |
| `split()`, `writeFile()`, `windowAlive()`, `envOr()`, `oneLine()`, `shape()`, `encode()/decode()`, `verifyFile()` | across `Feedback.cpp`, `Collections.cpp`, `Library.cpp`, `Updater.cpp`, `ShareLink`/`ShareCodec` | pairs of identical copies |

One `util` header in `mac/CedarCore` (string trim/lower/split, file read/write) replaces most of this at no behaviour risk. `Updater.cpp` is not near-identical (the two differ for platform reasons) but `verifyFile()` is.

### 4.4 Commented-out code blocks over 20 lines

Searched `//` runs and `/* */` blocks of at least 20 lines whose content looks like code (more than 30% of lines end in `;`, `{`, `}` or start with a control keyword), plus `#if 0`: **only two** exist, both in the wx app: `src/gui/OscopeCanvas.cpp:338` (59 lines, `/* */`) and `src/gui/MainApp.cpp:633` (24 lines). No `#if 0` anywhere. The native apps and the engine are clean. (The long banner comments "Edit by Joshua Lansford 2006-2007" in `guiGate.h`, `guiGate.cpp`, `logic_gate_select.cpp`, `MainFrame.cpp`, `MainApp.cpp` are prose history, not dead code; consider folding them into a `docs/History.md` note.)

### 4.5 Compiler warnings printed by CI

From the latest green runs on the three finals (`gh run view --log`): Linux native build 41248065934, Windows native build 41246890400, Core hardening 41249366814. The apps are nearly warning-free; the engine warnings are the volume.

| Where | Warning | Fix |
|---|---|---|
| `linux/App/Settings.cpp:564` | `-Wunused-function`: `stopRecording(SettingsWindow*)` defined but unused | delete or wire up |
| `linux/App/SessionCheck.cpp:1435` | `-Wunused-variable`: `growth` | delete |
| `linux/App/Updater.cpp:1529` | `-Wunused-result`: return of `system()` ignored | check or `(void)` with a comment |
| `mac/Tools/render_png.cpp:62` (Linux gcc and Windows MSVC C4477) | `%lu` with an `IDType` (unsigned long long) | cast to `unsigned long long`, use `%llu` |
| `update-signing/monocypher/monocypher.c(881,40)` (MSVC C4244) | u64 to u32 in vendored code | mark the `cl_updatesig` target third-party (`/W0`) rather than edit monocypher |
| `logic/include/logic_gate.h` (31 sites), `include/gui/guiGate.h` (12 sites, shown twice via `command/../guiGate.h`), `include/gui/command/cmd{MoveGate,MoveWire,CreateGate,ConnectWire,DisconnectWire}.h` (2 each) | `-Winconsistent-missing-override` x73 (clang, core-hardening) | add `override`; `mac/build.sh` silences it with `-Wno-inconsistent-missing-override`, which would then be removable |
| `logic/src/logic_gate_sequential.cpp:380` | `-Wself-assign-field`: a field assigned to itself | real bug candidate: look at what was meant |

### 4.6 GitHub Actions: pins and permissions

Counted on the merged tree. `windows-native.yml` and `linux-native.yml` are already fully SHA-pinned and have `permissions:` (top level `:17` / `:15`, publish job `:778` / `:595`). Everything else:

| Workflow | Unpinned `uses:` lines | Top-level `permissions:` |
|---|---|---|
| `ci.yml` | 13 (`:22 :44 :63 :83 :93 :98 :131 :134 :140 :153 :196 :199 :205`) | **missing**: insert before `concurrency:` at `:11` |
| `core-hardening.yml` | 4 (`:27 :88 :101 :120`) | **missing**: insert before `concurrency:` at `:12` |
| `release.yml` | 23 (`:40 :68 :74 :89 :201 :213 :230 :235 :243 :264 :277 :309 :314 :322 :447 :455 :465 :471 :499 :515 :528 :559 :591`) | **missing**: insert before `concurrency:` at `:18`; job `upload-release` already has its own (`:508-510` `contents: write`, `id-token: write`) |
| `windows.yml` | 6 (`:40 :59 :67 :125 :132 :140`) | present (`:20`) |
| `skia.yml` | 4 (`:55 :60 :77 :200`) | present (`:30`) |
| `skia-app.yml` | 3 (`:39 :116 :141`) | present (`:19`) |
| `nix.yml` | 2 (`:42 :44`) | present (`:29`) |

The three "missing" files are exactly the **11 open code-scanning alerts** (`actions/missing-workflow-permissions`: `ci.yml` 19/41/76/79/127/193, `release.yml` 31/34/209/270/453, line numbers of `main`'s older revision). One `permissions:\n  contents: read` block per file closes all 11.

SHAs for the `v` tags used (resolved with `gh api` on 2026-10-31; the first three match what `windows-native.yml` already pins):

```
actions/checkout@v7                  3d3c42e5aac5ba805825da76410c181273ba90b1
actions/upload-artifact@v7           cf430e030ddbb5b0abf93d22962f4752f3646cd9
actions/download-artifact@v8         9000827ccba6bdab643e8b6fd33ac0654aef8333
actions/cache@v6                     55cc8345863c7cc4c66a329aec7e433d2d1c52a9
actions/setup-python@v7              6d4fd22b20465ccc0f7354476588e8df7c9ca0e0
actions/setup-node@v7                949feb2413d6458794dcd2491c4babbbce0c15c1
hendrikmuhs/ccache-action@v1         5ebbd400eff9e74630f759d94ddd7b6c26299639
emscripten-core/setup-emsdk@v16      4528d102f7230f0e7b276855c01ea1159be0e984
cachix/install-nix-action@v31        75c75c31ebece974e48ebc36b4ad125bd97bd97b
softprops/action-gh-release@v3       efb35369e0ad2afab669f228072c1b0d510eae64
```

Format to use, as in `windows-native.yml:39`: `uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7`. Other CI items: `core-hardening.yml` `on: push:` is unfiltered; `ci.yml:6` / `nix.yml:14` / `release.yml:517` / `skia-app.yml:50` say `master`; no workflow builds the Mac app; the repo has no branch protection or rulesets (0 protected branches).

FYI, public repo: `mac/build.sh` carries a default Sentry DSN and the feedback `APP_KEY` default. The comments call both non-secret by design (a DSN can only submit events; the key "keeps out passers-by"); I did not copy them here. Consider at least reading them from CI secrets for release builds.

## 5. Open PRs, issues, alerts

| Item | State | Recommendation |
|---|---|---|
| PR #1 "Windows version, practice circuit, and stability fixes" (`windows/port` -> `main`, opened 2026-09-23, 42 files, +3,609/-90) | OPEN, `CONFLICTING` | **Close as superseded.** `windows/port` (`7a33996`) is an ancestor of all three finals, so everything in it reaches `main` with PR 1; its old Windows port was replaced by the native app. Comment with a link to PR 1, then tag and delete the branch with the others. Do not try to rebase it |
| Issues (open and closed) | 0 | nothing to do |
| PRs on `origin` (taciturnaxolotl) | none returned by `gh pr list` | nothing to do |
| Code scanning | 11 open, all `actions/missing-workflow-permissions` in `ci.yml` and `release.yml` | fixed by section 4.6 in PR 1 |
| Dependabot alerts | 0 | none |
| `AI security scan` workflow (dynamic, runs on `main`) | last run 2026-10-28 failed (run 40774193314) | look at its log; not part of the tidy |
| `Linux build` workflow (`linux.yml`, only exists on old `linux/port`-era branches) | registered in Actions, its file is on no final | disappears from use once `linux/port` and friends are deleted; remove the stale entry from Actions settings if it lingers |
| `Release Build` workflow | disabled manually | keep disabled; it is the wx release and, if wx is fenced, should stay that way |

## Appendix: how this was measured, and what was not

- Fetch of `private` on 2026-10-31; branch/tag lists from `git ls-remote`; "merged" from `git merge-base --is-ancestor` against the three finals; "unique" from `git rev-list --count <b> --not <finals>`; patch equivalence from `git cherry` against each final.
- Conflicts and the combined tree from `git merge-tree --write-tree` (no refs written; two throw-away unreferenced commit objects exist in the object store only).
- Duplicate helpers: function bodies hashed after whitespace normalisation across `windows/`, `linux/`, `mac/CedarCore`, `mac/Tools`, `src/`, `include/`. "Dead" files: basename/path searched in every text file of the merged tree and in all build lists.
- Warnings are from the three CI logs named above, not from a local build. **No code was built or run for this survey.** The template-naming CI breakage (2.3.1) is inferred from the workflow lines, not observed.
- Sizes are bytes of the merged tree; line counts are physical lines.

