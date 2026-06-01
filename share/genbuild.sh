#!/bin/sh
# ============================================================================
#  genbuild.sh -- generates build/build.h for the version banner.
#
#  v2.0.0.8 fix -- kept in sync with share/genbuild.bat (the Windows path).
#
#  Previous behaviour:
#    * BUILD_DATE came from `git log` -- the LAST COMMIT date -- so the
#      banner froze at the last commit (2026-04-21) on every build until a
#      new commit was made.  This repeatedly misled "is this my latest
#      binary?" checks.
#    * BUILD_DESC used `git describe --dirty` with no --always, so on a tree
#      whose tags are not ancestors of HEAD it returned empty and the desc
#      fell back to version.cpp's "v2.0.0.8-XDN-".
#
#  This version writes BOTH macros:
#    BUILD_DATE  -- actual BUILD WALL-CLOCK time (date '+...').  A build is
#                   timestamped when built, committed or not.  This is the
#                   value to trust for "when was this binary produced".
#    BUILD_DESC  -- `git describe --tags --always --dirty`.
#                   --always : short commit hash fallback when no tag is
#                              reachable, so BUILD_DESC is always populated.
#                   --dirty  : appends "-dirty" for uncommitted working tree.
#
#  Both macros are #ifndef-guarded in version.cpp, so defining them here
#  overrides the fallbacks.  build.h is regenerated every build
#  (use_build_info.pri sets .depends = FORCE).
#
#  Usage:  genbuild.sh <output-path>     (called by use_build_info.pri)
# ============================================================================

if [ $# -lt 1 ]; then
    echo "Usage: $0 <filename>"
    exit 1
fi

FILE="$1"

DESC=""
if command -v git >/dev/null 2>&1; then
    # --always: fall back to abbreviated commit hash when no tag is reachable.
    # --dirty:  mark a working tree with uncommitted changes.
    DESC="$(git describe --tags --always --dirty 2>/dev/null)"
fi

# Actual build wall-clock time (not commit time).
BUILDTIME="$(date '+%Y-%m-%d %H:%M:%S %z')"

if [ -n "$DESC" ]; then
    echo "#define BUILD_DESC \"$DESC\"" > "$FILE"
else
    echo "// No git description available" > "$FILE"
fi
echo "#define BUILD_DATE \"$BUILDTIME\"" >> "$FILE"

exit 0