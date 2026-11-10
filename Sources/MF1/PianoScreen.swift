import AppKit
import MF1Core
import PianoCore
import SwiftUI

// Piano's 240x240 screen: the open grand seen from above, its 88 strings glowing and swaying as each rings, with the
// two pages and the four values the knobs are on below. Ivory on black lacquer, the strings in brass.

enum PianoLayout {
    static let title = CGRect(x: 0, y: 0, width: 240, height: 34)
    static let art = CGRect(x: 0, y: 35, width: 240, height: 130)
    static func tab(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 118, y: 167, width: 116, height: 29) }
    static func chip(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 59, y: 200, width: 56, height: 37) }
    static let pill = CGRect(x: 14, y: 204, width: 212, height: 28)
}

private let lacquer = Color(hex: 0x0E0D0C)
private let ivory = Color(hex: 0xF2EBDD)
private let faded = Color(hex: 0x8C857A)
private let brass = Color(hex: 0xC9A44C)
private let plate = Color(hex: 0x3A2F1E)

struct PianoScreen: View {
    @ObservedObject var model: PianoModel
    // Changes on every screen frame; without a changing input, SwiftUI would skip the redraw.
    var frame = 0

    var body: some View {
        Canvas { ctx, size in
            ctx.scaleBy(x: size.width / 240, y: size.height / 240)
            var c = ctx
            draw(&c)
        }
    }

    private func text(_ ctx: inout GraphicsContext, _ s: String, _ p: CGPoint, size: CGFloat, color: Color,
                      anchor: UnitPoint = .center, weight: Font.Weight = .bold) {
        let t = Text(s).font(.system(size: size, weight: weight, design: .serif)).foregroundColor(color)
        ctx.draw(t, at: p, anchor: anchor)
    }

    private func draw(_ ctx: inout GraphicsContext) {
        ctx.fill(Path(CGRect(x: 0, y: 0, width: 240, height: 240)), with: .color(lacquer))
        drawPiano(&ctx)
        drawTitle(&ctx)
        drawTabs(&ctx)
        if let pill = model.pill {
            drawPill(&ctx, pill)
        } else {
            drawChips(&ctx)
        }
    }

    // A string's speaking length on the screen: long in the bass, short in the treble, as on a grand.
    static func stringTop(_ i: Int) -> CGFloat {
        let t = CGFloat(i) / 87
        return 40 + 92 * t * t * 0.55 + 40 * t
    }

    private func drawPiano(_ ctx: inout GraphicsContext) {
        let bottom: CGFloat = 158
        // The case: straight on the bass side, the bentside curving in towards the treble tail.
        var rim = Path()
        rim.move(to: CGPoint(x: 8, y: bottom + 4))
        rim.addLine(to: CGPoint(x: 8, y: 38))
        rim.addCurve(to: CGPoint(x: 232, y: 118), control1: CGPoint(x: 120, y: 30), control2: CGPoint(x: 150, y: 120))
        rim.addLine(to: CGPoint(x: 232, y: bottom + 4))
        rim.closeSubpath()
        ctx.fill(rim, with: .color(plate))
        ctx.stroke(rim, with: .color(brass.opacity(0.7)), lineWidth: 1.2)
        // The soundboard's ribs, faintly.
        for k in 0..<6 {
            let y = 60 + CGFloat(k) * 17
            var rib = Path()
            rib.move(to: CGPoint(x: 10, y: y + 20))
            rib.addLine(to: CGPoint(x: 230, y: y - 20))
            ctx.stroke(rib, with: .color(Color.black.opacity(0.18)), lineWidth: 1)
        }
        let phase = Double(frame) * 0.9
        for i in 0..<88 {
            let note = 21 + i
            let x = 12 + CGFloat(i) * 2.45
            let top = Self.stringTop(i)
            let ring = CGFloat(model.ringing(note))
            let held = model.levels[note] > 0 && ring > 0.02
            let colour = held ? Color(hex: 0xFFF3C4).opacity(0.35 + 0.65 * Double(ring)) : brass.opacity(0.45)
            var s = Path()
            if held {
                // A ringing string bows to and fro, most at its middle.
                let amp = 1.6 * ring * CGFloat(sin(phase + Double(i) * 0.7))
                s.move(to: CGPoint(x: x, y: bottom))
                for k in 1...6 {
                    let u = CGFloat(k) / 6
                    s.addLine(to: CGPoint(x: x + amp * sin(.pi * u), y: bottom - (bottom - top) * u))
                }
            } else {
                s.move(to: CGPoint(x: x, y: bottom))
                s.addLine(to: CGPoint(x: x, y: top))
            }
            ctx.stroke(s, with: .color(colour), lineWidth: held ? 1.1 : 0.7)
        }
        // The dampers sit on the strings until a key or the pedal lifts them; above F6 there are none.
        for i in 0..<68 {
            let note = 21 + i
            let held = note < 64 ? (model.led.soundingLo >> UInt64(note)) & 1 : (model.led.soundingHi >> UInt64(note - 64)) & 1
            let lifted = model.pedal || held != 0
            let x = 12 + CGFloat(i) * 2.45
            ctx.fill(Path(CGRect(x: x - 1, y: lifted ? 144 : 147, width: 2.2, height: 4)),
                     with: .color(lifted ? faded.opacity(0.5) : Color(hex: 0x1A1714)))
        }
        // The keyboard's edge.
        ctx.fill(Path(CGRect(x: 8, y: bottom, width: 224, height: 5)), with: .color(Color.black))
        if model.pedal {
            let r = CGRect(x: 182, y: 41, width: 48, height: 15)
            ctx.fill(Path(roundedRect: r, cornerRadius: 3), with: .color(brass))
            text(&ctx, "PEDAL", CGPoint(x: r.midX, y: r.midY), size: 9, color: lacquer, weight: .heavy)
        }
        text(&ctx, "Touch \(model.touchName(model.value(PF.touch)))", CGPoint(x: 14, y: 48), size: 8.5, color: ivory.opacity(0.75),
             anchor: .leading, weight: .semibold)
    }

    private func drawTitle(_ ctx: inout GraphicsContext) {
        ctx.fill(Path(PianoLayout.title), with: .color(lacquer))
        text(&ctx, PF.names[model.piano].uppercased(), CGPoint(x: 8, y: 13), size: 15, color: ivory, anchor: .leading,
             weight: .heavy)
        text(&ctx, PF.blurbs[model.piano], CGPoint(x: 9, y: 28), size: 8, color: faded, anchor: .leading, weight: .semibold)
        drawBattery(&ctx)
        ctx.fill(Path(CGRect(x: 0, y: 32, width: 240, height: 1.5)), with: .color(brass))
    }

    // The FM-1's battery icon where the firmware draws it. As in MF-1, the emulator's battery is a third full.
    private func drawBattery(_ ctx: inout GraphicsContext) {
        let r = CGRect(x: 207, y: 9, width: 24, height: 13)
        ctx.stroke(Path(roundedRect: r, cornerRadius: 3), with: .color(ivory), lineWidth: 1.5)
        ctx.fill(Path(CGRect(x: 231.5, y: 13, width: 2, height: 5)), with: .color(ivory))
        ctx.fill(Path(CGRect(x: 209.75, y: 11.5, width: 5, height: 8)), with: .color(ivory))
    }

    private func drawTabs(_ ctx: inout GraphicsContext) {
        for p in PianoPage.allCases {
            let r = PianoLayout.tab(p.rawValue)
            let focused = model.page == p
            ctx.fill(Path(r), with: .color(focused ? ivory : lacquer))
            ctx.stroke(Path(r.insetBy(dx: 0.5, dy: 0.5)), with: .color(ivory), lineWidth: 1)
            if focused { ctx.fill(Path(CGRect(x: r.minX, y: r.maxY - 3, width: r.width, height: 3)), with: .color(brass)) }
            text(&ctx, p.title.uppercased(), CGPoint(x: r.midX, y: r.midY - 1), size: 11, color: focused ? lacquer : ivory,
                 weight: .heavy)
        }
    }

    private func drawChips(_ ctx: inout GraphicsContext) {
        for (i, p) in model.params(model.page).enumerated() {
            let r = PianoLayout.chip(i)
            ctx.fill(Path(r), with: .color(lacquer))
            ctx.stroke(Path(r.insetBy(dx: 0.5, dy: 0.5)), with: .color(ivory.opacity(0.8)), lineWidth: 1)
            let v = model.value(p.field)
            text(&ctx, p.name, CGPoint(x: r.midX, y: r.minY + 8), size: 8, color: faded, weight: .semibold)
            text(&ctx, p.format(v), CGPoint(x: r.midX, y: r.minY + 21), size: 12, color: ivory, weight: .heavy)
            // A bar that fills as the value rises, brass up to where it stands.
            let range = p.range
            let t = CGFloat(v - range.lowerBound) / CGFloat(max(1, range.upperBound - range.lowerBound))
            let bar = CGRect(x: r.minX + 6, y: r.maxY - 7, width: r.width - 12, height: 3)
            ctx.fill(Path(bar), with: .color(faded.opacity(0.35)))
            ctx.fill(Path(CGRect(x: bar.minX, y: bar.minY, width: bar.width * t, height: bar.height)), with: .color(brass))
        }
    }

    private func drawPill(_ ctx: inout GraphicsContext, _ label: String) {
        let r = PianoLayout.pill
        ctx.fill(Path(r), with: .color(ivory))
        ctx.fill(Path(CGRect(x: r.minX, y: r.maxY - 3, width: r.width, height: 3)), with: .color(brass))
        text(&ctx, label, CGPoint(x: r.midX, y: r.midY - 1), size: 12, color: lacquer, weight: .heavy)
    }
}

// ------------------------------------------------------------------ screen clicks

enum PianoTarget: Equatable {
    case none, title, art, tab(Int), chip(Int), pill
}

extension PianoModel {
    func screenTarget(at p: CGPoint) -> PianoTarget {
        if PianoLayout.title.contains(p) { return .title }
        for i in 0..<PianoPage.allCases.count where PianoLayout.tab(i).insetBy(dx: -1.5, dy: -1.5).contains(p) { return .tab(i) }
        if pill != nil, PianoLayout.pill.contains(p) { return .pill }
        if pill == nil {
            for i in 0..<4 where PianoLayout.chip(i).contains(p) { return .chip(i) }
        }
        if PianoLayout.art.contains(p) { return .art }
        return .none
    }

    // A click on the strings presses or lifts the pedal; the pages and values work as on the panel.
    func screenClick(_ t: PianoTarget, back: Bool) {
        switch t {
        case .tab(let i): page = PianoPage(rawValue: i) ?? page
        case .chip(let i): clickChip(i, back: back)
        case .art: setPedal(!pedal)
        case .pill: pill = nil
        case .title, .none: break
        }
    }

    func screenTurn(_ t: PianoTarget, _ delta: Int) {
        guard delta != 0 else { return }
        switch t {
        case .title: turnPresets(delta)
        case .art: turnAlgorithm(delta)
        case .chip(let i): turnKnob(i, delta)
        case .tab: turnSelect(delta > 0 ? 1 : -1)
        default: break
        }
    }

    func pianoMenu() -> NSMenu {
        let menu = NSMenu()
        for (i, name) in PF.names.enumerated() {
            let item = ClosureMenuItem(title: name) { [weak self] in self?.selectPiano(i) }
            item.state = i == piano ? .on : .off
            menu.addItem(item)
        }
        return menu
    }
}

// Mouse input on the screen: clicks act on what is under the pointer, a vertical drag or the scroll wheel turns it
// like a knob, a click on the piano's name opens the list.
final class PianoScreenInputView: NSView {
    var model: PianoModel?
    private var target: PianoTarget = .none
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
            model.pianoMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        } else {
            model.screenClick(target, back: e.modifierFlags.contains(.shift))
        }
    }

    override func rightMouseDown(with e: NSEvent) {
        guard let model else { return }
        let t = model.screenTarget(at: screenPoint(e))
        if t == .title {
            model.pianoMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
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

struct PianoScreenInput: NSViewRepresentable {
    let model: PianoModel
    func makeNSView(context: Context) -> PianoScreenInputView {
        let v = PianoScreenInputView()
        v.model = model
        return v
    }
    func updateNSView(_ v: PianoScreenInputView, context: Context) { v.model = model }
}
