#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
QT_WIN="${QT_WIN:-$ROOT/../toolchains/qt-win/5.15.2/mingw81_64}"
QT_HOST="${QT_HOST:-$ROOT/../toolchains/native-root/usr/lib/qt5}"
MINGW_ROOT="${MINGW_ROOT:-$ROOT/../toolchains/mingw-root/usr}"
BUILD="$ROOT/build-win"
OBJ="$BUILD/obj"
MOC_OUT="$BUILD/moc"
DIST="$BUILD/dist"
TOOLBIN="$BUILD/toolbin"
CXX="$MINGW_ROOT/bin/x86_64-w64-mingw32-g++-posix"
CC="$MINGW_ROOT/bin/x86_64-w64-mingw32-gcc-posix"
MOC="$QT_HOST/bin/moc"

export LD_LIBRARY_PATH="$ROOT/../toolchains/native-root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

mkdir -p "$OBJ" "$MOC_OUT" "$DIST/plugins/platforms" "$TOOLBIN"
# The bundled cross compiler was configured with /usr/bin tool paths.  Keep
# the build self-contained by supplying the matching assembler and linker
# through a private -B prefix instead of relying on host binutils.
ln -sf "$MINGW_ROOT/bin/x86_64-w64-mingw32-as" "$TOOLBIN/as"
ln -sf "$MINGW_ROOT/bin/x86_64-w64-mingw32-ld" "$TOOLBIN/ld"

while IFS= read -r header; do
  base="$(basename "$header" .h)"
  "$MOC" -I"$ROOT/src" -I"$ROOT/src/DVB_T2" -I"$ROOT/src/DSP" \
    -I"$ROOT/src/third_party/libhackrf" -I"$ROOT/src/third_party/libusb" \
    "$header" -o "$MOC_OUT/moc_${base}.cpp"
done < <(rg -l 'Q_OBJECT' "$ROOT/src" | sort)

COMMON=(
  -std=gnu++17 -O2 -mavx2 -msse4.1 -B"$TOOLBIN/"
  -DUNICODE -D_UNICODE -DWIN32 -DWIN64 -DQT_NO_DEBUG
  -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB
  -DQT_CONCURRENT_LIB -DQT_PRINTSUPPORT_LIB -DQT_CORE_LIB
  -I"$ROOT/src" -I"$ROOT/src/DVB_T2" -I"$ROOT/src/DSP"
  -I"$ROOT/src/third_party/libhackrf" -I"$ROOT/src/third_party/libusb"
  -I"$ROOT/src/fftw3" -I"$QT_WIN/include" -I"$QT_WIN/include/QtCore"
  -I"$QT_WIN/include/QtGui" -I"$QT_WIN/include/QtWidgets"
  -I"$QT_WIN/include/QtNetwork" -I"$QT_WIN/include/QtConcurrent"
  -I"$QT_WIN/include/QtPrintSupport" -I"$QT_WIN/mkspecs/win32-g++"
  -idirafter "$MINGW_ROOT/share/mingw-w64/include"
)

"$CC" -c "$ROOT/src/third_party/libhackrf/hackrf.c" \
  -o "$OBJ/hackrf.o" -O2 -B"$TOOLBIN/" -DHACKRF_STATIC -DLIBRARY_VERSION='"2026.09.03"' \
  -I"$ROOT/src/third_party/libhackrf" -I"$ROOT/src/third_party/libusb" \
  -idirafter "$MINGW_ROOT/share/mingw-w64/include" -pthread

sources=(
  "$ROOT/src/main.cpp"
  "$ROOT/src/main_window.cpp"
  "$ROOT/src/rx_hackrf_pro.cpp"
  "$ROOT/src/plot.cpp"
  "$ROOT/src/qcustomplot.cpp"
  "$ROOT"/src/DVB_T2/*.cpp
  "$ROOT/src/DVB_T2/LDPC/tables_handler.cc"
  "$MOC_OUT"/*.cpp
)

for source in "${sources[@]}"; do
  base="$(basename "$source")"
  if [[ -n "${ONLY_SOURCES:-}" && -f "$OBJ/${base%.*}.o" && " $ONLY_SOURCES " != *" $base "* ]];then continue;fi
  echo "Compiling $base"
  extra=(); [[ "$base" == qcustomplot.cpp ]] && extra=(-O1)
  "$CXX" "${COMMON[@]}" "${extra[@]}" -c "$source" -o "$OBJ/${base%.*}.tmp.o"
  mv "$OBJ/${base%.*}.tmp.o" "$OBJ/${base%.*}.o"
done

"$CXX" -mwindows -B"$TOOLBIN/" -o "$DIST/HackRFT2Viewer.exe" "$OBJ"/*.o \
  -L"$QT_WIN/lib" -L"$ROOT/src/third_party/libusb" -L"$ROOT/src/fftw3" \
  -lQt5Widgets -lQt5PrintSupport -lQt5Gui -lQt5Network -lQt5Concurrent -lQt5Core \
  -llibusb-1.0 -llibfftw3f-3 -lwinpthread \
  -ldwmapi -luxtheme -lversion -lnetapi32 -luserenv -liphlpapi -lws2_32 \
  -lole32 -loleaut32 -luuid -lwinmm -limm32 -lshlwapi -lshell32 \
  -lgdi32 -luser32 -lkernel32 -ladvapi32

cp "$QT_WIN/bin/Qt5Concurrent.dll" "$DIST/"
cp "$ROOT/qt.conf" "$DIST/"
cp "$QT_WIN/bin/Qt5Core.dll" "$QT_WIN/bin/Qt5Gui.dll" \
   "$QT_WIN/bin/Qt5Network.dll" "$QT_WIN/bin/Qt5PrintSupport.dll" \
   "$QT_WIN/bin/Qt5Widgets.dll" "$DIST/"
cp "$QT_WIN/plugins/platforms/qwindows.dll" "$DIST/plugins/platforms/"
cp "$MINGW_ROOT/lib/gcc/x86_64-w64-mingw32/13-posix/libgcc_s_seh-1.dll" \
   "$MINGW_ROOT/lib/gcc/x86_64-w64-mingw32/13-posix/libstdc++-6.dll" "$DIST/"
cp "$MINGW_ROOT/x86_64-w64-mingw32/lib/libwinpthread-1.dll" "$DIST/"
cp "$ROOT/src/fftw3/libfftw3f-3.dll" "$ROOT/src/third_party/libusb/libusb-1.0.dll" "$DIST/"


echo "Windows build completed: $DIST/HackRFT2Viewer.exe"
