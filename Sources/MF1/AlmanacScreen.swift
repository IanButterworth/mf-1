import AlmanacCore
import AppKit
import MF1Core
import SwiftUI

// Almanac's 240x240 screen: the month's pixel-art scene, the four pages under it and the four values the
// knobs are on. Everything is drawn in screen pixels and scaled; the scene is AlmanacCore's, three screen
// pixels to one of its own, and moves at a few frames a second.

enum AlmanacLayout {
    static let title = CGRect(x: 0, y: 0, width: 240, height: 34)
    static let scale: CGFloat = 3
    static let art = CGRect(x: 0, y: 35, width: CGFloat(AL_SCENE_W) * scale, height: CGFloat(AL_SCENE_H) * scale)
    static func tab(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 59, y: 167, width: 56, height: 29) }
    static func chip(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 59, y: 200, width: 56, height: 37) }
    static let pill = CGRect(x: 14, y: 204, width: 212, height: 28)
}

private let ink = Color(hex: 0x1C2233)
private let paper = Color(hex: 0xEFE8D8)
private let faded = Color(hex: 0x8C93A8)

struct AlmanacScreen: View {
    @ObservedObject var model: AlmanacModel
    // Changes on every screen frame; without a changing input, SwiftUI would skip the redraw.
    var frame = 0

    var body: some View {
        let step = frame / (60 / AlmanacModel.sceneFPS)
        let activity = min(100, model.soundingCount * 25)
        Canvas { ctx, size in
            ctx.scaleBy(x: size.width / 240, y: size.height / 240)
            var c = ctx
            draw(&c, step: step, activity: activity)
        }
    }

    private func text(_ ctx: inout GraphicsContext, _ s: String, _ p: CGPoint, size: CGFloat, color: Color,
                      anchor: UnitPoint = .center, weight: Font.Weight = .bold) {
        let t = Text(s).font(.system(size: size, weight: weight, design: .monospaced)).foregroundColor(color)
        ctx.draw(t, at: p, anchor: anchor)
    }

    private func draw(_ ctx: inout GraphicsContext, step: Int, activity: Int) {
        ctx.fill(Path(CGRect(x: 0, y: 0, width: 240, height: 240)), with: .color(ink))
        drawScene(&ctx, step: step, activity: activity)
        drawTitle(&ctx)
        if mf1_bank_pending() {
            drawBankPrompt(&ctx)
            return
        }
        drawBadge(&ctx)
        drawTabs(&ctx)
        if let pill = model.pill {
            drawPill(&ctx, pill)
        } else {
            drawChips(&ctx)
        }
    }

    // The scene, as runs of same-coloured pixels along each row.
    private func drawScene(_ ctx: inout GraphicsContext, step: Int, activity: Int) {
        let w = Int(AL_SCENE_W), h = Int(AL_SCENE_H)
        var px = [UInt32](repeating: 0, count: w * h)
        almanac_scene(model.day, Int32(step), Int32(activity), &px)
        let k = AlmanacLayout.scale, top = AlmanacLayout.art.minY
        for y in 0..<h {
            var x = 0
            while x < w {
                let c = px[y * w + x]
                var e = x + 1
                while e < w && px[y * w + e] == c { e += 1 }
                ctx.fill(Path(CGRect(x: CGFloat(x) * k, y: top + CGFloat(y) * k, width: CGFloat(e - x) * k, height: k)),
                         with: .color(Color(hex: c)))
                x = e
            }
        }
    }

    private func drawTitle(_ ctx: inout GraphicsContext) {
        let info = model.info
        let accent = Color(hex: info.accent)
        ctx.fill(Path(AlmanacLayout.title), with: .color(ink))
        text(&ctx, info.month.uppercased(), CGPoint(x: 8, y: 13), size: 16, color: paper, anchor: .leading, weight: .heavy)
        text(&ctx, "\(info.title) · \(info.blurb)", CGPoint(x: 9, y: 28), size: 8, color: lighten(info.accent), anchor: .leading,
             weight: .semibold)
        text(&ctx, "▾", CGPoint(x: 196, y: 14), size: 12, color: faded)
        drawBattery(&ctx, color: paper)
        ctx.fill(Path(CGRect(x: 0, y: 34, width: 240, height: 1)), with: .color(accent))
    }

    private func lighten(_ v: UInt32) -> Color {
        func ch(_ s: UInt32) -> Double { (Double((v >> s) & 0xff) + 255 * 1.5) / 2.5 / 255 }
        return Color(red: ch(16), green: ch(8), blue: ch(0))
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
        ctx.fill(Path(roundedRect: r, cornerRadius: 4), with: .color(ink.opacity(0.92)))
        text(&ctx, "A bank arrived", CGPoint(x: 120, y: 82), size: 15, color: paper)
        text(&ctx, "Turn KNOB1-4 to store it", CGPoint(x: 120, y: 108), size: 10, color: faded)
        text(&ctx, "in bank A, B, C or D", CGPoint(x: 120, y: 124), size: 10, color: faded)
        text(&ctx, "MOOD cancels", CGPoint(x: 120, y: 152), size: 10, color: faded, weight: .semibold)
        for i in 0..<4 {
            let b = AlmanacLayout.chip(i)
            ctx.fill(Path(roundedRect: b, cornerRadius: 3), with: .color(paper))
            text(&ctx, ["A", "B", "C", "D"][i], CGPoint(x: b.midX, y: b.midY), size: 18, color: ink, weight: .heavy)
        }
    }

    // The sky, the place and how a key plays, over the top left of the picture.
    private func drawBadge(_ ctx: inout GraphicsContext) {
        let style = model.value(AL.play)
        var words = [AL.skies[model.value(AL.sky)], AL.places[model.value(AL.place)]]
        if style != Int(AL_SINGLE) { words.append(AL.styles[style]) }
        let label = words.joined(separator: " · ")
        let w = CGFloat(label.count) * 5.6 + 10
        let r = CGRect(x: 4, y: 39, width: w, height: 14)
        ctx.fill(Path(r), with: .color(ink.opacity(0.7)))
        text(&ctx, label, CGPoint(x: r.minX + 5, y: r.midY), size: 9, color: paper, anchor: .leading, weight: .semibold)
    }

    private func drawTabs(_ ctx: inout GraphicsContext) {
        let accent = Color(hex: model.info.accent)
        for p in Page.allCases {
            let r = AlmanacLayout.tab(p.rawValue)
            let focused = model.page == p
            ctx.fill(Path(r), with: .color(focused ? paper : Color.white.opacity(0.08)))
            if focused { ctx.fill(Path(CGRect(x: r.minX, y: r.maxY - 3, width: r.width, height: 3)), with: .color(accent)) }
            text(&ctx, p.title.uppercased(), CGPoint(x: r.midX, y: r.midY - 1), size: 10, color: focused ? ink : faded,
                 weight: .heavy)
        }
    }

    private func drawChips(_ ctx: inout GraphicsContext) {
        let accent = Color(hex: model.info.accent)
        for (i, p) in model.params(model.page).enumerated() {
            let r = AlmanacLayout.chip(i)
            ctx.fill(Path(r), with: .color(Color.white.opacity(0.06)))
            ctx.stroke(Path(r), with: .color(Color.white.opacity(0.18)), lineWidth: 1)
            let v = model.value(p.field)
            text(&ctx, p.name, CGPoint(x: r.midX, y: r.minY + 8), size: 8, color: faded, weight: .semibold)
            text(&ctx, p.format(v), CGPoint(x: r.midX, y: r.minY + 21), size: p.format(v).count > 6 ? 9.5 : 12, color: paper,
                 weight: .heavy)
            // A stepped bar, in blocks, as the scene is drawn.
            let range = p.range
            let t = Double(v - range.lowerBound) / Double(max(1, range.upperBound - range.lowerBound))
            let blocks = 8, lit = Int((t * Double(blocks)).rounded())
            for b in 0..<blocks {
                let cell = CGRect(x: r.minX + 5 + CGFloat(b) * 5.8, y: r.maxY - 7, width: 4.6, height: 3)
                ctx.fill(Path(cell), with: .color(b < lit ? accent : Color.white.opacity(0.12)))
            }
        }
    }

    private func drawPill(_ ctx: inout GraphicsContext, _ label: String) {
        let r = AlmanacLayout.pill
        ctx.fill(Path(r), with: .color(paper))
        ctx.fill(Path(CGRect(x: r.minX, y: r.maxY - 3, width: r.width, height: 3)), with: .color(Color(hex: model.info.accent)))
        text(&ctx, label, CGPoint(x: r.midX, y: r.midY - 1), size: 12, color: ink, weight: .heavy)
    }
}

// ------------------------------------------------------------------ screen clicks

enum AlmanacTarget: Equatable {
    case none, title, art, tab(Int), chip(Int), pill
}

extension AlmanacModel {
    func screenTarget(at p: CGPoint) -> AlmanacTarget {
        if AlmanacLayout.title.contains(p) { return .title }
        if mf1_bank_pending() {
            for i in 0..<4 where AlmanacLayout.chip(i).contains(p) { return .chip(i) }
            return .none
        }
        for i in 0..<Page.allCases.count where AlmanacLayout.tab(i).insetBy(dx: -1.5, dy: -1.5).contains(p) { return .tab(i) }
        if pill != nil, AlmanacLayout.pill.contains(p) { return .pill }
        if pill == nil {
            for i in 0..<4 where AlmanacLayout.chip(i).contains(p) { return .chip(i) }
        }
        if AlmanacLayout.art.contains(p) { return .art }
        return .none
    }

    // A click on the picture changes the sky; the pages and values work as on the panel.
    func screenClick(_ t: AlmanacTarget, back: Bool) {
        switch t {
        case .tab(let i): page = Page(rawValue: i) ?? page
        case .chip(let i):
            if mf1_bank_pending() {
                turnKnob(i, 1)
            } else {
                clickChip(i, back: back)
            }
        case .art: turnAlgorithm(back ? -1 : 1)
        case .pill: pill = nil
        case .title, .none: break
        }
    }

    func screenTurn(_ t: AlmanacTarget, _ delta: Int) {
        guard delta != 0 else { return }
        switch t {
        case .title, .art: turnPresets(delta)
        case .chip(let i): turnKnob(i, delta)
        case .tab: turnSelect(delta > 0 ? 1 : -1)
        default: break
        }
    }

    func monthMenu() -> NSMenu {
        let menu = NSMenu()
        for (i, m) in Self.months.enumerated() {
            let item = ClosureMenuItem(title: "\(m.month): \(m.title)") { [weak self] in self?.selectMonth(i) }
            item.state = i == month ? .on : .off
            menu.addItem(item)
        }
        return menu
    }
}

// Mouse input on the screen: clicks act on what is under the pointer, a vertical drag or the scroll
// wheel turns it like a knob, a click on the month name opens the month list.
final class AlmanacScreenInputView: NSView {
    var model: AlmanacModel?
    private var target: AlmanacTarget = .none
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
            model.monthMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        } else {
            model.screenClick(target, back: e.modifierFlags.contains(.shift))
        }
    }

    override func rightMouseDown(with e: NSEvent) {
        guard let model else { return }
        let t = model.screenTarget(at: screenPoint(e))
        if t == .title {
            model.monthMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
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

struct AlmanacScreenInput: NSViewRepresentable {
    let model: AlmanacModel
    func makeNSView(context: Context) -> AlmanacScreenInputView {
        let v = AlmanacScreenInputView()
        v.model = model
        return v
    }
    func updateNSView(_ v: AlmanacScreenInputView, context: Context) { v.model = model }
}
