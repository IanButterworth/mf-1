import AppKit
import MF1Core
import RhymesCore
import SwiftUI
import UniformTypeIdentifiers

// Rhymes: the same engine and panel as MF-1, run as nursery rhymes for small children to learn. The next key lights
// up; SONG picks the rhyme, LEVEL how much the lights help, LISTEN plays the song through first.

struct RhymesApp: App {
    @NSApplicationDelegateAdaptor(RhymesDelegate.self) var delegate
    private let model = RhymesModel.shared
    @AppStorage("frameless") private var frameless = true

    init() {
        Engine.registerFonts()
    }

    // As in MF-1, the panel lives in a window the delegate makes; this scene only carries menus.
    var body: some Scene {
        MenuBarExtra("Rhymes", isInserted: .constant(false)) { EmptyView() }
        .commands {
            CommandGroup(replacing: .appSettings) {}
            CommandGroup(replacing: .newItem) {
                NewWindowItems()
                Button("Export Sound Bank…") { RhymesApp.exportSounds() }.keyboardShortcut("e")
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
            CommandMenu("Songs") { RhymeMenu(model: model) }
            CommandMenu("Device") {
                OutputMenu()
                Divider()
                Button("All Notes Off") {
                    model.restart()
                    mf1_all_notes_off()
                }.keyboardShortcut(".", modifiers: .command)
                Button("This Song as Written") { model.resetSong() }
                Button("Clear Every Star…") {
                    let a = NSAlert()
                    a.messageText = "Clear the stars of every song?"
                    a.informativeText = "The stars earned at every level go, and each song starts again with none."
                    a.addButton(withTitle: "Clear")
                    a.addButton(withTitle: "Cancel")
                    if a.runModal() == .alertFirstButtonReturn { model.resetStars() }
                }
            }
            UpdateCommands()
            CommandGroup(replacing: .help) {
                Button("Rhymes Controls") { RhymesApp.showHelp() }
            }
        }
    }

    // The songs' sounds as an ordinary DX7 32-voice bank, so they can be loaded onto a real FM-1.
    static func exportSounds() {
        var buf = [UInt8](repeating: 0, count: 4104)
        rhymes_export_bank(&buf)
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "syx") ?? .data]
        panel.nameFieldStringValue = "Rhymes Sounds.syx"
        panel.message = "A DX7 bank of the songs' sounds. The effects are set on the FM-1 itself."
        if panel.runModal() == .OK, let url = panel.url {
            do { try Data(buf).write(to: url) } catch { Engine.alert(error.localizedDescription) }
        }
    }

    static func showHelp() {
        let a = NSAlert()
        a.messageText = "Rhymes Controls"
        a.informativeText = """
        Nursery rhymes to learn on the keys. The key to play next lights up, and the screen shows its note in colour \
        with the words under it. A right key plays the next note and moves the picture on; a finished song earns up \
        to three stars.
        SONG and < > pick the rhyme. LEVEL, or MAGIC, GUIDE and SOLO: Magic plays the song whatever key is \
        pressed; Guide waits for the lit key; Solo lights the key only when the child waits, or after a wrong one. \
        LISTEN plays the song through, its keys lighting as it goes; any key stops it. AGAIN starts the song again; \
        hold it to put the song's settings back. LINE moves to the start of a line, to practise it.
        KNOB1-4: Speed (how fast the song plays itself), Room (how much reverb), Sound (any of the songs' \
        instruments; SOUND steps through them) and Lights: Blink, Steady, or Ahead, which also lights the key after \
        next dimly (LIGHT steps through them).

        Keys: click or drag across the keybed, click the little keyboard on the screen, or play the Mac keyboard: \
        A W S E D R F G Y H U J K O L P ; [ ' from F3. Z / X are OCT- / OCT+.
        MIDI: the port "\(Engine.portName)" takes program changes (songs 1–\(RH.songCount)), and notes, which play the \
        song's sound but do not move the game on.
        """
        a.runModal()
    }
}

struct RhymeMenu: View {
    @ObservedObject var model: RhymesModel

    var body: some View {
        ForEach(0..<RH.songCount, id: \.self) { i in
            Toggle(RH.title(i), isOn: Binding(get: { model.song == i }, set: { _ in model.selectSong(i) }))
        }
        Divider()
        ForEach(0..<RH.levels.count, id: \.self) { l in
            Toggle(RH.levels[l], isOn: Binding(get: { model.value(RH.level) == l }, set: { _ in model.set(RH.level, l) }))
        }
        Divider()
        Button("Listen") { model.listen() }
        Button("Start Again") { model.restart() }
    }
}

struct RhymesContentView: View {
    @ObservedObject var model: RhymesModel
    @AppStorage("frameless") private var frameless = true

    var body: some View {
        ZStack {
            if !frameless { Color(hex: 0x1A1A1A).ignoresSafeArea() }
            PanelView(model: model, finish: .rhymes)
                .padding(frameless ? EdgeInsets(top: 1, leading: 1, bottom: 1, trailing: 1)
                                   : EdgeInsets(top: 30, leading: 22, bottom: 22, trailing: 22))
        }
        .ignoresSafeArea()
    }
}

final class RhymesDelegate: ModeDelegate {
    override var model: any ModeModel { RhymesModel.shared }
    override var title: String { "Rhymes" }
    override var frameName: String { "RhymesPanel" }
    override var titledColor: NSColor { NSColor(white: 0.1, alpha: 1) }
    override func startEngine() { Engine.startMode { RhymesModel.shared.start() } }
    override func makeContent() -> NSView { PanelHostingView(rootView: RhymesContentView(model: RhymesModel.shared)) }
    @MainActor override func launched() {
        RhymesSnapshot.runIfRequested(model: RhymesModel.shared)
        if CommandLine.arguments.contains("--selfcheck") { RhymesSelfCheck.run(RhymesModel.shared) }
    }
}

// Development aid, as for MF-1: `MF-1 --rhymes --snapshot out.png [screen|sheet] [song N] [level N] [play N]
// [step N]` renders offscreen and quits; play N plays the song's first N notes right, and N past the end finishes it.
// It never writes the user's songs.
enum RhymesSnapshot {
    @MainActor static func runIfRequested(model: RhymesModel) {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count else { return }
        func option(_ name: String) -> Int? {
            guard let j = args.firstIndex(of: name), j + 1 < args.count else { return nil }
            return Int(args[j + 1])
        }
        func prepare(_ m: RhymesModel, song: Int) {
            m.selectSong(song)
            if let l = option("level") { m.set(RH.level, l) }
            m.restart()
            m.pill = nil
            if let n = option("play") {
                var t = rh_tune_t()
                rh_tune(Int32(m.song), &t)
                for k in 0..<min(n, Int(t.count)) {
                    let key = Int(rh_key_of(Int32(t.noteAt(k))))
                    m.keyDown(key)
                    m.keyUp(key)
                }
                // Let the celebration get going.
                if n >= Int(t.count) { Thread.sleep(forTimeInterval: 1.6) } else { Thread.sleep(forTimeInterval: 0.25) }
            }
            m.poll()
            m.pill = nil
        }
        prepare(model, song: (option("song") ?? 1) - 1)
        let frame = (option("step") ?? 0) * (60 / RhymesModel.sceneFPS)
        let view: AnyView
        if args.contains("screen") {
            view = AnyView(RhymesScreen(model: model, frame: frame).frame(width: 960, height: 960))
        } else {
            view = AnyView(PanelView(model: model, finish: .rhymes).frame(width: 1615, height: 965)
                .background(Color(hex: 0x1A1A1A)))
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

// `MF-1 --rhymes --selfcheck` drives the model as the panel would and checks the game and engine follow. It saves the
// songs as it ends, with the current song back as written.
enum RhymesSelfCheck {
    static func run(_ m: RhymesModel) {
        var failed = 0
        func check(_ ok: Bool, _ what: String) {
            print(ok ? "ok   " : "FAIL ", what)
            if !ok { failed += 1 }
        }
        // Its notes play silently.
        let volume = mf1_global_get(Int32(MF1_G_VOLUME))
        mf1_global_set(Int32(MF1_G_VOLUME), 0)
        m.resetSong()
        let s0 = m.song
        m.turnPresets(1)
        check(m.song == (s0 + 1) % RH.songCount && Int(mf1_current_preset()) == m.song,
              "SONG moves to the next song and loads its sound")
        m.turnPresets(-1)
        m.press("SEL")
        check(m.value(RH.level) == Int(RH_GUIDE), "GUIDE picks the Guide level")
        m.restart()
        m.poll()
        let first = Int(rh_key_of(Int32(m.game.tune.noteAt(0))))
        var lit = false
        for _ in 0..<40 {
            m.poll()
            lit = lit || m.keyLed(first) == 1
            Thread.sleep(forTimeInterval: 0.02)
        }
        check(lit, "the first key of the song lights")
        m.keyDown(first)
        m.keyUp(first)
        m.poll()
        check(m.game.pos == 1 && m.game.state == Int32(RH_PLAYING), "the lit key plays the first note")
        m.keyDown(first == 0 ? 1 : 0)
        m.keyUp(first == 0 ? 1 : 0)
        m.poll()
        check(m.game.pos == 1 && m.game.misses == 1, "a wrong key waits on the same note")
        m.press("FX")
        check(m.value(RH.level) == Int(RH_MAGIC), "MAGIC picks the Magic level")
        m.press("PLAY/STOP")
        m.poll()
        check(m.game.state == Int32(RH_LISTEN), "LISTEN plays the song through")
        m.press("PLAY/STOP")
        m.poll()
        check(m.game.state == Int32(RH_READY), "LISTEN again stops it")
        m.press("SAVE")
        m.poll()
        check(m.game.pos == 0, "AGAIN goes back to the start")
        m.resetSong()
        check(m.value(RH.level) == Int(RH_GUIDE), "holding AGAIN puts the song back as written")
        mf1_global_set(Int32(MF1_G_VOLUME), volume)
        m.writeNow()
        print(failed == 0 ? "all passed" : "\(failed) failed")
        exit(failed == 0 ? 0 : 1)
    }
}
