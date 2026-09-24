# CedarLogic for Mac (native)

A SwiftUI/AppKit front end on top of the same C++ engine the wx app uses.
Nothing here is built by the main CMake project, and nothing here affects the
Windows or Linux builds.

    mac/build.sh              # -> mac/build/CedarLogic Native.app
    OPEN=1 mac/build.sh       # build, then launch
    CLEAN=1 mac/build.sh      # rebuild everything (after changing a header)

Needs only the Xcode Command Line Tools (Swift 6), macOS 14 or later.

- `CedarCore/` -- the engine for the app: a Core Graphics backend for the
  render Scene (`CGScene`), the document loader (`Document.cpp`) and the plain C
  interface Swift calls (`include/CedarCore.h`). It compiles the shared sources
  in `src/gui`, `format/` and `src/gui/route` with `CL_NO_WX`, which fences off
  the few wxWidgets pieces in them.
- `App/` -- the Swift app: documents, the canvas view, looks (presets you can
  customize) and Settings.

Stage 0 (this): open a .cdl and look around it. Next: simulation, editing,
wires, files and the library, then the extras.

`Tools/render_png.cpp` draws a page of a .cdl to a PNG through CedarCore, to
check the renderer without opening the app (see the build flags in build.sh).
