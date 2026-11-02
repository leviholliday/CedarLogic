// The CedarLogic interface's left panel (the wx app's PaletteFrame and
// klsMiniMap): a category menu over tiles on the canvas's own colour, sized by
// Settings > Appearance > Gate size, and the minimap under it. Drag a tile out
// and the gate is on the canvas at once, following the pointer until you let
// go; click one and it follows the pointer until you click.

import AppKit
import SwiftUI

struct CLSidePanel: View {
    let document: CoreDocument
    let canvas: CanvasController
    /// The side of a split the minimap follows (the one you're working in).
    var mapCanvas: CanvasController? = nil
    let page: Int
    @ObservedObject private var prefs = Prefs.shared
    @AppStorage("paletteCategory") private var categoryIndex = 0
    @ObservedObject private var myParts = MyParts.shared
    /// The picker's tag for My Parts, after the library's categories.
    static let myPartsTag = 1000

    private var categories: [GateLibrary.Category] { GateLibrary.categories }

    var body: some View {
        let chrome = CLChrome(dark: prefs.dark)
        VStack(spacing: 0) {
            Picker("", selection: $categoryIndex) {
                ForEach(categories) { c in
                    Text(prefs.showCategoryKeys && c.index < 10 ? "\(c.title)    ⇧\((c.index + 1) % 10)" : c.title).tag(c.index)
                }
                Divider()
                Text("My Parts").tag(Self.myPartsTag)
            }
            .labelsHidden()
            .padding(.horizontal, 8).padding(.vertical, 8)
            if categoryIndex == Self.myPartsTag {
                if myParts.parts.isEmpty {
                    VStack(spacing: 8) {
                        Image(systemName: "shippingbox").font(.system(size: 22)).foregroundStyle(.secondary)
                        Text("Select some gates, then choose Edit \u{25B8} Save as Part\u{2026} to keep them here.")
                            .font(.system(size: 11.5)).foregroundStyle(.secondary).multilineTextAlignment(.center)
                    }
                    .padding(16).frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    PaletteGrid(gates: myParts.parts.map(\.gate),
                                size: CGFloat(prefs.gateSize), dark: prefs.dark, names: true, canvas: canvas)
                        .id(myParts.parts.map(\.id).joined())
                    Text("Right-click a part to rename or delete it.")
                        .font(.system(size: 10.5)).foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .frame(maxWidth: .infinity).padding(.horizontal, 10).padding(.bottom, 8)
                }
            } else {
                PaletteGrid(gates: categories.indices.contains(categoryIndex) ? GateLibrary.gates(in: categories[categoryIndex]) : [],
                            size: CGFloat(prefs.gateSize), dark: prefs.dark, names: prefs.showGateNames, canvas: canvas)
                    .id(categoryIndex)
                    .transition(.opacity)
            }
            Rectangle().fill(chrome.sash).frame(height: 1)
            let map = mapCanvas ?? canvas
            MiniMap(document: document, canvas: map, status: map.status, page: page)
                .id(ObjectIdentifier(map))
                .frame(height: 130)
        }
        .background(chrome.canvas)
        .animation(.easeOut(duration: 0.15), value: categoryIndex)
        .onReceive(NotificationCenter.default.publisher(for: .clPaletteCategory)) { n in
            guard let from = n.object as? CanvasController, from === canvas || from === canvas.partner, let i = n.userInfo?["index"] as? Int,
                  categories.indices.contains(i) else { return }
            categoryIndex = i
        }
    }
}

private struct PaletteGrid: View {
    let gates: [GateLibrary.Gate]
    let size: CGFloat
    let dark: Bool
    let names: Bool
    let canvas: CanvasController

    var body: some View {
        ScrollView {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: size + 4), spacing: 2)], spacing: 2) {
                ForEach(gates) { gate in
                    CLGateTile(gate: gate, size: size, dark: dark, names: names, canvas: canvas)
                }
            }
            .padding(.horizontal, 6).padding(.bottom, 8)
        }
    }
}

/// Gate pictures, drawn once per size and theme.
@MainActor
enum TileCache {
    private static var images: [String: NSImage] = [:]

    /// A saved part was renamed or deleted: draw it afresh.
    static func forget(_ name: String) {
        images = images.filter { !$0.key.hasPrefix(name + "|") }
    }

    static func image(_ name: String, size: CGFloat, dark: Bool, scale: CGFloat) -> NSImage {
        let key = "\(name)|\(Int(size))|\(dark)|\(scale)"
        if let img = images[key] { return img }
        let w = Int(size * scale), h = Int(size * 0.8 * scale)
        let img = NSImage(size: NSSize(width: size, height: size * 0.8))
        if let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                               space: CGColorSpace(name: CGColorSpace.sRGB)!,
                               bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) {
            // The engine draws y down, in points.
            ctx.translateBy(x: 0, y: CGFloat(h))
            ctx.scaleBy(x: scale, y: -scale)
            if let part = MyParts.shared.part(named: name) {
                MyParts.draw(part, in: ctx, width: size, height: size * 0.8, scale: scale, dark: dark)
            } else {
                cl_library_draw_gate(name, ctx, size, size * 0.8, scale, dark)
            }
            if let cg = ctx.makeImage() { img.addRepresentation(NSBitmapImageRep(cgImage: cg)) }
        }
        images[key] = img
        return img
    }
}

struct CLGateTile: View {
    let gate: GateLibrary.Gate
    let size: CGFloat
    let dark: Bool
    let names: Bool
    let canvas: CanvasController
    @Environment(\.displayScale) private var scale
    @ObservedObject private var prefs = Prefs.shared
    @State private var hover = false
    @State private var dragging = false
    @State private var placed = false
    @State private var target: CanvasController?

    var body: some View {
        VStack(spacing: 1) {
            Image(nsImage: TileCache.image(gate.name, size: size, dark: dark, scale: scale))
                .frame(width: size, height: size * 0.8)
            if names {
                Text(gate.caption)
                    .font(.system(size: 9.5))
                    .lineLimit(1).truncationMode(.tail)
                    .foregroundStyle(.secondary)
                    .frame(width: size + 4)
            }
        }
        .padding(2)
        .background(RoundedRectangle(cornerRadius: 6).fill(prefs.accentColor(dark: dark).opacity(hover ? 0.10 : 0)))
        .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(prefs.accentColor(dark: dark).opacity(hover ? 0.9 : 0), lineWidth: 1.5))
        .contentShape(Rectangle())
        .onHover { h in withAnimation(.easeOut(duration: 0.12)) { hover = h } }
        .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .global)
            .onChanged { v in drag(v) }
            .onEnded { v in drop(v) })
        .help(gate.name.hasPrefix(MyParts.prefix) ? "\(gate.caption) \u{2014} right-click to rename or delete" : gate.caption)
        .contextMenu {
            if let part = MyParts.shared.part(named: gate.name) {
                Button("Rename\u{2026}") { MyParts.renameAsking(part) }
                Button("Delete\u{2026}") { MyParts.deleteAsking(part) }
            }
        }
    }

    /// A point in window coordinates (SwiftUI's global space) in a canvas's
    /// own coordinates, whether or not it's over it.
    private func viewPoint(_ g: CGPoint, in c: CanvasController) -> CGPoint? {
        guard let view = c.view, let window = view.window, let content = window.contentView else { return nil }
        return view.convert(CGPoint(x: g.x, y: content.bounds.height - g.y), from: nil)
    }
    private func isOver(_ g: CGPoint, _ c: CanvasController) -> Bool {
        guard let p = viewPoint(g, in: c), let view = c.view else { return false }
        return view.bounds.contains(p)
    }

    /// In a split, whichever side the pointer is over takes the gate.
    private var sides: [CanvasController] { [canvas, canvas.partner].compactMap { $0 } }
    /// Over the palette: the side next to it.
    private var nearestSide: CanvasController {
        sides.min { a, b in
            (a.view.map { $0.convert($0.bounds, to: nil).minX } ?? .infinity) <
            (b.view.map { $0.convert($0.bounds, to: nil).minX } ?? .infinity)
        } ?? canvas
    }

    private func drag(_ v: DragGesture.Value) {
        let moved = abs(v.translation.width) > 3 || abs(v.translation.height) > 3
        if !moved && !dragging { return }
        dragging = true
        // The side under the pointer takes the gate (on the way to the right
        // side it crosses the left, and moves over with you); over the
        // palette, the side next to it.
        let under = sides.first { isOver(v.location, $0) }
        let c = under ?? target ?? nearestSide
        if c !== target {
            if let old = target, placed { old.cancelFloating() }
            placed = false
            target = c
        }
        guard let view = c.view, let document = c.document, let p = viewPoint(v.location, in: c) else { return }
        let world = view.worldPoint(p)
        c.pointerMoved(world)
        // Placed at once, even while the pointer is still over the palette:
        // the canvas only draws inside itself, so the gate slides out from
        // under the panel as it's pulled across its edge.
        if !placed {
            placed = c.addGateFloating(gate.name, at: world)
            if placed { c.onActivate?() }
        } else if c.isFloating {
            _ = document.hover(page: c.page, at: world, unitsPerPoint: view.unitsPerPoint)
            view.needsDisplay = true
        }
    }

    private func drop(_ v: DragGesture.Value) {
        let canvas = target ?? canvas
        defer { dragging = false; placed = false; target = nil }
        guard dragging else {
            // A click: the gate follows the pointer until the next click.
            self.canvas.routed.addGateFloating(gate.name)
            return
        }
        // C already put it down (and connected it): nothing more to do.
        guard placed, canvas.isFloating, let view = canvas.view, let document = canvas.document else { return }
        if isOver(v.location, canvas), let p = viewPoint(v.location, in: canvas) {
            let world = view.worldPoint(p)
            _ = document.press(page: canvas.page, at: world, modifiers: [], unitsPerPoint: view.unitsPerPoint)
            document.release(at: world)
            canvas.edited()
            view.window?.makeFirstResponder(view)
        } else {
            canvas.cancelFloating()   // let go off the canvas: never mind
        }
    }
}

/// The whole page, small, with the canvas's visible area outlined in red.
/// The circuit's picture is kept until the circuit or the fit changes, so
/// moving around only redraws the outline.
struct MiniMap: View {
    let document: CoreDocument
    let canvas: CanvasController
    @ObservedObject var status: CanvasStatus
    let page: Int
    @ObservedObject private var prefs = Prefs.shared
    @Environment(\.displayScale) private var scale
    @State private var cache: (key: String, image: CGImage)?

    private func fit(_ size: CGSize) -> (box: CGRect, upp: CGFloat)? {
        guard let view = canvas.view, size.width > 4, size.height > 4 else { return nil }
        let visible = view.visibleWorldRect
        var box = document.bounds(ofPage: page).map { $0.union(visible) } ?? visible
        box = box.insetBy(dx: -2, dy: -2)
        let upp = max(box.width / size.width, box.height / size.height)
        return (box, upp)
    }

    var body: some View {
        let _ = status.version
        let pal = CLPalette(dark: prefs.dark, simView: false)
        GeometryReader { geo in
            let size = geo.size
            if let f = fit(size), let view = canvas.view {
                let originX = f.box.midX - size.width / 2 * f.upp
                let originY = f.box.midY + size.height / 2 * f.upp
                let key = "\(canvas.editVersion)|\(page)|\(prefs.dark)|\(Int(size.width))x\(Int(size.height))|\(Int(originX * 4))|\(Int(originY * 4))|\(Int(f.upp * 10000))"
                let image = picture(key: key, size: size, originX: originX, originY: originY, upp: f.upp, pal: pal)
                let v = view.visibleWorldRect
                ZStack(alignment: .topLeading) {
                    if let image { Image(decorative: image, scale: scale).resizable().frame(width: size.width, height: size.height) }
                    Rectangle().strokeBorder(Color.red.opacity(0.9), lineWidth: 1)
                        .frame(width: max(4, v.width / f.upp), height: max(4, v.height / f.upp))
                        .offset(x: (v.minX - originX) / f.upp, y: (originY - v.maxY) / f.upp)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { g in
                    view.center(on: CGPoint(x: originX + g.location.x * f.upp, y: originY - g.location.y * f.upp))
                })
            }
        }
        .background(pal.canvas)
        .help("The whole page. Click or drag to move there.")
    }

    private func picture(key: String, size: CGSize, originX: CGFloat, originY: CGFloat, upp: CGFloat, pal: CLPalette) -> CGImage? {
        if let cache, cache.key == key { return cache.image }
        let w = Int(size.width * scale), h = Int(size.height * scale)
        guard w > 0, h > 0, let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.setFillColor(pal.canvasCG)
        ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
        ctx.translateBy(x: 0, y: CGFloat(h))
        ctx.scaleBy(x: scale, y: -scale)
        var o = CLDrawOptions(dark: prefs.dark, accent: Int32(prefs.accent), wireScale: 1,
                              simView: false, thumbnail: true, showSelection: false, selectionFade: 1,
                              ink: Int32(CL_INK_NEVER))
        cl_document_draw_ex(document.handle, Int32(page), ctx, scale, originX, originY, upp, &o)
        guard let img = ctx.makeImage() else { return nil }
        DispatchQueue.main.async { cache = (key, img) }
        return img
    }
}
