import AppKit
import MF1Core
import SwiftUI
import UniformTypeIdentifiers
import WoofCore

// Woof: the same engine and panel as MF-1, run as a dog show. Turn DOG for a breed, PLACE for where it is, PAGE for
// what KNOB1-4 shape: the bark, the mood, the place, or how the keys play.

struct WoofApp: App {
    @NSApplicationDelegateAdaptor(WoofDelegate.self) var delegate
    private let model = WoofModel.shared
    @AppStorage("frameless") private var frameless = true

    init() {
        Engine.registerFonts()
    }

    // As in MF-1, the panel lives in a window the delegate makes; this scene only carries menus.
    var body: some Scene {
        MenuBarExtra("Woof", isInserted: .constant(false)) { EmptyView() }
        .commands {
            CommandGroup(replacing: .appSettings) {}
            CommandGroup(replacing: .newItem) {
                Button("Export Dog Bank…") { WoofApp.exportDogs() }.keyboardShortcut("e")
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
            CommandMenu("Dogs") { DogMenu(model: model) }
            CommandMenu("Device") {
                OutputMenu()
                Divider()
                Button("All Notes Off") { mf1_all_notes_off() }.keyboardShortcut(".", modifiers: .command)
                Button("This Dog as Bred") { model.resetDog() }
                Button("Every Dog as Bred…") {
                    let a = NSAlert()
                    a.messageText = "Put every dog back as bred?"
                    a.informativeText = "Each dog goes back to its own bark, place and mood, and your changes are lost."
                    a.addButton(withTitle: "Reset")
                    a.addButton(withTitle: "Cancel")
                    if a.runModal() == .alertFirstButtonReturn { model.resetAll() }
                }
            }
            CommandGroup(replacing: .help) {
                Button("Woof Controls") { WoofApp.showHelp() }
            }
        }
    }

    // The dogs as an ordinary DX7 32-voice bank, so they can be loaded onto a real FM-1.
    static func exportDogs() {
        var buf = [UInt8](repeating: 0, count: 4104)
        woof_export_bank(&buf)
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "syx") ?? .data]
        panel.nameFieldStringValue = "Woof Dogs.syx"
        panel.message = "A DX7 bank of the dogs as bred. The effects are set on the FM-1 itself."
        if panel.runModal() == .OK, let url = panel.url {
            do { try Data(buf).write(to: url) } catch { Engine.alert(error.localizedDescription) }
        }
    }

    static func showHelp() {
        let a = NSAlert()
        a.messageText = "Woof Controls"
        a.informativeText = """
        DOG picks the breed: its bark, its place and its mood. PLACE moves it: park, parlour, rooftops, farm or \
        river. PAGE moves KNOB1–4 between four pages; BARK, MOOD, PLACE and PLAY jump straight there.
        Bark: how big the dog is, how gruff, how long each bark lasts, and how far its pitch jumps (Yelp).
        Mood: from mopey to zoomies; Howl turns barks into howls; Growl adds a growl that carries on while a key is \
        held; Breath is the huff at the start.
        Place: where the dog is, how far away it sounds (reverb), dogs far off barking back (echo), and how large \
        the place is.
        Play: how a key plays. A single bark; Repeat, barking again and again while held; Chorus, the pack barking \
        together; or Chase, the pack barking in turn. Pack sets how many; Tempo and Barks set how fast.
        STYLE and PACK step through them. SHOW brings on the next dog every ten seconds, a dog show. < and > step the dogs.
        Hold RESET to put the dog back as bred.

        Keys: click or drag across the keybed, or play the Mac keyboard: \
        A W S E D R F G Y H U J K O L P ; [ ' from F3. Z / X are OCT- / OCT+.
        MIDI: the port "\(Engine.portName)" takes notes (single barks) and program changes (dogs 1–\(WoofModel.dogs.count)).
        """
        a.runModal()
    }
}

struct DogMenu: View {
    @ObservedObject var model: WoofModel

    var body: some View {
        ForEach(0..<WoofModel.dogs.count, id: \.self) { i in
            Toggle(WoofModel.dogs[i].breed, isOn: Binding(get: { model.dog == i }, set: { _ in model.selectDog(i) }))
        }
        Divider()
        Toggle("Dog Show", isOn: Binding(get: { model.parade }, set: { _ in model.toggleParade() }))
    }
}

struct WoofContentView: View {
    @ObservedObject var model: WoofModel
    @AppStorage("frameless") private var frameless = true

    var body: some View {
        ZStack {
            if !frameless { Color(hex: 0x1A1A1A).ignoresSafeArea() }
            PanelView(model: model, finish: .woof)
                .padding(frameless ? EdgeInsets(top: 1, leading: 1, bottom: 1, trailing: 1)
                                   : EdgeInsets(top: 30, leading: 22, bottom: 22, trailing: 22))
        }
        .ignoresSafeArea()
    }
}

final class WoofDelegate: ModeDelegate {
    override var model: any ModeModel { WoofModel.shared }
    override var title: String { "Woof" }
    override var frameName: String { "WoofPanel" }
    override var titledColor: NSColor { NSColor(white: 0.1, alpha: 1) }
    override func startEngine() { Engine.startMode { WoofModel.shared.start() } }
    override func makeContent() -> NSView { PanelHostingView(rootView: WoofContentView(model: WoofModel.shared)) }
    @MainActor override func launched() {
        WoofSnapshot.runIfRequested(model: WoofModel.shared)
        if CommandLine.arguments.contains("--selfcheck") { WoofSelfCheck.run(WoofModel.shared) }
    }
}

// Development aid, as for MF-1: `MF-1 --woof --snapshot out.png [screen|sheet] [dog N] [page N] [place N] [mood N]
// [play N] [bark 1] [step N]` renders offscreen and quits. It never writes the user's dogs.
enum WoofSnapshot {
    @MainActor static func runIfRequested(model: WoofModel) {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count else { return }
        func option(_ name: String) -> Int? {
            guard let j = args.firstIndex(of: name), j + 1 < args.count else { return nil }
            return Int(args[j + 1])
        }
        func configure(_ m: WoofModel) {
            if let p = option("page"), let page = WoofPage(rawValue: p) { m.page = page }
            if let v = option("place") { m.set(WF.place, v) }
            if let v = option("mood") { m.set(WF.mood, v) }
            if let v = option("play") { m.set(WF.play, v) }
            if let v = option("howl") { m.set(WF.howl, v) }
        }
        if let n = option("dog") { model.selectDog(n - 1) }
        configure(model)
        // Let the dog finish trotting in, then sound a note if a bark is asked for.
        model.tick += 200
        if option("bark") == 1 {
            mf1_key_down(66)
            Thread.sleep(forTimeInterval: 0.05)
            model.poll()
        }
        // The frame number the scene is drawn at: the animation step times ten.
        let frame = (option("step") ?? 0) * (60 / WoofModel.sceneFPS)
        let view: AnyView
        if args.contains("sheet") {
            // Every dog's screen as bred, four across.
            let cells = (0..<WoofModel.dogs.count).map { i -> WoofModel in
                let m = WoofModel()
                m.selectDog(i)
                configure(m)
                m.tick += 200
                return m
            }
            view = AnyView(VStack(spacing: 0) {
                ForEach(0..<(cells.count + 3) / 4, id: \.self) { row in
                    HStack(spacing: 0) {
                        ForEach(0..<min(4, cells.count - row * 4), id: \.self) { col in
                            WoofScreen(model: cells[row * 4 + col], frame: frame).frame(width: 480, height: 480)
                        }
                        Spacer(minLength: 0)
                    }
                }
            })
        } else if args.contains("screen") {
            view = AnyView(WoofScreen(model: model, frame: frame).frame(width: 960, height: 960))
        } else {
            view = AnyView(PanelView(model: model, finish: .woof).frame(width: 1615, height: 965).background(Color(hex: 0x1A1A1A)))
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

// `MF-1 --woof --selfcheck` drives the model as the panel would and checks the engine follows. It saves the dogs as
// it ends, with the current dog back as bred.
enum WoofSelfCheck {
    static func run(_ m: WoofModel) {
        var failed = 0
        func check(_ ok: Bool, _ what: String) {
            print(ok ? "ok   " : "FAIL ", what)
            if !ok { failed += 1 }
        }
        m.resetDog()
        let d0 = m.dog
        m.turnPresets(1)
        check(m.dog == (d0 + 1) % WoofModel.dogs.count && Int(mf1_current_preset()) == m.dog,
              "DOG moves to the next breed and loads its bark")
        m.turnPresets(-1)
        m.page = .mood
        m.turnKnob(1, 10)
        check(m.value(WF.howl) > WoofModel.recipe(m.dog)[WF.howl], "KNOB2 on the mood page turns the howl up")
        let place = m.value(WF.place)
        m.turnAlgorithm(place == Int(WF_PLACE_COUNT) - 1 ? -1 : 1)
        check(m.value(WF.place) != place, "PLACE moves the dog")
        m.press("SEL")
        check(m.page == .place, "the PLACE button goes to the place page")
        m.turnKnob(1, -60)
        check(m.value(WF.distance) == 0 && mf1_fx_get(4) == 0, "Distance at 0 switches the reverb off")
        m.press("ENV")
        m.set(WF.play, Int(WF_CHORUS))
        m.keyDown(5)
        check(m.isHeld(5), "a key is held while the pack barks")
        m.keyUp(5)
        m.set(WF.play, Int(WF_REPEAT))
        check(mf1_arp_on(), "repeat switches the arpeggiator on")
        m.set(WF.play, Int(WF_CHASE))
        m.press("LFO")
        check(m.value(WF.play) == Int(WF_SINGLE) && !mf1_arp_on(), "STYLE steps back round to single barks, arpeggiator off")
        let parade = m.parade
        m.press("GLO")
        check(m.parade != parade, "SHOW turns the dog show on and off")
        m.press("GLO")
        m.resetDog()
        check(m.value(WF.howl) == Int(WoofModel.recipe(m.dog)[WF.howl]) &&
              m.value(WF.distance) == Int(WoofModel.recipe(m.dog)[WF.distance]),
              "RESET puts the dog back as bred")
        m.writeNow()
        print(failed == 0 ? "all passed" : "\(failed) failed")
        exit(failed == 0 ? 0 : 1)
    }
}
