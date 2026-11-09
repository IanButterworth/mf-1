import AppKit
import MF1Core
import PhaseCore
import SwiftUI
import UniformTypeIdentifiers

// Phase: the same engine and panel as MF-1, run as process music. PIECE picks a piece, PROCESS how its parts move,
// PAGE what KNOB1-4 shape: the process, the cell, the sound or the time. Play a phrase on the keys and it becomes
// the cell.

struct PhaseApp: App {
    @NSApplicationDelegateAdaptor(PhaseDelegate.self) var delegate
    private let model = PhaseModel.shared
    @AppStorage("frameless") private var frameless = true

    init() {
        Engine.registerFonts()
    }

    // As in MF-1, the panel lives in a window the delegate makes; this scene only carries menus.
    var body: some Scene {
        MenuBarExtra("Phase", isInserted: .constant(false)) { EmptyView() }
        .commands {
            CommandGroup(replacing: .appSettings) {}
            CommandGroup(replacing: .newItem) {
                Button("Export Sound Bank…") { PhaseApp.exportSounds() }.keyboardShortcut("e")
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
            CommandMenu("Pieces") { PieceMenu(model: model) }
            CommandMenu("Device") {
                OutputMenu()
                Divider()
                Button("All Notes Off") {
                    model.stop()
                    mf1_all_notes_off()
                }.keyboardShortcut(".", modifiers: .command)
                Button("This Piece as Written") { model.resetPiece() }
                Button("Every Piece as Written…") {
                    let a = NSAlert()
                    a.messageText = "Put every piece back as written?"
                    a.informativeText = "Each piece goes back to its own cell, process and sound, and your cells and changes are lost."
                    a.addButton(withTitle: "Reset")
                    a.addButton(withTitle: "Cancel")
                    if a.runModal() == .alertFirstButtonReturn { model.resetAll() }
                }
            }
            UpdateCommands()
            CommandGroup(replacing: .help) {
                Button("Phase Controls") { PhaseApp.showHelp() }
            }
        }
    }

    // The pieces' sounds as an ordinary DX7 32-voice bank, so they can be loaded onto a real FM-1.
    static func exportSounds() {
        var buf = [UInt8](repeating: 0, count: 4104)
        phase_export_bank(&buf)
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "syx") ?? .data]
        panel.nameFieldStringValue = "Phase Sounds.syx"
        panel.message = "A DX7 bank of the pieces' sounds. The processes and effects are not in it."
        if panel.runModal() == .OK, let url = panel.url {
            do { try Data(buf).write(to: url) } catch { Engine.alert(error.localizedDescription) }
        }
    }

    static func showHelp() {
        let a = NSAlert()
        a.messageText = "Phase Controls"
        a.informativeText = """
        PLAY/STOP starts and stops the process. Play a phrase on the keys and, after a moment of rest, it becomes \
        the cell and the process starts again with it; a pause between keys becomes a rest. WRITE turns that off, \
        so the keys play along instead. BEGIN starts the process again from unison.
        PIECE picks a piece: its cell, process, sound and tempo. PROCESS changes how the parts move. PAGE moves \
        KNOB1–4 between four pages; PROC, CELL, SOUND and TIME jump straight there.
        Process: the process, its own amount (how fast parts drift, how far they jump or sit apart, how deep a swell \
        or how fast notes grow), how many parts, and how many repeats each stage lasts.
        Cell: its length, how many of its notes play, transpose, and how long each note is held.
        Sound: the sound, its brightness and decay, and the room.
        Time: tempo, echo and its steps, and Span, how far apart the parts sit in pitch.
        PARTS and SPAN step through those. < and > step the pieces. OCT- and OCT+ move the keys and the piece. \
        Hold RESET to put the piece and its cell back as written.

        Keys: click or drag across the keybed, or play the Mac keyboard: \
        A W S E D R F G Y H U J K O L P ; [ ' from F3. Z / X are OCT- / OCT+. Space plays and stops.
        MIDI: the port "\(Engine.portName)" takes notes and program changes (pieces 1–\(PhaseModel.pieces.count)).
        """
        a.runModal()
    }
}

struct PieceMenu: View {
    @ObservedObject var model: PhaseModel

    var body: some View {
        Button(model.playing ? "Stop" : "Play") { model.togglePlay() }.keyboardShortcut(.space, modifiers: [])
        Button("From the Beginning") { model.restart() }.keyboardShortcut(.return, modifiers: [.command])
        Toggle("Keys Write the Cell", isOn: Binding(get: { model.writes }, set: { _ in model.press("REC") }))
        Divider()
        ForEach(0..<PhaseModel.pieces.count, id: \.self) { i in
            Toggle(PhaseModel.pieces[i].title, isOn: Binding(get: { model.piece == i }, set: { _ in model.selectPiece(i) }))
        }
    }
}

struct PhaseContentView: View {
    @ObservedObject var model: PhaseModel
    @AppStorage("frameless") private var frameless = true

    var body: some View {
        ZStack {
            if !frameless { Color(hex: 0x1A1A1A).ignoresSafeArea() }
            PanelView(model: model, finish: .phase)
                .padding(frameless ? EdgeInsets(top: 1, leading: 1, bottom: 1, trailing: 1)
                                   : EdgeInsets(top: 30, leading: 22, bottom: 22, trailing: 22))
        }
        .ignoresSafeArea()
    }
}

final class PhaseDelegate: ModeDelegate {
    override var model: any ModeModel { PhaseModel.shared }
    override var title: String { "Phase" }
    override var frameName: String { "PhasePanel" }
    override var titledColor: NSColor { NSColor(white: 0.1, alpha: 1) }
    override func startEngine() { Engine.startMode { PhaseModel.shared.start() } }
    override func makeContent() -> NSView { PanelHostingView(rootView: PhaseContentView(model: PhaseModel.shared)) }
    @MainActor override func launched() {
        PhaseSnapshot.runIfRequested(model: PhaseModel.shared)
        if CommandLine.arguments.contains("--selfcheck") { PhaseSelfCheck.run(PhaseModel.shared) }
    }

    // The player lets go of every note before the engine stops, so nothing is left hanging.
    override func applicationWillTerminate(_ notification: Notification) {
        PhaseModel.shared.shutdown()
        super.applicationWillTerminate(notification)
    }
}

// Development aid, as for MF-1: `MF-1 --phase --snapshot out.png [screen|sheet] [piece N] [page N] [at STEPS]
// [process N] [write 1] [stopped 1]` renders offscreen and quits. `at` draws the process that many steps in;
// without it, the screen shows a moment where the process is well under way. It never reads or writes the user's
// pieces.
enum PhaseSnapshot {
    // A moment in each process worth seeing, in steps from the start.
    static func moment(_ m: PhaseModel) -> Double {
        let l = Double(m.value(PH.length)), h = Double(m.value(PH.repeats))
        let d = Double(phase_motion_steps(Int32(m.value(PH.process)), Int32(m.value(PH.motion)), Int32(m.value(PH.length))))
        switch m.value(PH.process) {
        case PH_PHASE, PH_RESULT: return 3 * (h + d) * l + l + 0.25
        case PH_TAPE: return 2.5 * d * l + 0.25
        case PH_PULSE: return 2.5 * h * l + 0.25
        case PH_CANON: return 3 * h * l + 0.25
        default: return 5 * h * l + 0.25
        }
    }

    @MainActor static func runIfRequested(model: PhaseModel) {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count else { return }
        func option(_ name: String) -> String? {
            guard let j = args.firstIndex(of: name), j + 1 < args.count else { return nil }
            return args[j + 1]
        }
        func configure(_ m: PhaseModel) {
            if let p = option("page").flatMap(Int.init), let page = PhasePage(rawValue: p) { m.page = page }
            if let v = option("process").flatMap(Int.init) { m.set(PH.process, v) }
            let steps = option("at").flatMap(Double.init) ?? moment(m)
            m.snapshotTick = option("stopped") == "1" ? nil : Int64(steps * Double(PH_TPS))
        }
        if let n = option("piece").flatMap(Int.init) { model.selectPiece(n - 1) }
        configure(model)
        if option("write") == "1" {
            model.snapshotPhrase([62, 65, 69, 67, 72, 69, 64])
        }
        let view: AnyView
        if args.contains("sheet") {
            // Every piece's screen under way, four across.
            let cells = (0..<PhaseModel.pieces.count).map { i -> PhaseModel in
                let m = PhaseModel()
                m.selectPiece(i)
                configure(m)
                return m
            }
            view = AnyView(VStack(spacing: 0) {
                ForEach(0..<(cells.count + 3) / 4, id: \.self) { row in
                    HStack(spacing: 0) {
                        ForEach(0..<min(4, cells.count - row * 4), id: \.self) { col in
                            PhaseScreen(model: cells[row * 4 + col]).frame(width: 480, height: 480)
                        }
                    }
                }
            })
        } else if args.contains("screen") {
            view = AnyView(PhaseScreen(model: model).frame(width: 960, height: 960))
        } else {
            view = AnyView(PanelView(model: model, finish: .phase).frame(width: 1615, height: 965).background(Color(hex: 0x1A1A1A)))
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

// `MF-1 --phase --selfcheck` drives the model as the panel would and checks the engine and the player follow. It
// saves the pieces as it ends, with the current piece back as written.
enum PhaseSelfCheck {
    static func run(_ m: PhaseModel) {
        var failed = 0
        func check(_ ok: Bool, _ what: String) {
            print(ok ? "ok   " : "FAIL ", what)
            if !ok { failed += 1 }
        }
        // Muted, so the check's notes are not heard.
        let volume = mf1_global_get(Int32(MF1_G_VOLUME))
        mf1_global_set(Int32(MF1_G_VOLUME), 0)
        m.resetPiece()
        let p0 = m.piece
        m.turnPresets(1)
        check(m.piece == (p0 + 1) % PhaseModel.pieces.count && Int(mf1_current_preset()) == m.piece,
              "PIECE moves to the next piece and loads its sound")
        m.turnPresets(-1)
        m.press("PLAY")
        Thread.sleep(forTimeInterval: 0.4)
        check(m.playing && phase_playing(), "PLAY starts the process")
        m.page = .process
        let parts = m.value(PH.parts)
        m.press("LFO")
        check(m.value(PH.parts) != parts, "PARTS steps the number of parts")
        m.press("PLAY")
        var held = 0
        for n in 0..<128 { held += Int(phase_refs(Int32(n))) }
        check(!m.playing && !phase_playing() && held == 0, "STOP stops it and lets go of every note")
        m.writes = true
        for k in [0, 2, 4] {
            m.keyDown(k)
            m.keyUp(k)
            Thread.sleep(forTimeInterval: 0.15)
        }
        check(m.writing && m.phrase.count == 3, "keys write a phrase")
        m.finishPhrase()
        check(!m.writing && m.cell.notes.filter { $0 >= 0 }.count == 3 && m.playing,
              "the phrase becomes the cell and the process starts with it")
        m.stop()
        m.resetPiece()
        check(m.cell.notes == PhaseModel.writtenCell(m.piece).notes, "RESET puts the cell back as written")
        m.writeNow()
        mf1_global_set(Int32(MF1_G_VOLUME), volume)
        print(failed == 0 ? "all passed" : "\(failed) failed")
        m.shutdown()
        exit(failed == 0 ? 0 : 1)
    }
}
