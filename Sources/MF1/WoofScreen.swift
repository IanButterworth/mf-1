import AppKit
import MF1Core
import SwiftUI
import WoofCore

// Woof's 240x240 screen: the dog's pixel-art scene, the four pages under it and the four values the knobs are
// on, in ink on white paper like an old cel cartoon. The scene is WoofCore's, two screen pixels to one of its
// own, and moves at a few frames a second.

enum WoofLayout {
    static let title = CGRect(x: 0, y: 0, width: 240, height: 34)
    static let scale: CGFloat = 2
    static let art = CGRect(x: 0, y: 35, width: CGFloat(WF_SCENE_W) * scale, height: CGFloat(WF_SCENE_H) * scale)
    static func tab(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 59, y: 167, width: 56, height: 29) }
    static func chip(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 59, y: 200, width: 56, height: 37) }
    static let pill = CGRect(x: 14, y: 204, width: 212, height: 28)
    static let bubble = CGRect(x: 150, y: 41, width: 82, height: 24)
}

private let ink = Color(hex: 0x1A1A1A)
private let paper = Color(hex: 0xFAF7F0)
private let faded = Color(hex: 0x8A8680)

struct WoofScreen: View {
    @ObservedObject var model: WoofModel
    // Changes on every screen frame; without a changing input, SwiftUI would skip the redraw.
    var frame = 0

    var body: some View {
        let step = frame / (60 / WoofModel.sceneFPS)
        let bark = model.soundingCount > 0 ? 100 : 0
        Canvas { ctx, size in
            ctx.scaleBy(x: size.width / 240, y: size.height / 240)
            var c = ctx
            draw(&c, step: step, bark: bark)
        }
    }

    private func text(_ ctx: inout GraphicsContext, _ s: String, _ p: CGPoint, size: CGFloat, color: Color,
                      anchor: UnitPoint = .center, weight: Font.Weight = .bold) {
        let t = Text(s).font(.system(size: size, weight: weight, design: .monospaced)).foregroundColor(color)
        ctx.draw(t, at: p, anchor: anchor)
    }

    private func draw(_ ctx: inout GraphicsContext, step: Int, bark: Int) {
        ctx.fill(Path(CGRect(x: 0, y: 0, width: 240, height: 240)), with: .color(paper))
        let arrived = model.arrivedFrames
        drawScene(&ctx, step: step, bark: bark, arrived: arrived)
        drawTitle(&ctx)
        if mf1_bank_pending() {
            drawBankPrompt(&ctx)
            return
        }
        drawBadge(&ctx)
        if bark > 0 && arrived >= 10 { drawBubble(&ctx, step: step) }
        drawTabs(&ctx)
        if let pill = model.pill {
            drawPill(&ctx, pill)
        } else {
            drawChips(&ctx)
        }
    }

    // The scene, as runs of same-coloured pixels along each row.
    private func drawScene(_ ctx: inout GraphicsContext, step: Int, bark: Int, arrived: Int) {
        let w = Int(WF_SCENE_W), h = Int(WF_SCENE_H)
        var px = [UInt32](repeating: 0, count: w * h)
        woof_scene(model.settings, Int32(step), Int32(bark), Int32(min(arrived, 1000)), &px)
        let k = WoofLayout.scale, top = WoofLayout.art.minY
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
        ctx.fill(Path(CGRect(x: 0, y: WoofLayout.art.maxY, width: 240, height: 1)), with: .color(ink))
    }

    private func drawTitle(_ ctx: inout GraphicsContext) {
        let info = model.info
        ctx.fill(Path(WoofLayout.title), with: .color(paper))
        // A few spots in the corner, as on the case.
        for (x, y, r) in [(170.0, 7.0, 3.2), (182.0, 20.0, 2.2), (160.0, 24.0, 1.6), (192.0, 9.0, 1.4)] as [(CGFloat, CGFloat, CGFloat)] {
            ctx.fill(Path(ellipseIn: CGRect(x: x - r, y: y - r, width: r * 2, height: r * 1.8)), with: .color(ink))
        }
        text(&ctx, info.breed.uppercased(), CGPoint(x: 8, y: 13), size: 16, color: ink, anchor: .leading, weight: .heavy)
        text(&ctx, info.blurb, CGPoint(x: 9, y: 28), size: 8, color: faded, anchor: .leading, weight: .semibold)
        drawBattery(&ctx, color: ink)
        // The collar: a band in the dog's colour.
        ctx.fill(Path(CGRect(x: 0, y: 32, width: 240, height: 3)), with: .color(Color(hex: info.accent)))
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
        text(&ctx, "BARK cancels", CGPoint(x: 120, y: 152), size: 10, color: faded, weight: .semibold)
        for i in 0..<4 {
            let b = WoofLayout.chip(i)
            ctx.fill(Path(roundedRect: b, cornerRadius: 3), with: .color(paper))
            text(&ctx, ["A", "B", "C", "D"][i], CGPoint(x: b.midX, y: b.midY), size: 18, color: ink, weight: .heavy)
        }
    }

    // The place and how a key plays, over the top left of the picture.
    private func drawBadge(_ ctx: inout GraphicsContext) {
        let style = model.value(WF.play)
        var words = [WF.places[model.value(WF.place)]]
        if style != Int(WF_SINGLE) { words.append(WF.styles[style]) }
        if model.parade { words.append("Show") }
        let label = words.joined(separator: " · ")
        let w = CGFloat(label.count) * 5.6 + 10
        let r = CGRect(x: 4, y: 39, width: w, height: 14)
        ctx.fill(Path(r), with: .color(ink.opacity(0.8)))
        text(&ctx, label, CGPoint(x: r.minX + 5, y: r.midY), size: 9, color: paper, anchor: .leading, weight: .semibold)
    }

    // What the dog says, in a speech bubble, shaking a little.
    private func drawBubble(_ ctx: inout GraphicsContext, step: Int) {
        let jig = CGFloat(step % 2)
        let r = WoofLayout.bubble.offsetBy(dx: jig, dy: -jig)
        var tail = Path()
        tail.move(to: CGPoint(x: r.minX + 14, y: r.maxY - 1))
        tail.addLine(to: CGPoint(x: r.minX + 4, y: r.maxY + 9))
        tail.addLine(to: CGPoint(x: r.minX + 24, y: r.maxY - 1))
        ctx.fill(Path(roundedRect: r, cornerRadius: 10), with: .color(paper))
        ctx.stroke(Path(roundedRect: r, cornerRadius: 10), with: .color(ink), lineWidth: 1.5)
        ctx.fill(tail, with: .color(paper))
        ctx.stroke(tail, with: .color(ink), lineWidth: 1.5)
        ctx.fill(Path(CGRect(x: r.minX + 13, y: r.maxY - 2.5, width: 12, height: 2.5)), with: .color(paper))
        let word = model.value(WF.growl) >= 50 && model.value(WF.howl) < 50 ? "GRRR" : model.info.word
        text(&ctx, word + "!", CGPoint(x: r.midX, y: r.midY), size: 13, color: ink, weight: .black)
    }

    private func drawTabs(_ ctx: inout GraphicsContext) {
        let accent = Color(hex: model.info.accent)
        for p in WoofPage.allCases {
            let r = WoofLayout.tab(p.rawValue)
            let focused = model.page == p
            ctx.fill(Path(r), with: .color(focused ? ink : paper))
            ctx.stroke(Path(r.insetBy(dx: 0.5, dy: 0.5)), with: .color(ink), lineWidth: 1)
            if focused { ctx.fill(Path(CGRect(x: r.minX, y: r.maxY - 3, width: r.width, height: 3)), with: .color(accent)) }
            text(&ctx, p.title.uppercased(), CGPoint(x: r.midX, y: r.midY - 1), size: 10, color: focused ? paper : ink,
                 weight: .heavy)
        }
    }

    private func drawChips(_ ctx: inout GraphicsContext) {
        let accent = Color(hex: model.info.accent)
        for (i, p) in model.params(model.page).enumerated() {
            let r = WoofLayout.chip(i)
            ctx.fill(Path(r), with: .color(paper))
            ctx.stroke(Path(r.insetBy(dx: 0.5, dy: 0.5)), with: .color(ink), lineWidth: 1)
            let v = model.value(p.field)
            text(&ctx, p.name, CGPoint(x: r.midX, y: r.minY + 8), size: 8, color: faded, weight: .semibold)
            text(&ctx, p.format(v), CGPoint(x: r.midX, y: r.minY + 21), size: p.format(v).count > 6 ? 9.5 : 12, color: ink,
                 weight: .heavy)
            // A row of spots that fill as the value rises.
            let range = p.range
            let t = Double(v - range.lowerBound) / Double(max(1, range.upperBound - range.lowerBound))
            let dots = 8, lit = Int((t * Double(dots)).rounded())
            for b in 0..<dots {
                let cell = CGRect(x: r.minX + 6 + CGFloat(b) * 5.8, y: r.maxY - 8, width: 4, height: 4)
                if b < lit {
                    ctx.fill(Path(ellipseIn: cell), with: .color(b == lit - 1 ? accent : ink))
                } else {
                    ctx.stroke(Path(ellipseIn: cell.insetBy(dx: 0.5, dy: 0.5)), with: .color(faded), lineWidth: 0.7)
                }
            }
        }
    }

    private func drawPill(_ ctx: inout GraphicsContext, _ label: String) {
        let r = WoofLayout.pill
        ctx.fill(Path(r), with: .color(ink))
        ctx.fill(Path(CGRect(x: r.minX, y: r.maxY - 3, width: r.width, height: 3)), with: .color(Color(hex: model.info.accent)))
        text(&ctx, label, CGPoint(x: r.midX, y: r.midY - 1), size: 12, color: paper, weight: .heavy)
    }
}

// ------------------------------------------------------------------ screen clicks

enum WoofTarget: Equatable {
    case none, title, art, tab(Int), chip(Int), pill
}

extension WoofModel {
    func screenTarget(at p: CGPoint) -> WoofTarget {
        if WoofLayout.title.contains(p) { return .title }
        if mf1_bank_pending() {
            for i in 0..<4 where WoofLayout.chip(i).contains(p) { return .chip(i) }
            return .none
        }
        for i in 0..<WoofPage.allCases.count where WoofLayout.tab(i).insetBy(dx: -1.5, dy: -1.5).contains(p) { return .tab(i) }
        if pill != nil, WoofLayout.pill.contains(p) { return .pill }
        if pill == nil {
            for i in 0..<4 where WoofLayout.chip(i).contains(p) { return .chip(i) }
        }
        if WoofLayout.art.contains(p) { return .art }
        return .none
    }

    // A click on the picture changes the place; the pages and values work as on the panel.
    func screenClick(_ t: WoofTarget, back: Bool) {
        switch t {
        case .tab(let i): page = WoofPage(rawValue: i) ?? page
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

    func screenTurn(_ t: WoofTarget, _ delta: Int) {
        guard delta != 0 else { return }
        switch t {
        case .title, .art: turnPresets(delta)
        case .chip(let i): turnKnob(i, delta)
        case .tab: turnSelect(delta > 0 ? 1 : -1)
        default: break
        }
    }

    func dogMenu() -> NSMenu {
        let menu = NSMenu()
        for (i, m) in Self.dogs.enumerated() {
            let item = ClosureMenuItem(title: m.breed) { [weak self] in self?.selectDog(i) }
            item.state = i == dog ? .on : .off
            menu.addItem(item)
        }
        return menu
    }
}

// Mouse input on the screen: clicks act on what is under the pointer, a vertical drag or the scroll
// wheel turns it like a knob, a click on the breed name opens the list of dogs.
final class WoofScreenInputView: NSView {
    var model: WoofModel?
    private var target: WoofTarget = .none
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
            model.dogMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        } else {
            model.screenClick(target, back: e.modifierFlags.contains(.shift))
        }
    }

    override func rightMouseDown(with e: NSEvent) {
        guard let model else { return }
        let t = model.screenTarget(at: screenPoint(e))
        if t == .title {
            model.dogMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
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

struct WoofScreenInput: NSViewRepresentable {
    let model: WoofModel
    func makeNSView(context: Context) -> WoofScreenInputView {
        let v = WoofScreenInputView()
        v.model = model
        return v
    }
    func updateNSView(_ v: WoofScreenInputView, context: Context) { v.model = model }
}
