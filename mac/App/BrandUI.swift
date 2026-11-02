// The launch screen's look, for the places that introduce CedarLogic (the
// welcome, the guided tour): the icon's near-black green, its faint grid, a
// neon bloom, glass cards, and neon buttons with dark ink on them. Always
// this look, whatever the app's theme -- it's CedarLogic's own.

import SwiftUI

/// The ground: the icon's dark green, its grid showing faintly towards the
/// middle, and a green bloom near the top.
struct BrandBackground: View {
    var bloom: UnitPoint = .init(x: 0.5, y: 0.18)
    var gridStep: CGFloat = 28

    var body: some View {
        ZStack {
            LinearGradient(colors: [Brand.ink, Brand.inkDeep], startPoint: .top, endPoint: .bottom)
            Canvas { ctx, size in
                var p = Path()
                var x = size.width.truncatingRemainder(dividingBy: gridStep) / 2
                while x < size.width { p.move(to: CGPoint(x: x, y: 0)); p.addLine(to: CGPoint(x: x, y: size.height)); x += gridStep }
                var y = size.height.truncatingRemainder(dividingBy: gridStep) / 2
                while y < size.height { p.move(to: CGPoint(x: 0, y: y)); p.addLine(to: CGPoint(x: size.width, y: y)); y += gridStep }
                ctx.stroke(p, with: .color(Brand.neon.opacity(0.06)), lineWidth: 0.5)
            }
            .mask(RadialGradient(colors: [.white, .white.opacity(0)], center: .center, startRadius: 60, endRadius: 520))
            RadialGradient(colors: [Brand.neon.opacity(0.16), Brand.neon.opacity(0)], center: bloom, startRadius: 4, endRadius: 360)
        }
        .allowsHitTesting(false)
    }
}

/// Colours for text on the brand ground.
enum BrandText {
    static let primary = Color(white: 0.95)
    static let secondary = Brand.dim
    static let faint = Color.white.opacity(0.4)
}

/// A glass card on the brand ground; `lit` gives it the neon edge.
struct BrandCard<Content: View>: View {
    var lit = false
    var radius: CGFloat = 14
    @ViewBuilder let content: Content

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: radius, style: .continuous)
        content
            .background(shape.fill(lit ? Brand.neon.opacity(0.09) : Color.white.opacity(0.045)))
            .overlay(shape.strokeBorder(lit ? Brand.neon.opacity(0.6) : Color.white.opacity(0.09), lineWidth: lit ? 1.3 : 1))
            .shadow(color: lit ? Brand.neon.opacity(0.18) : .clear, radius: 12)
    }
}

/// The eyebrow, title and line over each brand page.
struct BrandHeading: View {
    let eyebrow: String
    let title: String
    let line: String

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(eyebrow.uppercased()).font(.system(size: 11, weight: .bold)).kerning(1.6).foregroundStyle(Brand.neon)
            Text(title).font(.system(size: 28, weight: .bold)).foregroundStyle(BrandText.primary)
            Text(line).font(.system(size: 13.5)).foregroundStyle(BrandText.secondary).fixedSize(horizontal: false, vertical: true)
        }
    }
}

/// Neon (primary) or glass (secondary) pill buttons.
struct BrandButtonStyle: ButtonStyle {
    var primary = true
    var width: CGFloat? = nil

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 13, weight: .semibold))
            .foregroundStyle(primary ? Brand.ink : BrandText.primary)
            .padding(.horizontal, 20)
            .frame(minWidth: width, minHeight: 36)
            .background(
                Capsule().fill(primary
                    ? AnyShapeStyle(LinearGradient(colors: [Brand.neon, Brand.neonDeep], startPoint: .top, endPoint: .bottom))
                    : AnyShapeStyle(Color.white.opacity(configuration.isPressed ? 0.14 : 0.08)))
            )
            .overlay(Capsule().strokeBorder(primary ? Color.white.opacity(0.35) : Color.white.opacity(0.14), lineWidth: 1))
            .shadow(color: primary ? Brand.neon.opacity(configuration.isPressed ? 0.2 : 0.4) : .clear, radius: 10)
            .scaleEffect(configuration.isPressed ? 0.97 : 1)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }
}

/// A key, as drawn on the brand pages; `lit` while it's pressed.
struct BrandKeyCap: View {
    let label: String
    var lit = false
    var size: CGFloat = 38

    var body: some View {
        Text(label)
            .font(.system(size: size * 0.42, weight: .bold, design: .rounded))
            .foregroundStyle(lit ? Brand.ink : BrandText.primary)
            .frame(minWidth: size, minHeight: size)
            .padding(.horizontal, label.count > 1 ? 6 : 0)
            .background(RoundedRectangle(cornerRadius: size * 0.22, style: .continuous)
                .fill(lit ? AnyShapeStyle(Brand.neon) : AnyShapeStyle(Color.white.opacity(0.07))))
            .overlay(RoundedRectangle(cornerRadius: size * 0.22, style: .continuous)
                .strokeBorder(lit ? Color.white.opacity(0.4) : Color.white.opacity(0.16)))
            .shadow(color: lit ? Brand.neon.opacity(0.6) : .clear, radius: 10)
    }
}
