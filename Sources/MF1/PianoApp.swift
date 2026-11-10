import AppKit
import MF1Core
import PianoCore
import SwiftUI

// Piano: the same panel as MF-1, playing a grand piano made from measurements of a real one. Turn PIANO for a
// voicing, TOUCH for how hard the keys play, PAGE for what KNOB1-4 shape.

struct PianoApp: App {
    @NSApplicationDelegateAdaptor(PianoDelegate.self) var delegate
    private let model = PianoModel.shared
    @AppStorage("frameless") private var frameless = true

    init() {
        Engine.registerFonts()
    }

    // As in MF-1, the panel lives in a window the delegate makes; this scene only carries menus.
    var body: some Scene {
        MenuBarExtra("Piano", isInserted: .constant(false)) { EmptyView() }
        .commands {
            CommandGroup(replacing: .appSettings) {}
            CommandGroup(replacing: .newItem) {
                NewWindowItems()
            }
            CommandGroup(after: .toolbar) {
                Toggle("Frameless Window", isOn: applying($frameless) { delegate.setFrameless($0) })
                    .keyboardShortcut("f", modifiers: [.command, .control])
                Divider()
                ModeToggles()
            }
            CommandMenu("Pianos") { PianoMenu(model: model) }
            CommandMenu("Device") {
                OutputMenu()
                Divider()
                Button("All Notes Off") { mf1_all_notes_off() }.keyboardShortcut(".", modifiers: .command)
                Button("This Piano as Voiced") { model.resetPiano() }
                Button("Every Piano as Voiced…") {
                    let a = NSAlert()
                    a.messageText = "Put every piano back as voiced?"
                    a.informativeText = "Each piano goes back to its own tone, room and strings, and your changes are lost."
                    a.addButton(withTitle: "Reset")
                    a.addButton(withTitle: "Cancel")
                    if a.runModal() == .alertFirstButtonReturn { model.resetAll() }
                }
            }
            UpdateCommands()
            CommandGroup(replacing: .help) {
                Button("Piano Controls") { PianoApp.showHelp() }
            }
        }
    }

    static func showHelp() {
        let a = NSAlert()
        a.messageText = "Piano Controls"
        a.informativeText = """
        PIANO picks the voicing: a concert grand as measured, brighter or mellower hammers, a felt piano, a dry studio \
        or a hall, a honky-tonk or an old upright. < and > step them. TOUCH sets how hard the panel's keys play, \
        which sense no velocity; notes from MIDI play at their own velocity.
        PAGE, TONE and STRINGS move KNOB1–4 between two pages.
        Tone: the hammer, soft to hard; how long the strings ring; the room; the volume.
        Strings: how far apart each key's two or three strings are tuned (the slow beat in a held note); the knock of \
        hammer and key on the soundboard; how much the octaves are stretched; how quickly the dampers stop a note.
        PEDAL holds the sustain pedal down until pressed again, as does MIDI controller 64. SPACE steps the room \
        between room, hall and plate. Hold RESET to put the piano back as voiced.

        Keys: click or drag across the keybed, or play the Mac keyboard: \
        A W S E D R F G Y H U J K O L P ; [ ' from F3. Z / X are OCT- / OCT+.
        MIDI: the port "\(Engine.portName)" takes notes with their velocity, and the sustain pedal.
        """
        a.runModal()
    }
}

struct PianoMenu: View {
    @ObservedObject var model: PianoModel

    var body: some View {
        ForEach(0..<PF.names.count, id: \.self) { i in
            Toggle(PF.names[i], isOn: Binding(get: { model.piano == i }, set: { _ in model.selectPiano(i) }))
        }
        Divider()
        Toggle("Sustain Pedal", isOn: Binding(get: { model.pedal }, set: { model.setPedal($0) }))
    }
}

struct PianoContentView: View {
    @ObservedObject var model: PianoModel
    @AppStorage("frameless") private var frameless = true

    var body: some View {
        ZStack {
            if !frameless { Color(hex: 0x111111).ignoresSafeArea() }
            PanelView(model: model, finish: .piano)
                .padding(frameless ? EdgeInsets(top: 1, leading: 1, bottom: 1, trailing: 1)
                                   : EdgeInsets(top: 30, leading: 22, bottom: 22, trailing: 22))
        }
        .ignoresSafeArea()
    }
}

final class PianoDelegate: ModeDelegate {
    override var model: any ModeModel { PianoModel.shared }
    override var title: String { "Piano" }
    override var frameName: String { "PianoPanel" }
    override var titledColor: NSColor { NSColor(white: 0.07, alpha: 1) }
    override func startEngine() { Engine.startMode { PianoModel.shared.start() } }
    override func makeContent() -> NSView { PanelHostingView(rootView: PianoContentView(model: PianoModel.shared)) }
    @MainActor override func launched() {
        PianoSnapshot.runIfRequested(model: PianoModel.shared)
    }
}

// Development aid, as for MF-1: `MF-1 --piano --snapshot out.png [screen] [piano N] [page N] [chord 1] [pedal 1]`
// renders offscreen and quits. It never writes the user's pianos.
enum PianoSnapshot {
    @MainActor static func runIfRequested(model: PianoModel) {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count else { return }
        func option(_ name: String) -> Int? {
            guard let j = args.firstIndex(of: name), j + 1 < args.count else { return nil }
            return Int(args[j + 1])
        }
        if let n = option("piano") { model.selectPiano(n - 1) }
        if let p = option("page"), let page = PianoPage(rawValue: p) { model.page = page }
        if option("pedal") == 1 { model.setPedal(true) }
        if option("chord") == 1 {
            for k in [0, 4, 7, 12] { model.keyDown(k) }
            Thread.sleep(forTimeInterval: 0.15)
        }
        model.poll()
        let view: AnyView
        if args.contains("screen") {
            view = AnyView(PianoScreen(model: model, frame: 3).frame(width: 960, height: 960))
        } else {
            view = AnyView(PanelView(model: model, finish: .piano).frame(width: 1615, height: 965).background(Color(hex: 0x111111)))
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
