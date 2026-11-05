#!/usr/bin/env bash
# Release day, go-list step 1: the Windows and Linux builds of the release
# commit, published by CI and signed.
#
#   scripts/release-ci.sh --dry-run          every check; pushes nothing, signs nothing
#   scripts/release-ci.sh --go               push, wait for CI, sign (asks before signing)
#       --commit C          the release commit (default: private/main's tip; it must be that tip)
#       --platform P        windows, linux or both (default both)
#       --no-wait           push only (--go): don't wait for CI or sign
#       --no-sign           push and wait, but leave scripts/sign-preview.sh to you
#       --yes               don't ask (sign-preview.sh gets --yes too)
#       --timeout MIN       how long to wait for each run to start (default 10)
#
# Why: the workflows' publish jobs publish only from the exact branch names
# windows/native and linux/native (to the rolling pre-releases
# windows-native-testing and linux-native-testing), and main never runs them.
# So on release day main's tip is pushed to both names:
#
#   git push private <C>:refs/heads/windows/native
#   git push private <C>:refs/heads/linux/native
#
# Checks, before anything is pushed (all of them in --dry-run too):
#   - the remote "private" is github.com/leviholliday/CedarLogic (never origin)
#   - C is private/main's tip, and every workflow run on main for C finished
#     green (CI, Core hardening, CodeQL...); at least the "CI" one ran
#   - each branch only fast-forwards to C (its tip is an ancestor of C);
#     git push --dry-run against the remote says the same
#   - no run of the workflow on that branch is still going
# Then (--go): push, find the run for C, gh run watch it to the end, check the
# testing release's tag is on C, and run scripts/sign-preview.sh <platform>,
# which checks the release against the run's artifacts and signs it.
# In --dry-run the last part is scripts/sign-preview.sh <platform> --check-only
# on the testing release as it is now.
# shellcheck source-path=SCRIPTDIR
set -euo pipefail
ME=release-ci
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=release-lib.sh
. "$ROOT/scripts/release-lib.sh"

MODE='' COMMIT='' PLATFORMS='windows linux' WAIT=1 SIGN=1 YES=${YES:-0} TIMEOUT=10
while [ $# -gt 0 ]; do
	case "$1" in
		--dry-run) [ -z "$MODE" ] || die "--dry-run or --go, not both"; MODE=dry; shift ;;
		--go) [ -z "$MODE" ] || die "--dry-run or --go, not both"; MODE=go; shift ;;
		--commit) COMMIT=$2; shift 2 ;;
		--platform) case "$2" in windows|linux) PLATFORMS=$2 ;; both) PLATFORMS='windows linux' ;; *) die "--platform windows|linux|both" ;; esac; shift 2 ;;
		--no-wait) WAIT=0; SIGN=0; shift ;;
		--no-sign) SIGN=0; shift ;;
		--yes) YES=1; shift ;;
		--timeout) TIMEOUT=$2; shift 2 ;;
		-h|--help) sed -n '2,37p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown option $1" ;;
	esac
done
require_mode "$MODE"
command -v gh >/dev/null || die "needs the GitHub CLI (gh)"

# ---- the remote and the commit ---------------------------------------------------
URL=$(git -C "$ROOT" remote get-url "$PRIVATE_REMOTE" 2>/dev/null) || die "no git remote named $PRIVATE_REMOTE"
case "$URL" in
	*github.com[:/]"$SOURCE_REPO"|*github.com[:/]"$SOURCE_REPO".git) ;;
	*) die "remote $PRIVATE_REMOTE is $URL, not $SOURCE_REPO: refusing to push there" ;;
esac
git -C "$ROOT" fetch -q "$PRIVATE_REMOTE" main "$WIN_BRANCH" "$LINUX_BRANCH" || die "git fetch $PRIVATE_REMOTE failed"
MAIN=$(git -C "$ROOT" rev-parse "$PRIVATE_REMOTE/main")
if [ -z "$COMMIT" ]; then COMMIT=$MAIN; fi
COMMIT=$(git -C "$ROOT" rev-parse --verify "$COMMIT^{commit}") || die "not a commit: $COMMIT"
[ "$COMMIT" = "$MAIN" ] || die "the release commit ${COMMIT:0:12} isn't $PRIVATE_REMOTE/main's tip (${MAIN:0:12}): release from main's tip"
say "release commit $COMMIT \"$(git -C "$ROOT" log -1 --format=%s "$COMMIT")\" (build $(git -C "$ROOT" rev-list --count "$COMMIT"), committed $(git -C "$ROOT" log -1 --format=%ci "$COMMIT"))"

# ---- main is green -------------------------------------------------------------
check_main_green "$SOURCE_REPO" "$COMMIT" || die "main isn't green for ${COMMIT:0:12}: wait for (or fix) the runs above"

# ---- per platform ---------------------------------------------------------------
for P in $PLATFORMS; do
	if [ "$P" = windows ]; then BR=$WIN_BRANCH WF=$WIN_WORKFLOW TAG=$WIN_TAG; else BR=$LINUX_BRANCH WF=$LINUX_WORKFLOW TAG=$LINUX_TAG; fi
	say "--- $P: $BR"
	HEAD=$(git -C "$ROOT" rev-parse "$PRIVATE_REMOTE/$BR")
	PUSH=1
	if [ "$HEAD" = "$COMMIT" ]; then
		say "$BR is already on ${COMMIT:0:12}: nothing to push"
		PUSH=0
	elif git -C "$ROOT" merge-base --is-ancestor "$HEAD" "$COMMIT"; then
		say "$BR ${HEAD:0:12} -> ${COMMIT:0:12} is a fast-forward ($(git -C "$ROOT" rev-list --count "$HEAD..$COMMIT") commits)"
	else
		die "$BR (${HEAD:0:12}) isn't an ancestor of ${COMMIT:0:12}: it would not fast-forward. Look at what's on it first; this never force-pushes"
	fi
	PENDING=$(gh run list -R "$SOURCE_REPO" --workflow "$WF" --branch "$BR" --limit 20 --json status --jq '[.[] | select(.status != "completed")] | length')
	[ "$PENDING" = 0 ] || die "a $WF run on $BR is still going: wait for it first"

	if [ "$MODE" = dry ]; then
		if [ "$PUSH" = 1 ]; then
			git -C "$ROOT" push --dry-run "$PRIVATE_REMOTE" "$COMMIT:refs/heads/$BR" 2>&1 | sed "s/^/$ME:   git: /" >&2 \
				|| die "git push --dry-run to $BR failed"
			say "would run: git push $PRIVATE_REMOTE $COMMIT:refs/heads/$BR, then wait for $WF on it and run scripts/sign-preview.sh $P"
		fi
		# What's there now (the stand-in for the release build).
		NOWTAG=$(ref_commit "$SOURCE_REPO" "$TAG") || die "no tag $TAG"
		RUN=$(green_run "$SOURCE_REPO" "$WF" "$BR" "$HEAD")
		say "now: $TAG is on ${NOWTAG:0:12}, $BR on ${HEAD:0:12}, green run: ${RUN:-none}"
		say "scripts/sign-preview.sh $P --check-only (on $TAG as it is now)"
		bash "$ROOT/scripts/sign-preview.sh" "$P" --check-only || die "sign-preview.sh $P --check-only failed on the release as it is now"
		continue
	fi

	if [ "$PUSH" = 1 ]; then
		confirm "Push ${COMMIT:0:12} to $PRIVATE_REMOTE/$BR? CI then builds and publishes $TAG (beta testers get it once it's signed)" || die "not pushed"
		git -C "$ROOT" push "$PRIVATE_REMOTE" "$COMMIT:refs/heads/$BR" || die "git push to $BR failed"
	fi
	[ "$WAIT" = 1 ] || { say "pushed; not waiting (--no-wait)"; continue; }
	RUN='' i=0
	while [ -z "$RUN" ]; do
		RUN=$(gh run list -R "$SOURCE_REPO" --workflow "$WF" --branch "$BR" --commit "$COMMIT" --limit 5 --json databaseId --jq '.[0].databaseId // empty')
		[ -n "$RUN" ] && break
		i=$((i + 1)); [ "$i" -le $((TIMEOUT * 4)) ] || die "no $WF run for ${COMMIT:0:12} after $TIMEOUT minutes"
		sleep 15
	done
	say "watching https://github.com/$SOURCE_REPO/actions/runs/$RUN"
	gh run watch "$RUN" -R "$SOURCE_REPO" --exit-status --interval 30 >/dev/null || die "$WF run $RUN didn't finish green: look at it, fix, and run this again"
	[ "$(ref_commit "$SOURCE_REPO" "$TAG")" = "$COMMIT" ] || die "the run is green but $TAG isn't on ${COMMIT:0:12}: did its publish job run?"
	say "$TAG is on ${COMMIT:0:12} (run $RUN green)"
	if [ "$SIGN" = 1 ]; then
		ARGS=("$P"); [ "$YES" = 0 ] || ARGS+=(--yes)
		bash "$ROOT/scripts/sign-preview.sh" "${ARGS[@]}" || die "sign-preview.sh $P failed"
	else
		say "next: scripts/sign-preview.sh $P"
	fi
done
say "done ($MODE): $PLATFORMS on ${COMMIT:0:12}"
