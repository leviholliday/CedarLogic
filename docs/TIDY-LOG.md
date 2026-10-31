# CedarLogic 0.4 repo tidy: log

What was actually done on `private` (github.com/leviholliday/CedarLogic), following `docs/TIDY-PLAN.md`. Nothing was pushed, tagged or opened on `origin`.

## Part 1: branches and PRs (2026-10-31)

### 1. Archive tags and branch deletions

For each of the 53 branches the plan lists as "DELETE (tag first)":

1. Checked the tip on `private` was still the one in the plan (all 53 unchanged).
2. For the 47 merged ones, `git merge-base --is-ancestor private/<branch> private/<final>`: all 47 passed. None skipped.
3. Created an annotated tag `archive/<branch>` at the branch tip, message "Archive of branch <branch> (private) at <sha>, tagged 2026-10-31 before deleting the branch".
4. Pushed the 53 tags in one push, each named explicitly (`git push private refs/tags/archive/<branch> ...`, no `--tags`). One push of more than three tags creates no push events, so `core-hardening.yml` (which triggers on any push) did not run 53 times.
5. Checked with `git ls-remote` that every `archive/<branch>^{}` on `private` equals the branch head on `private` (53 of 53).
6. Deleted the 53 branches (`git push private --delete ...`).

Restore any of them with `git push private refs/tags/archive/<branch>^{commit}:refs/heads/<branch>`.

| Branch (deleted) | Tip = tag `archive/<branch>` | Contained in |
|---|---|---|
| `v4/mac-app` | `f00a63a` | all 3 finals |
| `v4/mac-core` | `e307059` | all 3 finals |
| `v4/mac-sweep` | `5716748` | all 3 finals |
| `windows/feedback` | `5acd906` | none (old wx port, kept by tag) |
| `windows/native-04` | `a621793` | windows/native-v4-int3 |
| `windows/native-channel` | `59a3ccc` | windows/native-v4-int3 |
| `windows/native-features` | `deb1a96` | windows/native-v4-int3 |
| `windows/native-pair` | `a2c792c` | windows/native-v4-int3 |
| `windows/native-polish` | `47b6656` | windows/native-v4-int3 |
| `windows/native-v4` | `89d397b` | windows/native-v4-int3 |
| `windows/native-v4-core` | `98e8c3d` | windows/native-v4-int3 |
| `windows/native-v4-int` | `c10511d` | windows/native-v4-int3 |
| `windows/native-v4-int2` | `b37c917` | windows/native-v4-int3 |
| `windows/native-v4-pin` | `cbedc20` | windows/native-v4-int3 |
| `windows/native-v4-session` | `cbedc20` | windows/native-v4-int3 |
| `windows/native-v4-signed` | `0baf7c0` | windows/native-v4-int3 |
| `windows/native-v4-ui` | `1899368` | windows/native-v4-int3 |
| `windows/phase1` | `4709f91` | none (old wx port, kept by tag) |
| `windows/port` | `7a33996` | all 3 finals |
| `linux/feedback` | `393b789` | none (old wx port, kept by tag) |
| `linux/native-04` | `0729b9b` | linux/native-v4-int3 |
| `linux/native-channel` | `7ecee4c` | linux/native-v4-int3 |
| `linux/native-features` | `bbdf875` | linux/native-v4-int3 |
| `linux/native-pair` | `d5da9bc` | linux/native-v4-int3 |
| `linux/native-polish` | `d562e4b` | linux/native-v4-int3 |
| `linux/native-v4` | `d25890e` | linux/native-v4-int3 |
| `linux/native-v4-core` | `5666fe7` | linux/native-v4-int3 |
| `linux/native-v4-int` | `a588310` | linux/native-v4-int3 |
| `linux/native-v4-int2` | `d3b2513` | linux/native-v4-int3 |
| `linux/native-v4-perf` | `7904751` | linux/native-v4-int3 |
| `linux/native-v4-pin` | `7904751` | linux/native-v4-int3 |
| `linux/native-v4-session` | `fb458cb` | linux/native-v4-int3 |
| `linux/native-v4-signed` | `12f3944` | linux/native-v4-int3 |
| `linux/native-v4-ui` | `212344b` | linux/native-v4-int3 |
| `linux/port` | `01de922` | none (old wx port, kept by tag) |
| `mac/feedback-cap` | `1f7b5bd` | all 3 finals |
| `mac/native` | `5029f4c` | all 3 finals |
| `mac/parity-gaps` | `7fa7dff` | all 3 finals |
| `mac/whats-new-0.4` | `64b2520` | all 3 finals |
| `ui/linux-channel` | `48c5ba9` | linux/native-v4-int3 |
| `ui/linux-menubar` | `46d32ba` | linux/native-v4-int3 |
| `ui/linux-polish` | `d562e4b` | linux/native-v4-int3 |
| `ui/windows-channel` | `59a3ccc` | windows/native-v4-int3 |
| `ui/windows-menubar` | `54f1236` | windows/native-v4-int3 |
| `ui/windows-polish` | `47b6656` | windows/native-v4-int3 |
| `sec/linux-apps` | `a3c261c` | linux/native-v4-int3 |
| `sec/mac-apps` | `25769f8` | all 3 finals |
| `sec/windows-apps` | `8901759` | windows/native-v4-int3 |
| `core/hardening` | `ca3237b` | all 3 finals |
| `pkg/installers` | `3eb9899` | all 3 finals |
| `docs/parity-2026-10-31` | `5f1292f` | all 3 finals |
| `claude/quirky-keller-1jc29u` | `52f960a` | none (old wx port, kept by tag) |
| `codex/stability-fixes` | `4a5fa0c` | none (its 1 commit has a patch-equivalent in a final; kept by tag) |

Totals: 53 tags created and pushed, 53 branches deleted (47 merged, 5 old wx ports, `codex/stability-fixes`). Local branches in the main checkout were not touched; the local `archive/*` tags exist in the shared clone.

Not touched: `main`, `v4/mac`, `windows/native`, `linux/native`, `windows/native-v4-int3`, `linux/native-v4-int3`, and the branches pushed after the plan was written (`docs/tidy-plan`, `rel/mac-0.4`, `tools/demo-mode`).

### 2. The seven "needs a look" branches

| Branch | Done |
|---|---|
| `docs/whats-new-0.4` (`f08c448`) | Merged into `v4/mac` (`064171a`, clean). Branch left in place. |
| `docs/tidy-plan` (`1d1e30b`) | Merged into `v4/mac` (`d5d05ec`, clean). Branch left in place. |
| `windows/pc-test` (`223a17f`) | Merged into `windows/native-v4-int3` (`b7f128e`, clean, adds `windows/PC-TEST.md` only). Branch left in place. |
| `pkg/tap-name` | Skipped: another agent is merging it. |
| `docs/soak-2026-10-31` | Skipped: another agent is still writing it. |
| `tools/signing-keys` (`b4b1406`) | Kept, not tagged. `scripts/signing-keys.sh` (backup/restore of both update-signing keys) is in no final and is not obsolete. Merges clean into `windows/native-v4-int3`; conflicts with `v4/mac` (`linux/install.sh`, add/add) and, per the plan, with the Linux final's `scripts/sign-preview.sh`. Owner decision: fold into PR 2 as the plan suggests. |
| `linux/native-v4-simbench` (`9280d2e`) | Kept, not tagged. Its bench rows with the simulation running (`linux/App/Bench.cpp`, +74) and `linux/SMOOTHNESS.md` numbers are not in the Linux final, which only has the frame-clock rows. Merges clean into `linux/native-v4-int3`. Fold into PR 3. |
| `linux/native-v4-desktop` (`f5fb814`) | Kept, not tagged. The A/C-key and Ctrl+Tab code was redone in the Linux final (`c5d4155`, `011095b`) and conflicts in `Canvas.cpp`/`TabSwitcher.cpp`, but its `linux/DESKTOP-TEST.md` notes (+49 lines) are not in the final. Take the notes by hand into PR 3, then archive. |

### 3. PR #1 (`windows/port` -> `main`)

https://github.com/leviholliday/CedarLogic/pull/1 was closed automatically by GitHub when its head branch `windows/port` was deleted in step 1 (the plan had closing it first; the order was reversed). A comment saying it is superseded by `windows/native-v4-int3`, and that its head is in all three finals and kept as `archive/windows/port`, was added: https://github.com/leviholliday/CedarLogic/pull/1#issuecomment-6550497185

### 4. PR 1 of the tidy sequence

Draft PR `v4/mac` -> `main`, "CedarLogic 0.4: Mac app, shared engine, packaging, docs": https://github.com/leviholliday/CedarLogic/pull/2

Not resolved or merged. Body lists the `README.md` conflict (keep `v4/mac`'s, fold `main`'s one edit in) and the four `master` -> `main` lines (`ci.yml:6`, `nix.yml:14`, `release.yml:517`, `skia-app.yml:50`; `skia.yml` is not to be edited).
