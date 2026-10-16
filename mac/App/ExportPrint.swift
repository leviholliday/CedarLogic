// Export a page as a PNG or PDF, and print it. Both draw the whole page scaled
// to fit, either in the window's colors or as a black-and-white line drawing
// (the wx app's print style), which is what prints well.

import AppKit
import UniformTypeIdentifiers

enum PageExport {
    enum Format: Int { case png, pdf }
    enum Style: Int { case colors, blackAndWhite }

    /// Points per world unit: a two-input gate comes out about 50 points wide.
    static let pointsPerUnit: CGFloat = 12
    static let margin: CGFloat = 24

    static func size(of document: CoreDocument, page: Int, ink: Bool = false) -> CGSize? {
        guard let box = ImageExport.box(document, page: page, ink: ink) else { return nil }
        return CGSize(width: box.width * pointsPerUnit + 2 * margin,
                      height: box.height * pointsPerUnit + 2 * margin)
    }

    /// Draws into `ctx`, whose y axis points up (a bitmap or PDF context).
    static func draw(_ document: CoreDocument, page: Int, in ctx: CGContext, size: CGSize,
                     scale: CGFloat, style: Style, theme: Theme, ink: Bool = false) {
        let paper = style == .blackAndWhite ? CGColor(gray: 1, alpha: 1) : theme.canvas.cgColor
        ctx.setFillColor(paper)
        ctx.fill(CGRect(origin: .zero, size: size))
        ctx.saveGState()
        ctx.translateBy(x: 0, y: size.height)
        ctx.scaleBy(x: 1, y: -1)
        let clStyle = style == .blackAndWhite ? Int32(CL_STYLE_PRINT)
            : Int32(theme.darkCircuit ? CL_STYLE_DARK : CL_STYLE_LIGHT)
        _ = cl_document_draw_fitted_ink(document.handle, Int32(page), ctx, size.width, size.height,
                                        margin, scale, clStyle, ink)
        ctx.restoreGState()
    }

    static func png(_ document: CoreDocument, page: Int, style: Style, theme: Theme, ink: Bool = false) -> Data? {
        guard var size = size(of: document, page: page, ink: ink) else { return nil }
        // Twice the points (Retina-sharp), capped so a huge page stays sane.
        var scale: CGFloat = 2
        let longest = max(size.width, size.height) * scale
        if longest > 12000 { scale *= 12000 / longest }
        size = CGSize(width: size.width.rounded(.up), height: size.height.rounded(.up))
        let w = Int(size.width * scale), h = Int(size.height * scale)
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.scaleBy(x: scale, y: scale)
        draw(document, page: page, in: ctx, size: size, scale: scale, style: style, theme: theme, ink: ink)
        guard let image = ctx.makeImage() else { return nil }
        let rep = NSBitmapImageRep(cgImage: image)
        rep.size = size   // 144 dpi, so it opens at its point size
        return rep.representation(using: .png, properties: [:])
    }

    static func pdf(_ document: CoreDocument, page: Int, style: Style, theme: Theme, ink: Bool = false) -> Data? {
        guard let size = size(of: document, page: page, ink: ink) else { return nil }
        let data = NSMutableData()
        var box = CGRect(origin: .zero, size: size)
        guard let consumer = CGDataConsumer(data: data as CFMutableData),
              let ctx = CGContext(consumer: consumer, mediaBox: &box, nil) else { return nil }
        ctx.beginPDFPage(nil)
        draw(document, page: page, in: ctx, size: size, scale: 1, style: style, theme: theme, ink: ink)
        ctx.endPDFPage()
        ctx.closePDF()
        return data as Data
    }

    /// The Export panel: pick a place, a format and a style.
    @MainActor
    static func run(_ document: CoreDocument, page: Int, suggestedName: String, window: NSWindow?) {
        guard document.bounds(ofPage: page) != nil else {
            NSSound.beep()
            return
        }
        let defaults = UserDefaults.standard
        let panel = NSSavePanel()
        panel.nameFieldStringValue = suggestedName
        panel.canCreateDirectories = true
        panel.isExtensionHidden = false

        let format = NSPopUpButton()
        format.addItems(withTitles: ["PNG Image", "PDF Document"])
        format.selectItem(at: defaults.integer(forKey: "exportFormat"))
        let style = NSPopUpButton()
        style.addItems(withTitles: ["Colors (as on screen)", "Black & White (for printing)"])
        style.selectItem(at: defaults.integer(forKey: "exportStyle"))
        // Include drawing: when the page has one, starting as it's shown.
        let includeInk = NSButton(checkboxWithTitle: "Include drawing", target: nil, action: nil)
        includeInk.state = document.inkShown ? .on : .off
        var rows: [[NSView]] = [[NSTextField(labelWithString: "Format:"), format],
                                [NSTextField(labelWithString: "Style:"), style]]
        if document.inkStrokeCount(page: page) > 0 { rows.append([NSGridCell.emptyContentView, includeInk]) }
        let grid = NSGridView(views: rows)
        grid.column(at: 0).xPlacement = .trailing
        grid.rowSpacing = 8
        grid.translatesAutoresizingMaskIntoConstraints = false
        let accessory = NSView(frame: NSRect(x: 0, y: 0, width: 360, height: rows.count > 2 ? 100 : 76))
        accessory.addSubview(grid)
        NSLayoutConstraint.activate([grid.centerXAnchor.constraint(equalTo: accessory.centerXAnchor),
                                     grid.centerYAnchor.constraint(equalTo: accessory.centerYAnchor)])
        panel.accessoryView = accessory

        let applyType = {
            panel.allowedContentTypes = [format.indexOfSelectedItem == 1 ? .pdf : .png]
        }
        applyType()
        let formatTarget = MenuAction(applyType)
        format.target = formatTarget
        format.action = #selector(MenuAction.run)

        let finish: (NSApplication.ModalResponse) -> Void = { response in
            _ = formatTarget   // kept alive for the panel's lifetime
            guard response == .OK, let url = panel.url else { return }
            defaults.set(format.indexOfSelectedItem, forKey: "exportFormat")
            defaults.set(style.indexOfSelectedItem, forKey: "exportStyle")
            let s = Style(rawValue: style.indexOfSelectedItem) ?? .colors
            let theme = LookStore.shared.settings.theme
            let ink = document.inkStrokeCount(page: page) > 0 && includeInk.state == .on
            let data = format.indexOfSelectedItem == 1
                ? pdf(document, page: page, style: s, theme: theme, ink: ink)
                : png(document, page: page, style: s, theme: theme, ink: ink)
            do {
                guard let data else { throw CocoaError(.fileWriteUnknown) }
                try data.write(to: url, options: .atomic)
            } catch {
                NSAlert(error: error).runModal()
            }
        }
        if let window { panel.beginSheetModal(for: window, completionHandler: finish) }
        else { finish(panel.runModal()) }
    }
}

/// What File > Print prints: the page fitted to the paper, black and white,
/// with the drawing when the print panel's Include drawing says so.
final class PagePrintView: NSView {
    let document: CoreDocument
    let page: Int
    @objc dynamic var includeInk = false { didSet { needsDisplay = true } }

    init(document: CoreDocument, page: Int, paper: NSSize) {
        self.document = document
        self.page = page
        super.init(frame: NSRect(origin: .zero, size: paper))
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        ctx.setFillColor(.white)
        ctx.fill(bounds)
        _ = cl_document_draw_fitted_ink(document.handle, Int32(page), ctx, bounds.width, bounds.height,
                                        0, 1, Int32(CL_STYLE_PRINT), includeInk && document.inkStrokeCount(page: page) > 0)
    }

    @MainActor
    static func print(_ document: CoreDocument, page: Int, window: NSWindow?) {
        guard document.bounds(ofPage: page) != nil else {
            NSSound.beep()
            return
        }
        let info = (NSPrintInfo.shared.copy() as! NSPrintInfo)
        info.orientation = .landscape   // circuits run left to right, as in the wx app
        info.horizontalPagination = .fit
        info.verticalPagination = .fit
        info.isHorizontallyCentered = true
        info.isVerticallyCentered = true
        let paper = info.imageablePageBounds.size
        let view = PagePrintView(document: document, page: page, paper: paper)
        view.includeInk = document.inkShown
        let op = NSPrintOperation(view: view, printInfo: info)
        op.showsPrintPanel = true
        op.showsProgressPanel = true
        op.printPanel.options.formUnion([.showsOrientation, .showsPaperSize, .showsScaling])
        if document.inkStrokeCount(page: page) > 0 { op.printPanel.addAccessoryController(InkPrintAccessory(view)) }
        if let window { op.runModal(for: window, delegate: nil, didRun: nil, contextInfo: nil) }
        else { op.run() }
    }
}

/// The print panel's Include drawing checkbox; the preview follows it.
final class InkPrintAccessory: NSViewController, NSPrintPanelAccessorizing {
    private let target: PagePrintView

    init(_ target: PagePrintView) {
        self.target = target
        super.init(nibName: nil, bundle: nil)
        title = "Drawing"
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    override func loadView() {
        let box = NSButton(checkboxWithTitle: "Include drawing", target: self, action: #selector(toggled(_:)))
        box.state = target.includeInk ? .on : .off
        let v = NSView(frame: NSRect(x: 0, y: 0, width: 300, height: 32))
        box.frame.origin = NSPoint(x: 20, y: 6)
        v.addSubview(box)
        view = v
    }

    @objc private func toggled(_ sender: NSButton) {
        willChangeValue(forKey: "includeInk")
        target.includeInk = sender.state == .on
        didChangeValue(forKey: "includeInk")
    }

    @objc var includeInk: Bool { target.includeInk }

    func localizedSummaryItems() -> [[NSPrintPanel.AccessorySummaryKey: String]] {
        [[.itemName: "Drawing", .itemDescription: target.includeInk ? "Included" : "Left out"]]
    }

    func keyPathsForValuesAffectingPreview() -> Set<String> { ["includeInk"] }
}
