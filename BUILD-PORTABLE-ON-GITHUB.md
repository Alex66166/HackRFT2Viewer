# One-click Windows portable build

The repository includes `.github/workflows/windows-portable.yml`.
It builds `HackRFT2Viewer.exe` on a Windows GitHub runner with MinGW-w64 + Qt 5,
runs `windeployqt`, adds the bundled FFTW/libusb DLLs, downloads the official
VLC 3.0.23 x64 ZIP, verifies SHA256
`992d19dbd0b8a7cde9167d2f7780b1ef6f92acc8a71acfa736101a21f35181e1`,
and creates:

- `HackRFT2Viewer-Portable-1.4.0.zip`
- `SHA256-1.4.0.txt`

Run it from GitHub Actions -> **Build Windows Portable** -> **Run workflow**.
