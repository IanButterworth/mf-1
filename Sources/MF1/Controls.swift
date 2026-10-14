import AppKit
import SwiftUI

// Colours for the three production finishes.
struct Finish {
    var name: String
    var caseTop: Color
    var caseBottom: Color
    // The keys and the bed around them are one piece of silicone.
    var keybed: Color
    var tray: Color
    var button: Color
    var legend: Color
    var unlitLegend: Color
    // Printed panel labels and key labels, where they differ from the
    // button legends (light cases print in a dark ink).
    var label: Color? = nil
    var keyLabel: Color? = nil
    // Unlit LED slits on coloured keys read as dark grooves.
    var slit: Color? = nil

    static let black = Finish(name: "Black", caseTop: Color(hex: 0x323234), caseBottom: Color(hex: 0x252527),
                              keybed: Color(hex: 0x2b2b2d), tray: Color(hex: 0x29292b),
                              button: Color(hex: 0x1c1d1e), legend: Color(hex: 0xdadada), unlitLegend: Color(hex: 0x58606a))
    static let purple = Finish(name: "Purple", caseTop: Color(hex: 0xd2aee0), caseBottom: Color(hex: 0xa983c6),
                               keybed: Color(hex: 0x6a5fab), tray: Color(hex: 0x7a6db0),
                               button: Color(hex: 0x6f64a8), legend: Color(hex: 0xffffff), unlitLegend: Color(hex: 0xb8b0e0))
    static let orange = Finish(name: "Orange", caseTop: Color(hex: 0xe0703a), caseBottom: Color(hex: 0xbd5a2e),
                               keybed: Color(hex: 0x7d453b), tray: Color(hex: 0x9e4c2e),
                               button: Color(hex: 0x8e574a), legend: Color(hex: 0xffffff), unlitLegend: Color(hex: 0xd9a58e))
    static let blackGreen = Finish(name: "Black-Green", caseTop: Color(hex: 0x313134), caseBottom: Color(hex: 0x242427),
                                   keybed: Color(hex: 0x66c6b7), tray: Color(hex: 0x5cc0b0),
                                   button: Color(hex: 0x74cdc0), legend: Color(hex: 0xffffff), unlitLegend: Color(hex: 0x24443f),
                                   label: Color(hex: 0xdadada), keyLabel: Color(hex: 0x1f3a36), slit: Color(hex: 0x1d2f2c))
    static let coolGray = Finish(name: "Cool Gray", caseTop: Color(hex: 0xeae3dd), caseBottom: Color(hex: 0xd8cfc8),
                                 keybed: Color(hex: 0x383b3e), tray: Color(hex: 0x3f3f43),
                                 button: Color(hex: 0x383b3e), legend: Color(hex: 0xffffff), unlitLegend: Color(hex: 0x6a6f73),
                                 label: Color(hex: 0x3a3a3c), keyLabel: Color(hex: 0x9a9fa3), slit: Color(hex: 0x55595c))
    static let whiteBlue = Finish(name: "White-Blue", caseTop: Color(hex: 0xf2ede7), caseBottom: Color(hex: 0xe2dbd3),
                                  keybed: Color(hex: 0x4d6c92), tray: Color(hex: 0x4e6b8e),
                                  button: Color(hex: 0x4f6c90), legend: Color(hex: 0xffffff), unlitLegend: Color(hex: 0xc5d3e2),
                                  label: Color(hex: 0x3b4655), keyLabel: Color(hex: 0xdce5ef), slit: Color(hex: 0x2b3d55))
    static let all = [black, purple, orange, blackGreen, coolGray, whiteBlue]
}

extension Color {
    init(hex: UInt32) {
        self.init(red: Double((hex >> 16) & 0xff) / 255, green: Double((hex >> 8) & 0xff) / 255,
                  blue: Double(hex & 0xff) / 255)
    }
}

// ------------------------------------------------------------------ knob input

// Mouse drag and scroll wheel turn an endless encoder in detents.
final class KnobInputView: NSView {
    var onTurn: (Int) -> Void = { _ in }
    var onAbsolute: ((CGFloat) -> Void)? = nil
    private var lastY: CGFloat = 0
    private var dragAccum: CGFloat = 0
    private var scrollAccum: CGFloat = 0

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func mouseDown(with event: NSEvent) {
        lastY = event.locationInWindow.y
        dragAccum = 0
        NSCursor.hide()
    }

    override func mouseUp(with event: NSEvent) {
        NSCursor.unhide()
    }

    override func mouseDragged(with event: NSEvent) {
        let dy = event.locationInWindow.y - lastY
        lastY = event.locationInWindow.y
        if let abs = onAbsolute {
            abs(dy)
            return
        }
        // Holding Option turns in finer steps.
        let step: CGFloat = event.modifierFlags.contains(.option) ? 14 : 6
        dragAccum += dy
        let detents = Int(dragAccum / step)
        if detents != 0 {
            dragAccum -= CGFloat(detents) * step
            onTurn(detents)
        }
    }

    override func scrollWheel(with event: NSEvent) {
        let d = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / 8 : event.scrollingDeltaY
        if let abs = onAbsolute {
            abs(d * 4)
            return
        }
        scrollAccum += d
        let detents = Int(scrollAccum)
        if detents != 0 {
            scrollAccum -= CGFloat(detents)
            onTurn(detents)
        }
    }
}

struct KnobInput: NSViewRepresentable {
    var onTurn: (Int) -> Void
    var onAbsolute: ((CGFloat) -> Void)? = nil
    func makeNSView(context: Context) -> KnobInputView {
        let v = KnobInputView()
        v.onTurn = onTurn
        v.onAbsolute = onAbsolute
        return v
    }
    func updateNSView(_ v: KnobInputView, context: Context) {
        v.onTurn = onTurn
        v.onAbsolute = onAbsolute
    }
}

// ------------------------------------------------------------------ knob

struct KnobView: View {
    var diameter: CGFloat
    var angle: Double  // radians, for the drawn rotation
    var pointer: Bool  // MASTER has a white dot
    var onTurn: (Int) -> Void
    var onAbsolute: ((CGFloat) -> Void)? = nil

    var body: some View {
        ZStack {
            Circle()
                .fill(Color.black.opacity(0.55))
                .frame(width: diameter * 1.08, height: diameter * 1.08)
                .offset(y: diameter * 0.06)
                .blur(radius: diameter * 0.05)
            Canvas { ctx, size in
                let r = size.width / 2
                let c = CGPoint(x: r, y: r)
                // Knurled skirt.
                ctx.fill(Path(ellipseIn: CGRect(origin: .zero, size: size)),
                         with: .radialGradient(Gradient(colors: [Color(hex: 0x3a3a3a), Color(hex: 0x111111)]),
                                               center: c, startRadius: 0, endRadius: r))
                for i in 0..<36 {
                    let a = Double(i) / 36 * 2 * .pi + angle
                    var p = Path()
                    p.move(to: CGPoint(x: c.x + cos(a) * r * 0.80, y: c.y + sin(a) * r * 0.80))
                    p.addLine(to: CGPoint(x: c.x + cos(a) * r * 0.98, y: c.y + sin(a) * r * 0.98))
                    ctx.stroke(p, with: .color(.white.opacity(0.10)), lineWidth: max(0.5, r * 0.05))
                }
                // Cap.
                let cap = CGRect(x: c.x - r * 0.74, y: c.y - r * 0.74, width: r * 1.48, height: r * 1.48)
                ctx.fill(Path(ellipseIn: cap),
                         with: .linearGradient(Gradient(colors: [Color(hex: 0x3b3b3d), Color(hex: 0x161617)]),
                                               startPoint: CGPoint(x: c.x, y: cap.minY), endPoint: CGPoint(x: c.x, y: cap.maxY)))
                ctx.stroke(Path(ellipseIn: cap), with: .color(.white.opacity(0.08)), lineWidth: 0.6)
                if pointer {
                    let a = angle - .pi / 2
                    let d = CGRect(x: c.x + cos(a) * r * 0.52 - r * 0.12, y: c.y + sin(a) * r * 0.52 - r * 0.12,
                                   width: r * 0.24, height: r * 0.24)
                    ctx.fill(Path(ellipseIn: d), with: .color(.white))
                }
            }
            .frame(width: diameter, height: diameter)
            if !Snapshot.active {
                KnobInput(onTurn: onTurn, onAbsolute: onAbsolute)
                    .frame(width: diameter * 1.3, height: diameter * 1.3)
            }
        }
    }
}

// ------------------------------------------------------------------ buttons

struct PanelButton: View {
    var label: String
    var size: CGSize
    var finish: Finish
    var lit: Color?
    var fontSize: CGFloat
    var onPress: () -> Void
    var onRelease: () -> Void = {}
    var onLongPress: (() -> Void)? = nil
    @State private var down = false
    @State private var longFired = false
    @State private var pressTask: DispatchWorkItem?

    var body: some View {
        let r = size.height * 0.18
        ZStack {
            RoundedRectangle(cornerRadius: r)
                .fill(Color.black.opacity(0.6))
                .offset(y: down ? size.height * 0.02 : size.height * 0.06)
                .blur(radius: size.height * 0.04)
            RoundedRectangle(cornerRadius: r)
                .fill(LinearGradient(colors: [finish.button.opacity(1).lighter(0.06), finish.button],
                                     startPoint: .top, endPoint: .bottom))
                .overlay(RoundedRectangle(cornerRadius: r).stroke(Color.white.opacity(0.06), lineWidth: 0.6))
                .scaleEffect(down ? 0.97 : 1)
            legend
        }
        .frame(width: size.width, height: size.height)
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 0)
            .onChanged { _ in
                guard !down else { return }
                down = true
                longFired = false
                if let lp = onLongPress {
                    let t = DispatchWorkItem {
                        longFired = true
                        lp()
                    }
                    pressTask = t
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.6, execute: t)
                } else {
                    onPress()
                }
            }
            .onEnded { _ in
                down = false
                pressTask?.cancel()
                if onLongPress != nil && !longFired { onPress() }
                onRelease()
            })
    }

    @ViewBuilder private var legend: some View {
        let color = lit ?? finish.unlitLegend
        let glow = lit != nil
        if label == "PLAY/STOP" {
            VStack(spacing: fontSize * 0.12) {
                Text("PLAY").legendStyle(fontSize * 0.9, color, glow)
                Rectangle().fill(color).frame(width: fontSize * 2.6, height: max(0.6, fontSize * 0.08))
                Text("STOP").legendStyle(fontSize * 0.9, color, glow)
            }
        } else {
            Text(label).legendStyle(fontSize, color, glow)
        }
    }
}

extension Text {
    func legendStyle(_ size: CGFloat, _ color: Color, _ glow: Bool) -> some View {
        self.font(.custom("Orbitron-Bold", size: size))
            .foregroundColor(color)
            .shadow(color: glow ? color.opacity(0.9) : .clear, radius: size * 0.35)
    }
}

extension Color {
    func lighter(_ amount: Double) -> Color {
        let ns = NSColor(self).usingColorSpace(.sRGB) ?? .gray
        return Color(red: min(1, ns.redComponent + amount), green: min(1, ns.greenComponent + amount),
                     blue: min(1, ns.blueComponent + amount))
    }
}

// ------------------------------------------------------------------ key

struct KeyCap: View {
    var size: CGSize
    var finish: Finish
    var led: Double

    // A lit slit is the white LED; an unlit one shows the finish's groove.
    private var ledColor: Color {
        let lit = Color(red: 0.86, green: 0.95, blue: 1.0)
        guard let slit = finish.slit else { return lit.opacity(0.10 + 0.9 * led) }
        return led > 0.2 ? lit.opacity(led) : slit
    }
    var pressed: Bool
    var label: String?
    var labelSize: CGFloat

    var body: some View {
        // Silicone keys squash rather than travel: the cap flattens towards
        // the bed, bulges a little sideways and loses its shadow, then
        // springs back with a small rubbery overshoot.
        let squash: CGFloat = pressed ? 0.92 : 1
        let bulge: CGFloat = pressed ? 1.035 : 1
        let base = finish.keybed
        ZStack {
            Capsule()
                .fill(Color.black.opacity(pressed ? 0.3 : 0.55))
                .offset(y: pressed ? size.width * 0.01 : size.width * 0.07)
                .blur(radius: size.width * (pressed ? 0.02 : 0.06))
            ZStack {
                Capsule()
                    .fill(LinearGradient(colors: pressed ? [base.lighter(0.0), base.lighter(-0.035)]
                                                         : [base.lighter(0.075), base.lighter(-0.01)],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(Capsule().stroke(Color.white.opacity(pressed ? 0.025 : 0.07), lineWidth: 0.6))
                // LED light pipe.
                let slitH = size.height * (label == nil ? 0.52 : 0.42)
                Capsule()
                    .fill(ledColor)
                    .frame(width: max(1.2, size.width * 0.11), height: slitH)
                    .shadow(color: Color(red: 0.7, green: 0.92, blue: 1).opacity(led * 0.9), radius: size.width * 0.22 * led)
                    .offset(y: label == nil ? 0 : -size.height * 0.12)
                if let label {
                    Text(label)
                        .font(.system(size: labelSize, weight: .regular))
                        .foregroundColor((finish.keyLabel ?? finish.legend).opacity(0.85))
                        .offset(y: size.height * 0.28)
                }
            }
            .scaleEffect(x: bulge, y: squash, anchor: .bottom)
        }
        .frame(width: size.width, height: size.height)
        .animation(pressed ? .easeOut(duration: 0.03) : .spring(response: 0.17, dampingFraction: 0.45), value: pressed)
    }
}

// ------------------------------------------------------------------ screen input

// Mouse input on the screen. Clicks act on what is under the pointer; a
// vertical drag or the scroll wheel turns it like a knob. A plain click on
// the voice name opens the preset menu, a drag on it scrolls presets.
final class ScreenInputView: NSView {
    var model: DeviceModel?
    private var target: ScreenTarget = .none
    private var lastY: CGFloat = 0
    private var accum: CGFloat = 0
    private var scrollAccum: CGFloat = 0
    private var dragged = false

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
            model.presetMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        } else {
            model.screenClick(target, back: e.modifierFlags.contains(.shift), double: e.clickCount >= 2)
        }
    }

    override func rightMouseDown(with e: NSEvent) {
        guard let model else { return }
        let t = model.screenTarget(at: screenPoint(e))
        if t == .title {
            model.presetMenu().popUp(positioning: nil, at: convert(e.locationInWindow, from: nil), in: self)
        } else {
            model.screenClick(t, back: true, double: false)
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

struct ScreenInput: NSViewRepresentable {
    let model: DeviceModel
    func makeNSView(context: Context) -> ScreenInputView {
        let v = ScreenInputView()
        v.model = model
        return v
    }
    func updateNSView(_ v: ScreenInputView, context: Context) { v.model = model }
}
