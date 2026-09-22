/*****************************************************************************
   Project: CEDAR Logic Simulator

   RenderMode: global rendering-mode flags. Extracted from the MainApp
   God-singleton (Workstream C; the code even noted doingBitmapExport
   "shouldn't be here"). Reach it via renderMode().
     doingBitmapExport - offscreen bitmap export in progress (skips GL display
                         lists / connection dots that don't survive it).
     headlessRender    - --render mode: load, dump a PNG, exit; suppress modals.
     darkMode          - the live app theme right now (screen only -- every
                         print/export path ignores this and stays light, see
                         RenderStyle::print()). The single source of truth the
                         canvases and window chrome read each frame; ThemeManager
                         (MainFrame) is what changes it.
*****************************************************************************/

#pragma once

class RenderMode {
public:
	bool doingBitmapExport = false;
	bool headlessRender = false;
	bool darkMode = false;
};

RenderMode& renderMode();
