import AppKit
import MF1Core
import SwiftUI

// iScream's 240x240 screen: the sundae you are building, the toppings tray under it and the four
// values the knobs are on. Everything is drawn in screen pixels and scaled.

enum IScreamLayout {
    static let title = CGRect(x: 0, y: 0, width: 240, height: 34)
    static let art = CGRect(x: 0, y: 34, width: 240, height: 130)
    static func tile(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 34, y: 167, width: 31, height: 30) }
    static func chip(_ i: Int) -> CGRect { CGRect(x: 5 + CGFloat(i) * 59, y: 201, width: 55, height: 36) }
    static let pill = CGRect(x: 14, y: 204, width: 212, height: 28)
    // Where the point of the cone stands.
    static let apex = CGPoint(x: 120, y: 161)
}

// How tall the sundae stands, so it can be scaled to fit the screen.
struct SundaeShape {
    static let coneH: CGFloat = 50
    static let r: CGFloat = 25
    static let step: CGFloat = 22

    let scoops: Int
    let cream: Bool
    var scale: CGFloat {
        let h = -topY + (cream ? 36 : 4)
        return min(1.3, 122 / h)
    }
    // Scoop i, counted from the cone up, as a height above the point of the cone (negative is up).
    func center(_ i: Int) -> CGFloat { -(Self.coneH + 8) - CGFloat(i) * Self.step }
    var topY: CGFloat { center(scoops - 1) - Self.r }
}

private struct Rng {
    var s: UInt64
    init(_ seed: UInt64) { s = seed &* 0x9E3779B97F4A7C15 &+ 0x1234567 }
    mutating func next() -> CGFloat {
        s = s &* 6364136223846793005 &+ 1442695040888963407
        return CGFloat(s >> 40) / CGFloat(1 << 24)
    }
    mutating func range(_ lo: CGFloat, _ hi: CGFloat) -> CGFloat { lo + next() * (hi - lo) }
}

private func hexColor(_ v: UInt32, _ a: Double = 1) -> Color { Color(hex: v).opacity(a) }

private func mixColor(_ a: UInt32, _ b: UInt32, _ t: Double) -> Color {
    func ch(_ v: UInt32, _ s: UInt32) -> Double { Double((v >> s) & 0xff) }
    let r = ch(a, 16) * (1 - t) + ch(b, 16) * t
    let g = ch(a, 8) * (1 - t) + ch(b, 8) * t
    let bl = ch(a, 0) * (1 - t) + ch(b, 0) * t
    return Color(red: r / 255, green: g / 255, blue: bl / 255)
}

private func lighten(_ v: UInt32, _ t: Double) -> Color { mixColor(v, 0xffffff, t) }

struct IScreamScreen: View {
    @ObservedObject var model: IScreamModel
    // Changes on every screen frame; without a changing input, SwiftUI would skip the redraw.
    var frame = 0

    var body: some View {
        let t = Double(frame) / 60
        let peak = Double(mf1_peak())
        Canvas { ctx, size in
            ctx.scaleBy(x: size.width / 240, y: size.height / 240)
            var c = ctx
            draw(&c, time: t, peak: peak)
        }
    }

    private func text(_ ctx: inout GraphicsContext, _ s: String, _ p: CGPoint, size: CGFloat, color: Color,
                      anchor: UnitPoint = .center, weight: Font.Weight = .bold) {
        let t = Text(s).font(.system(size: size, weight: weight, design: .rounded)).foregroundColor(color)
        ctx.draw(t, at: p, anchor: anchor)
    }

    // ---------------------------------------------------------------- frame

    private func draw(_ ctx: inout GraphicsContext, time: Double, peak: Double) {
        let full = CGRect(x: 0, y: 0, width: 240, height: 240)
        ctx.fill(Path(full), with: .linearGradient(Gradient(colors: [Color(hex: 0xffe6f3), Color(hex: 0xffb4dc)]),
                                                     startPoint: .zero, endPoint: CGPoint(x: 0, y: 240)))
        drawBackdrop(&ctx, time: time)
        drawTitle(&ctx)
        if mf1_bank_pending() {
            drawBankPrompt(&ctx)
            return
        }
        drawSundae(&ctx, time: time, peak: peak)
        drawBadge(&ctx)
        drawTray(&ctx)
        if let pill = model.pill {
            drawPill(&ctx, pill)
        } else {
            drawChips(&ctx)
        }
    }

    private func drawBackdrop(_ ctx: inout GraphicsContext, time: Double) {
        var rng = Rng(7)
        for _ in 0..<26 {
            let x = rng.range(0, 240), y = rng.range(36, 162), r = rng.range(2, 7)
            ctx.fill(Path(ellipseIn: CGRect(x: x - r, y: y - r, width: r * 2, height: r * 2)), with: .color(.white.opacity(0.22)))
        }
        // A few sparkles that twinkle.
        for i in 0..<5 {
            let x = rng.range(10, 230), y = rng.range(40, 150)
            let a = 0.35 + 0.35 * sin(time * 2.2 + Double(i) * 1.7)
            sparkle(&ctx, CGPoint(x: x, y: y), 4, .white.opacity(a))
        }
    }

    private func sparkle(_ ctx: inout GraphicsContext, _ c: CGPoint, _ r: CGFloat, _ color: Color) {
        var p = Path()
        p.move(to: CGPoint(x: c.x, y: c.y - r))
        p.addQuadCurve(to: CGPoint(x: c.x + r, y: c.y), control: c)
        p.addQuadCurve(to: CGPoint(x: c.x, y: c.y + r), control: c)
        p.addQuadCurve(to: CGPoint(x: c.x - r, y: c.y), control: c)
        p.addQuadCurve(to: CGPoint(x: c.x, y: c.y - r), control: c)
        ctx.fill(p, with: .color(color))
    }

    private func drawTitle(_ ctx: inout GraphicsContext) {
        let info = model.info
        ctx.fill(Path(CGRect(x: 0, y: 0, width: 240, height: 34)), with: .color(Color(hex: 0xff5fae).opacity(0.18)))
        text(&ctx, info.title, CGPoint(x: 9, y: 14), size: 19, color: Color(hex: 0x8a1a57), anchor: .leading, weight: .heavy)
        text(&ctx, info.blurb, CGPoint(x: 10, y: 28), size: 9.5, color: Color(hex: 0xb23a7e), anchor: .leading, weight: .semibold)
        text(&ctx, "▾", CGPoint(x: 196, y: 15), size: 14, color: Color(hex: 0xb23a7e))
        drawBattery(&ctx, color: Color(hex: 0x8a1a57))
        ctx.fill(Path(CGRect(x: 0, y: 33, width: 240, height: 1)), with: .color(Color(hex: 0xff5fae).opacity(0.35)))
    }

    // The FM-1's battery icon where the firmware draws it. As in MF-1, the emulator's battery is a third full.
    private func drawBattery(_ ctx: inout GraphicsContext, color: Color) {
        let r = CGRect(x: 207, y: 9, width: 24, height: 13)
        ctx.stroke(Path(roundedRect: r, cornerRadius: 3), with: .color(color), lineWidth: 1.5)
        ctx.fill(Path(CGRect(x: 231.5, y: 13, width: 2, height: 5)), with: .color(color))
        ctx.fill(Path(CGRect(x: 209.75, y: 11.5, width: 5, height: 8)), with: .color(color))
    }

    private func drawBankPrompt(_ ctx: inout GraphicsContext) {
        let r = CGRect(x: 16, y: 56, width: 208, height: 120)
        ctx.fill(Path(roundedRect: r, cornerRadius: 14), with: .color(.white.opacity(0.85)))
        text(&ctx, "A bank arrived", CGPoint(x: 120, y: 82), size: 17, color: Color(hex: 0x8a1a57))
        text(&ctx, "Turn KNOB1–4 to store it", CGPoint(x: 120, y: 108), size: 12, color: Color(hex: 0xb23a7e))
        text(&ctx, "in bank A, B, C or D", CGPoint(x: 120, y: 124), size: 12, color: Color(hex: 0xb23a7e))
        text(&ctx, "HOME cancels", CGPoint(x: 120, y: 152), size: 11, color: Color(hex: 0xb23a7e), weight: .semibold)
        for i in 0..<4 {
            let b = IScreamLayout.chip(i)
            ctx.fill(Path(roundedRect: b, cornerRadius: 9), with: .color(.white.opacity(0.9)))
            text(&ctx, ["A", "B", "C", "D"][i], CGPoint(x: b.midX, y: b.midY), size: 18, color: Color(hex: 0xd2227f), weight: .heavy)
        }
    }

    // ---------------------------------------------------------------- sundae

    private func drawSundae(_ ctx: inout GraphicsContext, time: Double, peak: Double) {
        let s = model.sundae
        let info = model.info
        let n = max(1, min(3, Int(s[IS.scoops])))
        let creamOn = s[IS.top(IS.cream, 0)] != 0
        let shape = SundaeShape(scoops: n, cream: creamOn)
        let loud = min(1, peak * 4)
        let wobble = loud * 0.035 * sin(time * 18)
        var g = ctx
        g.translateBy(x: IScreamLayout.apex.x, y: IScreamLayout.apex.y)
        g.scaleBy(x: shape.scale * (1 - CGFloat(wobble) * 0.6), y: shape.scale * (1 + CGFloat(wobble)))

        // Ground shadow.
        g.fill(Path(ellipseIn: CGRect(x: -26, y: -2, width: 52, height: 8)), with: .color(Color(hex: 0xb23a7e).opacity(0.18)))

        drawCone(&g, s)
        let melt = Double(s[IS.melt] - 50) / 50
        for i in 0..<n {
            drawScoop(&g, cy: shape.center(i), info: info, seed: UInt64(flavorSeed() + i * 7))
        }
        if melt > 0.05 { drawMelt(&g, shape: shape, info: info, melt: melt, time: time) }
        let top = shape.center(n - 1)
        if s[IS.top(IS.fudge, 0)] != 0 { drawFudge(&g, cy: top, amount: Double(s[IS.top(IS.fudge, 1)]) / 100) }
        if s[IS.top(IS.caramel, 0)] != 0 { drawCaramel(&g, cy: top, amount: Double(s[IS.top(IS.caramel, 1)]) / 100, n: n, shape: shape) }
        if s[IS.top(IS.chips, 0)] != 0 { drawChips(&g, shape: shape, n: n, amount: Double(s[IS.top(IS.chips, 1)]) / 100) }
        if creamOn { drawCream(&g, base: CGPoint(x: 0, y: shape.topY + 5), k: 0.8 + CGFloat(s[IS.top(IS.cream, 1)]) / 100 * 0.35) }
        if s[IS.top(IS.sprinkles, 0)] != 0 {
            drawSprinkles(&g, top: shape.topY, cream: creamOn, center: top, amount: Double(s[IS.top(IS.sprinkles, 1)]) / 100, time: time)
        }
        if creamOn { drawCherry(&g, at: CGPoint(x: 1, y: shape.topY - 33 * (0.8 + CGFloat(s[IS.top(IS.cream, 1)]) / 100 * 0.35) + 2)) }
        if s[IS.fizz] > 0 { drawFizz(&g, shape: shape, amount: Double(s[IS.fizz]) / 100, time: time) }
        drawFocus(&g, shape: shape, time: time)
    }

    private func flavorSeed() -> Int { model.flavor * 31 + 5 }

    // The cone: its style is the filter's type, how toasted it is the cutoff, how crunchy the resonance.
    private func coneOutline(_ style: Int) -> Path {
        let h = SundaeShape.coneH
        var p = Path()
        if style == 2 {
            p.move(to: CGPoint(x: -32, y: -h))
            p.addLine(to: CGPoint(x: 32, y: -h))
            p.addLine(to: CGPoint(x: 19, y: -3))
            p.addQuadCurve(to: CGPoint(x: 14, y: 0), control: CGPoint(x: 18, y: 0))
            p.addLine(to: CGPoint(x: -14, y: 0))
            p.addQuadCurve(to: CGPoint(x: -19, y: -3), control: CGPoint(x: -18, y: 0))
        } else {
            p.move(to: CGPoint(x: -27, y: -h))
            p.addLine(to: CGPoint(x: 27, y: -h))
            p.addLine(to: CGPoint(x: 2.5, y: -1))
            p.addQuadCurve(to: CGPoint(x: -2.5, y: -1), control: CGPoint(x: 0, y: 3))
        }
        p.closeSubpath()
        return p
    }

    private func drawCone(_ ctx: inout GraphicsContext, _ s: [Int32]) {
        let style = Int(s[IS.coneStyle])
        let outline = coneOutline(style)
        if s[IS.coneOn] == 0 {
            // No cone: the shape is left as a faint dotted outline.
            ctx.stroke(outline, with: .color(.white.opacity(0.7)), style: StrokeStyle(lineWidth: 1.6, dash: [3, 3]))
            return
        }
        let toast = Double(s[IS.toast]) / 107
        let crunch = Double(s[IS.crunch]) / 100
        let h = SundaeShape.coneH
        var g = ctx
        g.clip(to: outline)
        if style == 2 {
            // A paper cup in pink and white stripes.
            g.fill(outline, with: .color(.white))
            var x: CGFloat = -34
            while x < 34 {
                var stripe = Path()
                stripe.move(to: CGPoint(x: x, y: -h))
                stripe.addLine(to: CGPoint(x: x + 8, y: -h))
                stripe.addLine(to: CGPoint(x: x * 0.58 + 4.6, y: 0))
                stripe.addLine(to: CGPoint(x: x * 0.58, y: 0))
                g.fill(stripe, with: .color(mixColor(0xff8fc8, 0xd2227f, toast)))
                x += 16
            }
            g.fill(Path(CGRect(x: -34, y: -h, width: 68, height: 6)), with: .color(Color(hex: 0xd2227f).opacity(0.85)))
        } else {
            let pale: UInt32 = style == 1 ? 0xf6dfae : 0xefc98a
            let deep: UInt32 = style == 1 ? 0xd9a35a : 0xc47f35
            g.fill(outline, with: .linearGradient(Gradient(colors: [mixColor(pale, deep, toast), mixColor(pale, deep, min(1, toast + 0.25))]),
                                                    startPoint: CGPoint(x: -20, y: -h), endPoint: CGPoint(x: 20, y: 0)))
            let ink = mixColor(deep, 0x6b3a12, 0.5).opacity(0.35 + 0.5 * crunch)
            let width: CGFloat = 0.9 + CGFloat(crunch) * 1.4
            if style == 0 {
                // Waffle: a crosshatch.
                var x: CGFloat = -60
                while x < 60 {
                    var a = Path(), b = Path()
                    a.move(to: CGPoint(x: x, y: -h)); a.addLine(to: CGPoint(x: x + 40, y: 0))
                    b.move(to: CGPoint(x: x + 40, y: -h)); b.addLine(to: CGPoint(x: x, y: 0))
                    g.stroke(a, with: .color(ink), lineWidth: width)
                    g.stroke(b, with: .color(ink), lineWidth: width)
                    x += 11
                }
            } else {
                // Sugar cone: fine ribs and a rim.
                var x: CGFloat = -30
                while x <= 30 {
                    var a = Path()
                    a.move(to: CGPoint(x: x, y: -h)); a.addLine(to: CGPoint(x: x * 0.08, y: 0))
                    g.stroke(a, with: .color(ink), lineWidth: width * 0.8)
                    x += 6
                }
                g.fill(Path(CGRect(x: -30, y: -h, width: 60, height: 7)), with: .color(mixColor(deep, 0x8a5420, 0.4).opacity(0.8)))
            }
        }
        ctx.stroke(outline, with: .color(Color(hex: 0x8a5a2a).opacity(0.55)), lineWidth: 1)
    }

    private func scallops(_ cx: CGFloat, _ y: CGFloat, _ width: CGFloat, _ count: Int, _ r: CGFloat) -> Path {
        var p = Path()
        for j in 0..<count {
            let x = cx - width / 2 + width * CGFloat(j) / CGFloat(count - 1)
            p.addEllipse(in: CGRect(x: x - r, y: y - r, width: r * 2, height: r * 2))
        }
        return p
    }

    private func drawScoop(_ ctx: inout GraphicsContext, cy: CGFloat, info: FlavorInfo, seed: UInt64) {
        let r = SundaeShape.r
        let c = CGPoint(x: 0, y: cy)
        // The ruffled foot under the ball.
        var foot = Path(roundedRect: CGRect(x: -r * 1.12, y: cy + r * 0.55, width: r * 2.24, height: r * 0.34), cornerRadius: r * 0.17)
        foot.addPath(scallops(0, cy + r * 0.86, r * 2.0, 9, r * 0.17))
        ctx.fill(foot, with: .linearGradient(Gradient(colors: [lighten(info.color, 0.08), mixColor(info.color, info.accent, 0.55)]),
                                              startPoint: CGPoint(x: 0, y: cy + r * 0.5), endPoint: CGPoint(x: 0, y: cy + r * 1.05)))
        ctx.stroke(foot, with: .color(hexColor(info.accent, 0.35)), lineWidth: 0.7)
        // The ball.
        let ball = Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: r * 2, height: r * 2))
        ctx.fill(ball, with: .radialGradient(
            Gradient(colors: [lighten(info.color, 0.3), hexColor(info.color), mixColor(info.color, info.accent, 0.75)]),
            center: CGPoint(x: c.x - r * 0.32, y: c.y - r * 0.38), startRadius: 0, endRadius: r * 1.5))
        var inside = ctx
        inside.clip(to: ball)
        drawBits(&inside, c: c, r: r, info: info, seed: seed)
        ctx.stroke(ball, with: .color(hexColor(info.accent, 0.4)), lineWidth: 0.8)
        ctx.fill(Path(ellipseIn: CGRect(x: c.x - r * 0.62, y: c.y - r * 0.72, width: r * 0.55, height: r * 0.3)),
                 with: .color(.white.opacity(0.4)))
    }

    // What is mixed through the scoop, so each flavor reads at a glance.
    private func drawBits(_ ctx: inout GraphicsContext, c: CGPoint, r: CGFloat, info: FlavorInfo, seed: UInt64) {
        var rng = Rng(seed)
        let bits = hexColor(info.bits)
        func spot() -> CGPoint {
            while true {
                let x = rng.range(-r, r), y = rng.range(-r, r)
                if x * x + y * y < r * r * 0.82 { return CGPoint(x: c.x + x, y: c.y + y) }
            }
        }
        switch info.pattern {
        case 1:
            for _ in 0..<18 {
                let p = spot()
                ctx.fill(Path(ellipseIn: CGRect(x: p.x - 1, y: p.y - 0.8, width: 2, height: 1.6)), with: .color(bits))
            }
        case 2:
            for _ in 0..<9 {
                let p = spot()
                let w = rng.range(4, 7), h = rng.range(3, 5)
                var q = ctx
                q.translateBy(x: p.x, y: p.y)
                q.rotate(by: .radians(Double(rng.range(0, 3))))
                q.fill(Path(roundedRect: CGRect(x: -w / 2, y: -h / 2, width: w, height: h), cornerRadius: 1.4), with: .color(bits))
            }
        case 3:
            for k in 0..<3 {
                var p = Path()
                let rad = r * (0.35 + 0.28 * CGFloat(k))
                p.addArc(center: CGPoint(x: c.x - r * 0.1, y: c.y + r * 0.1), radius: rad, startAngle: .degrees(200 + Double(k) * 25),
                         endAngle: .degrees(20 + Double(k) * 25), clockwise: false)
                ctx.stroke(p, with: .color(bits.opacity(0.85)), style: StrokeStyle(lineWidth: 3, lineCap: .round))
            }
        case 4:
            let cols = [bits, Color.white, hexColor(info.accent), mixColor(info.bits, 0xffffff, 0.5)]
            for k in 0..<16 {
                let p = spot()
                var q = ctx
                q.translateBy(x: p.x, y: p.y)
                q.rotate(by: .radians(Double(rng.range(0, 3))))
                q.fill(Path(roundedRect: CGRect(x: -2, y: -1, width: 4, height: 2), cornerRadius: 0.8), with: .color(cols[k % 4]))
            }
        case 5:
            var x = c.x - r * 1.6
            while x < c.x + r * 1.6 {
                var p = Path()
                p.move(to: CGPoint(x: x, y: c.y - r)); p.addLine(to: CGPoint(x: x + r * 0.8, y: c.y + r))
                ctx.stroke(p, with: .color(bits.opacity(0.75)), lineWidth: 3.2)
                x += 11
            }
        default: break
        }
    }

    // Melt: the scoop runs down over the cone.
    private func drawMelt(_ ctx: inout GraphicsContext, shape: SundaeShape, info: FlavorInfo, melt: Double, time: Double) {
        let r = SundaeShape.r
        let y0 = shape.center(0) + r * 0.9
        let xs: [CGFloat] = [-0.8, -0.3, 0.35, 0.82]
        for (i, f) in xs.enumerated() {
            let len = CGFloat(4 + 15 * melt) * CGFloat(0.7 + 0.3 * sin(time * 0.8 + Double(i) * 1.9))
            let x = f * r
            let drop = CGRect(x: x - 2.4, y: y0 - 2, width: 4.8, height: len + 2)
            ctx.fill(Path(roundedRect: drop, cornerRadius: 2.4), with: .color(hexColor(info.color)))
            ctx.fill(Path(ellipseIn: CGRect(x: x - 3, y: y0 + len - 3, width: 6, height: 6)), with: .color(hexColor(info.color)))
        }
    }

    private func drawFudge(_ ctx: inout GraphicsContext, cy: CGFloat, amount: Double) {
        let r = SundaeShape.r
        let ball = Path(ellipseIn: CGRect(x: -r, y: cy - r, width: r * 2, height: r * 2))
        let edge = cy - r * 0.45 + CGFloat(amount) * r * 0.75
        var g = ctx
        g.clip(to: ball)
        var cap = Path()
        cap.move(to: CGPoint(x: -r - 2, y: cy - r - 2))
        cap.addLine(to: CGPoint(x: r + 2, y: cy - r - 2))
        cap.addLine(to: CGPoint(x: r + 2, y: edge))
        var x = r + 2
        var up = true
        while x > -r - 2 {
            let nx = x - r * 0.45
            cap.addQuadCurve(to: CGPoint(x: nx, y: edge), control: CGPoint(x: (x + nx) / 2, y: edge + (up ? 6 : -2)))
            x = nx
            up.toggle()
        }
        cap.closeSubpath()
        g.fill(cap, with: .linearGradient(Gradient(colors: [Color(hex: 0x6a3a22), Color(hex: 0x3c1f12)]),
                                           startPoint: CGPoint(x: 0, y: cy - r), endPoint: CGPoint(x: 0, y: edge + 6)))
        g.fill(Path(ellipseIn: CGRect(x: -r * 0.55, y: cy - r * 0.8, width: r * 0.5, height: r * 0.22)), with: .color(.white.opacity(0.25)))
        // Drips run over the edge.
        let lens: [CGFloat] = [0.9, 0.45, 1.1, 0.6, 0.8]
        for (i, f) in lens.enumerated() {
            let x = -r * 0.78 + CGFloat(i) * r * 0.39
            let len = (4 + CGFloat(amount) * r * 0.7) * f
            let y = edge + 2
            let drop = CGRect(x: x - 2.4, y: y - 3, width: 4.8, height: len + 3)
            ctx.fill(Path(roundedRect: drop, cornerRadius: 2.4), with: .color(Color(hex: 0x4a2515)))
            ctx.fill(Path(ellipseIn: CGRect(x: x - 3, y: y + len - 3, width: 6, height: 6)), with: .color(Color(hex: 0x4a2515)))
        }
    }

    private func drawCaramel(_ ctx: inout GraphicsContext, cy: CGFloat, amount: Double, n: Int, shape: SundaeShape) {
        let r = SundaeShape.r
        let w: CGFloat = 1.6 + CGFloat(amount) * 2.6
        for k in 0..<2 {
            var p = Path()
            let y0 = cy - r * 0.1 + CGFloat(k) * r * 0.55
            p.move(to: CGPoint(x: -r * 0.9, y: y0))
            for j in 1...6 {
                let x = -r * 0.9 + CGFloat(j) * r * 0.3
                p.addLine(to: CGPoint(x: x, y: y0 + (j % 2 == 0 ? -5 : 5)))
            }
            ctx.stroke(p, with: .color(Color(hex: 0xd68a1f)), style: StrokeStyle(lineWidth: w, lineCap: .round, lineJoin: .round))
            ctx.stroke(p, with: .color(Color(hex: 0xffc866).opacity(0.8)), style: StrokeStyle(lineWidth: w * 0.35, lineCap: .round, lineJoin: .round))
        }
    }

    private func drawChips(_ ctx: inout GraphicsContext, shape: SundaeShape, n: Int, amount: Double) {
        var rng = Rng(99)
        let count = 5 + Int(amount * 14)
        let r = SundaeShape.r
        let top = shape.center(n - 1)
        for _ in 0..<count {
            let x = rng.range(-r * 0.85, r * 0.85)
            let y = top + rng.range(-r * 0.75, r * 1.9)
            guard y < shape.center(0) + r * 0.6 else { continue }
            var q = ctx
            q.translateBy(x: x, y: y)
            q.rotate(by: .radians(Double(rng.range(0, 6))))
            var tri = Path()
            tri.move(to: CGPoint(x: -2.6, y: 2))
            tri.addLine(to: CGPoint(x: 2.6, y: 2))
            tri.addLine(to: CGPoint(x: 0, y: -2.8))
            tri.closeSubpath()
            q.fill(tri, with: .color(Color(hex: 0x4a2a18)))
            q.fill(Path(CGRect(x: -0.4, y: -1.4, width: 1, height: 1.6)), with: .color(.white.opacity(0.25)))
        }
    }

    // A dollop of whipped cream with a curled tip. base is the middle of its foot.
    private func drawCream(_ ctx: inout GraphicsContext, base: CGPoint, k: CGFloat) {
        let tiers: [(w: CGFloat, h: CGFloat, y: CGFloat)] = [(46, 15, 0), (35, 14, -9), (24, 13, -17)]
        for t in tiers {
            let rect = CGRect(x: base.x - t.w * k / 2, y: base.y + t.y * k - t.h * k / 2, width: t.w * k, height: t.h * k)
            let p = Path(roundedRect: rect, cornerRadius: t.h * k / 2)
            ctx.fill(p, with: .linearGradient(Gradient(colors: [.white, Color(hex: 0xe8e4f4)]),
                                               startPoint: CGPoint(x: 0, y: rect.minY), endPoint: CGPoint(x: 0, y: rect.maxY)))
            ctx.stroke(p, with: .color(Color(hex: 0xb9b0d0).opacity(0.7)), lineWidth: 0.8)
        }
        var tip = Path()
        tip.move(to: CGPoint(x: base.x - 8 * k, y: base.y - 22 * k))
        tip.addQuadCurve(to: CGPoint(x: base.x + 2 * k, y: base.y - 36 * k), control: CGPoint(x: base.x - 6 * k, y: base.y - 32 * k))
        tip.addQuadCurve(to: CGPoint(x: base.x + 8 * k, y: base.y - 22 * k), control: CGPoint(x: base.x + 6 * k, y: base.y - 30 * k))
        tip.closeSubpath()
        ctx.fill(tip, with: .color(.white))
        ctx.stroke(tip, with: .color(Color(hex: 0xb9b0d0).opacity(0.7)), lineWidth: 0.8)
        // Ridges.
        for t in tiers {
            for j in -1...1 {
                var ridge = Path()
                let x = base.x + CGFloat(j) * t.w * k * 0.22
                ridge.move(to: CGPoint(x: x, y: base.y + t.y * k - t.h * k * 0.35))
                ridge.addLine(to: CGPoint(x: x + 1.5, y: base.y + t.y * k + t.h * k * 0.35))
                ctx.stroke(ridge, with: .color(Color(hex: 0xb9b0d0).opacity(0.55)), lineWidth: 0.7)
            }
        }
    }

    private func drawCherry(_ ctx: inout GraphicsContext, at p: CGPoint) {
        var stem = Path()
        stem.move(to: CGPoint(x: p.x, y: p.y - 3))
        stem.addQuadCurve(to: CGPoint(x: p.x + 7, y: p.y - 12), control: CGPoint(x: p.x + 1, y: p.y - 9))
        ctx.stroke(stem, with: .color(Color(hex: 0x3f7a2e)), style: StrokeStyle(lineWidth: 1.4, lineCap: .round))
        let ball = Path(ellipseIn: CGRect(x: p.x - 5.5, y: p.y - 5, width: 11, height: 11))
        ctx.fill(ball, with: .radialGradient(Gradient(colors: [Color(hex: 0xff6a7a), Color(hex: 0xc8102e)]),
                                              center: CGPoint(x: p.x - 1.5, y: p.y - 2), startRadius: 0, endRadius: 8))
        ctx.fill(Path(ellipseIn: CGRect(x: p.x - 3, y: p.y - 3.4, width: 3, height: 2)), with: .color(.white.opacity(0.7)))
    }

    private static let sprinkleColors: [UInt32] = [0xff3d8b, 0x37b6ff, 0xffd23d, 0x56d66b, 0xff8a2b, 0xa066ff]

    private func drawSprinkles(_ ctx: inout GraphicsContext, top: CGFloat, cream: Bool, center: CGFloat, amount: Double,
                               time: Double) {
        var rng = Rng(1234)
        let count = 6 + Int(amount * 26)
        let r = SundaeShape.r
        let up: CGFloat = cream ? 22 : 0
        for i in 0..<count {
            let x = rng.range(-r * 0.9, r * 0.9)
            let y = center + rng.range(-r * 0.75 - up, r * 0.55)
            let rot = Double(rng.range(0, 3.14))
            let col = Self.sprinkleColors[i % Self.sprinkleColors.count]
            let twinkle = 0.75 + 0.25 * sin(time * 3 + Double(i) * 2.3)
            var q = ctx
            q.translateBy(x: x, y: y)
            q.rotate(by: .radians(rot))
            q.fill(Path(roundedRect: CGRect(x: -2.8, y: -0.9, width: 5.6, height: 1.8), cornerRadius: 0.9), with: .color(hexColor(col, twinkle)))
        }
    }

    // Fizz: little bubbles rise off the top.
    private func drawFizz(_ ctx: inout GraphicsContext, shape: SundaeShape, amount: Double, time: Double) {
        let count = 2 + Int(amount * 5)
        for i in 0..<count {
            let phase = (time * (0.35 + 0.05 * Double(i)) + Double(i) * 0.37).truncatingRemainder(dividingBy: 1)
            let x = CGFloat(sin(Double(i) * 2.1) * 26)
            let y = shape.topY - 6 - CGFloat(phase) * 26
            let r = CGFloat(1.4 + Double(i % 3) * 0.7)
            ctx.stroke(Path(ellipseIn: CGRect(x: x - r, y: y - r, width: r * 2, height: r * 2)),
                       with: .color(.white.opacity(0.85 * (1 - phase))), lineWidth: 1)
        }
    }

    private func drawFocus(_ ctx: inout GraphicsContext, shape: SundaeShape, time: Double) {
        let r = SundaeShape.r
        let n = shape.scoops
        var box: CGRect
        switch model.focus {
        case .cone:
            box = CGRect(x: -36, y: -SundaeShape.coneH - 3, width: 72, height: SundaeShape.coneH + 6)
        case .scoop:
            box = CGRect(x: -r - 8, y: shape.topY - 4, width: r * 2 + 16, height: shape.center(0) + r * 1.1 - shape.topY + 8)
        default:
            let top = shape.center(n - 1)
            box = CGRect(x: -r - 8, y: top - r - 4 - (model.focus == .cream ? 30 : 0), width: r * 2 + 16, height: r * 2 + 8 + (model.focus == .cream ? 30 : 0))
        }
        let a = 0.55 + 0.45 * sin(time * 5)
        ctx.stroke(Path(roundedRect: box, cornerRadius: 10), with: .color(.white.opacity(a)),
                   style: StrokeStyle(lineWidth: 1.8 / shape.scale, lineCap: .round, dash: [5, 4]))
    }

    // What the knobs are on, at the top right of the picture.
    private func drawBadge(_ ctx: inout GraphicsContext) {
        let f = model.focus
        let on = model.isOn(f)
        let label = f.title
        let w = CGFloat(label.count) * 6.4 + 22
        let r = CGRect(x: 236 - w, y: 42, width: w, height: 20)
        ctx.fill(Path(roundedRect: r, cornerRadius: 10), with: .color(.white.opacity(0.85)))
        ctx.stroke(Path(roundedRect: r, cornerRadius: 10), with: .color(Color(hex: 0xff5fae).opacity(0.7)), lineWidth: 1)
        text(&ctx, label, CGPoint(x: r.minX + 18, y: r.midY), size: 11, color: Color(hex: 0x8a1a57), anchor: .leading)
        ctx.fill(Path(ellipseIn: CGRect(x: r.minX + 6, y: r.midY - 4, width: 8, height: 8)),
                 with: .color(on ? Color(hex: 0xff2d8a) : Color(hex: 0xc9a3b8)))
    }

    // ---------------------------------------------------------------- tray and chips

    private func drawTray(_ ctx: inout GraphicsContext) {
        let info = model.info
        for i in 0..<Focus.allCases.count {
            let f = Focus(rawValue: i)!
            let r = IScreamLayout.tile(i)
            let focused = model.focus == f
            let on = model.isOn(f)
            let path = Path(roundedRect: r, cornerRadius: 9)
            ctx.fill(path, with: .color(focused ? .white : Color.white.opacity(0.45)))
            ctx.stroke(path, with: .color(focused ? Color(hex: 0xff2d8a) : Color(hex: 0xff5fae).opacity(0.5)), lineWidth: focused ? 2 : 1)
            var g = ctx
            g.translateBy(x: r.midX, y: r.midY + 1)
            if !on { g.opacity = 0.35 }
            drawIcon(&g, f, info: info)
        }
    }

    private func drawIcon(_ ctx: inout GraphicsContext, _ f: Focus, info: FlavorInfo) {
        switch f {
        case .scoop:
            var g = ctx
            g.scaleBy(x: 0.5, y: 0.5)
            drawScoop(&g, cy: 0, info: info, seed: 3)
        case .cone:
            var g = ctx
            g.translateBy(x: 0, y: 9)
            g.scaleBy(x: 0.42, y: 0.42)
            let o = coneOutline(0)
            g.fill(o, with: .color(Color(hex: 0xe3b26b)))
            g.stroke(o, with: .color(Color(hex: 0x8a5a2a)), lineWidth: 2)
            var lines = Path()
            for x in stride(from: -20, through: 20, by: 14) {
                lines.move(to: CGPoint(x: CGFloat(x), y: -50)); lines.addLine(to: CGPoint(x: CGFloat(x) * 0.1, y: -2))
            }
            g.stroke(lines, with: .color(Color(hex: 0x8a5a2a).opacity(0.7)), lineWidth: 1.6)
        case .cream:
            var g = ctx
            g.translateBy(x: 0, y: 9)
            g.scaleBy(x: 0.5, y: 0.5)
            drawCream(&g, base: .zero, k: 1)
        case .fudge:
            var blob = Path()
            blob.move(to: CGPoint(x: -10, y: -6))
            blob.addQuadCurve(to: CGPoint(x: 10, y: -6), control: CGPoint(x: 0, y: -14))
            blob.addLine(to: CGPoint(x: 10, y: 2))
            blob.addQuadCurve(to: CGPoint(x: 7, y: 8), control: CGPoint(x: 9, y: 8))
            blob.addQuadCurve(to: CGPoint(x: 4, y: 2), control: CGPoint(x: 5, y: 8))
            blob.addLine(to: CGPoint(x: -3, y: 2))
            blob.addQuadCurve(to: CGPoint(x: -6, y: 10), control: CGPoint(x: -3, y: 11))
            blob.addQuadCurve(to: CGPoint(x: -9, y: 2), control: CGPoint(x: -9, y: 10))
            blob.closeSubpath()
            ctx.fill(blob, with: .color(Color(hex: 0x4a2515)))
        case .caramel:
            var p = Path()
            p.move(to: CGPoint(x: -10, y: 3))
            for j in 1...5 { p.addLine(to: CGPoint(x: -10 + CGFloat(j) * 4, y: j % 2 == 0 ? 3 : -5)) }
            ctx.stroke(p, with: .color(Color(hex: 0xd68a1f)), style: StrokeStyle(lineWidth: 3.4, lineCap: .round, lineJoin: .round))
        case .chips:
            for (x, y, a) in [(-6, -2, 0.4), (5, -5, 1.6), (3, 5, 3.0), (-5, 6, 4.5)] as [(CGFloat, CGFloat, Double)] {
                var q = ctx
                q.translateBy(x: x, y: y)
                q.rotate(by: .radians(a))
                var tri = Path()
                tri.move(to: CGPoint(x: -3.6, y: 3)); tri.addLine(to: CGPoint(x: 3.6, y: 3)); tri.addLine(to: CGPoint(x: 0, y: -4))
                tri.closeSubpath()
                q.fill(tri, with: .color(Color(hex: 0x4a2a18)))
            }
        case .sprinkles:
            for (i, p) in ([(-7, -4), (4, -6), (8, 1), (-2, 3), (-8, 7), (3, 8)] as [(CGFloat, CGFloat)]).enumerated() {
                var q = ctx
                q.translateBy(x: p.0, y: p.1)
                q.rotate(by: .radians(Double(i) * 1.1))
                q.fill(Path(roundedRect: CGRect(x: -3.4, y: -1.1, width: 6.8, height: 2.2), cornerRadius: 1.1),
                       with: .color(hexColor(Self.sprinkleColors[i])))
            }
        }
    }

    private func drawChips(_ ctx: inout GraphicsContext) {
        let ps = model.params(model.focus)
        let on = model.isOn(model.focus)
        for (i, p) in ps.enumerated() {
            let r = IScreamLayout.chip(i)
            let path = Path(roundedRect: r, cornerRadius: 9)
            let dim = !on && !p.isSwitch
            ctx.fill(path, with: .color(.white.opacity(dim ? 0.45 : 0.9)))
            ctx.stroke(path, with: .color(Color(hex: 0xff5fae).opacity(0.7)), lineWidth: 1)
            let v = model.value(p.field)
            text(&ctx, p.name, CGPoint(x: r.midX, y: r.minY + 9), size: 9.5, color: Color(hex: 0xb23a7e), weight: .semibold)
            if p.isSwitch {
                let color = v != 0 ? Color(hex: 0xff2d8a) : Color(hex: 0xb9a0b0)
                text(&ctx, p.format(v), CGPoint(x: r.midX, y: r.minY + 24), size: 14, color: color, weight: .heavy)
            } else {
                text(&ctx, p.format(v), CGPoint(x: r.midX, y: r.minY + 20), size: 13, color: Color(hex: 0x8a1a57), weight: .heavy)
                let range = p.range
                let t = CGFloat(v - range.lowerBound) / CGFloat(max(1, range.upperBound - range.lowerBound))
                let track = CGRect(x: r.minX + 7, y: r.maxY - 6.5, width: r.width - 14, height: 3.5)
                ctx.fill(Path(roundedRect: track, cornerRadius: 1.75), with: .color(Color(hex: 0xff5fae).opacity(0.22)))
                ctx.fill(Path(roundedRect: CGRect(x: track.minX, y: track.minY, width: max(3.5, track.width * t), height: track.height),
                              cornerRadius: 1.75), with: .color(Color(hex: 0xff2d8a)))
            }
        }
    }

    private func drawPill(_ ctx: inout GraphicsContext, _ label: String) {
        let r = IScreamLayout.pill
        ctx.fill(Path(roundedRect: r, cornerRadius: 14), with: .color(Color(hex: 0xff2d8a)))
        ctx.stroke(Path(roundedRect: r, cornerRadius: 14), with: .color(.white.opacity(0.7)), lineWidth: 1.5)
        text(&ctx, label, CGPoint(x: r.midX, y: r.midY), size: 14, color: .white)
    }
}

// ------------------------------------------------------------------ screen clicks

enum IScreamTarget: Equatable {
    case none, title, cone, scoop, tile(Int), chip(Int), pill
}

extension IScreamModel {
    func screenTarget(at p: CGPoint) -> IScreamTarget {
        if IScreamLayout.title.contains(p) { return .title }
        if mf1_bank_pending() {
            for i in 0..<4 where IScreamLayout.chip(i).contains(p) { return .chip(i) }
            return .none
        }
        for i in 0..<Focus.allCases.count where IScreamLayout.tile(i).insetBy(dx: -1.5, dy: -1.5).contains(p) { return .tile(i) }
        if pill != nil, IScreamLayout.pill.contains(p) { return .pill }
        if pill == nil {
            for i in 0..<4 where IScreamLayout.chip(i).contains(p) { return .chip(i) }
        }
        if IScreamLayout.art.contains(p) {
            // The lower half of the sundae is the cone.
            let shape = SundaeShape(scoops: max(1, min(3, value(IS.scoops))), cream: value(IS.top(IS.cream, 0)) != 0)
            let coneTop = IScreamLayout.apex.y - (SundaeShape.coneH * shape.scale)
            return p.y > coneTop ? .cone : .scoop
        }
        return .none
    }

    func screenClick(_ t: IScreamTarget, back: Bool) {
        switch t {
        case .tile(let i):
            let f = Focus(rawValue: i)!
            if focus == f { toggle(f) } else { focus = f }
        case .chip(let i):
            if mf1_bank_pending() {
                turnKnob(i, 1)
            } else {
                clickChip(i, back: back)
            }
        case .cone: focus = .cone
        case .scoop: focus = .scoop
        case .pill: pill = nil
        case .title, .none: break
        }
    }

    func screenTurn(_ t: IScreamTarget, _ delta: Int) {
        guard delta != 0 else { return }
        switch t {
        case .title: turnPresets(delta)
        case .chip(let i): turnKnob(i, delta)
        case .tile: turnSelect(delta > 0 ? 1 : -1)
        case .cone, .scoop: turnPresets(delta)
        default: break
        }
    }

    func flavorMenu() -> NSMenu {
        let menu = NSMenu()
        for (i, f) in Self.flavors.enumerated() {
            let item = ClosureMenuItem(title: f.title) { [weak self] in self?.selectFlavor(i) }
            item.state = i == flavor ? .on : .off
            menu.addItem(item)
        }
        return menu
    }
}

// Mouse input on the screen: clicks act on what is under the pointer, a vertical drag or the scroll
// wheel turns it like a knob, a click on the flavor name opens the flavor list.
final class IScreamScreenInputView: NSView {
    var model: IScreamModel?
    private var target: IScreamTarget = .none
    private var lastY: CGFloat = 0
    private var accum: CGFloat = 0
    private var scrollAccum: CGFloat = 0
    private var dragged = false
    override var mouseDownCanMoveWindow: Bool { false }
    override var isFlipped: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    private func screenPoint(_ e: NSEvent) -> CGPoint {
        let p = convert(e.locationInWindow, from: nil)
        return CGPoint(x: p.x / bounds.width * 240, y: p.y / bounds.height * 240)
    }

    override func mouseDown(with e: NSEvent) {
        guard let model else { return }
        target = model.screenTarget(at: screenPoint(e))
        lastY = e.locationInWindow.y
        accum = 0
        dragged = false
    }

    override func mouseDragged(with e: NSEvent) {
        guard let model else { return }
        accum += e.locationInWindow.y - lastY
        lastY = e.locationInWindow.y
        let step: CGFloat = e.modifierFlags.contains(.option) ? 12 : 5
        let detents = Int(accum / step)
        if detents != 0 {
            accum -= CGFloat(detents) * step
            dragged = true
            model.screenTurn(target, detents)
        }
    }

    override func mouseUp(with e: NSEvent) {
        guard let model, !dragged else { return }
        if target == .title {
            model.flavorMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        } else {
            model.screenClick(target, back: e.modifierFlags.contains(.shift))
        }
    }

    override func rightMouseDown(with e: NSEvent) {
        guard let model else { return }
        let t = model.screenTarget(at: screenPoint(e))
        if t == .title {
            model.flavorMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        } else {
            model.screenClick(t, back: true)
        }
    }

    override func scrollWheel(with e: NSEvent) {
        guard let model else { return }
        let d = e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 8 : e.scrollingDeltaY
        scrollAccum += d
        let detents = Int(scrollAccum)
        if detents != 0 {
            scrollAccum -= CGFloat(detents)
            model.screenTurn(model.screenTarget(at: screenPoint(e)), detents)
        }
    }
}

struct IScreamScreenInput: NSViewRepresentable {
    let model: IScreamModel
    func makeNSView(context: Context) -> IScreamScreenInputView {
        let v = IScreamScreenInputView()
        v.model = model
        return v
    }
    func updateNSView(_ v: IScreamScreenInputView, context: Context) { v.model = model }
}
