@echo off
REM ===========================================================================
REM  genbuild.bat -- generates build/build.h for the version banner.
REM
REM  v2.0.0.8 fix.  The previous version of this script:
REM    * emitted ONLY #define BUILD_DATE, never BUILD_DESC, so the Windows
REM      build always fell back to version.cpp's "v2.0.0.8-XDN-" desc;
REM    * set BUILD_DATE from `git log` -- the LAST COMMIT date.  That made
REM      the banner read 2026-04-21 on every build until a new commit was
REM      made, which repeatedly misled diagnosis ("is this my latest
REM      binary?").  The build machinery was working -- it was reporting
REM      commit date, and the tree had not been committed.
REM
REM  This version writes BOTH macros into build.h:
REM    BUILD_DATE  -- the actual BUILD WALL-CLOCK time (%DATE% %TIME%).
REM                   A build is now timestamped when it is built, whether
REM                   or not anything was committed.  This is the value to
REM                   trust for "when was this binary produced".
REM    BUILD_DESC  -- `git describe --tags --always --dirty`.
REM                   --always  : falls back to a short commit hash when no
REM                               tag is reachable (this tree's situation:
REM                               tags exist but none are ancestors of HEAD),
REM                               so BUILD_DESC is ALWAYS populated.
REM                   --dirty   : appends "-dirty" when the working tree has
REM                               uncommitted changes -- so an uncommitted
REM                               build is visibly marked as such.
REM
REM  Both macros are #ifndef-guarded in version.cpp, so defining them here
REM  overrides the fallbacks.  build.h is regenerated every build
REM  (use_build_info.pri sets .depends = FORCE).
REM
REM  Usage:  genbuild.bat <output-path>      (called by use_build_info.pri)
REM ===========================================================================

if "%1"=="" (
    echo Usage: genbuild.bat ^<filename^>
    exit /b 1
)

REM --- BUILD_DESC: git describe, always populated, dirty-marked ---------------
set "DESC="
for /f "delims=" %%d in ('git describe --tags --always --dirty 2^>NUL') do set "DESC=%%d"

REM --- BUILD_DATE: actual build wall-clock time ------------------------------
REM %DATE% and %TIME% are locale-dependent; this records them verbatim.
set "BUILDTIME=%DATE% %TIME%"

REM --- write build.h --------------------------------------------------------
if defined DESC (
    echo #define BUILD_DESC "%DESC%" > %1
) else (
    echo // No git description available > %1
)
echo #define BUILD_DATE "%BUILDTIME%" >> %1

exit /b 0