// The canvas in the CedarLogic interface: drawn and driven the way the wx
// app's canvas is (GUICanvas / klsGLCanvas) -- its colours and grid, its
// little animations (a selection's halo fading in, the drag box fading out,
// the grid fading up on a new tab, a closing tab dimming, eased zoom), what a
// scroll does per device, the single-letter keys, Simulation View and Lock.

import AppKit

extension CircuitCanvasNSView {

    // MARK: Drawing

    func drawCL(_ ctx: CGContext) {
        let prefs = Prefs.shared
        let sim = controller?.simView ?? false
        let dark = prefs.dark || sim
        let pal = CLPalette(dark: dark, simView: sim)
        let scale = window?.backingScaleFactor ?? 2
        ctx.setFillColor(pal.canvasCG)
        ctx.fill(bounds)
        if prefs.showGrid { drawCLGrid(ctx, pal: pal, scale: scale, fade: controller?.appearProgress ?? 1) }
        guard let document else { return }
        var o = CLDrawOptions(dark: dark, accent: Int32(prefs.accent), wireScale: prefs.wireScale,
                              simView: sim, thumbnail: false, showSelection: true,
                              selectionFade: controller?.selectionFade ?? 1)
        cl_document_draw_ex(document.handle, Int32(page), ctx, scale, origin.x, origin.y, unitsPerPoint, &o)
        if sim {
            cl_simview_draw_flow(document.handle, Int32(page), ctx, scale, origin.x, origin.y, unitsPerPoint,
                                 controller?.flowPhase ?? 0, prefs.wireScale)
            // The wire under the pointer, lit up whole.
            let a = prefs.accentRGB(dark: true)
            cl_edit_draw_overlay(document.handle, Int32(page), ctx, scale, origin.x, origin.y, unitsPerPoint, a.0, a.1, a.2)
        } else {
            let a = prefs.accentRGB(dark: dark)
            let accent = CGColor(srgbRed: a.0, green: a.1, blue: a.2, alpha: 1)
            cl_edit_draw_overlay(document.handle, Int32(page), ctx, scale, origin.x, origin.y, unitsPerPoint, a.0, a.1, a.2)
            if let box = document.selectionBox {
                drawBox(ctx, viewRect(box), accent, 1)
            } else if let box = controller?.dragFadeBox, let alpha = controller?.dragFadeAlpha, alpha > 0 {
                drawBox(ctx, viewRect(box), accent, alpha)
            }
        }
        // A tab on its way out dims towards the background.
        if let p = controller?.closeProgress, p > 0 {
            ctx.setFillColor(pal.canvasCG.copy(alpha: p)!)
            ctx.fill(bounds)
        }
    }

    private func drawBox(_ ctx: CGContext, _ r: CGRect, _ accent: CGColor, _ alpha: Double) {
        ctx.setFillColor(accent.copy(alpha: 0.25 * alpha)!)
        ctx.fill(r)
        ctx.setStrokeColor(accent.copy(alpha: alpha)!)
        ctx.setLineWidth(1)
        ctx.stroke(r.insetBy(dx: 0.5, dy: 0.5))
    }

    /// GUICanvas::drawGridInto: a line (or dot) every grid unit, spread out
    /// so they're never closer than 13 pixels; every fifth one darker.
    private func drawCLGrid(_ ctx: CGContext, pal: CLPalette, scale: CGFloat, fade: Double) {
        CLGrid.draw(ctx, pal: pal, scale: scale, fade: fade, origin: origin, unitsPerPoint: unitsPerPoint, size: bounds.size)
    }

    // MARK: Camera

    /// The part of the page on screen, world coordinates (y up).
    var visibleWorldRect: CGRect {
        CGRect(x: origin.x, y: origin.y - bounds.height * unitsPerPoint,
               width: bounds.width * unitsPerPoint, height: bounds.height * unitsPerPoint)
    }

    func center(on world: CGPoint) {
        zoomAnim = nil
        origin = CGPoint(x: world.x - bounds.width * unitsPerPoint / 2, y: world.y + bounds.height * unitsPerPoint / 2)
        needsDisplay = true
    }

    /// Zoom to an exact scale, keeping the middle of the view where it is.
    func zoomTo(unitsPerPoint target: CGFloat) {
        zoom(by: unitsPerPoint / target, at: CGPoint(x: bounds.midX, y: bounds.midY))
    }

    var zoomAnimating: Bool { zoomAnim != nil }

    /// klsGLCanvas::animateZoomTo: ease to a new camera over 140 ms.
    func animateZoom(by factor: CGFloat) {
        let base = zoomAnim.map { ($0.toOrigin, $0.toUPP) } ?? (origin, unitsPerPoint)
        let c = CGPoint(x: bounds.midX, y: bounds.midY)
        let world = CGPoint(x: base.0.x + c.x * base.1, y: base.0.y - c.y * base.1)
        let upp = min(max(base.1 / factor, minUnitsPerPoint), maxUnitsPerPoint)
        startZoomAnimation(to: CGPoint(x: world.x - c.x * upp, y: world.y + c.y * upp), upp: upp)
    }

    /// Glide to `world` in the middle, zooming in if it's too far out to
    /// see a part (Find).
    func animateCenter(on world: CGPoint) {
        guard bounds.width > 0 else { center(on: world); return }
        let upp = min(unitsPerPoint, 0.12)
        startZoomAnimation(to: CGPoint(x: world.x - bounds.width * upp / 2, y: world.y + bounds.height * upp / 2), upp: upp)
    }

    func animateZoomToFit() {
        guard bounds.width > 0, bounds.height > 0 else { return }
        let box = document?.bounds(ofPage: page) ?? CGRect(x: -20, y: -15, width: 40, height: 30)
        let pad: CGFloat = 3
        let upp = min(max(max((box.width + 2 * pad) / bounds.width, (box.height + 2 * pad) / bounds.height), minUnitsPerPoint), maxUnitsPerPoint)
        startZoomAnimation(to: CGPoint(x: box.midX - bounds.width * upp / 2, y: box.midY + bounds.height * upp / 2), upp: upp)
    }

    private func startZoomAnimation(to o: CGPoint, upp: CGFloat) {
        zoomAnim = ZoomAnimation(fromOrigin: origin, toOrigin: o, fromUPP: unitsPerPoint, toUPP: upp, start: CACurrentMediaTime())
        stepZoomAnimation()
    }

    /// Called every frame by the controller's clock.
    func stepZoomAnimation() {
        guard let a = zoomAnim else { return }
        let t = min(1, (CACurrentMediaTime() - a.start) / 0.14)
        let e = CGFloat(1 - pow(1 - t, 3))
        // The scale eases geometrically; the origin follows.
        unitsPerPoint = a.fromUPP * pow(a.toUPP / a.fromUPP, e)
        origin = CGPoint(x: a.fromOrigin.x + (a.toOrigin.x - a.fromOrigin.x) * e,
                         y: a.fromOrigin.y + (a.toOrigin.y - a.fromOrigin.y) * e)
        if t >= 1 { zoomAnim = nil; origin = a.toOrigin; unitsPerPoint = a.toUPP }
        needsDisplay = true
    }

    /// Per device (Settings > Canvas): a wheel mouse zooms and a trackpad
    /// moves around, by default. Cmd+scroll always zooms; Shift+scroll moves
    /// sideways. "In" is the physical up, whatever natural scrolling says.
    func clScrollWheel(_ e: NSEvent) {
        zoomAnim = nil
        let prefs = Prefs.shared
        let p = convert(e.locationInWindow, from: nil)
        let trackpad = e.hasPreciseScrollingDeltas
        let cmd = e.modifierFlags.contains(.command), shift = e.modifierFlags.contains(.shift)
        let dx = e.scrollingDeltaX, dy = e.scrollingDeltaY
        let vertical = abs(dy) >= abs(dx)
        let zooms = trackpad ? prefs.trackpadScroll == 0 : prefs.mouseWheel == 0
        let reverse = trackpad ? prefs.reverseTrackpad : prefs.reverseWheel
        let inSign: CGFloat = (e.isDirectionInvertedFromDevice ? -1 : 1) * (reverse ? -1 : 1)
        let steps = trackpad ? dy / 40 : dy / 4
        if cmd {
            zoom(by: pow(1.25, steps), at: p)
        } else if zooms && !shift && vertical {
            zoom(by: pow(1.25, steps * inSign), at: p)
        } else if trackpad {
            pan(byPoints: dx, dy)
        } else if shift || !vertical {
            pan(byPoints: (shift ? dy : dx) * 12, 0)
        } else {
            pan(byPoints: 0, dy * 12)
        }
    }

    // MARK: Pointer

    /// Returns true when it handled the press.
    func clMouseDown(_ event: NSEvent, at p: CGPoint) -> Bool {
        guard let controller, let document else { return false }
        zoomAnim = nil
        if CanvasController.pendingGate != nil, controller.placePendingGate(at: worldPoint(p)) {
            needsDisplay = true
            return true   // it's on the pointer now; the next click drops it
        }
        if spaceDown || event.modifierFlags.contains(.command) { return false }
        // Simulation View and Lock: parts still take clicks (switches,
        // keypads); anywhere else, a drag moves around.
        if controller.simView || controller.locked {
            if document.click(page: page, at: worldPoint(p)) {
                controller.redraw()
            } else {
                drag = .pan(last: p)
                NSCursor.closedHand.set()
                if controller.locked && !controller.simView { controller.lockNudge() }
            }
            return true
        }
        if event.clickCount == 2 && !controller.isFloating {
            _ = document.press(page: page, at: worldPoint(p), modifiers: [], unitsPerPoint: unitsPerPoint)
            document.release(at: worldPoint(p))
            if document.singleSelectedGate(page: page) != nil {
                controller.showSettings()
            } else {
                animateZoomToFit()
            }
            controller.selectionChanged()
            return true
        }
        return false
    }

    func clRightMouseDown(_ event: NSEvent) -> Bool {
        guard let controller, let document else { return true }
        if controller.simView { return true }
        if controller.locked { controller.lockNudge(); return true }
        if Prefs.shared.rightClickRotate {
            let p = worldPoint(convert(event.locationInWindow, from: nil))
            if document.contextTarget(page: page, at: p, unitsPerPoint: unitsPerPoint) == .gate {
                controller.rotate()
                return true
            }
        }
        return false
    }

    // MARK: Keys

    /// The canvas's keys (GUICanvas::OnKeyDown), with the single-letter ones
    /// looked up in Settings > Shortcuts. Returns true when it handled the key.
    /// Something is being moved: dragged with the button down, or floating.
    private func moving() -> Bool {
        if case .edit = drag { return true }
        return controller?.isFloating ?? false
    }

    func clKeyDown(_ e: NSEvent) -> Bool {
        guard let controller else { return false }
        let flags = e.modifierFlags.intersection([.command, .option, .control, .shift])
        let shift = flags.contains(.shift)
        let bare = flags.subtracting(.shift).isEmpty

        if controller.simView {
            switch e.keyCode {
            case 53: controller.simView = false
            case 49: if !e.isARepeat { controller.toggleRunning() }
            case 123: pan(byPoints: 40, 0)
            case 124: pan(byPoints: -40, 0)
            case 125: pan(byPoints: 0, -40)
            case 126: pan(byPoints: 0, 40)
            default:
                let ch = e.charactersIgnoringModifiers ?? ""
                if ch == "=" || ch == "+" { animateZoom(by: 1 / 0.75) }
                else if ch == "-" { animateZoom(by: 0.75) }
                else if let a = ShortcutStore.shared.canvasAction(for: e),
                        [.stepClock, .truthTable, .checkCircuit, .shortcuts, .darkMode, .nextTab, .previousTab].contains(a) {
                    controller.perform(a)
                } else { return bare }
            }
            return true
        }
        if controller.tidyActive && bare && [36, 76, 53, 48].contains(e.keyCode) { return false }   // the preview's keys
        if e.keyCode == 53 && CanvasController.pendingGate != nil { CanvasController.pendingGate = nil; return true }
        // Escape mid-move: first just the connections C made, then the move.
        if e.keyCode == 53, let document, moving(), cl_edit_take_back_connects(document.handle) > 0 {
            controller.note("Took back the connections.")
            controller.redraw()
            return true
        }
        if e.keyCode == 53 && controller.isFloating { controller.cancelFloating(); return true }
        if e.keyCode == 53, case .edit = drag {
            document?.cancelGesture()
            drag = .none
            controller.edited()
            return true
        }

        // Arrows nudge the selection, or move around when nothing's selected.
        if bare && (123...126).contains(e.keyCode) && !(controller.hasSelection && controller.canEdit) {
            let step: CGFloat = shift ? 200 : 40
            switch e.keyCode {
            case 123: pan(byPoints: step, 0)
            case 124: pan(byPoints: -step, 0)
            case 125: pan(byPoints: 0, -step)
            default: pan(byPoints: 0, step)
            }
            return true
        }
        // Shift+1...9, 0: that palette category (the tenth is 0).
        if flags == .shift, let n = [18: 1, 19: 2, 20: 3, 21: 4, 23: 5, 22: 6, 26: 7, 28: 8, 25: 9, 29: 10][Int(e.keyCode)] {
            NotificationCenter.default.post(name: .clPaletteCategory, object: controller, userInfo: ["index": n - 1])
            return true
        }
        let action = ShortcutStore.shared.canvasAction(for: e)
        // C while a gate is moving (dragged, or on the pointer from the
        // palette, A or a paste): connect it to the pins it's next to and keep
        // moving, as in wx. The connections are kept at the drop; Escape
        // takes back just them.
        if action == .quickCopy, moving() {
            // Not while it's still over the palette: it would land out of sight.
            let overCanvas = window.map { bounds.contains(convert($0.mouseLocationOutsideOfEventStream, from: nil)) } ?? true
            if controller.canEdit, overCanvas, let document {
                let n = Int(cl_edit_connect_while_moving(document.handle, Int32(page), unitsPerPoint))
                if n > 0 { controller.note("Connected \(n) pin\(n == 1 ? "" : "s"). Escape takes it back.") }
                else if n == 0 { controller.note("Nothing close enough to connect.") }
                controller.redraw()
            }
            return true
        }
        if let action {
            if !controller.canEdit && action.editsCircuit { controller.lockNudge(); return true }
            controller.perform(action)
            return true
        }
        if bare && (e.keyCode == 51 || e.keyCode == 117) && !controller.canEdit { controller.lockNudge(); return true }
        return false
    }

    /// Space let go: a tap zooms to fit (a hold-and-drag moved around).
    func clSpaceTapped(panned: Bool) {
        if controller?.simView == true { return }   // there Space pauses (on the way down)
        if !panned { animateZoomToFit() }
    }
}

/// An eased camera move (see animateZoom).
struct ZoomAnimation {
    var fromOrigin: CGPoint, toOrigin: CGPoint
    var fromUPP: CGFloat, toUPP: CGFloat
    var start: CFTimeInterval
}

extension ShortcutAction {
    /// Acts on one page, so in a split it goes to the side in use.
    var actsOnPage: Bool {
        switch self {
        case .exportImage, .print, .cut, .copy, .paste, .duplicate, .selectAll, .addGate, .rotate, .straighten,
             .tidy, .quickCopy, .quickPaste, .quickCut, .quickDuplicate, .zoomIn, .zoomOut, .zoomFit, .zoomActual,
             .stepClock, .truthTable, .checkCircuit, .closeTab, .newTab: true
        default: false
        }
    }

    /// Refused while the circuit is locked.
    var editsCircuit: Bool {
        switch self {
        case .cut, .paste, .duplicate, .addGate, .rotate, .straighten, .tidy, .quickPaste, .quickCut, .quickDuplicate: true
        default: false
        }
    }
}

extension Notification.Name {
    static let clPaletteCategory = Notification.Name("clPaletteCategory")
}

/// The canvas grid (the wx GUICanvas grid), for the canvas and for pictures
/// of it such as Ctrl+Tab's.
@MainActor
enum CLGrid {
    static func draw(_ ctx: CGContext, pal: CLPalette, scale: CGFloat, fade: Double,
                     origin: CGPoint, unitsPerPoint: CGFloat, size: CGSize) {
        let prefs = Prefs.shared
        let unitsPerPixel = unitsPerPoint / scale
        let space = max(1, Int(13 * unitsPerPixel))
        let hair = 1 / scale
        let x0 = Int(floor(origin.x / CGFloat(space))), x1 = Int(ceil((origin.x + size.width * unitsPerPoint) / CGFloat(space)))
        let y0 = Int(floor((origin.y - size.height * unitsPerPoint) / CGFloat(space))), y1 = Int(ceil(origin.y / CGFloat(space)))
        guard x1 >= x0, y1 >= y0, x1 - x0 < 4000, y1 - y0 < 4000 else { return }
        func major(_ i: Int) -> Bool { prefs.majorGrid && ((i % 5) + 5) % 5 == 0 }
        func sx(_ i: Int) -> CGFloat { (CGFloat(i * space) - origin.x) / unitsPerPoint }
        func sy(_ i: Int) -> CGFloat { (origin.y - CGFloat(i * space)) / unitsPerPoint }

        if prefs.gridStyle == 1 {
            let r = 1.1 / scale, rMajor = 1.7 / scale
            let dot = pal.grid(0.08 * 3 * fade), dotMajor = pal.grid(0.08 * 5 * fade)
            guard (x1 - x0) * (y1 - y0) < 60000 else { return }
            for ix in x0...x1 {
                for iy in y0...y1 {
                    let m = major(ix) && major(iy)
                    let rr = m ? rMajor : r
                    ctx.setFillColor(m ? dotMajor : dot)
                    ctx.fillEllipse(in: CGRect(x: sx(ix) - rr, y: sy(iy) - rr, width: 2 * rr, height: 2 * rr))
                }
            }
            return
        }
        for pass in 0..<2 {
            let isMajor = pass == 1
            // On the dark canvas the darker lines stay quieter, so low wires
            // don't read as grid.
            ctx.setStrokeColor(pal.grid((isMajor ? 0.08 * (pal.dark && !pal.simView ? 1.6 : 2.5) : 0.08) * fade))
            ctx.setLineWidth(hair)
            for i in x0...x1 where major(i) == isMajor {
                let x = (sx(i) * scale).rounded() / scale + hair / 2
                ctx.move(to: CGPoint(x: x, y: 0)); ctx.addLine(to: CGPoint(x: x, y: size.height))
            }
            for i in y0...y1 where major(i) == isMajor {
                let y = (sy(i) * scale).rounded() / scale + hair / 2
                ctx.move(to: CGPoint(x: 0, y: y)); ctx.addLine(to: CGPoint(x: size.width, y: y))
            }
            ctx.strokePath()
        }
    }
}
