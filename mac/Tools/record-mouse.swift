// Records how your pointer moves, so the demo (App/DemoMode.swift) can move
// like you do. Run it, build a small circuit by hand, then press Ctrl-C:
//
//   swift mac/Tools/record-mouse.swift ~/Desktop/mouse.csv
//
// It writes one line about 120 times a second: seconds since the start,
// x and y (points, top left of the main display), and whether the left
// button is down. It only reads where the pointer is (no keys, no screen),
// so it needs no permission.

import AppKit

let path = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "mouse.csv"
FileManager.default.createFile(atPath: path, contents: nil)
guard let out = FileHandle(forWritingAtPath: path) else { print("Can't write \(path)"); exit(1) }
out.write("t,x,y,down\n".data(using: .utf8)!)
let height = NSScreen.screens.first?.frame.height ?? 0
let start = Date()
var lines = 0
signal(SIGINT) { _ in exit(0) }
atexit { print("\nSaved to \(path)") }
print("Recording your pointer. Build something, then press Ctrl-C here to stop.")
while true {
    let p = NSEvent.mouseLocation
    let down = NSEvent.pressedMouseButtons & 1 == 1 ? 1 : 0
    out.write(String(format: "%.4f,%.1f,%.1f,%d\n", Date().timeIntervalSince(start), p.x, height - p.y, down).data(using: .utf8)!)
    lines += 1
    usleep(8_333)
}
