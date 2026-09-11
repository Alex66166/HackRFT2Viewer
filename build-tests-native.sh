#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
QT_NATIVE="${QT_NATIVE:-$ROOT/../toolchains/native-root/usr}"
BUILD="${BUILD:-$ROOT/build-native}"
mkdir -p "$BUILD/obj" "$BUILD/moc"
export LD_LIBRARY_PATH="$QT_NATIVE/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM=offscreen
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_NATIVE/lib/x86_64-linux-gnu/qt5/plugins/platforms"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
FLAGS=(-DQT_NO_OPENGL -std=gnu++17 -O1 -g -fPIC -mavx2 -msse4.1 -DQT_WIDGETS_LIB -DQT_PRINTSUPPORT_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB
 -MMD -MP -I"$ROOT/src" -I"$ROOT/src/DVB_T2" -I"$ROOT/src/DSP" -I"$ROOT/src/fftw3" -I"$ROOT/src/third_party/libusb" -I"$ROOT/src/third_party/libhackrf"
 -I"$QT_NATIVE/include" -I"$QT_NATIVE/include/x86_64-linux-gnu/qt5")
for module in QtCore QtGui QtWidgets QtNetwork QtConcurrent QtPrintSupport;do FLAGS+=(-I"$QT_NATIVE/include/x86_64-linux-gnu/qt5/$module");done
if [[ "${SANITIZE:-1}" == 1 ]];then FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer);else FLAGS+=(-O2);fi
LIBS=(-L"$QT_NATIVE/lib/x86_64-linux-gnu" -Wl,-rpath,"$QT_NATIVE/lib/x86_64-linux-gnu" -lQt5Widgets -lQt5PrintSupport -lQt5Gui -lQt5Network -lQt5Concurrent -lQt5Core -l:libfftw3f.so.3 -l:libusb-1.0.so.0 -lpthread)
compile(){
 local source="$1" target="$2"
 local dependency_changed=0
 if [[ -f "$target.d" ]];then while IFS= read -r dependency;do [[ -f "$dependency" && "$dependency" -nt "$target" ]] && dependency_changed=1;done < <(sed 's/\\$//' "$target.d" | tr ' ' '\n');fi
 if [[ ! -f "$target" || "$source" -nt "$target" || "$dependency_changed" == 1 || "${REBUILD:-0}" == 1 ]];then
   echo "Compile $(basename "$source")"
   g++ "${FLAGS[@]}" -MF "$target.d" -c "$source" -o "$target.tmp"
   mv "$target.tmp" "$target"
 fi
}
if [[ "${SKIP_CORE:-0}" != 1 ]];then
 while IFS= read -r header;do
  base="$(basename "$header" .h)"
  "$QT_NATIVE/lib/qt5/bin/moc" "$header" -o "$BUILD/moc/moc_$base.cpp.tmp"
  if ! cmp -s "$BUILD/moc/moc_$base.cpp.tmp" "$BUILD/moc/moc_$base.cpp";then mv "$BUILD/moc/moc_$base.cpp.tmp" "$BUILD/moc/moc_$base.cpp";fi
 done < <(rg -l Q_OBJECT "$ROOT/src" | sort)
 gcc -O1 -g -DHACKRF_STATIC -DLIBRARY_VERSION='"2026.09.03"' -I"$ROOT/src/third_party/libusb" -I"$ROOT/src/third_party/libhackrf" -c "$ROOT/src/third_party/libhackrf/hackrf.c" -o "$BUILD/obj/hackrf.o"
 for source in "$ROOT/src/rx_hackrf_pro.cpp" "$ROOT"/src/DVB_T2/*.cpp "$ROOT/src/DVB_T2/LDPC/tables_handler.cc" "$BUILD"/moc/*.cpp;do
  base="$(basename "$source")";[[ "$base" == moc_plot.cpp || "$base" == moc_qcustomplot.cpp ]] && continue
  compile "$source" "$BUILD/obj/${base%.*}.o"
 done
fi
CORE=()
for object in "$BUILD"/obj/*.o;do
 case "$(basename "$object")" in main_window.o|plot.o|qcustomplot.o|moc_plot.o|moc_qcustomplot.o) continue;;esac
 CORE+=("$object")
done
if [[ "${SKIP_CORE:-0}" != 1 ]];then
 for name in ${TESTS:-p1_acquisition_test p2_acquisition_test receiver_regression_test bch_corrector_test fec_vector_test transport_output_test diagnostics_test};do
  g++ "${FLAGS[@]}" "$ROOT/tests/$name.cpp" "${CORE[@]}" "${LIBS[@]}" -o "$BUILD/$name"
  timeout "${TEST_TIMEOUT:-120}" "$BUILD/$name" "$@"
 done
fi
if [[ "${NO_GUI:-0}" == 1 ]];then exit 0;fi
GUI=()
for source in "$ROOT/src/main_window.cpp" "$ROOT/src/plot.cpp" "$ROOT/src/qcustomplot.cpp" "$BUILD/moc/moc_plot.cpp" "$BUILD/moc/moc_qcustomplot.cpp";do
 base="$(basename "$source")";object="$BUILD/obj/${base%.*}.o";compile "$source" "$object";GUI+=("$object")
done
g++ "${FLAGS[@]}" "$ROOT/tests/gui_smoke_test.cpp" "${CORE[@]}" "${GUI[@]}" "${LIBS[@]}" -o "$BUILD/gui_smoke_test"
timeout "${TEST_TIMEOUT:-120}" "$BUILD/gui_smoke_test"
