# Programmer-review toolchain environment (Git Bash). Source this file.
# Resolves the Qt 6.10.1 MinGW kit that ships with C:\Qt; nothing is cached.
QT_ROOT=/c/Qt
QT_KIT=$QT_ROOT/6.10.1/mingw_64
MINGW_BIN=$(ls -d $QT_ROOT/Tools/mingw1310_64/bin 2>/dev/null || ls -d $QT_ROOT/Tools/mingw*/bin | tail -1)
export PATH="$MINGW_BIN:$QT_KIT/bin:$QT_ROOT/Tools/Ninja:$QT_ROOT/Tools/CMake_64/bin:$PATH"
WS=/c/Users/admin/Documents/my_workspace/stm32/triol/power_analyzer
OUT=$WS/.build/review-2026-11-11/programmer
SRC=$WS/telemetry-ux-docking/tests/review/2026-11-11/programmer
export QT_KIT WS OUT SRC
