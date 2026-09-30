<p align="center"><a href="https://cedarlogic.netlify.app"><img src="https://cedarlogic.netlify.app/assets/img/og.png" alt="CedarLogic: Build it. Watch it think. The free logic gate simulator for Mac." width="100%"></a></p>

# CedarLogic

A free, open-source digital logic simulator. Drag in gates, wire them up and watch the signals flow; then press one key for the truth table, the Karnaugh map and the simplest formula.

**[Website](https://cedarlogic.netlify.app)** · **[Download for Mac](https://cedarlogic.netlify.app/download/)** · [All releases](https://github.com/leviholliday/CedarLogic-Releases/releases) · [Truth table generator](https://cedarlogic.netlify.app/truth-table-generator/)

## CedarLogic for Mac

A native Mac app (SwiftUI and AppKit over this repository's C++ simulation engine), on the [`mac/native`](../../tree/mac/native/mac) branch. macOS 14 or later, Apple silicon.

- **Build fast:** 100+ parts; press **A** and type to add one, drag pin to pin to wire, **C** connects while you drag, **S** straightens.
- **Check your work:** **T** gives the truth table, a Karnaugh map for each light and the simplest SOP and POS; **Build from Formula** turns an expression into a wired circuit.
- **See it run:** Simulation View, an oscilloscope with timing-diagram export, wires that light up as you point at them, memory you can read and edit.
- **It keeps itself:** Your Circuits with automatic saving and version history, templates, and your own reusable parts.
- **Hand it in:** PNG and PDF export with your name and result, printing, and .cdl copies for older CedarLogic.

Build it with `mac/build.sh` (no Xcode needed); `mac/README.md` has the details and the checks.

CedarLogic began at [Cedarville University](https://www.cedarville.edu/) and was modernized and brought to the Mac by [Kieran Klukas](https://github.com/taciturnaxolotl/CedarLogic). This version was rebuilt and extended with Claude, Anthropic's AI model.


## The original

CedarLogic is a digital logic simulator made for university classroom instruction. It includes all the basic gates, buses, JK and D flip flops, muxes, decoders, and a [Z80 micro-processor](https://en.wikipedia.org/wiki/Zilog_Z80). At [Cedarville University](https://www.cedarville.edu/) it has been used by Computer Architecture 1 students to build and simulate a full [mano-machine](https://en.wikipedia.org/wiki/Mano_machine). 

## What's new in the classic (wx) app

This fork also reworks the classic cross-platform CedarLogic. It builds on [Kieran Klukas's modernized CedarLogic](https://github.com/taciturnaxolotl/CedarLogic) (which brought the Skia renderer, the Sparkle updater and much more) and on the original from Cedarville University, and adds these changes on top.

**Look and feel**
- Dark mode, an accent color, a dot grid, and a wire thickness setting
- A native Preferences window in place of the old dialog
- A restyled toolbar, a side panel you can resize by dragging, and gates that resize smoothly
- A status bar showing zoom, cursor position, and gate counts (each can be turned off)
- Smoother zooming and panning, with separate scroll settings for trackpad and mouse

**Simulation**
- Simulation View: a dark live mode where signals move along the wires as dashes, with a control bar
- A truth table generator

**Editing**
- Delete wires, connect by clicking, and press C to connect while dragging
- Pins that are close together connect when you drop a gate, not only pins that touch
- Straighten Route on a wire's right-click menu, and straightening several selected wires at once without them overlapping
- Cmd+D to duplicate, with a choice of whether it goes through the clipboard
- Shortcuts for zoom, nudging, focus mode, and panning with Space

**Pages and files**
- A Ctrl+Tab page switcher, and new tabs that open in front with a short animation
- Circuits are kept in the app with version history
- Exported images can include a strip with the circuit's name and result

**Stability**
- Fixed races when choosing New or Open while a simulation is running, which could make circuits behave unpredictably after opening
- Fixed a hang when letting a circuit settle, and false "circuit too heavy" warnings on manual steps
- Closed tabs no longer stay in memory after their undo history is gone
- Invalid time-step settings are corrected instead of crashing the simulation

## Contributing

All improvements, especially to stability, are welcome. Please see [Contributing](./docs/Contributing.md) for more.

## Building

To build the source code yourself, [clone the repo](https://docs.github.com/en/repositories/creating-and-managing-repositories/cloning-a-repository)
and read the [Building](./docs/Building.md) instructions.

Building doc also includes a few notes for development.

## File format

[CDL-Format](./docs/CDL-Format.md) defines the specification for the `.cdl` file
