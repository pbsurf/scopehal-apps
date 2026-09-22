#!/usr/bin/env bash
# Instrumented build wrapper for diagnosing "small change triggers a huge
# rebuild" incidents. Doesn't modify the CMake-generated Makefiles (they get
# regenerated on every reconfigure, so edits to them wouldn't stick) -- instead
# it wraps a normal build and records, per run, into build-logs/run-<timestamp>/:
#
#   git-head.txt              HEAD commit at the start of this run
#   git-status.txt             uncommitted changes (git status --short)
#   git-diff-stat.txt          shortstat of uncommitted changes
#   git-diff-full.txt          full uncommitted diff
#   CMakeCache.txt              full copy of the cache used for this run
#   src-mtimes-before.txt       mtime of every git-tracked source file, pre-build
#   dryrun-before-reconfigure.txt   `make -n` output BEFORE reconfiguring
#   reconfigure.log             `cmake .` output (isolated from the real build)
#   dryrun-after-reconfigure.txt    `make -n` output AFTER reconfiguring, BEFORE
#                                   building -- this isolates exactly what the
#                                   reconfigure step alone marked stale, with
#                                   zero compile cost
#   memory-samples.txt          system-wide `free -m` sampled every 2s during
#                                the real build (peak RSS/swap for OOM diagnosis)
#   build.log                   full stdout+stderr of the real `make` run
#   build-mtimes-after.txt      mtime of every object/lib/exe in build/, post-build
#   rebuilt-artifacts.txt       build artifacts whose mtime actually changed
#   rebuilt-per-make-output.txt "Building"/"Linking" lines parsed from build.log
#
# and appends one tab-separated line to build-logs/summary.tsv for quick
# eyeballing across many cycles.
#
# Usage:
#   ./instrumented-build.sh ["short label describing what you just changed"]
#
# Env overrides:
#   BUILD_DIR=/path/to/build   (default: ./build)
#   JOBS=N                     (default: nproc)
#   SKIP_RECONFIGURE=1         (skip the explicit `cmake .` step -- useful to
#                               isolate "does a bare `make` alone, with no
#                               reconfigure, ever rebuild things unexpectedly")

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
LOG_ROOT="$REPO_ROOT/build-logs"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$LOG_ROOT/run-$RUN_ID"
JOBS="${JOBS:-$(nproc)}"
LABEL="${1:-}"

if [[ ! -d "$BUILD_DIR" ]]; then
	echo "error: build dir '$BUILD_DIR' does not exist" >&2
	exit 1
fi

mkdir -p "$RUN_DIR"
log() { echo "[$(date +%H:%M:%S)] $*"; }

cd "$REPO_ROOT"

log "run $RUN_ID starting (label: '${LABEL:-none}')"

# 1. git state
git rev-parse HEAD > "$RUN_DIR/git-head.txt" 2>&1
git status --short > "$RUN_DIR/git-status.txt" 2>&1
git diff --stat > "$RUN_DIR/git-diff-stat.txt" 2>&1
git diff > "$RUN_DIR/git-diff-full.txt" 2>&1

# 2. full cmake cache snapshot, for later diffing between runs
cp "$BUILD_DIR/CMakeCache.txt" "$RUN_DIR/CMakeCache.txt" 2>/dev/null

# 3. source mtimes before (git-tracked files only)
git ls-files -z | xargs -0 stat -c '%Y %n' 2>/dev/null | sort -k2 > "$RUN_DIR/src-mtimes-before.txt"

# 4. build artifact mtimes before
find "$BUILD_DIR" -type f \( -name '*.o' -o -name '*.a' -o -name '*.so*' -o -name '*.make' -o -perm -u+x \) \
	-printf '%T@ %p\n' 2>/dev/null | sort -k2 > "$RUN_DIR/build-mtimes-before.txt"

# 5. what does make think is stale RIGHT NOW, before touching anything
( cd "$BUILD_DIR" && make -n -j1 ) > "$RUN_DIR/dryrun-before-reconfigure.txt" 2>&1

# 6. explicit reconfigure, timed and logged in isolation from the real build
RECONF_START=$(date +%s)
if [[ -z "${SKIP_RECONFIGURE:-}" ]]; then
	( cd "$BUILD_DIR" && cmake . ) > "$RUN_DIR/reconfigure.log" 2>&1
else
	echo "(skipped: SKIP_RECONFIGURE=1)" > "$RUN_DIR/reconfigure.log"
fi
RECONF_END=$(date +%s)

# 7. what does make think is stale immediately after reconfiguring, still
#    without compiling anything -- isolates the reconfigure step's effect
( cd "$BUILD_DIR" && make -n -j1 ) > "$RUN_DIR/dryrun-after-reconfigure.txt" 2>&1

# 8. background system memory sampler for the real build
: > "$RUN_DIR/memory-samples.txt"
(
	while true; do
		ts=$(date +%s)
		mem=$(free -m | awk '/^Mem:/{printf "used=%sMB avail=%sMB", $3, $7}')
		swap=$(free -m | awk '/^Swap:/{print $3}')
		echo "$ts $mem swap=${swap}MB" >> "$RUN_DIR/memory-samples.txt"
		sleep 2
	done
) &
MEM_PID=$!

# 9. the real build, timed
BUILD_START=$(date +%s)
( cd "$BUILD_DIR" && make -j"$JOBS" ) > "$RUN_DIR/build.log" 2>&1
BUILD_EXIT=$?
BUILD_END=$(date +%s)

kill "$MEM_PID" 2>/dev/null
wait "$MEM_PID" 2>/dev/null

# 10. build artifact mtimes after
find "$BUILD_DIR" -type f \( -name '*.o' -o -name '*.a' -o -name '*.so*' -o -name '*.make' -o -perm -u+x \) \
	-printf '%T@ %p\n' 2>/dev/null | sort -k2 > "$RUN_DIR/build-mtimes-after.txt"

# 11. diff the before/after artifact mtimes -> ground truth of what actually rebuilt
join -j2 -o 1.1,2.1,0 "$RUN_DIR/build-mtimes-before.txt" "$RUN_DIR/build-mtimes-after.txt" 2>/dev/null \
	| awk '$1!=$2{print $3, "old="$1, "new="$2}' > "$RUN_DIR/rebuilt-artifacts.txt"
# artifacts that exist now but didn't before (new files)
comm -13 \
	<(awk '{print $2}' "$RUN_DIR/build-mtimes-before.txt" | sort) \
	<(awk '{print $2}' "$RUN_DIR/build-mtimes-after.txt" | sort) \
	>> "$RUN_DIR/rebuilt-artifacts.txt"

# 12. what make itself reported building/linking
grep -E "^\[.*%\] (Building|Linking)" "$RUN_DIR/build.log" > "$RUN_DIR/rebuilt-per-make-output.txt"

# 13. summary line
DURATION=$((BUILD_END - BUILD_START))
RECONF_DURATION=$((RECONF_END - RECONF_START))
N_COMPILED=$(grep -c "^\[.*%\] Building" "$RUN_DIR/build.log")
N_LINKED=$(grep -c "^\[.*%\] Linking" "$RUN_DIR/build.log")
PEAK_MEM=$(awk -F'used=' '{split($2,a," "); gsub("MB","",a[1]); if(a[1]+0>m)m=a[1]+0} END{print m+0}' "$RUN_DIR/memory-samples.txt")
GIT_HEAD=$(cat "$RUN_DIR/git-head.txt")
DIRTY_FILES=$(wc -l < "$RUN_DIR/git-status.txt")

if [[ ! -f "$LOG_ROOT/summary.tsv" ]]; then
	printf 'run_id\ttimestamp\tgit_head\tlabel\tdirty_files\treconf_s\tbuild_s\tcompiled\tlinked\tpeak_mem_MB\texit\trun_dir\n' > "$LOG_ROOT/summary.tsv"
fi
printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
	"$RUN_ID" "$(date -Iseconds)" "$GIT_HEAD" "$LABEL" "$DIRTY_FILES" "$RECONF_DURATION" "$DURATION" "$N_COMPILED" "$N_LINKED" "$PEAK_MEM" "$BUILD_EXIT" "$RUN_DIR" \
	>> "$LOG_ROOT/summary.tsv"

log "done: exit=$BUILD_EXIT reconf=${RECONF_DURATION}s build=${DURATION}s compiled=$N_COMPILED linked=$N_LINKED peak_mem=${PEAK_MEM}MB"
log "details: $RUN_DIR"
log "summary appended: $LOG_ROOT/summary.tsv"
