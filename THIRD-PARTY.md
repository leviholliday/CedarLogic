# Third-party code

What CedarLogic is built with that someone else wrote, and under which license. CedarLogic itself is under the [GNU GPL v3](LICENSE); the original Cedarville University code also carries the notice in [`res/LICENSE.txt`](res/LICENSE.txt) (BSD 3-Clause).

"Shipped" means a copy goes out inside the app or its installer. "System" means the app uses the copy already on the computer and ships none. "Build only" means it is used to build or test and doesn't reach users.

## CedarLogic for Mac

| What | Version | License | How | Where |
|---|---|---|---|---|
| [Sparkle](https://sparkle-project.org) | 2.10.1 | [MIT](https://github.com/sparkle-project/Sparkle/blob/2.x/LICENSE) | Shipped (`Contents/Frameworks/Sparkle.framework`), for updates | `mac/build.sh` downloads it and checks its SHA-256 |
| [Sentry Cocoa](https://github.com/getsentry/sentry-cocoa) | not pinned (TODO) | [MIT](https://github.com/getsentry/sentry-cocoa/blob/main/LICENSE.md) | Shipped when present at build time (`Sentry.framework`), for opt-in crash reports | `mac/build.sh` uses `build/_deps/sentry-cocoa/mac` if it is there; nothing in the repo downloads or pins it |
| Apple frameworks (AppKit, SwiftUI, Core Graphics, Core Text, CryptoKit, Compression, Security, SF Symbols) | macOS 14+ | Apple's terms | System | |

## CedarLogic for Windows

| What | Version | License | How | Where |
|---|---|---|---|---|
| [Monocypher](https://monocypher.org) | 4.0.2 | [BSD 2-Clause or CC0](update-signing/monocypher/LICENCE.md) | Shipped (compiled in), checks update signatures | `update-signing/monocypher/` |
| [Inno Setup](https://jrsoftware.org/isinfo.php) | 6 (CI installs 6.7.1) | [Inno Setup License](https://jrsoftware.org/files/is/license.txt) | Its setup program is shipped inside `CedarLogic-Windows-Setup-*.exe` | `windows/installer/CedarLogic.iss` |
| Win32, Direct2D, DirectWrite, WIC, WinHTTP, CNG, DPAPI, Media Foundation | Windows 10/11 | Microsoft's terms | System | |
| Fonts: Arial, Segoe UI, Segoe Fluent Icons (Windows 11) or Segoe MDL2 Assets (Windows 10) | | Microsoft's terms | System | |

The Windows app has its own deflate code for Share Link (`windows/App/Deflate`), written for it, because Windows has no zlib.

## CedarLogic for Linux

| What | Version | License | How | Where |
|---|---|---|---|---|
| [Monocypher](https://monocypher.org) | 4.0.2 | [BSD 2-Clause or CC0](update-signing/monocypher/LICENCE.md) | Shipped (compiled in), checks update signatures | `update-signing/monocypher/` |
| [OpenSSL](https://www.openssl.org) libcrypto | 3.x | [Apache 2.0](https://www.openssl.org/source/license.html) | Shipped in the AppImage only; the .deb uses the system's | `linux/package.sh` |
| [AppImage runtime](https://github.com/AppImage/type2-runtime), via [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) | | MIT | Shipped in the AppImage (the part that starts it) | `linux/package.sh` |
| [GTK 3](https://www.gtk.org), [GLib/GIO](https://gitlab.gnome.org/GNOME/glib) | 3.x / 2.x | LGPL 2.1 or later | System | `linux/CMakeLists.txt` |
| [Cairo](https://www.cairographics.org) | | LGPL 2.1 or MPL 1.1 | System | |
| [Pango](https://pango.gnome.org) | | LGPL 2.1 or later | System | |
| [fontconfig](https://www.freedesktop.org/wiki/Software/fontconfig/) | | MIT-style | System | |
| [FreeType](https://freetype.org) | | FreeType License or GPL 2 | System | |
| [libsecret](https://gitlab.gnome.org/GNOME/libsecret) | | LGPL 2.1 or later | System, optional: loaded at run time when installed, to keep the sync code in the desktop's keyring | `linux/App/Keyring.cpp` |
| [curl](https://curl.se) (the program) | | [curl license](https://curl.se/docs/copyright.html) | System program, for Sync and Send Feedback | |
| [FFmpeg](https://ffmpeg.org) (the program) | | LGPL 2.1 or later (GPL in some builds) | System program, optional: records Send Feedback videos as MP4 when installed | |
| Fonts: Liberation Sans or DejaVu Sans, and the desktop's own | | SIL OFL / Bitstream Vera-style | System | `linux/CedarCore/CairoScene.cpp` |
| Icons: the desktop's icon theme | | the theme's | System | `linux/App/Chrome.h` |

## Shared by every app

| What | License | How | Where |
|---|---|---|---|
| The original CedarLogic (Cedarville University, 2007 and 2017) | BSD 3-Clause ([`res/LICENSE.txt`](res/LICENSE.txt)) | Shipped: the simulator and much of the engine descend from it | `logic/`, `src/gui/` |
| [Kieran Klukas's CedarLogic](https://github.com/taciturnaxolotl/CedarLogic) | GPL v3 ([`LICENSE`](LICENSE)) | Shipped: the modernized engine this fork builds on | throughout |
| Classic help pages (the original CedarLogic help, taken out of `KLS_Logic.chm`) | as the original CedarLogic | Shipped as Classic Help in each app | `res/help/` |
| QR code encoder | written for CedarLogic, following the same steps and tables as Project Nayuki's [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) (MIT), as its header says | Shipped (compiled in), for sync QR codes | `mac/CedarCore/QrCodeGen.cpp` |

## Build and test only

| What | Version | License | Where |
|---|---|---|---|
| [doctest](https://github.com/doctest/doctest) | 2.4.11 | MIT | `logic/`, `format/` and `mac/Tools/headless` tests (fetched by CMake) |
| [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) | | MIT | makes the Linux AppImage |
| [Emscripten](https://emscripten.org) | latest in CI | MIT or University of Illinois/NCSA | `wasm/` (its output, with Emscripten's runtime code, is what a website build would ship) |
| CMake, Ninja, clang, MSVC, MinGW-w64, Python, Node, Xvfb, xdotool | | their own | build scripts and CI |

## The legacy wx app (CedarLogic Classic)

Only the root `CMakeLists.txt` builds these. None of them is in the native apps.

| What | Version | License | How |
|---|---|---|---|
| [wxWidgets](https://www.wxwidgets.org) | 3.2.9 (fetched) or the system's 3.2+ | [wxWindows Library Licence 3.1](https://www.wxwidgets.org/about/licence/) | Shipped (linked in statically when CMake fetches it; the system's copy on Linux) |
| [Skia](https://skia.org), the [aseprite build](https://github.com/aseprite/skia) | aseprite-m124 | BSD 3-Clause | Shipped (statically linked) |
| [Sparkle](https://sparkle-project.org) | 2.9.6 | MIT | Shipped in the Mac build |
| [WinSparkle](https://winsparkle.org) | 0.9.4 | MIT | Shipped in the Windows build |
| [Lucide](https://lucide.dev) icons | | ISC ([`res/icons/LICENSE.md`](res/icons/LICENSE.md)) | Shipped (toolbar icons) |
| `res/arial.glf` (glyph outlines for the OpenGL label font) | | TODO: unknown. In the repo since the original (2016); Arial itself belongs to Monotype | Shipped in the Mac wx bundle |
| [NSIS](https://nsis.sourceforge.io) | | zlib/libpng | its installer stub, in the Windows installer |
| OpenGL | | | System |
