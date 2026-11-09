import AlmanacCore
import AppKit
import MF1Core
import SwiftUI
import UniformTypeIdentifiers

// Almanac: the same engine and panel as MF-1, run as twelve months. Turn MONTH for a sound, SKY for the
// weather, PAGE for what KNOB1-4 shape: the mood, the weather, the land, or how the keys play.

struct AlmanacApp: App {
    @NSApplicationDelegateAdaptor(AlmanacDelegate.self) var delegate
    private let model = AlmanacModel.shared
    @AppStorage("frameless") private var frameless = true

    init() {
        Engine.registerFonts()
    }

    // As in MF-1, the panel lives in a window the delegate makes; this scene only carries menus.
    var body: some Scene {
        MenuBarExtra("Almanac", isInserted: .constant(false)) { EmptyView() }
        .commands {
            CommandGroup(replacing: .appSettings) {}
            CommandGroup(replacing: .newItem) {
                Button("Export Month Bank…") { AlmanacApp.exportMonths() }.keyboardShortcut("e")
                Button("Export Current Voice…") { Engine.exportVoice() }.keyboardShortcut("e", modifiers: [.command, .shift])
                Divider()
                Button("Import SysEx…") { Engine.openImportPanel() }.keyboardShortcut("o")
            }
            CommandGroup(after: .toolbar) {
                Toggle("Frameless Window", isOn: applying($frameless) { delegate.setFrameless($0) })
                    .keyboardShortcut("f", modifiers: [.command, .control])
                Divider()
                ModeToggles()
            }
            CommandMenu("Months") { MonthMenu(model: model) }
            CommandMenu("Device") {
                OutputMenu()
                Divider()
                Button("All Notes Off") { mf1_all_notes_off() }.keyboardShortcut(".", modifiers: .command)
                Button("This Month as Composed") { model.resetMonth() }
                Button("Every Month as Composed…") {
                    let a = NSAlert()
                    a.messageText = "Put every month back as composed?"
                    a.informativeText = "Each month goes back to its own sky, place and mood, and your changes are lost."
                    a.addButton(withTitle: "Reset")
                    a.addButton(withTitle: "Cancel")
                    if a.runModal() == .alertFirstButtonReturn { model.resetAll() }
                }
            }
            UpdateCommands()
            CommandGroup(replacing: .help) {
                Button("Almanac Controls") { AlmanacApp.showHelp() }
            }
        }
    }

    // The months as an ordinary DX7 32-voice bank, so they can be loaded onto a real FM-1.
    static func exportMonths() {
        var buf = [UInt8](repeating: 0, count: 4104)
        almanac_export_bank(&buf)
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "syx") ?? .data]
        panel.nameFieldStringValue = "Almanac Months.syx"
        panel.message = "A DX7 bank of the months under their own skies. The effects are set on the FM-1 itself."
        if panel.runModal() == .OK, let url = panel.url {
            do { try Data(buf).write(to: url) } catch { Engine.alert(error.localizedDescription) }
        }
    }

    static func showHelp() {
        let a = NSAlert()
        a.messageText = "Almanac Controls"
        a.informativeText = """
        MONTH picks the month: its voice, sky, place and mood. SKY changes the weather: clear, breeze, rain, \
        fog, storm or snow. PAGE moves KNOB1–4 between four pages; MOOD, WTHR, LAND and PLAY jump straight there.
        Mood: how the month feels (wistful to elated), its warmth, how long notes linger, and shimmer (vibrato).
        Weather: the sky, how strongly it colours the sound, how loud the weather itself is under the notes \
        (Elements), and how fast it moves (Gusts).
        Land: the place, how far away it sounds (reverb), its echo, and how large it is.
        Play: how a key plays. Single notes; the month's chord; a split, with a drone under the lowest twelve \
        keys; or a pattern of the chord on the arpeggiator. Voicing spreads the chord; Tempo and Notes set the \
        pattern's speed. STYLE, VOICE and SKY step through style, voicing and sky. < and > step the months.
        Hold RESET to put the month back as composed.

        Keys: click or drag across the keybed, or play the Mac keyboard: \
        A W S E D R F G Y H U J K O L P ; [ ' from F3. Z / X are OCT- / OCT+.
        MIDI: the port "\(Engine.portName)" takes notes (played as single notes) and program changes (months 1–12).
        """
        a.runModal()
    }
}

struct MonthMenu: View {
    @ObservedObject var model: AlmanacModel

    var body: some View {
        ForEach(0..<AlmanacModel.months.count, id: \.self) { i in
            let m = AlmanacModel.months[i]
            Toggle("\(m.month): \(m.title)", isOn: Binding(get: { model.month == i }, set: { _ in model.selectMonth(i) }))
        }
    }
}

struct AlmanacContentView: View {
    @ObservedObject var model: AlmanacModel
    @AppStorage("frameless") private var frameless = true

    var body: some View {
        ZStack {
            if !frameless { Color(hex: 0x1C2233).ignoresSafeArea() }
            PanelView(model: model, finish: .almanac)
                .padding(frameless ? EdgeInsets(top: 1, leading: 1, bottom: 1, trailing: 1)
                                   : EdgeInsets(top: 30, leading: 22, bottom: 22, trailing: 22))
        }
        .ignoresSafeArea()
    }
}

final class AlmanacDelegate: ModeDelegate {
    override var model: any ModeModel { AlmanacModel.shared }
    override var title: String { "Almanac" }
    override var frameName: String { "AlmanacPanel" }
    override var titledColor: NSColor { NSColor(red: 0.11, green: 0.13, blue: 0.2, alpha: 1) }
    override func startEngine() { Engine.startMode { AlmanacModel.shared.start() } }
    override func makeContent() -> NSView { PanelHostingView(rootView: AlmanacContentView(model: AlmanacModel.shared)) }
    @MainActor override func launched() {
        AlmanacSnapshot.runIfRequested(model: AlmanacModel.shared)
        if CommandLine.arguments.contains("--selfcheck") { AlmanacSelfCheck.run(AlmanacModel.shared) }
    }
}

// Development aid, as for MF-1: `MF-1 --almanac --snapshot out.png [screen|sheet] [month N] [page N] [sky N]
// [place N] [mood N] [step N] [play N]` renders offscreen and quits. It never writes the user's days.
enum AlmanacSnapshot {
    @MainActor static func runIfRequested(model: AlmanacModel) {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count else { return }
        func option(_ name: String) -> Int? {
            guard let j = args.firstIndex(of: name), j + 1 < args.count else { return nil }
            return Int(args[j + 1])
        }
        func configure(_ m: AlmanacModel) {
            if let p = option("page"), let page = Page(rawValue: p) { m.page = page }
            if let v = option("sky") { m.set(AL.sky, v) }
            if let v = option("place") { m.set(AL.place, v) }
            if let v = option("mood") { m.set(AL.mood, v) }
            if let v = option("play") { m.set(AL.play, v) }
        }
        if let n = option("month") { model.selectMonth(n - 1) }
        configure(model)
        // The frame number the scene is drawn at: the animation step times ten.
        let frame = (option("step") ?? 0) * (60 / AlmanacModel.sceneFPS)
        let view: AnyView
        if args.contains("sheet") {
            // Every month's screen as composed, four across.
            let cells = (0..<AlmanacModel.months.count).map { i -> AlmanacModel in
                let m = AlmanacModel()
                m.selectMonth(i)
                configure(m)
                return m
            }
            view = AnyView(VStack(spacing: 0) {
                ForEach(0..<3) { row in
                    HStack(spacing: 0) {
                        ForEach(0..<4) { col in
                            AlmanacScreen(model: cells[row * 4 + col], frame: frame).frame(width: 480, height: 480)
                        }
                    }
                }
            })
        } else if args.contains("screen") {
            view = AnyView(AlmanacScreen(model: model, frame: frame).frame(width: 960, height: 960))
        } else {
            view = AnyView(PanelView(model: model, finish: .almanac).frame(width: 1615, height: 965).background(Color(hex: 0x1C2233)))
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

// `MF-1 --almanac --selfcheck` drives the model as the panel would and checks the engine follows. It saves the
// days as it ends, with the current month back as composed.
enum AlmanacSelfCheck {
    static func run(_ m: AlmanacModel) {
        var failed = 0
        func check(_ ok: Bool, _ what: String) {
            print(ok ? "ok   " : "FAIL ", what)
            if !ok { failed += 1 }
        }
        m.resetMonth()
        let m0 = m.month
        m.turnPresets(1)
        check(m.month == (m0 + 1) % 12 && Int(mf1_current_preset()) == m.month, "MONTH moves to the next month and loads its voice")
        m.turnPresets(-1)
        m.page = .mood
        m.turnKnob(0, -10)
        check(m.value(AL.mood) < AlmanacModel.recipe(m.month)[AL.mood], "KNOB1 on the mood page turns the mood down")
        let sky = m.value(AL.sky)
        m.turnAlgorithm(1)
        check(m.value(AL.sky) == min(sky + 1, Int(AL_SKY_COUNT) - 1), "SKY steps the weather")
        m.set(AL.sky, Int(AL_FOG))
        check(mf1_fx_get(0) == 1 && mf1_fx_get(1) == 0, "fog puts the low pass filter on")
        m.press("SEL")
        check(m.page == .land, "the LAND button goes to the land page")
        m.turnKnob(1, -60)
        check(m.value(AL.distance) == 0 && mf1_fx_get(4) == 0, "Distance at 0 switches the reverb off")
        m.press("ENV")
        m.set(AL.play, Int(AL_CHORD))
        m.keyDown(5)
        check(m.isHeld(5), "a key is held while the chord sounds")
        m.keyUp(5)
        m.set(AL.play, Int(AL_PATTERN))
        check(mf1_arp_on(), "the pattern style switches the arpeggiator on")
        m.press("LFO")
        check(m.value(AL.play) == Int(AL_SINGLE) && !mf1_arp_on(), "STYLE steps back round to single notes, arpeggiator off")
        m.resetMonth()
        check(m.value(AL.mood) == Int(AlmanacModel.recipe(m.month)[AL.mood]) && m.value(AL.distance) > 0,
              "RESET puts the month back as composed")
        m.writeNow()
        print(failed == 0 ? "all passed" : "\(failed) failed")
        exit(failed == 0 ? 0 : 1)
    }
}
