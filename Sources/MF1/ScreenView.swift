import MF1Core
import SwiftUI

// The 1.54" 240x240 TFT. Everything is drawn in screen pixels and scaled.
struct ScreenView: View {
    @ObservedObject var model: DeviceModel
    // Changes on every screen frame. Without a changing input, SwiftUI
    // would skip redrawing the scope between model changes.
    var frame = 0

    static let bgTop = Color(red: 0.69, green: 0.84, blue: 0.90)
    static let bgBottom = Color(red: 0.58, green: 0.77, blue: 0.86)
    static let boxTop = Color(red: 0.86, green: 0.95, blue: 0.98)
    static let boxBottom = Color(red: 0.66, green: 0.85, blue: 0.93)
    static let boxBorder = Color(red: 0.33, green: 0.50, blue: 0.60)
    static let dimTop = Color(red: 0.50, green: 0.58, blue: 0.64)
    static let dimBottom = Color(red: 0.38, green: 0.47, blue: 0.55)
    static let ink = Color(red: 0.07, green: 0.12, blue: 0.16)
    static let red = Color(red: 0.88, green: 0.25, blue: 0.20)

    var body: some View {
        Canvas { ctx, size in
            ctx.scaleBy(x: size.width / 240, y: size.height / 240)
            draw(&ctx)
        }
    }

    // ---------------------------------------------------------------- drawing helpers

    private func text(_ ctx: inout GraphicsContext, _ s: String, _ p: CGPoint, size: CGFloat = 14,
                      color: Color = ScreenView.ink, anchor: UnitPoint = .center, weight: Font.Weight = .regular) {
        let t = Text(s).font(.system(size: size, weight: weight)).foregroundColor(color)
        ctx.draw(t, at: p, anchor: anchor)
    }

    private func box(_ ctx: inout GraphicsContext, _ r: CGRect, on: Bool = true, radius: CGFloat = 7) {
        let path = Path(roundedRect: r, cornerRadius: radius)
        var shadow = ctx
        shadow.translateBy(x: 1.5, y: 2)
        shadow.fill(path, with: .color(.black.opacity(0.18)))
        ctx.fill(path, with: .linearGradient(Gradient(colors: on ? [Self.boxTop, Self.boxBottom] : [Self.dimTop, Self.dimBottom]),
                                             startPoint: CGPoint(x: r.midX, y: r.minY), endPoint: CGPoint(x: r.midX, y: r.maxY)))
        ctx.stroke(path, with: .color(Self.boxBorder.opacity(on ? 0.8 : 0.6)), lineWidth: 1)
    }

    private func pill(_ ctx: inout GraphicsContext, _ label: String) {
        let r = ScreenLayout.pill
        box(&ctx, r, on: true, radius: 6)
        text(&ctx, label, CGPoint(x: r.midX, y: r.midY), size: 15)
    }

    // ---------------------------------------------------------------- frame

    private func draw(_ ctx: inout GraphicsContext) {
        let full = CGRect(x: 0, y: 0, width: 240, height: 240)
        let home = model.mode == .home && model.popup == nil && !overlayActive
        if home {
            ctx.fill(Path(full), with: .linearGradient(Gradient(colors: [Color(red: 0.20, green: 0.34, blue: 0.42),
                                                                           Color(red: 0.17, green: 0.30, blue: 0.38)]),
                                                         startPoint: .zero, endPoint: CGPoint(x: 0, y: 240)))
        } else {
            ctx.fill(Path(full), with: .linearGradient(Gradient(colors: [Self.bgTop, Self.bgBottom]),
                                                         startPoint: .zero, endPoint: CGPoint(x: 0, y: 240)))
        }
        // Top bar: voice number and name, battery.
        let titleColor = home ? Color(white: 0.92) : Self.ink
        text(&ctx, model.presetTitle, CGPoint(x: 7, y: 16), size: 18, color: titleColor, anchor: .leading)
        drawBattery(&ctx, color: titleColor)
        if mf1_global_get(Int32(MF1_G_EXTENDED)) != 0 {
            // Extended mode marker, so it is always clear the sound may go
            // beyond what an FM-1 can play.
            let r = CGRect(x: 172, y: 8, width: 30, height: 15)
            ctx.stroke(Path(roundedRect: r, cornerRadius: 3), with: .color(titleColor.opacity(0.8)), lineWidth: 1)
            text(&ctx, "EXT", CGPoint(x: r.midX, y: r.midY), size: 10, color: titleColor, weight: .semibold)
        }
        ctx.fill(Path(CGRect(x: 0, y: 31, width: 240, height: 1)), with: .color(titleColor.opacity(0.35)))

        if overlayActive {
            drawOverlay(&ctx)
        } else if let p = model.popup {
            drawPopup(&ctx, p)
        } else {
            switch model.mode {
            case .home: drawScope(&ctx)
            case .fx: drawFx(&ctx)
            default: drawGrid(&ctx)
            }
        }
        if !overlayActive {
            if model.mode == .home && model.popup == nil {
                pill(&ctx, model.pill ?? "OSC")
            } else if case .algorithm = model.popup {
                pill(&ctx, "Algorithm: \(Int(mf1_edit_get(134)) + 1)")
            } else if model.popup == nil {
                pill(&ctx, model.pill ?? model.pageTitle)
            }
        }
    }

    private var overlayActive: Bool {
        mf1_bank_pending() || model.renaming != nil || model.copying != nil || model.resetting
    }

    private func drawBattery(_ ctx: inout GraphicsContext, color: Color) {
        let r = CGRect(x: 207, y: 9, width: 24, height: 13)
        ctx.stroke(Path(roundedRect: r, cornerRadius: 3), with: .color(color), lineWidth: 1.5)
        ctx.fill(Path(CGRect(x: 231.5, y: 13, width: 2, height: 5)), with: .color(color))
        // Room for three bars; one is lit. An emulator's battery is always
        // a third full, as a joke.
        ctx.fill(Path(CGRect(x: 209.75, y: 11.5, width: 5, height: 8)), with: .color(color))
    }

    // ---------------------------------------------------------------- pages

    static var drawCount = 0

    private func drawScope(_ ctx: inout GraphicsContext) {
        Self.drawCount += 1
        var samples = [Float](repeating: 0, count: 1024)
        mf1_scope(&samples, 1024)
        // Start at a rising zero crossing so a steady tone stands still.
        var start = 0
        for i in 1..<(1024 - 480) where samples[i - 1] <= 0 && samples[i] > 0 {
            start = i
            break
        }
        var path = Path()
        let mid: CGFloat = 116
        for x in 0..<240 {
            let s = CGFloat(samples[start + x * 2])
            let y = mid - max(-1, min(1, s * 2.2)) * 78
            if x == 0 { path.move(to: CGPoint(x: 0, y: y)) } else { path.addLine(to: CGPoint(x: CGFloat(x), y: y)) }
        }
        ctx.stroke(Path(CGRect(x: 0, y: mid, width: 240, height: 0.5)), with: .color(.white.opacity(0.12)))
        ctx.stroke(path, with: .color(Color(red: 0.95, green: 0.85, blue: 0.35)), lineWidth: 1.6)
    }

    private func drawGrid(_ ctx: inout GraphicsContext) {
        let ps = model.params
        for i in 0..<4 {
            let r = ScreenLayout.grid(i)
            guard i < ps.count, !ps[i].name.isEmpty else { continue }
            let p = ps[i]
            box(&ctx, r)
            if p.bars {
                text(&ctx, p.name, CGPoint(x: r.midX, y: r.minY + 16), size: 16)
                let v = p.get()
                let blocks = Int((Double(v) / 100 * 10).rounded())
                for b in 0..<10 {
                    let br = CGRect(x: r.minX + 8 + CGFloat(b) * 8.8, y: r.minY + 36, width: 7, height: 22)
                    ctx.fill(Path(roundedRect: br, cornerRadius: 1.5),
                             with: .color(b < blocks ? Color(red: 0.12, green: 0.36, blue: 0.52) : Self.boxBorder.opacity(0.18)))
                }
            } else if p.action != nil {
                text(&ctx, p.name, CGPoint(x: r.midX, y: r.midY), size: 15)
            } else if !p.showName {
                text(&ctx, p.format(p.get()), CGPoint(x: r.midX, y: r.midY), size: 18)
            } else {
                text(&ctx, p.name, CGPoint(x: r.midX, y: r.minY + 22), size: 15)
                text(&ctx, p.format(p.get()), CGPoint(x: r.midX, y: r.minY + 48), size: 14)
            }
        }
        if model.mode == .seq && mf1_seq_recording() {
            let step = mf1_seq_playing() ? Int(mf1_seq_play_step()) : Int(mf1_seq_rec_step())
            text(&ctx, "● REC  step \(step + 1)", CGPoint(x: 120, y: 198), size: 11, color: Self.red)
        }
    }

    private func drawFx(_ ctx: inout GraphicsContext) {
        for pos in 0..<6 {
            let effect = FxEffect(rawValue: Int(mf1_fx_order(Int32(pos))))!
            let on = mf1_fx_get(effect.ccs[3]) != 0
            let r = ScreenLayout.fxSlot(pos)
            box(&ctx, r, on: on, radius: 6)
            let ink = on ? Self.ink : Color(white: 0.9)
            if effect == .distortion {
                text(&ctx, "Distort", CGPoint(x: r.midX, y: r.midY - 9), size: 15, color: ink)
                text(&ctx, "ion", CGPoint(x: r.midX, y: r.midY + 9), size: 15, color: ink)
            } else {
                text(&ctx, effect.title, CGPoint(x: r.midX, y: r.midY), size: 15, color: ink)
            }
            if pos == model.fxSelected {
                // The dot blinks while choosing and stays solid while grabbed.
                let visible = model.fxGrabbed || (model.tick / 20) % 2 == 0
                if visible {
                    ctx.fill(Path(ellipseIn: CGRect(x: r.midX - 3.5, y: r.maxY + 5, width: 7, height: 7)), with: .color(Self.red))
                }
            }
        }
    }

    // ---------------------------------------------------------------- popups

    private func drawPopup(_ ctx: inout GraphicsContext, _ p: Popup) {
        switch p {
        case .presets:
            let cur = model.preset
            for row in -2...2 {
                let n = Int(mf1_preset_count())
                let i = ((cur + row) % n + n) % n
                let r = ScreenLayout.presetRow(row)
                if row == 0 { box(&ctx, r, radius: 6) }
                text(&ctx, String(format: "%03d %@", i + 1, Engine.presetName(i)), CGPoint(x: 26, y: r.midY),
                     size: row == 0 ? 17 : 15, color: Self.ink.opacity(row == 0 ? 1 : 0.55), anchor: .leading)
            }
        case .algorithm:
            drawAlgorithm(&ctx, Int(mf1_edit_get(134)))
        case .value(let name, let value):
            let r = CGRect(x: 40, y: 70, width: 160, height: 92)
            box(&ctx, r)
            text(&ctx, name, CGPoint(x: r.midX, y: r.minY + 28), size: 18)
            text(&ctx, value, CGPoint(x: r.midX, y: r.minY + 62), size: 26, weight: .medium)
            pill(&ctx, name)
        case .text(let s):
            let r = CGRect(x: 20, y: 84, width: 200, height: 60)
            box(&ctx, r)
            text(&ctx, s, CGPoint(x: r.midX, y: r.midY), size: 17)
            pill(&ctx, model.pageTitle)
        }
    }

    private func drawOverlay(_ ctx: inout GraphicsContext) {
        if mf1_bank_pending() {
            text(&ctx, "Bulk Dump: 32 Voices", CGPoint(x: 120, y: 50), size: 16)
            text(&ctx, "1. \(Engine.pendingName(0))", CGPoint(x: 120, y: 74), size: 13, color: Self.ink.opacity(0.7))
            for i in 0..<4 {
                let r = ScreenLayout.bankBox(i)
                box(&ctx, r)
                text(&ctx, "Bank \(["A", "B", "C", "D"][i])", CGPoint(x: r.midX, y: r.midY - 7), size: 16)
                text(&ctx, "KNOB\(i + 1)", CGPoint(x: r.midX, y: r.midY + 12), size: 10, color: Self.ink.opacity(0.6))
            }
            pill(&ctx, "Select Bank")
            return
        }
        if model.resetting {
            text(&ctx, "Reset Patches", CGPoint(x: 120, y: 54), size: 17)
            for i in 0..<4 {
                let r = ScreenLayout.resetBox(i)
                box(&ctx, r, on: i == model.resetBank)
                text(&ctx, "Bank \(["A", "B", "C", "D"][i])", CGPoint(x: r.midX, y: r.midY), size: 17,
                     color: i == model.resetBank ? Self.ink : Color(white: 0.9))
            }
            pill(&ctx, "SELECT: confirm")
            return
        }
        if let name = model.renaming {
            text(&ctx, "Rename", CGPoint(x: 120, y: 52), size: 17)
            for i in 0..<10 {
                let r = ScreenLayout.renameCell(i)
                box(&ctx, r, on: i == model.renameCursor, radius: 4)
                text(&ctx, String(name[i]), CGPoint(x: r.midX, y: r.midY), size: 17, weight: .medium)
            }
            let classes = ["A-Z", "a-z", "Symbol", "0-9"]
            for i in 0..<4 {
                let r = ScreenLayout.renameClass(i)
                box(&ctx, r, on: i == model.renameClass, radius: 5)
                text(&ctx, classes[i], CGPoint(x: r.midX, y: r.midY), size: 12,
                     color: i == model.renameClass ? Self.ink : Color(white: 0.9))
            }
            text(&ctx, "OP1-4 set  OP5 delete  K3 char", CGPoint(x: 120, y: 174), size: 11, color: Self.ink.opacity(0.7))
            pill(&ctx, "SELECT: confirm")
            return
        }
        if let src = model.copying {
            text(&ctx, "Copy Voice", CGPoint(x: 120, y: 56), size: 17)
            let r = ScreenLayout.copyBox
            box(&ctx, r)
            text(&ctx, String(format: "%03d %@", src + 1, Engine.presetName(src)), CGPoint(x: r.midX, y: r.midY), size: 17)
            text(&ctx, "KNOB4 source", CGPoint(x: 120, y: 162), size: 12, color: Self.ink.opacity(0.7))
            pill(&ctx, "SELECT: confirm")
        }
    }

    // ---------------------------------------------------------------- algorithm diagram

    // Modulator -> target pairs and the feedback operator, per DX7 algorithm.
    static let algorithms: [(edges: [(Int, Int)], fb: Int)] = [
        ([(2, 1), (6, 5), (5, 4), (4, 3)], 6), ([(2, 1), (6, 5), (5, 4), (4, 3)], 2),
        ([(3, 2), (2, 1), (6, 5), (5, 4)], 6), ([(3, 2), (2, 1), (6, 5), (5, 4)], 4),
        ([(2, 1), (4, 3), (6, 5)], 6), ([(2, 1), (4, 3), (6, 5)], 5),
        ([(2, 1), (4, 3), (5, 3), (6, 5)], 6), ([(2, 1), (4, 3), (5, 3), (6, 5)], 4),
        ([(2, 1), (4, 3), (5, 3), (6, 5)], 2), ([(3, 2), (2, 1), (5, 4), (6, 4)], 3),
        ([(3, 2), (2, 1), (5, 4), (6, 4)], 6), ([(2, 1), (4, 3), (5, 3), (6, 3)], 2),
        ([(2, 1), (4, 3), (5, 3), (6, 3)], 6), ([(2, 1), (4, 3), (5, 4), (6, 4)], 6),
        ([(2, 1), (4, 3), (5, 4), (6, 4)], 2), ([(2, 1), (3, 1), (4, 3), (5, 1), (6, 5)], 6),
        ([(2, 1), (3, 1), (4, 3), (5, 1), (6, 5)], 2), ([(2, 1), (3, 1), (4, 1), (5, 4), (6, 5)], 3),
        ([(3, 2), (2, 1), (6, 4), (6, 5)], 6), ([(3, 1), (3, 2), (5, 4), (6, 4)], 3),
        ([(3, 1), (3, 2), (6, 4), (6, 5)], 3), ([(2, 1), (6, 3), (6, 4), (6, 5)], 6),
        ([(3, 2), (6, 4), (6, 5)], 6), ([(6, 3), (6, 4), (6, 5)], 6),
        ([(6, 4), (6, 5)], 6), ([(3, 2), (5, 4), (6, 4)], 6),
        ([(3, 2), (5, 4), (6, 4)], 3), ([(2, 1), (5, 4), (4, 3)], 5),
        ([(4, 3), (6, 5)], 6), ([(5, 4), (4, 3)], 5),
        ([(6, 5)], 6), ([], 6),
    ]

    private func drawAlgorithm(_ ctx: inout GraphicsContext, _ alg: Int) {
        let def = Self.algorithms[max(0, min(31, alg))]
        var targets: [Int: [Int]] = [:]
        for (m, t) in def.edges { targets[m, default: []].append(t) }
        let carriers = (1...6).filter { targets[$0] == nil }
        // Rows: carriers at 0, each modulator one above its target.
        var level: [Int: Int] = [:]
        func lv(_ op: Int) -> Int {
            if let l = level[op] { return l }
            let l = targets[op].map { $0.map(lv).max()! + 1 } ?? 0
            level[op] = l
            return l
        }
        for op in 1...6 { _ = lv(op) }
        // Columns: carriers left to right, modulators over their target.
        var col: [Int: Int] = [:]
        var used: [Int: Set<Int>] = [:]
        for (i, c) in carriers.enumerated() {
            col[c] = i
            used[0, default: []].insert(i)
        }
        let maxLevel = level.values.max() ?? 0
        if maxLevel > 0 {
            for l in 1...maxLevel {
                for op in (1...6).filter({ level[$0] == l }) {
                    var c = targets[op]!.compactMap { col[$0] }.min() ?? 0
                    while used[l, default: []].contains(c) { c += 1 }
                    col[op] = c
                    used[l, default: []].insert(c)
                }
            }
        }
        let width = (col.values.max() ?? 0) + 1
        let dx: CGFloat = 36, dy: CGFloat = 32
        let x0 = 120 - CGFloat(width - 1) * dx / 2
        let yBase: CGFloat = 150
        func center(_ op: Int) -> CGPoint {
            CGPoint(x: x0 + CGFloat(col[op]!) * dx, y: yBase - CGFloat(level[op]!) * dy)
        }
        let lineColor = Self.ink.opacity(0.75)
        for (m, t) in def.edges {
            let a = center(m), b = center(t)
            var p = Path()
            p.move(to: CGPoint(x: a.x, y: a.y + 9))
            if a.x != b.x {
                p.addLine(to: CGPoint(x: a.x, y: (a.y + b.y) / 2))
                p.addLine(to: CGPoint(x: b.x, y: (a.y + b.y) / 2))
            }
            p.addLine(to: CGPoint(x: b.x, y: b.y - 9))
            ctx.stroke(p, with: .color(lineColor), lineWidth: 1.4)
        }
        // OUT bar under the carriers.
        let outY: CGFloat = 182
        let cx = carriers.map { center($0).x }
        let out = CGRect(x: min(cx.min()! - 16, 92), y: outY - 9, width: max(cx.max()! - cx.min()! + 32, 56), height: 18)
        for c in carriers {
            var p = Path()
            p.move(to: CGPoint(x: center(c).x, y: center(c).y + 9))
            p.addLine(to: CGPoint(x: center(c).x, y: out.minY))
            ctx.stroke(p, with: .color(lineColor), lineWidth: 1.4)
        }
        box(&ctx, out, radius: 6)
        text(&ctx, "OUT", CGPoint(x: out.midX, y: out.midY), size: 13, weight: .medium)
        // Feedback loop drawn as a bracket on the right of the operator.
        let f = center(def.fb)
        var fp = Path()
        fp.move(to: CGPoint(x: f.x, y: f.y - 9))
        fp.addLine(to: CGPoint(x: f.x, y: f.y - 15))
        fp.addLine(to: CGPoint(x: f.x + 15, y: f.y - 15))
        fp.addLine(to: CGPoint(x: f.x + 15, y: f.y + 13))
        fp.addLine(to: CGPoint(x: f.x, y: f.y + 13))
        ctx.stroke(fp, with: .color(lineColor), lineWidth: 1.4)
        let mutes = Int(mf1_op_mutes())
        for op in 1...6 {
            let c = center(op)
            let r = CGRect(x: c.x - 10, y: c.y - 9, width: 20, height: 18)
            box(&ctx, r, on: mutes & (1 << (op - 1)) == 0, radius: 4)
            text(&ctx, "\(op)", CGPoint(x: c.x, y: c.y), size: 13, weight: .medium)
        }
        let badge = CGRect(x: 10, y: 40, width: 38, height: 24)
        box(&ctx, badge, radius: 8)
        text(&ctx, String(format: "%02d", alg + 1), CGPoint(x: badge.midX, y: badge.midY), size: 15, weight: .medium)
    }
}

// Where things are on the 240x240 screen, shared by drawing and clicks.
enum ScreenLayout {
    static let title = CGRect(x: 0, y: 0, width: 240, height: 32)
    static let pill = CGRect(x: 22, y: 205, width: 196, height: 25)
    static let copyBox = CGRect(x: 20, y: 84, width: 200, height: 56)

    // Parameter boxes: KNOB1 top left .. KNOB4 bottom right.
    static func grid(_ i: Int) -> CGRect {
        CGRect(x: 13 + CGFloat(i % 2) * 111, y: 41 + CGFloat(i / 2) * 81, width: 103, height: 71)
    }
    static func fxSlot(_ pos: Int) -> CGRect {
        CGRect(x: 7 + CGFloat(pos % 3) * 77, y: 44 + CGFloat(pos / 3) * 80, width: 72, height: 58)
    }
    // Preset list rows, -2...2 around the current preset.
    static func presetRow(_ row: Int) -> CGRect {
        CGRect(x: 14, y: 116 + CGFloat(row) * 32 - 14, width: 212, height: 28)
    }
    static func bankBox(_ i: Int) -> CGRect {
        CGRect(x: 13 + CGFloat(i % 2) * 111, y: 92 + CGFloat(i / 2) * 54, width: 103, height: 46)
    }
    static func resetBox(_ i: Int) -> CGRect {
        CGRect(x: 13 + CGFloat(i % 2) * 111, y: 76 + CGFloat(i / 2) * 58, width: 103, height: 50)
    }
    static func renameCell(_ i: Int) -> CGRect { CGRect(x: 10 + CGFloat(i) * 22, y: 78, width: 20, height: 30) }
    static func renameClass(_ i: Int) -> CGRect { CGRect(x: 10 + CGFloat(i) * 56, y: 126, width: 52, height: 26) }
}
