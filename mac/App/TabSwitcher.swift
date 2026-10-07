// Ctrl+Tab (the wx app's tab switcher, TabSwitcherMac.mm and MainFrame's
// handleTabSwitchKey): tabs in the order you last used them, the one you're
// on first. A quick Ctrl+Tab flips to the previous tab; held for a moment,
// a glass panel of the tabs shows, Tab and Shift+Tab move through it, the
// pointer picks, and letting go of Ctrl switches. Escape changes nothing.

import AppKit
import QuartzCore
import SwiftUI

/// What a window offers the switcher: its tabs, most recently used first
/// (the one in front first), and how to go to one.
struct TabSwitchOffer {
    let document: CoreDocument
    let pages: [Int]
    let go: (Int) -> Void
}

@MainActor
final class TabSwitcher {
    static let shared = TabSwitcher()

    private var offer: TabSwitchOffer?
    private var active = false
    private var shown = false
    private var started = CACurrentMediaTime()
    private var timer: Timer?
    private var panel: NSPanel?
    private let model = SwitcherModel()
    private var monitor: Any?
    private var showMouse = NSPoint.zero

    /// The key monitor, ahead of the menus and the window's own Ctrl+Tab
    /// (which moves the keyboard focus between controls).
    func install() {
        guard monitor == nil else { return }
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] e in
            guard let self else { return e }
            let f = e.modifierFlags.intersection(.deviceIndependentFlagsMask)
            let ctrl = f.contains(.control) && f.isDisjoint(with: [.command, .option])
            if e.keyCode == 48 && ctrl {
                return self.key(backwards: f.contains(.shift)) ? nil : e
            }
            if e.keyCode == 53 && self.active {
                self.cancel()
                return nil
            }
            return e
        }
    }

    private func key(backwards: Bool) -> Bool {
        if active {
            guard let n = offer?.pages.count, n > 0 else { return true }
            model.selected = (model.selected + (backwards ? n - 1 : 1)) % n
            return true
        }
        guard let canvas = CanvasController.front, canvas.view?.window?.isKeyWindow == true,
              let o = canvas.tabSwitch?(), o.pages.count >= 2 else { return false }
        offer = TabSwitchOffer(document: o.document, pages: Array(o.pages.prefix(10)), go: o.go)
        active = true
        shown = false
        model.selected = backwards ? offer!.pages.count - 1 : 1
        started = CACurrentMediaTime()
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 0.015, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        return true
    }

    private func tick() {
        guard active else { timer?.invalidate(); return }
        if !NSEvent.modifierFlags.contains(.control) { commit(model.selected); return }
        // Only if Ctrl is still down after a moment: a quick Ctrl+Tab just
        // flips to the previous tab.
        if !shown && CACurrentMediaTime() - started >= 0.18 { show() }
    }

    private func commit(_ index: Int) {
        guard active, let o = offer else { return }
        let target = o.pages.indices.contains(index) ? o.pages[index] : nil
        cancel()
        if let target, index != 0 { o.go(target) }
    }

    private func cancel() {
        timer?.invalidate()
        timer = nil
        active = false
        shown = false
        offer = nil
        hide()
    }

    // MARK: The panel

    private static let thumbW: CGFloat = 208, thumbH: CGFloat = 130
    private static let labelH: CGFloat = 18, labelGap: CGFloat = 8, cardPad: CGFloat = 10
    private static let gap: CGFloat = 6, panelPad: CGFloat = 14, radius: CGFloat = 28, perRow = 5

    private func show() {
        guard let o = offer, let parent = CanvasController.front?.view?.window else { return }
        shown = true
        showMouse = NSEvent.mouseLocation
        let dark = Prefs.shared.dark
        let n = o.pages.count
        let perRow = min(n, Self.perRow), rows = (n + Self.perRow - 1) / Self.perRow
        let cellW = Self.thumbW + 2 * Self.cardPad
        let cellH = Self.cardPad + Self.thumbH + Self.labelGap + Self.labelH + Self.cardPad
        let W = CGFloat(perRow) * cellW + CGFloat(perRow - 1) * Self.gap + 2 * Self.panelPad
        let H = CGFloat(rows) * cellH + CGFloat(rows - 1) * Self.gap + 2 * Self.panelPad
        let pf = parent.frame
        let frame = NSRect(x: (pf.midX - W / 2).rounded(), y: (pf.midY - H / 2).rounded(), width: W, height: H)

        let cards = o.pages.map { p in
            SwitcherCard(title: o.document.pageName(p), image: Self.thumbnail(o.document, p, dark: dark))
        }

        let view = SwitcherView(cards: cards, model: model, dark: dark, cellW: cellW, cellH: cellH,
                                onHover: { [weak self] i in
                                    guard let self, NSEvent.mouseLocation != self.showMouse else { return }
                                    self.model.selected = i
                                },
                                onClick: { [weak self] i in
                                    DispatchQueue.main.async { self?.commit(i) }
                                })
        let host = NSHostingView(rootView: view)
        host.frame = NSRect(x: 0, y: 0, width: W, height: H)

        let p = SwitcherPanel(contentRect: frame, styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        p.isOpaque = false
        p.backgroundColor = .clear
        p.hasShadow = true
        p.isReleasedWhenClosed = false
        p.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
        if #available(macOS 26.0, *) {
            let glass = NSGlassEffectView(frame: host.frame)
            glass.cornerRadius = Self.radius
            glass.style = .regular
            glass.contentView = host
            p.contentView = glass
        } else {
            let blur = NSVisualEffectView(frame: host.frame)
            blur.material = .hudWindow
            blur.blendingMode = .behindWindow
            blur.state = .active
            blur.wantsLayer = true
            blur.layer?.cornerRadius = Self.radius
            blur.layer?.masksToBounds = true
            blur.addSubview(host)
            p.contentView = blur
        }
        p.alphaValue = 0
        parent.addChildWindow(p, ordered: .above)
        p.orderFront(nil)
        NSAnimationContext.runAnimationGroup { ctx in
            ctx.duration = 0.12
            ctx.timingFunction = CAMediaTimingFunction(name: .easeOut)
            p.animator().alphaValue = 1
        }
        panel = p
    }

    private func hide() {
        guard let p = panel else { return }
        panel = nil
        p.parent?.removeChildWindow(p)
        NSAnimationContext.runAnimationGroup({ ctx in
            ctx.duration = 0.10
            p.animator().alphaValue = 0
        }, completionHandler: { p.orderOut(nil) })
    }

    /// A tab as you last saw it (GUICanvas::renderThumbnail): the same centre
    /// and zoom as its view, scaled so the whole visible area fits. A tab
    /// never looked at is shown whole.
    private static func thumbnail(_ doc: CoreDocument, _ page: Int, dark: Bool) -> CGImage? {
        let scale: CGFloat = 2
        let w = Int(thumbW * scale), h = Int(thumbH * scale)
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.setFillColor(CLPalette(dark: dark, simView: false).canvasCG)
        ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
        ctx.translateBy(x: 0, y: CGFloat(h))
        ctx.scaleBy(x: scale, y: -scale)   // points, top-left origin, like the canvas
        // The view showing it now, or the camera it was left with.
        let id = doc.pageID(page)
        let front = CanvasController.front
        let showing = [front, front?.partner].compactMap { $0?.view }.first { $0.pageKey == id && $0.bounds.width > 50 }
        let size = showing?.bounds.size ?? front?.view?.bounds.size ?? CGSize(width: 900, height: 600)
        let cam: (CGPoint, CGFloat)? = showing.map { ($0.visibleCenter, $0.unitsPerPoint) } ?? doc.cameras[id]
        if let (center, upp) = cam, size.width > 50, size.height > 50 {
            let tUPP = max(size.width * upp / thumbW, size.height * upp / thumbH)
            let prefs = Prefs.shared
            let origin = CGPoint(x: center.x - thumbW / 2 * tUPP, y: center.y + thumbH / 2 * tUPP)
            if prefs.showGrid {
                CLGrid.draw(ctx, pal: CLPalette(dark: dark, simView: false), scale: scale, fade: 1,
                            origin: origin, unitsPerPoint: tUPP, size: CGSize(width: thumbW, height: thumbH))
            }
            var o = CLDrawOptions(dark: dark, accent: Int32(prefs.accent), wireScale: prefs.wireScale,
                                  simView: false, thumbnail: false, showSelection: false, selectionFade: 1,
                                  ink: Int32(CL_INK_FOLLOW))
            cl_document_draw_ex(doc.handle, Int32(page), ctx, Double(scale), origin.x, origin.y, tUPP, &o)
        } else {
            _ = cl_document_draw_fitted(doc.handle, Int32(page), ctx, Double(thumbW), Double(thumbH), 10, Double(scale),
                                        Int32(dark ? CL_STYLE_DARK : CL_STYLE_LIGHT))
        }
        return ctx.makeImage()
    }
}

/// Never takes the keyboard from the circuit window, which has to keep
/// getting the Tab presses and noticing Ctrl let go.
private final class SwitcherPanel: NSPanel {
    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }
}

private struct SwitcherCard: Identifiable {
    let id = UUID()
    let title: String
    let image: CGImage?
}

private final class SwitcherModel: ObservableObject {
    @Published var selected = 0
}

private struct SwitcherView: View {
    let cards: [SwitcherCard]
    @ObservedObject var model: SwitcherModel
    let dark: Bool
    let cellW: CGFloat, cellH: CGFloat
    let onHover: (Int) -> Void
    let onClick: (Int) -> Void

    var body: some View {
        let rows = stride(from: 0, to: cards.count, by: 5).map { Array($0..<min($0 + 5, cards.count)) }
        VStack(spacing: 6) {
            ForEach(rows, id: \.self) { row in
                HStack(spacing: 6) {
                    ForEach(row, id: \.self) { i in card(i) }
                }
            }
        }
        .padding(14)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func card(_ i: Int) -> some View {
        let on = i == model.selected
        return VStack(spacing: 8) {
            Group {
                if let img = cards[i].image {
                    Image(decorative: img, scale: 2).resizable().scaledToFit()
                } else {
                    Color.clear
                }
            }
            .frame(width: 208, height: 130)
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder((dark ? Color.white : Color.black).opacity(dark ? 0.14 : 0.12), lineWidth: 0.5))
            Text(cards[i].title)
                .font(.system(size: 12, weight: on ? .semibold : .regular))
                .foregroundStyle(on ? Color.primary : Color.secondary)
                .lineLimit(1).truncationMode(.tail)
                .frame(width: 208, height: 18)
        }
        .padding(10)
        .frame(width: cellW, height: cellH)
        .background(
            RoundedRectangle(cornerRadius: 18)
                .fill((dark ? Color.white : Color.black).opacity(on ? (dark ? 0.16 : 0.08) : 0))
                .overlay(RoundedRectangle(cornerRadius: 18).strokeBorder((dark ? Color.white : Color.black).opacity(on ? (dark ? 0.14 : 0.06) : 0)))
        )
        .contentShape(Rectangle())
        .onContinuousHover { phase in if case .active = phase { onHover(i) } }
        .onTapGesture { onClick(i) }
    }
}

