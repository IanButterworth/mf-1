import AppKit
import IScreamCore
import MF1Core
import SwiftUI
import UniformTypeIdentifiers

// iScream: the same engine and panel as MF-1, run as a small synth made of ice cream. It has no
// banks to dive into: turn FLAVOR for a sound, SELECT for the cone or a topping, KNOB1-4 to shape it.

struct IScreamApp: App {
    @NSApplicationDelegateAdaptor(IScreamDelegate.self) var delegate
    private let model = IScreamModel.shared
    @AppStorage("frameless") private var frameless = true

    init() {
        Engine.registerFonts()
    }

    // As in MF-1, the panel lives in a window the delegate makes; this scene only carries menus.
    var body: some Scene {
        MenuBarExtra("iScream", isInserted: .constant(false)) { EmptyView() }
        .commands {
            CommandGroup(replacing: .appSettings) {}
            CommandGroup(replacing: .newItem) {
                Button("Export Flavor Bank…") { IScreamApp.exportFlavors() }.keyboardShortcut("e")
                Button("Export Current Voice…") { Engine.exportVoice() }.keyboardShortcut("e", modifiers: [.command, .shift])
                Divider()
                Button("Import SysEx…") { Engine.openImportPanel() }.keyboardShortcut("o")
            }
            CommandGroup(after: .toolbar) {
                Toggle("Frameless Window", isOn: applying($frameless) { delegate.setFrameless($0) })
                    .keyboardShortcut("f", modifiers: [.command, .control])
                Divider()
                Toggle("iScream Mode", isOn: Binding(get: { true }, set: { Engine.switchMode(iScream: $0) }))
            }
            CommandMenu("Flavors") { FlavorMenu(model: model) }
            CommandMenu("Device") {
                OutputMenu()
                Divider()
                Button("All Notes Off") { mf1_all_notes_off() }.keyboardShortcut(".", modifiers: .command)
                Button("Fresh Scoop (reset this flavor)") { model.freshScoop() }
                Button("Reset All Flavors…") {
                    let a = NSAlert()
                    a.messageText = "Reset every flavor?"
                    a.informativeText = "Each flavor goes back to how it was designed, and your changes are lost."
                    a.addButton(withTitle: "Reset")
                    a.addButton(withTitle: "Cancel")
                    if a.runModal() == .alertFirstButtonReturn { model.resetAll() }
                }
            }
            CommandGroup(replacing: .help) {
                Button("iScream Controls") { IScreamApp.showHelp() }
            }
        }
    }

    // The flavors as an ordinary DX7 32-voice bank, so they can be loaded onto a real FM-1.
    static func exportFlavors() {
        var buf = [UInt8](repeating: 0, count: 4104)
        iscream_export_bank(&buf)
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "syx") ?? .data]
        panel.nameFieldStringValue = "iScream Flavors.syx"
        panel.message = "A DX7 bank of the flavors. The cone and toppings are effects, set on the FM-1 itself."
        if panel.runModal() == .OK, let url = panel.url {
            do { try Data(buf).write(to: url) } catch { Engine.alert(error.localizedDescription) }
        }
    }

    static func showHelp() {
        let a = NSAlert()
        a.messageText = "iScream Controls"
        a.informativeText = """
        FLAVOR picks the sound. SCOOPS makes it wider, one to three scoops.
        TOPPING (the SELECT knob) moves between the scoop, the cone and the five toppings; \
        KNOB1–4 change what the screen shows. Turning a knob on something that is off switches it on.
        The buttons jump straight there: SCOOP, CONE, CREAM, FUDGE, CARML, CHIPS, SPRNK. \
        Press the one you are on to switch it on or off. < and > step through the flavors.
        Hold RESET to put the flavor back as it was designed.

        Cone = filter (waffle low pass, sugar band pass, cup high pass), Whipped Cream = chorus, \
        Hot Fudge = distortion, Caramel = phaser and tape wobble, Choc Chips = delay, Sprinkles = reverb.

        Keys: click or drag across the keybed, or play the Mac keyboard: \
        A W S E D R F G Y H U J K O L P ; [ ' from F3. Z / X are OCT- / OCT+.
        MIDI: the port "\(Engine.portName)" takes notes and program changes (flavors 1–12).
        """
        a.runModal()
    }
}

struct FlavorMenu: View {
    @ObservedObject var model: IScreamModel

    var body: some View {
        ForEach(0..<IScreamModel.flavors.count, id: \.self) { i in
            Toggle(IScreamModel.flavors[i].title, isOn: Binding(
                get: { model.flavor == i },
                set: { _ in model.selectFlavor(i) }))
        }
    }
}

struct IScreamContentView: View {
    @ObservedObject var model: IScreamModel
    @AppStorage("frameless") private var frameless = true

    var body: some View {
        ZStack {
            if !frameless { Color(hex: 0x6b1546).ignoresSafeArea() }
            PanelView(model: model, finish: .barbie)
                .padding(frameless ? EdgeInsets(top: 1, leading: 1, bottom: 1, trailing: 1)
                                   : EdgeInsets(top: 30, leading: 22, bottom: 22, trailing: 22))
        }
        .ignoresSafeArea()
    }
}

final class IScreamDelegate: NSObject, NSApplicationDelegate {
    private var window: PanelWindow?
    private var timer: Timer?
    private var monitor: Any?
    private var computerKeys: [UInt16: Int] = [:]
    private var octKeys: [UInt16: Int] = [:]
    private var seenDown: Set<UInt16> = []

    func applicationDidFinishLaunching(_ notification: Notification) {
        Engine.startIScream()
        let model = IScreamModel.shared
        let w = PanelWindow(contentRect: NSRect(x: 0, y: 0, width: 1180, height: 1180 * PanelMetrics.height / PanelMetrics.width),
                            styleMask: [.borderless], backing: .buffered, defer: false)
        w.contentView = PanelHostingView(rootView: IScreamContentView(model: model))
        w.title = "iScream"
        w.isMovableByWindowBackground = false
        w.contentAspectRatio = NSSize(width: PanelMetrics.width, height: PanelMetrics.height)
        w.isReleasedWhenClosed = false
        window = w
        setFrameless(UserDefaults.standard.object(forKey: "frameless") as? Bool ?? true)
        if !w.setFrameUsingName("IScreamPanel") { w.center() }
        w.setFrameAutosaveName("IScreamPanel")
        w.makeKeyAndOrderFront(nil)
        monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp]) { [weak self] e in
            self?.handle(e) == true ? nil : e
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { [weak self] _ in
            self?.releaseLiftedKeys()
            IScreamModel.shared.poll()
        }
        RunLoop.main.add(timer!, forMode: .common)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
            IScreamSnapshot.runIfRequested(model: model)
            if CommandLine.arguments.contains("--selfcheck") { IScreamSelfCheck.run(model) }
        }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        IScreamModel.shared.writeNow()
        return .terminateNow
    }

    func applicationWillTerminate(_ notification: Notification) { mf1_stop() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        window?.makeKeyAndOrderFront(nil)
        return false
    }

    func applicationDidResignActive(_ notification: Notification) {
        computerKeys.removeAll()
        octKeys.removeAll()
        seenDown.removeAll()
        IScreamModel.shared.releaseAllKeys()
        IScreamModel.shared.releaseOct()
        KnobInputView.showCursor()
    }

    func setFrameless(_ frameless: Bool) {
        guard let w = window else { return }
        let frame = w.frame
        w.styleMask = frameless ? [.borderless, .miniaturizable, .resizable]
                                : [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView]
        if !frameless {
            w.titlebarAppearsTransparent = true
            w.titleVisibility = .hidden
        }
        w.isOpaque = !frameless
        w.backgroundColor = frameless ? .clear : NSColor(red: 0.42, green: 0.08, blue: 0.27, alpha: 1)
        w.hasShadow = true
        w.setFrame(frame, display: true)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { w.invalidateShadow() }
    }

    // ---------------------------------------------------------------- Mac keyboard

    @discardableResult
    private func release(_ code: UInt16) -> Bool {
        seenDown.remove(code)
        if let k = computerKeys.removeValue(forKey: code) {
            IScreamModel.shared.keyUp(k)
            return true
        }
        if let dir = octKeys.removeValue(forKey: code) {
            IScreamModel.shared.octUp(dir)
            return true
        }
        return false
    }

    private func releaseLiftedKeys() {
        for code in Array(computerKeys.keys) + Array(octKeys.keys) {
            if CGEventSource.keyState(.combinedSessionState, key: code) {
                seenDown.insert(code)
            } else if seenDown.contains(code) {
                release(code)
            }
        }
    }

    private func handle(_ e: NSEvent) -> Bool {
        let model = IScreamModel.shared
        let down = e.type == .keyDown
        if !down && release(e.keyCode) { return true }
        if e.modifierFlags.contains(.command) { return false }
        if let dir = AppDelegate.octaveKeys[e.keyCode] {
            if down && !e.isARepeat && octKeys[e.keyCode] == nil {
                octKeys[e.keyCode] = dir
                model.octDown(dir)
            }
            return true
        }
        guard let semi = AppDelegate.noteKeys[e.keyCode] else { return false }
        if e.isARepeat { return true }
        let k: Int
        if let i = DeviceModel.lowerOffsets.firstIndex(of: semi) {
            k = i
        } else if let i = DeviceModel.upperOffsets.firstIndex(of: semi) {
            k = 16 + i
        } else {
            return true
        }
        if down {
            computerKeys[e.keyCode] = k
            model.keyDown(k)
        }
        return true
    }
}

// Development aid, as for MF-1: `MF-1 --iscream --snapshot out.png [flavor N] [focus N] [scoops N]` renders
// the panel offscreen and quits. It never writes the user's sundaes.
enum IScreamSnapshot {
    @MainActor static func runIfRequested(model: IScreamModel) {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count else { return }
        func option(_ name: String) -> Int? {
            guard let j = args.firstIndex(of: name), j + 1 < args.count else { return nil }
            return Int(args[j + 1])
        }
        if let f = option("flavor") { model.selectFlavor(f - 1) }
        if let f = option("focus"), let focus = Focus(rawValue: f) { model.focus = focus }
        if let n = option("scoops") { model.set(IS.scoops, n) }
        // "screen" renders only the 240x240 screen, four times as large, to look at the drawing.
        let screenOnly = args.contains("screen")
        let time = Double(option("time") ?? 0) / 10
        let view: AnyView
        if args.contains("sheet") {
            // Every flavor's screen as it is designed, four across.
            let cells = (0..<IScreamModel.flavors.count).map { i -> IScreamModel in
                let m = IScreamModel()
                m.selectFlavor(i)
                if let f = option("focus"), let focus = Focus(rawValue: f) { m.focus = focus }
                return m
            }
            view = AnyView(VStack(spacing: 0) {
                ForEach(0..<3) { row in
                    HStack(spacing: 0) {
                        ForEach(0..<4) { col in
                            IScreamScreen(model: cells[row * 4 + col], frame: Int(time * 60)).frame(width: 480, height: 480)
                        }
                    }
                }
            })
        } else if screenOnly {
            view = AnyView(IScreamScreen(model: model, frame: Int(time * 60)).frame(width: 960, height: 960))
        } else {
            view = AnyView(PanelView(model: model, finish: .barbie).frame(width: 1615, height: 965).background(Color(hex: 0x6b1546)))
        }
        let r = ImageRenderer(content: view)
        r.scale = 1
        if let img = r.nsImage, let tiff = img.tiffRepresentation, let rep = NSBitmapImageRep(data: tiff),
           let png = rep.representation(using: .png, properties: [:]) {
            try? png.write(to: URL(fileURLWithPath: args[i + 1]))
        }
        exit(0)
    }
}

// `MF-1 --iscream --selfcheck` drives the model as the panel would and checks the engine follows. It saves the
// sundaes as it ends, with the current flavor reset to its recipe.
enum IScreamSelfCheck {
    static func run(_ m: IScreamModel) {
        var failed = 0
        func check(_ ok: Bool, _ what: String) {
            print(ok ? "ok   " : "FAIL ", what)
            if !ok { failed += 1 }
        }
        m.freshScoop()
        let f0 = m.flavor
        m.turnPresets(1)
        check(m.flavor == f0 + 1 && Int(mf1_current_preset()) == f0 + 1, "FLAVOR moves to the next flavor and loads its voice")
        m.turnPresets(-1)
        m.focus = .scoop
        m.turnAlgorithm(1)
        check(m.value(IS.scoops) == 2, "SCOOPS adds a scoop")
        m.turnKnob(0, 5)
        check(m.value(IS.sweet) > 50, "KNOB1 on the scoop turns Sweet up")
        m.turnSelect(1)
        check(m.focus == .cone, "TOPPING moves from the scoop to the cone")
        m.turnKnob(3, -1)
        check(m.value(IS.coneOn) == 0 && mf1_fx_get(0) == 0, "KNOB4 down switches the cone off in the engine")
        m.turnKnob(1, 3)
        check(m.value(IS.coneOn) == 1 && mf1_fx_get(0) == 1 && Int(mf1_fx_get(2)) == m.value(IS.toast), "turning a knob on a cone that is off switches it on")
        m.press("EDIT")
        check(m.focus == .chips && m.value(IS.top(IS.chips, 0)) == 0, "the CHIPS button focuses the chips without switching them")
        m.press("EDIT")
        check(m.value(IS.top(IS.chips, 0)) == 1 && mf1_fx_get(8) != 0, "pressing it again switches the delay on")
        m.turnKnob(0, 10)
        check(Int(mf1_fx_get(11)) == m.value(IS.top(IS.chips, 1)) && m.value(IS.top(IS.chips, 1)) > 40, "KNOB1 on the chips moves the delay mix in the engine")
        m.press("GLO")
        m.turnKnob(2, 1)
        check(Int(mf1_fx_get(5)) == m.value(IS.top(IS.sprinkles, 3)) && Int(mf1_fx_get(5)) <= 2, "the sprinkles' room knob stays on room, hall or plate")
        m.freshScoop()
        check(m.value(IS.scoops) == 1 && m.value(IS.sweet) == 50, "a fresh scoop puts the recipe back")
        m.writeNow()
        print(failed == 0 ? "all passed" : "\(failed) failed")
        exit(failed == 0 ? 0 : 1)
    }
}
