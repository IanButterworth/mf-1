import MF1Core
import SwiftUI

// The front panel, laid out in millimetres from a top-down photo of the
// unit (161.5 x 96.5 mm) and scaled to the window.
struct PanelView<M: PanelDriver>: View {
    @ObservedObject var model: M
    var finish: Finish
    @State private var knobAngles = [Double](repeating: 0, count: 7)
    @AppStorage("frameless") private var frameless = true
    @State private var keysDown: Set<Int> = []
    @State private var dragKey: Int? = nil

    // Generic types cannot hold stored statics, so the measurements live in PanelMetrics.
    static var width: CGFloat { PanelMetrics.width }
    static var height: CGFloat { PanelMetrics.height }
    static var lowerX: [CGFloat] { PanelMetrics.lowerX }
    static var upperX: [CGFloat] { PanelMetrics.upperX }
    static var lowerY: CGFloat { PanelMetrics.lowerY }
    static var upperY: CGFloat { PanelMetrics.upperY }
    static var keySize: CGSize { PanelMetrics.keySize }
    static var upperKeySize: CGSize { PanelMetrics.upperKeySize }

    var body: some View {
        GeometryReader { geo in
            let s = geo.size.width / Self.width
            ZStack(alignment: .topLeading) {
                caseShape(s)
                labels(s)
                knobs(s)
                screen(s)
                octButtons(s)
                buttonGrid(s)
                keybed(s)
            }
            .coordinateSpace(name: "panel")
        }
        .aspectRatio(Self.width / Self.height, contentMode: .fit)
    }

    private func at(_ x: CGFloat, _ y: CGFloat, _ s: CGFloat) -> CGPoint { CGPoint(x: x * s, y: y * s) }

    // ---------------------------------------------------------------- case

    private func caseShape(_ s: CGFloat) -> some View {
        CaseView(scale: s, finish: finish, frameless: frameless).equatable()
    }

    private func label(_ text: String, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat) -> some View {
        Text(text)
            .font(.custom("Share Tech Mono", size: 2.35 * s))
            .foregroundColor(finish.label ?? finish.legend)
            .fixedSize()
            .position(at(x, y, s))
    }

    private func labels(_ s: CGFloat) -> some View {
        ZStack(alignment: .topLeading) {
            let names = model.labels
            label(names.master, 12.9, 7.3, s)
            label(names.select, 29.4, 7.3, s)
            label(names.presets, 12.9, 24.2, s)
            label(names.algorithm, 30.2, 24.2, s)
            ForEach(0..<4) { i in
                label(names.knobs[i], [91.0, 109.2, 127.3, 145.8][i], 7.3, s)
            }
        }
    }

    // ---------------------------------------------------------------- knobs

    private func knobs(_ s: CGFloat) -> some View {
        let d = 7.6 * s
        let positions: [(CGFloat, CGFloat)] = [(12.9, 14.4), (29.4, 14.4), (12.9, 31.3), (30.2, 31.3),
                                               (91.0, 14.4), (109.2, 14.4), (127.3, 14.4), (145.8, 14.4)]
        let names = ["MASTER", "SELECT", "PRESETS", "ALGORITHM", "KNOB1", "KNOB2", "KNOB3", "KNOB4"]
        return ZStack(alignment: .topLeading) {
            // MASTER: a 300 degree pot.
            let vol = Double(mf1_global_get(Int32(MF1_G_VOLUME)))
            KnobView(name: names[0], value: "\(Int(vol))", diameter: d, finish: finish,
                     angle: (vol / 100 - 0.5) * (300.0 / 180.0) * .pi, pointer: true,
                     onTurn: { model.turnMaster(by: Double($0)) },
                     onAbsolute: { d in
                         let before = Int(mf1_global_get(Int32(MF1_G_VOLUME))) / 10
                         model.turnMaster(by: Double(d) * 0.5)
                         // The pot turns smoothly, so it clicks only every 10 steps.
                         if Int(mf1_global_get(Int32(MF1_G_VOLUME))) / 10 != before { KnobInputView.tick() }
                     })
                .position(at(positions[0].0, positions[0].1, s))
            ForEach(1..<8) { i in
                KnobView(name: names[i], diameter: d, finish: finish, angle: knobAngles[i - 1], pointer: false, onTurn: { delta in
                    knobAngles[i - 1] += Double(delta) * 0.26
                    switch i {
                    case 1: model.turnSelect(delta)
                    case 2: model.turnPresets(delta)
                    case 3: model.turnAlgorithm(delta)
                    default: model.turnKnob(i - 4, delta)
                    }
                })
                .position(at(positions[i].0, positions[i].1, s))
            }
        }
    }

    // ---------------------------------------------------------------- screen

    private func screen(_ s: CGFloat) -> some View {
        ZStack {
            RoundedRectangle(cornerRadius: 4.2 * s)
                .fill(Color(hex: 0x0b0d0d))
                .frame(width: 38.1 * s, height: 38.1 * s)
                .shadow(color: .black.opacity(0.5), radius: 0.6 * s, y: 0.4 * s)
            Ticking(clock: LedClock.screen) { model.screenView(frame: LedClock.screen.frame) }
                .frame(width: 28.4 * s, height: 28.4 * s)
                .clipShape(RoundedRectangle(cornerRadius: 0.6 * s))
            if !Snapshot.active {
                model.screenInput().frame(width: 28.4 * s, height: 28.4 * s)
            }
            // Glass sheen.
            RoundedRectangle(cornerRadius: 4.2 * s)
                .fill(LinearGradient(colors: [.white.opacity(0.07), .clear, .clear], startPoint: .topLeading, endPoint: .bottomTrailing))
                .frame(width: 38.1 * s, height: 38.1 * s)
                .allowsHitTesting(false)
        }
        .position(at(60.8, 26.2, s))
    }

    // ---------------------------------------------------------------- buttons

    private func octButtons(_ s: CGFloat) -> some View {
        ZStack {
            RoundedRectangle(cornerRadius: 1.8 * s)
                .fill(finish.tray)
                .overlay(RoundedRectangle(cornerRadius: 1.8 * s).stroke(Color.black.opacity(0.5), lineWidth: 0.4 * s))
                .frame(width: 26 * s, height: 7.9 * s)
                .position(at(21.95, 44.6, s))
            Ticking {
                PanelButton(label: "OCT-", size: CGSize(width: 10.6 * s, height: 5.8 * s), finish: finish,
                            lit: model.buttonLit("OCT-"), fontSize: 1.9 * s,
                            onPress: { model.octDown(-1) }, onRelease: { model.octUp(-1) })
                    .position(at(15.8, 44.6, s))
                PanelButton(label: "OCT+", size: CGSize(width: 10.6 * s, height: 5.8 * s), finish: finish,
                            lit: model.buttonLit("OCT+"), fontSize: 1.9 * s,
                            onPress: { model.octDown(1) }, onRelease: { model.octUp(1) })
                    .position(at(28.1, 44.6, s))
            }
        }
    }

    private func buttonGrid(_ s: CGFloat) -> some View {
        let names = [["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"], ["HOME", "SAVE", "ARP", "SEQ", "PLAY/STOP", "REC"]]
        let xs: [CGFloat] = [93.3, 103.5, 113.5, 123.6, 133.7, 143.8]
        let ys: [CGFloat] = [32.7, 42.9]
        return ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 2.4 * s)
                .fill(finish.tray)
                .overlay(RoundedRectangle(cornerRadius: 2.4 * s).stroke(Color.black.opacity(0.5), lineWidth: 0.4 * s))
                .frame(width: 65.2 * s, height: 22.4 * s)
                .position(at(118.55, 37.8, s))
            Circle().fill(Color.black.opacity(0.7)).frame(width: 0.7 * s).position(at(118.6, 37.8, s))
            Ticking {
                ForEach(0..<2) { r in
                    ForEach(0..<6) { c in
                        let name = names[r][c]
                        let key = name == "PLAY/STOP" ? "PLAY" : name
                        PanelButton(label: model.buttonLegend(name), size: CGSize(width: 8.5 * s, height: 8.5 * s), finish: finish,
                                    lit: model.buttonLit(key), fontSize: 1.95 * s,
                                    onPress: { model.press(key) },
                                    onLongPress: key == "SAVE" ? { model.save() } : nil)
                            .position(at(xs[c], ys[r], s))
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- keybed

    private func keyRect(_ k: Int) -> CGRect {
        if k < 16 {
            let x = Self.lowerX[k]
            return CGRect(x: x - Self.keySize.width / 2, y: Self.lowerY - Self.keySize.height / 2,
                          width: Self.keySize.width, height: Self.keySize.height)
        }
        let x = Self.upperX[k - 16]
        return CGRect(x: x - Self.upperKeySize.width / 2, y: Self.upperY - Self.upperKeySize.height / 2,
                      width: Self.upperKeySize.width, height: Self.upperKeySize.height)
    }

    private func keyAt(_ p: CGPoint, _ s: CGFloat) -> Int? {
        let mm = CGPoint(x: p.x / s, y: p.y / s)
        for k in 0..<27 where keyRect(k).insetBy(dx: -0.5, dy: -0.5).contains(mm) { return k }
        return nil
    }

    private func keybed(_ s: CGFloat) -> some View {
        ZStack(alignment: .topLeading) {
            // The bed sits below the keys, so it is the same silicone in shade.
            RoundedRectangle(cornerRadius: 6 * s)
                .fill(finish.keybed.lighter(-0.035))
                .overlay(RoundedRectangle(cornerRadius: 6 * s)
                    .stroke(LinearGradient(colors: [.black.opacity(0.6), .white.opacity(0.08)], startPoint: .top, endPoint: .bottom),
                            lineWidth: 0.5 * s))
                .frame(width: 151.1 * s, height: 34.8 * s)
                .position(at(80.75, 72.6, s))
            ForEach([44.4, 71.5, 108.1, 135.4] as [CGFloat], id: \.self) { x in
                Circle().fill(Color.black.opacity(0.75)).frame(width: 0.7 * s).position(at(x, 72.4, s))
            }
            Ticking {
                ForEach(0..<27) { k in
                    let r = keyRect(k)
                    let label = k >= 16 ? model.upperKeyLabel(k - 16) : nil
                    KeyCap(size: CGSize(width: r.width * s, height: r.height * s), finish: finish,
                           led: model.keyLed(k), pressed: keysDown.contains(k) || model.isHeld(k),
                           label: label, labelSize: 1.75 * s)
                        .accessibilityElement(children: .ignore)
                        .accessibilityLabel("key \(noteName(model.shownNote(key: k)))\(label.map { " " + $0 } ?? "")")
                        .accessibilityAddTraits(.isButton)
                        .accessibilityAction {
                            model.keyDown(k)
                            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { model.keyUp(k) }
                        }
                        .position(at(r.midX, r.midY, s))
                }
            }
            // One gesture for the whole keybed, so a drag slides across keys.
            Color.clear
                .contentShape(Rectangle())
                .frame(width: 151.1 * s, height: 34.8 * s)
                .position(at(80.75, 72.6, s))
                .accessibilityHidden(true)
                .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .named("panel"))
                    .onChanged { v in
                        let k = keyAt(v.location, s)
                        if k != dragKey {
                            if let old = dragKey {
                                model.keyUp(old)
                                keysDown.remove(old)
                            }
                            if let k {
                                model.keyDown(k)
                                keysDown.insert(k)
                            }
                            dragKey = k
                        }
                    }
                    .onEnded { _ in
                        if let old = dragKey {
                            model.keyUp(old)
                            keysDown.remove(old)
                        }
                        dragKey = nil
                    })
        }
    }
}

// The panel's measurements in millimetres, from the photo.
enum PanelMetrics {
    static let width: CGFloat = 161.5
    static let height: CGFloat = 96.5

    // Key centres (mm). Lower row: 16 keys; upper row: 11 keys.
    static let lowerX: [CGFloat] = (0..<16).map { 12.5 + 9.14 * CGFloat($0) }
    static let upperX: [CGFloat] = [16.9, 26.0, 35.0, 53.3, 62.5, 80.8, 89.8, 99.0, 117.3, 126.5, 144.6]
    static let lowerY: CGFloat = 80.4
    static let upperY: CGFloat = 65.4
    static let keySize = CGSize(width: 7.9, height: 14.6)
    static let upperKeySize = CGSize(width: 7.6, height: 12.6)
}

// Yamaha octave numbering, where middle C (MIDI 60) is C3.
func noteName(_ n: Int) -> String { DeviceModel.noteName(n) }

// The static case artwork. It is equatable so SwiftUI skips it on the
// 60 Hz LED redraws, which keeps the main thread free for input.
struct CaseView: View, Equatable {
    var scale: CGFloat
    var finish: Finish
    var frameless: Bool

    static func == (a: CaseView, b: CaseView) -> Bool {
        a.scale == b.scale && a.finish.name == b.finish.name && a.frameless == b.frameless
    }

    var body: some View {
        let s = scale
        ZStack {
            // Frameless windows get the system shadow around the case shape;
            // a painted one would be cut off by the window edge as a dark line.
            if !frameless {
                RoundedRectangle(cornerRadius: 9 * s)
                    .fill(Color.black.opacity(0.5))
                    .offset(y: 1.2 * s)
                    .blur(radius: 1.5 * s)
            }
            RoundedRectangle(cornerRadius: 9 * s)
                .fill(LinearGradient(colors: [finish.caseTop, finish.caseBottom], startPoint: .top, endPoint: .bottom))
            if let spots = finish.spots {
                SpotsView(color: spots).clipShape(RoundedRectangle(cornerRadius: 9 * s)).allowsHitTesting(false)
            }
            if !Snapshot.active {
                WindowDragArea().clipShape(RoundedRectangle(cornerRadius: 9 * s))
            }
            RoundedRectangle(cornerRadius: 9 * s)
                .stroke(LinearGradient(colors: [.white.opacity(0.18), .black.opacity(0.2)], startPoint: .top, endPoint: .bottom),
                        lineWidth: 0.5 * s)
            // Faint grain, so large flat areas do not look like plastic film.
            // A fixed seed keeps it from shimmering when it is redrawn.
            Canvas { ctx, size in
                var seed: UInt64 = 0x9E3779B97F4A7C15
                func next() -> CGFloat {
                    seed = seed &* 6364136223846793005 &+ 1442695040888963407
                    return CGFloat(seed >> 40) / CGFloat(1 << 24)
                }
                for _ in 0..<1400 {
                    let x = next() * size.width, y = next() * size.height
                    ctx.fill(Path(CGRect(x: x, y: y, width: 0.8, height: 0.8)), with: .color(.white.opacity(0.025)))
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 9 * s))
            .allowsHitTesting(false)
        }
        .frame(width: PanelMetrics.width * s, height: PanelMetrics.height * s)
    }
}

// Irregular round spots scattered over the case, from a fixed seed so they never move.
struct SpotsView: View {
    var color: Color

    var body: some View {
        Canvas { ctx, size in
            var seed: UInt64 = 0xD1B54A32D192ED03
            func next() -> CGFloat {
                seed = seed &* 6364136223846793005 &+ 1442695040888963407
                return CGFloat(seed >> 40) / CGFloat(1 << 24)
            }
            let unit = size.width / 180
            for _ in 0..<48 {
                let c = CGPoint(x: next() * size.width, y: next() * size.height)
                let r = unit * (0.6 + next() * next() * 2.0)
                // Each spot is a few overlapping blobs, so the edges are uneven like a real coat.
                for k in 0..<3 {
                    let a = next() * 2 * .pi, d = r * 0.35 * CGFloat(k)
                    let rr = r * (1 - 0.18 * CGFloat(k)) * (0.85 + next() * 0.3)
                    let p = CGPoint(x: c.x + cos(a) * d, y: c.y + sin(a) * d)
                    ctx.fill(Path(ellipseIn: CGRect(x: p.x - rr, y: p.y - rr * 0.9, width: rr * 2, height: rr * 1.8)),
                             with: .color(color))
                }
            }
        }
    }
}

// Drives the LEDs and the screen. Only views wrapped in Ticking redraw on
// each frame, so the rest of the panel stays idle and input is not held up.
final class LedClock: ObservableObject {
    static let shared = LedClock()
    // The screen has its own clock, as it changes at a different rate.
    static let screen = LedClock()
    @Published var frame = 0
}

struct Ticking<Content: View>: View {
    @ObservedObject var clock: LedClock = LedClock.shared
    @ViewBuilder var content: () -> Content

    var body: some View {
        let _ = clock.frame
        ZStack(alignment: .topLeading) { content() }
    }
}
