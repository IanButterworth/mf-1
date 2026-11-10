import AppKit
import MF1Core
import PhaseCore
import SwiftUI

// Phase's 240x240 screen: a graphic score. Each part is a ring of its cell's steps, turning under a fixed red line
// that is now, so parts that have drifted apart sit rotated against each other and the notes they strike together
// line up. Black on paper with one red, drawn only with circles, lines, rectangles and text, so a firmware could
// draw it with its own fills.

enum PhaseLayout {
    static let title = CGRect(x: 0, y: 0, width: 240, height: 30)
    static let score = CGRect(x: 0, y: 31, width: 240, height: 132)
    static let center = CGPoint(x: 120, y: 92)
    static let outer: CGFloat = 55
    static func tab(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 59, y: 168, width: 56, height: 26) }
    static func chip(_ i: Int) -> CGRect { CGRect(x: 3 + CGFloat(i) * 59, y: 200, width: 56, height: 37) }
    static let pill = CGRect(x: 14, y: 204, width: 212, height: 28)
    // The score alone: this much larger, centred lower, with the status this far down.
    static let bigScale: CGFloat = 1.65
    static let bigCenterY: CGFloat = 108
    static let bigStatusDrop: CGFloat = 68

    // The ring each part plays on: the first outermost.
    static func radius(_ q: Int, of parts: Int) -> CGFloat {
        guard parts > 1 else { return outer }
        return outer - CGFloat(q) * min(16, 33 / CGFloat(parts - 1))
    }
}

private let ink = Color(hex: 0x141414)
private let paper = Color(hex: 0xF3F0E8)
private let red = Color(hex: 0xD7261E)
private let faded = Color(hex: 0x9A958C)
private let pale = Color(hex: 0xD6D1C6)

struct PhaseScreen: View {
    @ObservedObject var model: PhaseModel
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
        ctx.fill(Path(CGRect(x: 0, y: 0, width: 240, height: 240)), with: .color(paper))
        if model.bigScore {
            drawScore(&ctx, model.view(), big: true)
            return
        }
        drawTitle(&ctx)
        if mf1_bank_pending() {
            drawBankPrompt(&ctx)
            return
        }
        if model.writing {
            drawPhrase(&ctx)
        } else {
            drawScore(&ctx, model.view())
        }
        drawTabs(&ctx)
        if let pill = model.pill {
            drawPill(&ctx, pill)
        } else {
            drawChips(&ctx)
        }
    }

    private func drawTitle(_ ctx: inout GraphicsContext) {
        ctx.fill(Path(ellipseIn: CGRect(x: 9, y: 9, width: 9, height: 9)), with: .color(red))
        text(&ctx, model.info.title.uppercased(), CGPoint(x: 25, y: 14), size: 10.5, color: ink, anchor: .leading,
             bold: true, kerning: 2.6)
        drawBattery(&ctx)
        ctx.fill(Path(CGRect(x: 9, y: 27, width: 222, height: 0.75)), with: .color(ink))
    }

    // The FM-1's battery icon where the firmware draws it. As in MF-1, the emulator's battery is a third full.
    private func drawBattery(_ ctx: inout GraphicsContext) {
        let r = CGRect(x: 207, y: 8, width: 22, height: 12)
        ctx.stroke(Path(r), with: .color(ink), lineWidth: 1.2)
        ctx.fill(Path(CGRect(x: 229.5, y: 11.5, width: 2, height: 5)), with: .color(ink))
        ctx.fill(Path(CGRect(x: 209.5, y: 10.5, width: 5, height: 7)), with: .color(ink))
    }

    private func drawBankPrompt(_ ctx: inout GraphicsContext) {
        let r = CGRect(x: 16, y: 50, width: 208, height: 110)
        ctx.fill(Path(r), with: .color(ink))
        text(&ctx, "A BANK ARRIVED", CGPoint(x: 120, y: 76), size: 12, color: paper, bold: true, kerning: 2)
        text(&ctx, "Turn KNOB1-4 to store it", CGPoint(x: 120, y: 102), size: 10, color: pale)
        text(&ctx, "in bank A, B, C or D", CGPoint(x: 120, y: 117), size: 10, color: pale)
        text(&ctx, "PROC cancels", CGPoint(x: 120, y: 142), size: 9, color: faded)
        for i in 0..<4 {
            let b = PhaseLayout.chip(i)
            ctx.fill(Path(b), with: .color(i == 0 ? red : ink))
            text(&ctx, ["A", "B", "C", "D"][i], CGPoint(x: b.midX, y: b.midY), size: 16, color: paper, bold: true)
        }
    }

    // ---------------------------------------------------------------- the score

    private func point(_ angle: Double, _ r: CGFloat) -> CGPoint {
        CGPoint(x: PhaseLayout.center.x + CGFloat(cos(angle)) * r, y: PhaseLayout.center.y + CGFloat(sin(angle)) * r)
    }

    private func dot(_ ctx: inout GraphicsContext, _ p: CGPoint, _ r: CGFloat, _ color: Color) {
        ctx.fill(Path(ellipseIn: CGRect(x: p.x - r, y: p.y - r, width: r * 2, height: r * 2)), with: .color(color))
    }

    private func drawScore(_ ctx: inout GraphicsContext, _ v: PhaseView, big: Bool = false) {
        let parts = Int(v.parts), length = max(2, Int(v.length))
        let shown = model.playing || model.snapshotTick != nil
        let views = withUnsafeBytes(of: v.part) { Array($0.bindMemory(to: PhasePartView.self)) }
        let status = ctx
        if big {
            let k = PhaseLayout.bigScale, c = PhaseLayout.center
            ctx.translateBy(x: c.x, y: PhaseLayout.bigCenterY)
            ctx.scaleBy(x: k, y: k)
            ctx.translateBy(x: -c.x, y: -c.y)
        }

        // Now: a red line from the centre up through every ring.
        let top = PhaseLayout.center.y - PhaseLayout.outer - 9
        let inner = PhaseLayout.radius(parts - 1, of: parts)
        ctx.fill(Path(CGRect(x: 119.4, y: top, width: 1.2, height: PhaseLayout.outer + 9 - inner + 9)), with: .color(red))

        for q in 0..<parts {
            let part = views[q]
            let r = PhaseLayout.radius(q, of: parts)
            let pos = Double(part.pos_milli) / 1000
            let active = part.active
            let ns = notes(part), lens = lengths(part)
            // Higher notes sit a little further out, within the ring's own span, so the rings stay apart.
            let sounding = ns.prefix(length).filter { $0 >= 0 }
            let mid = Double((sounding.min() ?? 60) + (sounding.max() ?? 60)) / 2
            let half = max(6, Double((sounding.max() ?? 60) - (sounding.min() ?? 60)) / 2)
            var ring = Path()
            ring.addEllipse(in: CGRect(x: PhaseLayout.center.x - r, y: PhaseLayout.center.y - r, width: r * 2, height: r * 2))
            if active {
                ctx.stroke(ring, with: .color(ink), lineWidth: 0.7)
            } else {
                ctx.stroke(ring, with: .color(pale), style: StrokeStyle(lineWidth: 0.7, dash: [1.5, 2.5]))
            }
            func angle(_ step: Double) -> Double { -Double.pi / 2 + 2 * Double.pi * (step - pos) / Double(length) }
            // The first step: a bar across the ring, so how far the rings have turned against each other shows.
            let a0 = angle(0)
            var bar = Path()
            bar.move(to: point(a0, r - 5))
            bar.addLine(to: point(a0, r + 5))
            ctx.stroke(bar, with: .color(active ? ink : pale), lineWidth: 1.4)
            for i in 0..<length {
                let a = angle(Double(i))
                let n = ns[i]
                if n < 0 {
                    dot(&ctx, point(a, r), 0.8, active ? faded : pale)
                    continue
                }
                let rr = r + CGFloat(max(-1, min(1, (Double(n) - mid) / half))) * 2.5
                let len = Double(lens[i]) / 1000
                // Sounding: the play line is inside the note.
                let into = (pos - Double(i)).truncatingRemainder(dividingBy: Double(length))
                let since = into < 0 ? into + Double(length) : into
                let lit = active && shown && since < max(len, 0.15)
                let color = !active ? pale : lit ? red : ink
                if len > 0.55 {
                    var arc = Path()
                    arc.addArc(center: PhaseLayout.center, radius: rr, startAngle: .radians(a),
                               endAngle: .radians(angle(Double(i) + min(len, Double(length) - 0.2))), clockwise: false)
                    ctx.stroke(arc, with: .color(color), lineWidth: 1.8)
                }
                dot(&ctx, point(a, rr), lit ? 3.6 : 2.5, color)
            }
        }
        if !shown {
            // Stopped: a play sign in the middle.
            var tri = Path()
            let c = PhaseLayout.center
            tri.move(to: CGPoint(x: c.x - 4, y: c.y - 6))
            tri.addLine(to: CGPoint(x: c.x + 6, y: c.y))
            tri.addLine(to: CGPoint(x: c.x - 4, y: c.y + 6))
            tri.closeSubpath()
            ctx.fill(tri, with: .color(ink))
        }
        var s = status
        if big { s.translateBy(x: 0, y: PhaseLayout.bigStatusDrop) }
        drawStatus(&s, v, views, shown: shown)
        ctx = status
    }

    private func notes(_ p: PhasePartView) -> [Int] {
        withUnsafeBytes(of: p.note) { Array($0.bindMemory(to: Int8.self)).map { Int($0) } }
    }

    private func lengths(_ p: PhasePartView) -> [Int] {
        withUnsafeBytes(of: p.len_milli) { Array($0.bindMemory(to: Int32.self)).map { Int($0) } }
    }

    // Under the rings: the process in red, how it stands, and the offset.
    private func drawStatus(_ ctx: inout GraphicsContext, _ v: PhaseView, _ views: [PhasePartView],
                            shown: Bool) {
        let process = model.value(PH.process)
        let length = Int(v.length)
        let on = views.prefix(Int(v.parts)).filter(\.active).count
        let stage = Int(v.stage), stages = Int(v.stages)
        var state: String
        var figure: String
        switch process {
        case PH_PHASE, PH_RESULT, PH_TAPE, PH_SHIFT:
            let off = Double(v.offset_milli) / 1000
            figure = v.drifting ? String(format: "+%.1f/%d", off, length) : "+\(Int(off))/\(length)"
            state = process == Int(PH_TAPE) ? "sliding" : v.drifting ? "drifting" : off == 0 ? "unison" : "locked"
        case PH_CANON:
            figure = v.parts > 1 ? "−\(-Int(v.lead_milli) / 1000)/\(length)" : "0/\(length)"
            state = "\(on) of \(v.parts) in"
        case PH_BUILD:
            let rests = stages / 2
            let fills = stage <= rests ? stage : stages - stage
            figure = "+\(fills)/\(rests)"
            state = stage < rests ? "filling" : "emptying"
        case PH_PULSE:
            figure = "\(stage + 1)/\(max(1, stages))"
            state = "\(on) of \(v.parts) sounding"
        default:
            let gate = max(1, model.value(PH.gate) * 10)
            let factor = Double(views[0].len_milli.0) / Double(gate)
            figure = String(format: factor < 10 ? "×%.1f" : "×%.0f", factor)
            state = stage < 8 ? "growing" : "shrinking"
        }
        if !shown { state = "stopped" }
        let name = PH.processes[max(0, min(PH.processes.count - 1, process))].uppercased()
        text(&ctx, name, CGPoint(x: 10, y: 152), size: 9, color: red, anchor: .leading, bold: true, kerning: 2.2)
        text(&ctx, state.uppercased(), CGPoint(x: 10, y: 162), size: 6.5, color: faded, anchor: .leading, kerning: 1.4)
        text(&ctx, figure, CGPoint(x: 230, y: 155), size: 15, color: ink, anchor: .trailing, bold: true)
    }

    // While a phrase is being written: the cell it will become, its notes as stems on a line in their steps, the
    // newest in red, and a red bar running out as the wait for the phrase to end goes by.
    private func drawPhrase(_ ctx: inout GraphicsContext) {
        let ns = PhaseModel.cell(from: model.phrase)
        let last = ns.lastIndex { $0 >= 0 } ?? 0
        let x0: CGFloat = 20, w: CGFloat = 200, base: CGFloat = 104
        text(&ctx, "WRITING", CGPoint(x: 10, y: 44), size: 9, color: red, anchor: .leading, bold: true, kerning: 2.2)
        text(&ctx, "\(ns.count)/\(PH.maxSteps)", CGPoint(x: 230, y: 44), size: 9, color: faded, anchor: .trailing,
             kerning: 1)
        ctx.fill(Path(CGRect(x: x0, y: base, width: w, height: 0.75)), with: .color(ink))
        for i in 0..<PH.maxSteps {
            let x = x0 + (CGFloat(i) + 0.5) * w / CGFloat(PH.maxSteps)
            let n = i < ns.count ? ns[i] : -1
            guard n >= 0 else {
                ctx.fill(Path(CGRect(x: x - 0.5, y: base + 3, width: 1, height: 3)), with: .color(i < ns.count ? ink : pale))
                continue
            }
            let y = base - CGFloat(max(-30, min(44, Double(n - 60) * 1.7)))
            let color = i == last ? red : ink
            ctx.fill(Path(CGRect(x: x - 0.5, y: min(y, base), width: 1, height: abs(base - y))), with: .color(color))
            dot(&ctx, CGPoint(x: x, y: y), 3.4, color)
        }
        let left = 1 - model.phraseWait
        ctx.fill(Path(CGRect(x: x0, y: 146, width: w * CGFloat(left), height: 3)), with: .color(red))
        text(&ctx, "THE PHRASE BECOMES THE CELL", CGPoint(x: 120, y: 158), size: 6.5, color: faded, kerning: 1.4)
    }

    // ---------------------------------------------------------------- pages and values

    private func drawTabs(_ ctx: inout GraphicsContext) {
        for p in PhasePage.allCases {
            let r = PhaseLayout.tab(p.rawValue)
            let focused = model.page == p
            if focused {
                ctx.fill(Path(r), with: .color(ink))
                ctx.fill(Path(CGRect(x: r.minX, y: r.minY, width: 4, height: r.height)), with: .color(red))
            } else {
                ctx.fill(Path(CGRect(x: r.minX, y: r.minY, width: r.width, height: 0.75)), with: .color(ink))
            }
            text(&ctx, p.title.uppercased(), CGPoint(x: r.midX + (focused ? 2 : 0), y: r.midY), size: 8,
                 color: focused ? paper : ink, bold: focused, kerning: 1.4)
        }
    }

    private func drawChips(_ ctx: inout GraphicsContext) {
        for (i, p) in model.params(model.page).enumerated() {
            let r = PhaseLayout.chip(i)
            if i > 0 { ctx.fill(Path(CGRect(x: r.minX - 1.5, y: r.minY + 4, width: 0.6, height: r.height - 8)), with: .color(pale)) }
            let v = model.value(p.field)
            let label = p.format(v)
            text(&ctx, p.name.uppercased(), CGPoint(x: r.midX, y: r.minY + 6), size: 6, color: faded, kerning: 1.1)
            text(&ctx, label, CGPoint(x: r.midX, y: r.minY + 19), size: label.count > 6 ? 10 : 13, color: ink, bold: true)
            // A track with a red square where the value sits.
            let range = p.range
            let t = CGFloat(v - range.lowerBound) / CGFloat(max(1, range.upperBound - range.lowerBound))
            let tr = CGRect(x: r.minX + 7, y: r.maxY - 6, width: r.width - 14, height: 0.75)
            ctx.fill(Path(tr), with: .color(ink))
            ctx.fill(Path(CGRect(x: tr.minX + t * tr.width - 2.5, y: tr.midY - 2.5, width: 5, height: 5)), with: .color(red))
        }
    }

    private func drawPill(_ ctx: inout GraphicsContext, _ label: String) {
        let r = PhaseLayout.pill
        ctx.fill(Path(r), with: .color(ink))
        ctx.fill(Path(CGRect(x: r.minX, y: r.minY, width: 6, height: r.height)), with: .color(red))
        text(&ctx, label, CGPoint(x: r.midX + 3, y: r.midY), size: 11.5, color: paper, bold: true, kerning: 0.6)
    }
}

// ------------------------------------------------------------------ screen clicks

enum PhaseTarget: Equatable {
    case none, title, score, tab(Int), chip(Int), pill
    // The enlarged score shows nothing to click: a click or a turn only brings the pages back.
    case wake
}

extension PhaseModel {
    func screenTarget(at p: CGPoint) -> PhaseTarget {
        if bigScore { return .wake }
        if PhaseLayout.title.contains(p) { return .title }
        if mf1_bank_pending() {
            for i in 0..<4 where PhaseLayout.chip(i).contains(p) { return .chip(i) }
            return .none
        }
        for i in 0..<PhasePage.allCases.count where PhaseLayout.tab(i).insetBy(dx: -1.5, dy: -1.5).contains(p) { return .tab(i) }
        if pill != nil, PhaseLayout.pill.contains(p) { return .pill }
        if pill == nil {
            for i in 0..<4 where PhaseLayout.chip(i).contains(p) { return .chip(i) }
        }
        if PhaseLayout.score.contains(p) { return .score }
        return .none
    }

    // A click on the score plays or stops; the pages and values work as on the panel.
    func screenClick(_ t: PhaseTarget, back: Bool) {
        touch()
        switch t {
        case .tab(let i): page = PhasePage(rawValue: i) ?? page
        case .chip(let i):
            if mf1_bank_pending() {
                turnKnob(i, 1)
            } else {
                clickChip(i, back: back)
            }
        case .score: if back { restart() } else { togglePlay() }
        case .pill: pill = nil
        case .title, .none, .wake: break
        }
    }

    func screenTurn(_ t: PhaseTarget, _ delta: Int) {
        guard delta != 0 else { return }
        touch()
        switch t {
        case .title: turnPresets(delta)
        case .score: turnAlgorithm(delta)
        case .chip(let i): turnKnob(i, delta)
        case .tab: turnSelect(delta > 0 ? 1 : -1)
        default: break
        }
    }

    func pieceMenu() -> NSMenu {
        let menu = NSMenu()
        for (i, m) in Self.pieces.enumerated() {
            let item = ClosureMenuItem(title: m.title) { [weak self] in self?.selectPiece(i) }
            item.state = i == piece ? .on : .off
            menu.addItem(item)
        }
        return menu
    }
}

// Mouse input on the screen: clicks act on what is under the pointer, a vertical drag or the scroll wheel turns it
// like a knob, a click on the title opens the list of pieces.
final class PhaseScreenInputView: NSView {
    var model: PhaseModel?
    private var target: PhaseTarget = .none
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
            model.pieceMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        } else {
            model.screenClick(target, back: e.modifierFlags.contains(.shift))
        }
    }

    override func rightMouseDown(with e: NSEvent) {
        guard let model else { return }
        let t = model.screenTarget(at: screenPoint(e))
        if t == .title {
            model.pieceMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
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

struct PhaseScreenInput: NSViewRepresentable {
    let model: PhaseModel
    func makeNSView(context: Context) -> PhaseScreenInputView {
        let v = PhaseScreenInputView()
        v.model = model
        return v
    }
    func updateNSView(_ v: PhaseScreenInputView, context: Context) { v.model = model }
}
