import AppKit
import ArpeggiCore
import MF1Core
import SwiftUI

// Arpeggi's 240x240 screen: a night sky the lanes draw on. Across the top, the pool, slot by slot; in a ring the
// slot the next key replaces is marked. Below it, each note the lanes strike is a streak at its pitch, A in amber
// and B in blue, starting at the right edge, which is now, and drifting left as it ages, as long as it was held.
// Drawn only with rectangles, circles and text, so a firmware could draw it with its own fills.

enum ArpLayout {
    static let title = CGRect(x: 0, y: 0, width: 240, height: 30)
    static let strip = CGRect(x: 9, y: 33, width: 222, height: 17)
    static let sky = CGRect(x: 9, y: 54, width: 222, height: 92)
    static func tab(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 47, y: 168, width: 45, height: 26) }
    static func chip(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 59, y: 200, width: 56, height: 37) }
    static let pill = CGRect(x: 14, y: 204, width: 212, height: 28)
    // How far back the sky reaches, in milliseconds.
    static let window = 2500.0
}

private let night = Color(hex: 0x0E1022)
private let ink = Color(hex: 0xECE9FF)
private let amber = Color(hex: 0xFFB547)
private let blue = Color(hex: 0x4FD8FF)
private let rose = Color(hex: 0xFF5DA2)
private let faded = Color(hex: 0x7A7DA8)
private let pale = Color(hex: 0x2C2F56)

struct ArpeggiScreen: View {
    @ObservedObject var model: ArpeggiModel
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
                      anchor: UnitPoint = .center, bold: Bool = false, kerning: CGFloat = 0) {
        let t = Text(s).font(.custom(bold ? "Futura-Bold" : "Futura-Medium", size: size)).kerning(kerning)
            .foregroundColor(color)
        ctx.draw(t, at: p, anchor: anchor)
    }

    private func draw(_ ctx: inout GraphicsContext) {
        ctx.fill(Path(CGRect(x: 0, y: 0, width: 240, height: 240)), with: .color(night))
        drawStars(&ctx)
        drawTitle(&ctx)
        if mf1_bank_pending() {
            drawBankPrompt(&ctx)
            return
        }
        let pool = model.pool()
        drawStrip(&ctx, pool)
        drawSky(&ctx, pool)
        drawStatus(&ctx, pool)
        drawTabs(&ctx)
        if let pill = model.pill {
            drawPill(&ctx, pill)
        } else {
            drawChips(&ctx)
        }
    }

    // A few fixed stars, the same every frame.
    private func drawStars(_ ctx: inout GraphicsContext) {
        var x: UInt32 = 0x9E3779B9
        for _ in 0..<34 {
            x = x &* 1664525 &+ 1013904223
            let px = CGFloat(x % 232) + 4
            x = x &* 1664525 &+ 1013904223
            let py = CGFloat(x % 110) + 36
            let r: CGFloat = x % 7 == 0 ? 0.8 : 0.45
            ctx.fill(Path(ellipseIn: CGRect(x: px - r, y: py - r, width: r * 2, height: r * 2)), with: .color(pale))
        }
    }

    private func drawTitle(_ ctx: inout GraphicsContext) {
        ctx.fill(Path(ellipseIn: CGRect(x: 9, y: 9, width: 9, height: 9)), with: .color(amber))
        ctx.fill(Path(ellipseIn: CGRect(x: 14, y: 9, width: 9, height: 9)), with: .color(blue.opacity(0.85)))
        text(&ctx, model.info.title.uppercased(), CGPoint(x: 30, y: 14), size: 10.5, color: ink, anchor: .leading,
             bold: true, kerning: 2.4)
        let r = CGRect(x: 207, y: 8, width: 22, height: 12)
        ctx.stroke(Path(r), with: .color(ink), lineWidth: 1.2)
        ctx.fill(Path(CGRect(x: 229.5, y: 11.5, width: 2, height: 5)), with: .color(ink))
        ctx.fill(Path(CGRect(x: 209.5, y: 10.5, width: 5, height: 7)), with: .color(ink))
        ctx.fill(Path(CGRect(x: 9, y: 27, width: 222, height: 0.75)), with: .color(pale))
    }

    private func drawBankPrompt(_ ctx: inout GraphicsContext) {
        let r = CGRect(x: 16, y: 50, width: 208, height: 110)
        ctx.fill(Path(r), with: .color(pale))
        text(&ctx, "A BANK ARRIVED", CGPoint(x: 120, y: 76), size: 12, color: ink, bold: true, kerning: 2)
        text(&ctx, "Turn KNOB1-4 to store it", CGPoint(x: 120, y: 102), size: 10, color: ink)
        text(&ctx, "in bank A, B, C or D", CGPoint(x: 120, y: 117), size: 10, color: ink)
        text(&ctx, "A cancels", CGPoint(x: 120, y: 142), size: 9, color: faded)
        for i in 0..<4 {
            let b = ArpLayout.chip(i)
            ctx.fill(Path(b), with: .color(i == 0 ? amber : pale))
            text(&ctx, ["A", "B", "C", "D"][i], CGPoint(x: b.midX, y: b.midY), size: 16, color: i == 0 ? night : ink,
                 bold: true)
        }
    }

    // ---------------------------------------------------------------- the pool

    // The pool slot by slot: in a ring, Ring slots with the oldest marked; else the notes as they came.
    private func drawStrip(_ ctx: inout GraphicsContext, _ pool: ApPool) {
        let notes = model.poolNotes(pool)
        let ring = Int(pool.hold) == Int(AP_RING)
        let slots = ring ? Int(pool.ring) : max(1, notes.count)
        let s = ArpLayout.strip
        if notes.isEmpty && !ring {
            text(&ctx, model.writes ? "PLAY A CHORD, OR PRESS PLAY" : "KEYS PLAY ALONG", CGPoint(x: s.midX, y: s.midY),
                 size: 7, color: faded, kerning: 1.4)
            return
        }
        let w = min(36, (s.width - CGFloat(slots - 1) * 3) / CGFloat(slots))
        let x0 = s.midX - (CGFloat(slots) * w + CGFloat(slots - 1) * 3) / 2
        let full = ring && notes.count == slots
        for i in 0..<slots {
            let r = CGRect(x: x0 + CGFloat(i) * (w + 3), y: s.minY, width: w, height: s.height)
            if i < notes.count {
                let next = full && i == Int(pool.next)
                ctx.fill(Path(roundedRect: r, cornerRadius: 3), with: .color(pale))
                if next { ctx.stroke(Path(roundedRect: r.insetBy(dx: 0.5, dy: 0.5), cornerRadius: 3), with: .color(rose), lineWidth: 1) }
                let name = AP.noteName(notes[i] + model.octave * 12)
                text(&ctx, name, CGPoint(x: r.midX, y: r.midY), size: w < 22 ? 6.5 : 8, color: ink, bold: true)
            } else {
                ctx.stroke(Path(roundedRect: r.insetBy(dx: 0.5, dy: 0.5), cornerRadius: 3), with: .color(pale),
                           style: StrokeStyle(lineWidth: 0.8, dash: [2, 2]))
            }
        }
        // In a ring that is not yet full, the next key fills the first empty slot.
        if ring && !full && notes.count < slots {
            let r = CGRect(x: x0 + CGFloat(notes.count) * (w + 3), y: s.minY, width: w, height: s.height)
            ctx.stroke(Path(roundedRect: r.insetBy(dx: 0.5, dy: 0.5), cornerRadius: 3), with: .color(rose), lineWidth: 1)
        }
    }

    // ---------------------------------------------------------------- the sky

    private func drawSky(_ ctx: inout GraphicsContext, _ pool: ApPool) {
        let s = ArpLayout.sky
        let notes = model.poolNotes(pool)
        let hits = model.recent()
        let shift = model.octave * 12
        let span = model.value(AP.span), pitch = model.value(AP.duo) != 0 ? model.value(AP.pitch) : 0
        var lo = (notes.min() ?? 60) + shift + min(0, pitch)
        var hi = (notes.max() ?? 72) + shift + 12 * (span - 1) + max(0, pitch)
        for h in hits where h.ago_ms < Int32(ArpLayout.window) {
            lo = min(lo, Int(h.note))
            hi = max(hi, Int(h.note))
        }
        lo -= 2
        hi += 2
        if hi - lo < 14 { hi = lo + 14 }
        func y(_ n: Int) -> CGFloat { s.maxY - CGFloat(n - lo) / CGFloat(hi - lo) * s.height }
        func x(_ ms: Double) -> CGFloat { s.maxX - CGFloat(ms / ArpLayout.window) * s.width }

        // The strings: each pool note in every octave of the span, faint.
        for o in 0..<span {
            for n in Set(notes) {
                let yy = y(n + shift + 12 * o)
                ctx.fill(Path(CGRect(x: s.minX, y: yy - 0.3, width: s.width, height: 0.6)), with: .color(pale))
            }
        }
        // The beat, as ticks along the bottom moving left with time.
        if let now = model.nowTick() {
            let msPerTick = 60000.0 / Double(model.value(AP.tempo)) / Double(AP_PPQ)
            let ppq = Int64(AP_PPQ)
            var b = now / ppq
            while true {
                let ago = Double(now - b * ppq) * msPerTick
                if ago > ArpLayout.window || b < 0 { break }
                let bar = b % 4 == 0
                ctx.fill(Path(CGRect(x: x(ago) - 0.4, y: s.maxY + 2, width: 0.8, height: bar ? 4 : 2)),
                         with: .color(bar ? faded : pale))
                b -= 1
            }
        }
        ctx.fill(Path(CGRect(x: s.maxX, y: s.minY, width: 0.8, height: s.height)), with: .color(pale))

        // Oldest first, so the newest streaks lie on top.
        for h in hits.reversed() {
            let ago = Double(h.ago_ms), len = max(30, Double(h.length_ms))
            if ago - len > ArpLayout.window { continue }
            let color = h.lane == 0 ? amber : blue
            let fade = max(0.18, 1 - ago / ArpLayout.window)
            let yy = y(Int(h.note))
            let x1 = x(max(0, ago - len)), x0 = x(ago)
            let sounding = ago < len
            ctx.fill(Path(roundedRect: CGRect(x: x0, y: yy - 1.4, width: max(1.5, x1 - x0), height: 2.8), cornerRadius: 1.4),
                     with: .color(color.opacity(fade * 0.75)))
            if sounding {
                let r: CGFloat = 3.4
                ctx.fill(Path(ellipseIn: CGRect(x: x1 - r * 1.8, y: yy - r * 1.8, width: r * 3.6, height: r * 3.6)),
                         with: .color(color.opacity(0.18)))
                ctx.fill(Path(ellipseIn: CGRect(x: x1 - r, y: yy - r, width: r * 2, height: r * 2)), with: .color(color))
            } else {
                ctx.fill(Path(ellipseIn: CGRect(x: x0 - 1.6, y: yy - 1.6, width: 3.2, height: 3.2)),
                         with: .color(color.opacity(fade)))
            }
        }
    }

    // Under the sky: how the lanes relate, in the lanes' colours, and how the keys fill the pool.
    private func drawStatus(_ ctx: inout GraphicsContext, _ pool: ApPool) {
        let duo = model.value(AP.duo) != 0
        let rel = duo ? AP.pick(AP.relations, model.value(AP.relation)).uppercased() : "A ALONE"
        text(&ctx, rel, CGPoint(x: 10, y: 157), size: 9, color: duo ? blue : amber, anchor: .leading, bold: true,
             kerning: 2.2)
        let hold = AP.pick(AP.holds, model.value(AP.hold)).uppercased()
        let ringText = model.value(AP.hold) == Int(AP_RING) ? " \(pool.count)/\(pool.ring)" : ""
        let state = model.playing || model.snapshotTick != nil ?"\(model.value(AP.tempo)) BPM" : "STOPPED"
        text(&ctx, "\(hold)\(ringText) · \(state)", CGPoint(x: 230, y: 157), size: 7, color: faded, anchor: .trailing,
             kerning: 1.2)
    }

    // ---------------------------------------------------------------- pages and values

    private func drawTabs(_ ctx: inout GraphicsContext) {
        for p in ArpPage.allCases {
            let r = ArpLayout.tab(p.rawValue)
            let focused = model.page == p
            let accent = p == .a ? amber : p == .b ? blue : rose
            if focused {
                ctx.fill(Path(roundedRect: r, cornerRadius: 3), with: .color(pale))
                ctx.fill(Path(CGRect(x: r.minX + 6, y: r.maxY - 3, width: r.width - 12, height: 2)), with: .color(accent))
            }
            text(&ctx, p.title.uppercased(), CGPoint(x: r.midX, y: r.midY - (focused ? 1 : 0)), size: 8,
                 color: focused ? ink : faded, bold: focused, kerning: 1.2)
        }
    }

    private func drawChips(_ ctx: inout GraphicsContext) {
        let accent = model.page == .a ? amber : model.page == .b ? blue : rose
        for (i, p) in model.params(model.page).enumerated() {
            let r = ArpLayout.chip(i)
            let on = model.applies(p.field)
            if i > 0 { ctx.fill(Path(CGRect(x: r.minX - 1.5, y: r.minY + 4, width: 0.6, height: r.height - 8)), with: .color(pale)) }
            let v = model.value(p.field)
            let label = p.format(v)
            text(&ctx, p.name.uppercased(), CGPoint(x: r.midX, y: r.minY + 6), size: 6, color: faded, kerning: 1.1)
            text(&ctx, label, CGPoint(x: r.midX, y: r.minY + 19), size: label.count > 6 ? 10 : 13,
                 color: on ? ink : faded.opacity(0.6), bold: true)
            let range = p.range
            let t = CGFloat(v - range.lowerBound) / CGFloat(max(1, range.upperBound - range.lowerBound))
            let tr = CGRect(x: r.minX + 7, y: r.maxY - 6, width: r.width - 14, height: 0.75)
            ctx.fill(Path(tr), with: .color(pale))
            ctx.fill(Path(ellipseIn: CGRect(x: tr.minX + t * tr.width - 2.5, y: tr.midY - 2.5, width: 5, height: 5)),
                     with: .color(on ? accent : faded))
        }
    }

    private func drawPill(_ ctx: inout GraphicsContext, _ label: String) {
        let r = ArpLayout.pill
        ctx.fill(Path(roundedRect: r, cornerRadius: 5), with: .color(pale))
        ctx.fill(Path(CGRect(x: r.minX + 8, y: r.maxY - 3, width: r.width - 16, height: 1.5)), with: .color(rose))
        text(&ctx, label, CGPoint(x: r.midX, y: r.midY - 1), size: 11.5, color: ink, bold: true, kerning: 0.6)
    }
}

// ------------------------------------------------------------------ screen clicks

enum ArpTarget: Equatable {
    case none, title, sky, strip, tab(Int), chip(Int), pill
}

extension ArpeggiModel {
    func screenTarget(at p: CGPoint) -> ArpTarget {
        if ArpLayout.title.contains(p) { return .title }
        if mf1_bank_pending() {
            for i in 0..<4 where ArpLayout.chip(i).contains(p) { return .chip(i) }
            return .none
        }
        for i in 0..<ArpPage.allCases.count where ArpLayout.tab(i).insetBy(dx: -1, dy: -1.5).contains(p) { return .tab(i) }
        if pill != nil, ArpLayout.pill.contains(p) { return .pill }
        if pill == nil {
            for i in 0..<4 where ArpLayout.chip(i).contains(p) { return .chip(i) }
        }
        if ArpLayout.strip.insetBy(dx: 0, dy: -2).contains(p) { return .strip }
        if p.y > ArpLayout.strip.maxY && p.y < 165 { return .sky }
        return .none
    }

    // A click on the sky plays or stops, on the pool clears it; the pages and values work as on the panel.
    func screenClick(_ t: ArpTarget, back: Bool) {
        switch t {
        case .tab(let i): page = ArpPage(rawValue: i) ?? page
        case .chip(let i):
            if mf1_bank_pending() {
                turnKnob(i, 1)
            } else {
                clickChip(i, back: back)
            }
        case .sky: togglePlay()
        case .strip: clear()
        case .pill: pill = nil
        case .title, .none: break
        }
    }

    func screenTurn(_ t: ArpTarget, _ delta: Int) {
        guard delta != 0 else { return }
        switch t {
        case .title: turnPresets(delta)
        case .sky: turnAlgorithm(delta)
        case .strip: set(AP.ring, value(AP.ring) + delta)
        case .chip(let i): turnKnob(i, delta)
        case .tab: turnSelect(delta > 0 ? 1 : -1)
        default: break
        }
    }

    func sceneMenu() -> NSMenu {
        let menu = NSMenu()
        for (i, m) in Self.scenes.enumerated() {
            let item = ClosureMenuItem(title: m.title) { [weak self] in self?.selectScene(i) }
            item.state = i == scene ? .on : .off
            menu.addItem(item)
        }
        return menu
    }
}

// Mouse input on the screen: clicks act on what is under the pointer, a vertical drag or the scroll wheel turns it
// like a knob, a click on the title opens the list of scenes.
final class ArpeggiScreenInputView: NSView {
    var model: ArpeggiModel?
    private var target: ArpTarget = .none
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
            model.sceneMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        } else {
            model.screenClick(target, back: e.modifierFlags.contains(.shift))
        }
    }

    override func rightMouseDown(with e: NSEvent) {
        guard let model else { return }
        let t = model.screenTarget(at: screenPoint(e))
        if t == .title {
            model.sceneMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
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

struct ArpeggiScreenInput: NSViewRepresentable {
    let model: ArpeggiModel
    func makeNSView(context: Context) -> ArpeggiScreenInputView {
        let v = ArpeggiScreenInputView()
        v.model = model
        return v
    }
    func updateNSView(_ v: ArpeggiScreenInputView, context: Context) { v.model = model }
}
