# Building 1.4.0

Windows x64 target: Qt 5.15.2 MinGW libraries, GCC 13.2 POSIX-threaded MinGW-w64, AVX2.
The Linux cross-build uses bash build-windows-cross.sh.
Set QT_WIN, QT_HOST (host moc prefix) and MINGW_ROOT when using different paths.
qt.conf sets the bundled plugin location. The script copies Qt, compiler runtime, FFTW and libusb DLLs.
For Qt Creator on Windows, open HackRFT2Viewer.pro in a MinGW 64-bit kit, build Release and run windeployqt.
Deploy FFTW, libusb 1.0.29 MinGW runtime beside the application; include qt.conf and platform plugins.
Copy the official unmodified VLC 3.0.23 Windows ZIP contents to a vlc subdirectory.
Build outputs are excluded from the source archive.

Native regression tests: bash build-tests-native.sh, with QT_NATIVE pointing to a Qt 5.15 development prefix.
The supplied Linux path layout expects Ubuntu x86_64 headers and shared libraries.
Dependencies: Qt5 Core/Gui/Widgets/Network/PrintSupport/Concurrent, libusb-1.0, single-precision FFTW3, OpenGL headers, GCC, rg.
AddressSanitizer and UndefinedBehaviorSanitizer are enabled by default; SANITIZE=0 disables instrumentation.
LeakSanitizer is disabled because task enumeration is unavailable in the build environment.
Set SKIP_CORE=1 for just the GUI check, NO_GUI=1 for core tests, TESTS for a space-separated subset.
Set BUILD to a separate directory when changing compiler/sanitizer flags. TEST_TIMEOUT defaults to 120 seconds per test.

The GUI test uses QT_QPA_PLATFORM=offscreen and writes build-native/gui-smoke-0.png through gui-smoke-3.png.
Tests do not require RF hardware. They validate specific components; they are not a full broadcast-I/Q reception test.

Packaging the final distribution:

```bash
python3 package-portable.py --vlc-zip /absolute/path/vlc-3.0.23-win64.zip --output /absolute/path/deliverables
```

This verifies the official VLC archive hash, includes the complete application source and notices,
and emits portable/source ZIPs and a SHA256 manifest. The output directory must be outside the project.
For a local incremental cross-build, ONLY_SOURCES may list changed C++ basenames; omit it for a clean/full validation build.
