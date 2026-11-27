import AppKit
import MF1Core
import RhymesCore
import SwiftUI

// Rhymes' 240x240 screen: the song's title and stars, its picture, the next few notes as coloured beads with the words
// under them, and a little keyboard of the song's keys showing which comes next. The picture is RhymesCore's, two
// screen pixels to one of its own. The real screen is about 3 cm across, so nothing here is drawn small: the text is
// 11 points or more and the beads and keys are big. The notes are in the colours children's bells use, so the next
// note is a colour as well as a key.

enum RhymesLayout {
    static let title = CGRect(x: 0, y: 0, width: 240, height: 34)
    static let scale: CGFloat = 2
    static let art = CGRect(x: 0, y: 35, width: CGFloat(RH_SCENE_W) * scale, height: CGFloat(RH_SCENE_H) * scale)
    static let train = CGRect(x: 0, y: 164, width: 240, height: 38)
    static let keys = CGRect(x: 0, y: 203, width: 240, height: 37)
    static let pill = CGRect(x: 14, y: 168, width: 212, height: 28)
    static let lowerWhite = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
    static let upperSemis = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]

    // The white keys shown, first and how many: the song's own, at least eight.
    static func window(_ t: rh_tune_t) -> (first: Int, count: Int) {
        var tune = t
        var first: Int32 = 0, count: Int32 = 16
        rh_key_window(&tune, &first, &count)
        return (Int(first), Int(count))
    }

    static func whiteKey(_ i: Int, _ w: (first: Int, count: Int)) -> CGRect? {
        guard i >= w.first && i < w.first + w.count else { return nil }
        let kw = 240 / CGFloat(w.count)
        return CGRect(x: CGFloat(i - w.first) * kw, y: keys.minY, width: kw, height: keys.height)
    }

    // An upper key sits over the gap between the two lower keys either side of it.
    static func blackKey(_ i: Int, _ w: (first: Int, count: Int)) -> CGRect? {
        let below = lowerWhite.lastIndex { $0 < upperSemis[i] } ?? 0
        guard below >= w.first && below + 1 < w.first + w.count else { return nil }
        let kw = 240 / CGFloat(w.count)
        return CGRect(x: CGFloat(below + 1 - w.first) * kw - kw * 0.32, y: keys.minY, width: kw * 0.64, height: 22)
    }
}

private let ink = Color(hex: 0x2B2B2B)
private let paper = Color(hex: 0xFFFDF7)
private let faded = Color(hex: 0x9A948A)
private let gold = Color(hex: 0xFFC83D)

extension rh_tune_t {
    func noteAt(_ i: Int) -> Int { withUnsafeBytes(of: note) { Int($0[i]) } }
    func lenAt(_ i: Int) -> Int { withUnsafeBytes(of: len) { Int($0[i]) } }
    func lineAt(_ i: Int) -> Int { withUnsafeBytes(of: line) { Int($0[max(0, min(Int(count) - 1, i))]) } }
    func syllable(_ i: Int) -> String {
        var t = self
        var buf = [CChar](repeating: 0, count: 16)
        rh_syllable(&t, Int32(i), &buf)
        return String(cString: buf)
    }
}

// A five-pointed star.
func starPath(_ c: CGPoint, _ r: CGFloat) -> Path {
    var p = Path()
    for i in 0..<10 {
        let a = -Double.pi / 2 + Double(i) * Double.pi / 5
        let rr = i % 2 == 0 ? r : r * 0.42
        let pt = CGPoint(x: c.x + rr * CGFloat(cos(a)), y: c.y + rr * CGFloat(sin(a)))
        if i == 0 { p.move(to: pt) } else { p.addLine(to: pt) }
    }
    p.closeSubpath()
    return p
}

struct RhymesScreen: View {
    @ObservedObject var model: RhymesModel
    // Changes on every screen frame; without a changing input, SwiftUI would skip the redraw.
    var frame = 0

    var body: some View {
        let step = frame / (60 / RhymesModel.sceneFPS)
        Canvas { ctx, size in
            ctx.scaleBy(x: size.width / 240, y: size.height / 240)
            var c = ctx
            draw(&c, step: step)
        }
    }

    private func text(_ ctx: inout GraphicsContext, _ s: String, _ p: CGPoint, size: CGFloat, color: Color,
                      anchor: UnitPoint = .center, weight: Font.Weight = .bold) {
        let t = Text(s).font(.system(size: size, weight: weight, design: .rounded)).foregroundColor(color)
        ctx.draw(t, at: p, anchor: anchor)
    }

    private var accent: Color { Color(hex: RH.accent(model.song)) }

    private func draw(_ ctx: inout GraphicsContext, step: Int) {
        let g = model.game
        ctx.fill(Path(CGRect(x: 0, y: 0, width: 240, height: 240)), with: .color(paper))
        drawScene(&ctx, step: step)
        drawTitle(&ctx)
        if mf1_bank_pending() {
            drawBankPrompt(&ctx)
            return
        }
        if g.state == Int32(RH_LISTEN) { drawBadge(&ctx, "♪ Listen") }
        if g.state == Int32(RH_ENCORE) { drawBadge(&ctx, "★ Your song! ★") }
        if g.state == Int32(RH_READY) && model.pill == nil { drawBadge(&ctx, RH.levelHints[model.value(RH.level)]) }
        if g.state == Int32(RH_DONE) {
            drawDone(&ctx, step: step)
        }
        if let pill = model.pill {
            drawPill(&ctx, pill)
        } else {
            drawTrain(&ctx, step: step)
        }
        drawKeys(&ctx)
    }

    private func view() -> rh_view_t {
        var g = model.game
        var v = rh_view_t()
        rh_game_view(&g, model.now, model.led.soundingLo != 0 || model.led.soundingHi != 0 ? 1 : 0, &v)
        return v
    }

    // The picture, as runs of same-coloured pixels along each row.
    private func drawScene(_ ctx: inout GraphicsContext, step: Int) {
        let w = Int(RH_SCENE_W), h = Int(RH_SCENE_H)
        var px = [UInt32](repeating: 0, count: w * h)
        var v = view()
        rh_scene(Int32(model.song), &v, Int32(step), &px)
        let k = RhymesLayout.scale, top = RhymesLayout.art.minY
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
        ctx.fill(Path(RhymesLayout.title), with: .color(paper))
        let title = RH.title(model.song)
        // As big as fits beside the stars.
        let size = min(19, 150 / (CGFloat(title.count) * 0.56))
        text(&ctx, title, CGPoint(x: 7, y: 16), size: size, color: accent, anchor: .leading, weight: .heavy)
        // The most stars this song has earned at this level.
        for i in 0..<3 {
            let p = starPath(CGPoint(x: 163 + CGFloat(i) * 15, y: 16), 7.5)
            if i < model.stars {
                ctx.fill(p, with: .color(gold))
                ctx.stroke(p, with: .color(Color(hex: 0xD99A00)), lineWidth: 1)
            } else {
                ctx.stroke(p, with: .color(faded.opacity(0.8)), lineWidth: 1.4)
            }
        }
        drawBattery(&ctx)
        ctx.fill(Path(CGRect(x: 0, y: 32, width: 240, height: 3)), with: .color(accent))
    }

    // The FM-1's battery icon where the firmware draws it. As in MF-1, the emulator's battery is a third full.
    private func drawBattery(_ ctx: inout GraphicsContext) {
        let r = CGRect(x: 207, y: 6, width: 24, height: 13)
        ctx.stroke(Path(roundedRect: r, cornerRadius: 3), with: .color(ink), lineWidth: 1.5)
        ctx.fill(Path(CGRect(x: 231.5, y: 10, width: 2, height: 5)), with: .color(ink))
        ctx.fill(Path(CGRect(x: 209.75, y: 8.5, width: 5, height: 8)), with: .color(ink))
    }

    private func drawBankPrompt(_ ctx: inout GraphicsContext) {
        let r = CGRect(x: 16, y: 56, width: 208, height: 120)
        ctx.fill(Path(roundedRect: r, cornerRadius: 6), with: .color(ink.opacity(0.92)))
        text(&ctx, "A bank arrived", CGPoint(x: 120, y: 82), size: 15, color: paper)
        text(&ctx, "Turn KNOB1-4 to store it", CGPoint(x: 120, y: 108), size: 10, color: faded)
        text(&ctx, "in bank A, B, C or D", CGPoint(x: 120, y: 124), size: 10, color: faded)
        text(&ctx, "SONG cancels", CGPoint(x: 120, y: 152), size: 10, color: faded, weight: .semibold)
    }

    // A word over the bottom of the picture.
    private func drawBadge(_ ctx: inout GraphicsContext, _ label: String) {
        let w = CGFloat(label.count) * 7.6 + 20
        let r = CGRect(x: 120 - w / 2, y: 139, width: w, height: 21)
        ctx.fill(Path(roundedRect: r, cornerRadius: 10.5), with: .color(ink.opacity(0.8)))
        text(&ctx, label, CGPoint(x: r.midX, y: r.midY), size: 13, color: .white, weight: .heavy)
    }

    // A finished song: a cheer and the stars it earned, popping in one by one.
    private func drawDone(_ ctx: inout GraphicsContext, step: Int) {
        let g = model.game
        let since = Int(model.now &- g.done_at) / Int(RH_SCENE_MS)
        guard since >= 4 else { return }
        let r = CGRect(x: 40, y: 58, width: 160, height: 78)
        ctx.fill(Path(roundedRect: r.offsetBy(dx: 0, dy: 3), cornerRadius: 14), with: .color(.black.opacity(0.25)))
        ctx.fill(Path(roundedRect: r, cornerRadius: 14), with: .color(paper))
        ctx.stroke(Path(roundedRect: r, cornerRadius: 14), with: .color(accent), lineWidth: 3)
        let stars = Int(g.stars)
        let word = stars >= 3 ? "Wow!" : stars == 2 ? "Super!" : "Well done!"
        text(&ctx, word, CGPoint(x: 120, y: 80), size: 26, color: accent, weight: .black)
        for i in 0..<3 {
            let shown = since >= 7 + i * 3
            let pop: CGFloat = shown && since < 10 + i * 3 ? 3 : 0
            let p = starPath(CGPoint(x: 92 + CGFloat(i) * 28, y: 112), 11 + pop)
            if i < stars && shown {
                ctx.fill(p, with: .color(gold))
                ctx.stroke(p, with: .color(Color(hex: 0xD99A00)), lineWidth: 1.2)
            } else {
                ctx.stroke(p, with: .color(faded.opacity(0.6)), lineWidth: 1.2)
            }
        }
    }

    // The notes coming up, each a coloured bead with its letter and the syllable sung on it underneath, the next
    // one big and bobbing. Four at a time, so each is big enough to read on the real screen.
    private func drawTrain(_ ctx: inout GraphicsContext, step: Int) {
        var g = model.game
        let t = g.tune
        let n = Int(t.count)
        guard n > 0 else { return }
        let cur = Int(rh_game_current(&g, model.now))
        guard cur >= 0 else { return }
        let y: CGFloat = 178
        for i in cur..<min(n, cur + 4) {
            let off = i - cur
            let x: CGFloat = off == 0 ? 34 : 44 + CGFloat(off) * 52
            let note = t.noteAt(i)
            let colour = Color(hex: rh_note_colour(Int32(note)))
            let big = off == 0
            let r: CGFloat = big ? 14 : 11
            // A long note is a wider bead.
            let wide: CGFloat = t.lenAt(i) >= 4 ? r * 0.5 : 0
            let bounce: CGFloat = big && g.state != Int32(RH_LISTEN) && g.state != Int32(RH_ENCORE)
                ? CGFloat([0, -1, -2, -1][step % 4]) : 0
            let bead = CGRect(x: x - r - wide, y: y - r + bounce, width: 2 * (r + wide), height: 2 * r)
            ctx.fill(Path(roundedRect: bead, cornerRadius: r), with: .color(colour))
            ctx.stroke(Path(roundedRect: bead, cornerRadius: r), with: .color(big ? ink : ink.opacity(0.5)),
                       lineWidth: big ? 2.5 : 1.5)
            let name = ["C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"][note % 12]
            text(&ctx, name, CGPoint(x: x, y: y + bounce), size: big ? 16 : 13, color: .white, weight: .black)
            text(&ctx, t.syllable(i), CGPoint(x: x, y: 198), size: big ? 13 : 11.5, color: big ? ink : faded,
                 weight: .heavy)
        }
    }

    // The song's keys, at least eight, the next one in its note's colour while its light is on.
    private func drawKeys(_ ctx: inout GraphicsContext) {
        var g = model.game
        let w = RhymesLayout.window(g.tune)
        var target = -1, colour = Color.clear
        let cur = Int(rh_game_current(&g, model.now))
        if cur >= 0 && cur < Int(g.tune.count) {
            let note = g.tune.noteAt(cur)
            target = Int(rh_key_of(Int32(note)))
            colour = Color(hex: rh_note_colour(Int32(note)))
        }
        let playing = g.state == Int32(RH_LISTEN) || g.state == Int32(RH_ENCORE)
        func fill(_ k: Int, white: Bool) -> Color {
            let bright = (model.bright >> UInt32(k)) & 1 != 0, dim = (model.dim >> UInt32(k)) & 1 != 0
            if model.isHeld(k) { return Color(hex: 0xB8C4CE) }
            if k == target && (bright || playing) { return colour }
            if bright { return gold }
            if dim { return gold.opacity(0.5) }
            return white ? .white : ink
        }
        for i in 0..<16 {
            guard let r = RhymesLayout.whiteKey(i, w)?.insetBy(dx: 0.75, dy: 0) else { continue }
            ctx.fill(Path(roundedRect: r, cornerRadius: 3), with: .color(fill(i, white: true)))
            ctx.stroke(Path(roundedRect: r, cornerRadius: 3), with: .color(ink.opacity(0.7)), lineWidth: 1.5)
        }
        for i in 0..<11 {
            guard let r = RhymesLayout.blackKey(i, w) else { continue }
            ctx.fill(Path(roundedRect: r, cornerRadius: 3), with: .color(fill(16 + i, white: false)))
            ctx.stroke(Path(roundedRect: r, cornerRadius: 3), with: .color(ink), lineWidth: 1.5)
        }
    }

    private func drawPill(_ ctx: inout GraphicsContext, _ label: String) {
        let r = RhymesLayout.pill
        ctx.fill(Path(roundedRect: r, cornerRadius: 14), with: .color(ink))
        text(&ctx, label, CGPoint(x: r.midX, y: r.midY), size: label.count > 24 ? 12 : 14, color: .white, weight: .heavy)
    }
}

// ------------------------------------------------------------------ screen clicks

enum RhymesTarget: Equatable {
    case none, title, art, key(Int), train
}

extension RhymesModel {
    func screenTarget(at p: CGPoint) -> RhymesTarget {
        if RhymesLayout.title.contains(p) { return .title }
        if RhymesLayout.keys.contains(p) {
            let w = RhymesLayout.window(game.tune)
            for i in 0..<11 where RhymesLayout.blackKey(i, w)?.contains(p) == true { return .key(16 + i) }
            for i in 0..<16 where RhymesLayout.whiteKey(i, w)?.contains(p) == true { return .key(i) }
        }
        if RhymesLayout.train.contains(p) { return .train }
        if RhymesLayout.art.contains(p) { return .art }
        return .none
    }

    func songMenu() -> NSMenu {
        let menu = NSMenu()
        for i in 0..<RH.songCount {
            let earned = best[i].max() ?? 0
            let item = ClosureMenuItem(title: RH.title(i) + (earned > 0 ? "  " + String(repeating: "★", count: earned) : "")) {
                [weak self] in self?.selectSong(i)
            }
            item.state = i == song ? .on : .off
            menu.addItem(item)
        }
        return menu
    }
}

// Mouse input on the screen: the little keyboard plays, a click on the picture listens to the song, a click on the
// title opens the list of songs, and the scroll wheel over the title changes song.
final class RhymesScreenInputView: NSView {
    var model: RhymesModel?
    private var held: Int?
    private var scrollAccum: CGFloat = 0
    override var mouseDownCanMoveWindow: Bool { false }
    override var isFlipped: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    private func screenPoint(_ e: NSEvent) -> CGPoint {
        let p = convert(e.locationInWindow, from: nil)
        return CGPoint(x: p.x / bounds.width * 240, y: p.y / bounds.height * 240)
    }

    override func mouseDown(with e: NSEvent) {
        guard let model else { return }
        switch model.screenTarget(at: screenPoint(e)) {
        case .key(let k):
            held = k
            model.keyDown(k)
        case .title: model.songMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        case .art: model.listen()
        case .train: model.pill = nil
        case .none: break
        }
    }

    override func mouseUp(with e: NSEvent) {
        if let k = held { model?.keyUp(k) }
        held = nil
    }

    override func rightMouseDown(with e: NSEvent) {
        guard let model else { return }
        if model.screenTarget(at: screenPoint(e)) == .title {
            model.songMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        }
    }

    override func scrollWheel(with e: NSEvent) {
        guard let model, model.screenTarget(at: screenPoint(e)) == .title else { return }
        let d = e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 8 : e.scrollingDeltaY
        scrollAccum += d
        let detents = Int(scrollAccum)
        if detents != 0 {
            scrollAccum -= CGFloat(detents)
            model.turnPresets(detents)
        }
    }
}

struct RhymesScreenInput: NSViewRepresentable {
    let model: RhymesModel
    func makeNSView(context: Context) -> RhymesScreenInputView {
        let v = RhymesScreenInputView()
        v.model = model
        return v
    }
    func updateNSView(_ v: RhymesScreenInputView, context: Context) { v.model = model }
}
