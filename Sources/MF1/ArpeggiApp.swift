import AppKit
import ArpeggiCore
import MF1Core
import SwiftUI
import UniformTypeIdentifiers

// Arpeggi: the same engine and panel as MF-1, run as two arpeggiators playing one pool of notes. SCENE picks a
// scene, RELATION how the lanes play together, PAGE what KNOB1-4 shape.

struct ArpeggiApp: App {
    @NSApplicationDelegateAdaptor(ArpeggiDelegate.self) var delegate
    private let model = ArpeggiModel.shared
    @AppStorage("frameless") private var frameless = true

    init() {
        Engine.registerFonts()
    }

    // As in MF-1, the panel lives in a window the delegate makes; this scene only carries menus.
    var body: some Scene {
        MenuBarExtra("Arpeggi", isInserted: .constant(false)) { EmptyView() }
        .commands {
            CommandGroup(replacing: .appSettings) {}
            CommandGroup(replacing: .newItem) {
                NewWindowItems()
                Button("Export Sound Bank…") { ArpeggiApp.exportSounds() }.keyboardShortcut("e")
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
            CommandMenu("Scenes") { SceneMenu(model: model) }
            CommandMenu("Device") {
                OutputMenu()
                Divider()
                Button("All Notes Off") {
                    model.stop()
                    mf1_all_notes_off()
                }.keyboardShortcut(".", modifiers: .command)
                Button("This Scene as Written") { model.resetScene() }
                Button("Every Scene as Written…") {
                    let a = NSAlert()
                    a.messageText = "Put every scene back as written?"
                    a.informativeText = "Each scene goes back to its own shapes, relation and sound, and your changes are lost."
                    a.addButton(withTitle: "Reset")
                    a.addButton(withTitle: "Cancel")
                    if a.runModal() == .alertFirstButtonReturn { model.resetAll() }
                }
            }
            UpdateCommands()
            CommandGroup(replacing: .help) {
                Button("Arpeggi Controls") { ArpeggiApp.showHelp() }
            }
        }
    }

    // The scenes' sounds as an ordinary DX7 32-voice bank, so they can be loaded onto a real FM-1.
    static func exportSounds() {
        var buf = [UInt8](repeating: 0, count: 4104)
        arpeggi_export_bank(&buf)
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "syx") ?? .data]
        panel.nameFieldStringValue = "Arpeggi Sounds.syx"
        panel.message = "A DX7 bank of the scenes' sounds. The arpeggios and effects are not in it."
        if panel.runModal() == .OK, let url = panel.url {
            do { try Data(buf).write(to: url) } catch { Engine.alert(error.localizedDescription) }
        }
    }

    static func showHelp() {
        let a = NSAlert()
        a.messageText = "Arpeggi Controls"
        a.informativeText = """
        Play notes and two arpeggiators, A and B, play them. HOLD (EDIT) sets how the keys fill the pool of notes: \
        Hold plays only while keys are down, Latch keeps a chord until the next, and Ring keeps a ring of notes \
        where each new key replaces the oldest. PLAY starts and stops; with no notes, it starts the scene's own \
        chord. CLEAR empties the pool; hold it for the scene as written. WRITE off lets the keys play along instead.
        SCENE picks a scene. RELATION decides how A and B meet: Together, Hocket (one line, a note from each in \
        turn), Answer (bar by bar), Canon (B three steps behind), Mirror (B upside down) or Trade (swapping shapes \
        every two bars). DUO switches B on and off.
        PAGE moves KNOB1–4 between pages; A, B, NOTES, SOUND and TIME jump straight there.
        A and B: shape, rate, rhythm and gate (over 100% the notes overlap).
        Notes: hold mode, ring size, span in octaves, and B's pitch against A.
        Sound: the sound, its brightness and decay, and the room. Time: tempo, swing, echo and its steps.
        < and > step the scenes. OCT- and OCT+ move the keys and what is playing.

        Keys: click or drag across the keybed, or play the Mac keyboard: \
        A W S E D R F G Y H U J K O L P ; [ ' from F3. Z / X are OCT- / OCT+. Space plays and stops.
        MIDI: the port "\(Engine.portName)" takes program changes (scenes 1–\(ArpeggiModel.scenes.count)); its \
        notes play the sound directly.
        """
        a.runModal()
    }
}

struct SceneMenu: View {
    @ObservedObject var model: ArpeggiModel

    var body: some View {
        Button(model.playing ? "Stop" : "Play") { model.togglePlay() }.keyboardShortcut(.space, modifiers: [])
        Button("Clear Notes") { model.clear() }.keyboardShortcut(.delete, modifiers: [.command])
        Toggle("Keys Feed the Arps", isOn: Binding(get: { model.writes }, set: { _ in model.press("REC") }))
        Divider()
        ForEach(0..<ArpeggiModel.scenes.count, id: \.self) { i in
            Toggle(ArpeggiModel.scenes[i].title, isOn: Binding(get: { model.scene == i }, set: { _ in model.selectScene(i) }))
        }
    }
}

struct ArpeggiContentView: View {
    @ObservedObject var model: ArpeggiModel
    @AppStorage("frameless") private var frameless = true

    var body: some View {
        ZStack {
            if !frameless { Color(hex: 0x1A1A1A).ignoresSafeArea() }
            PanelView(model: model, finish: .arpeggi)
                .padding(frameless ? EdgeInsets(top: 1, leading: 1, bottom: 1, trailing: 1)
                                   : EdgeInsets(top: 30, leading: 22, bottom: 22, trailing: 22))
        }
        .ignoresSafeArea()
    }
}

final class ArpeggiDelegate: ModeDelegate {
    override var model: any ModeModel { ArpeggiModel.shared }
    override var title: String { "Arpeggi" }
    override var frameName: String { "ArpeggiPanel" }
    override var titledColor: NSColor { NSColor(white: 0.1, alpha: 1) }
    override func startEngine() { Engine.startMode { ArpeggiModel.shared.start() } }
    override func makeContent() -> NSView { PanelHostingView(rootView: ArpeggiContentView(model: ArpeggiModel.shared)) }
    @MainActor override func launched() {
        ArpeggiSnapshot.runIfRequested(model: ArpeggiModel.shared)
        if CommandLine.arguments.contains("--selfcheck") { ArpeggiSelfCheck.run(ArpeggiModel.shared) }
    }

    // The player lets go of every note before the engine stops, so nothing is left hanging.
    override func applicationWillTerminate(_ notification: Notification) {
        ArpeggiModel.shared.shutdown()
        super.applicationWillTerminate(notification)
    }
}

// Development aid, as for MF-1: `MF-1 --arpeggi --snapshot out.png [screen|sheet] [scene N] [page N] [at BEATS]
// [relation N]` renders offscreen and quits, drawing the scene's own chord that many beats in (6 by default). It
// never reads or writes the user's scenes.
enum ArpeggiSnapshot {
    @MainActor static func runIfRequested(model: ArpeggiModel) {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count else { return }
        func option(_ name: String) -> String? {
            guard let j = args.firstIndex(of: name), j + 1 < args.count else { return nil }
            return args[j + 1]
        }
        func configure(_ m: ArpeggiModel) {
            if let p = option("page").flatMap(Int.init), let page = ArpPage(rawValue: p) { m.page = page }
            if let v = option("relation").flatMap(Int.init) { m.set(AP.relation, v) }
            let beats = option("at").flatMap(Double.init) ?? 6.3
            m.snapshotTick = Int64(beats * Double(AP_PPQ))
        }
        if let n = option("scene").flatMap(Int.init) { model.selectScene(n - 1) }
        configure(model)
        let view: AnyView
        if args.contains("sheet") {
            let cells = (0..<ArpeggiModel.scenes.count).map { i -> ArpeggiModel in
                let m = ArpeggiModel()
                m.selectScene(i)
                configure(m)
                return m
            }
            view = AnyView(VStack(spacing: 0) {
                ForEach(0..<(cells.count + 3) / 4, id: \.self) { row in
                    HStack(spacing: 0) {
                        ForEach(0..<min(4, cells.count - row * 4), id: \.self) { col in
                            ArpeggiScreen(model: cells[row * 4 + col]).frame(width: 480, height: 480)
                        }
                    }
                }
            })
        } else if args.contains("screen") {
            view = AnyView(ArpeggiScreen(model: model).frame(width: 960, height: 960))
        } else {
            view = AnyView(PanelView(model: model, finish: .arpeggi).frame(width: 1615, height: 965).background(Color(hex: 0x1A1A1A)))
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

// `MF-1 --arpeggi --selfcheck` drives the model as the panel would, muted, and checks the engine and the player
// follow. It saves the scenes as it ends, with the current scene back as written.
enum ArpeggiSelfCheck {
    static func run(_ m: ArpeggiModel) {
        var failed = 0
        func check(_ ok: Bool, _ what: String) {
            print(ok ? "ok   " : "FAIL ", what)
            if !ok { failed += 1 }
        }
        let volume = mf1_global_get(Int32(MF1_G_VOLUME))
        mf1_global_set(Int32(MF1_G_VOLUME), 0)
        m.resetScene()
        let c0 = m.scene
        m.turnPresets(1)
        check(m.scene == (c0 + 1) % ArpeggiModel.scenes.count && Int(mf1_current_preset()) == m.scene,
              "SCENE moves to the next scene and loads its sound")
        m.turnPresets(-1)
        m.clear()
        m.press("PLAY")
        Thread.sleep(forTimeInterval: 0.3)
        var pool = m.pool()
        check(m.playing && arpeggi_playing() && Int(pool.count) == ArpeggiModel.seed(m.scene).count,
              "PLAY with no notes starts the scene's own chord")
        m.press("PLAY")
        var held = 0
        for n in 0..<128 { held += Int(arpeggi_refs(Int32(n))) }
        check(!m.playing && held == 0, "STOP lets go of every note")
        // Ring of Five: keys fill the ring and the sixth replaces the first.
        m.selectScene(0)
        m.clear()
        for k in [0, 2, 4, 5, 7, 9] {
            m.keyDown(k)
            m.keyUp(k)
            Thread.sleep(forTimeInterval: 0.05)
        }
        pool = m.pool()
        let notes = m.poolNotes(pool)
        check(m.playing && notes.count == 5 && notes[0] == m.noteFor(key: 9) - m.octave * 12,
              "Ring: the sixth key replaces the oldest")
        m.press("EDIT")
        m.press("EDIT")
        check(m.value(AP.hold) == Int(AP_LATCH), "HOLD steps the hold mode")
        m.press("SAVE")
        check(!m.playing && m.pool().count == 0, "CLEAR empties the pool")
        m.resetScene()
        m.writeNow()
        mf1_global_set(Int32(MF1_G_VOLUME), volume)
        print(failed == 0 ? "all passed" : "\(failed) failed")
        m.shutdown()
        exit(failed == 0 ? 0 : 1)
    }
}
