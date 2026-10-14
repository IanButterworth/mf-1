import AppKit
import MF1Core
import SwiftUI
import UniformTypeIdentifiers

@main
struct MF1App: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
    @StateObject private var model = DeviceModel()
    @AppStorage("finish") private var finishName = "Black"
    @AppStorage("quirks") private var quirks = true

    // Fonts must be registered before the first view is drawn, or SwiftUI
    // keeps the fallback font for views it does not redraw.
    init() {
        Engine.registerFonts()
    }

    var body: some Scene {
        Window("MF-1", id: "main") {
            ContentView(model: model, finish: Finish.all.first { $0.name == finishName } ?? .black)
                .onAppear { delegate.model = model }
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1180, height: 1180 * PanelView.height / PanelView.width + 40)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Import SysEx…") { Engine.openImportPanel() }.keyboardShortcut("o")
                Menu("Export Bank") {
                    ForEach(0..<4) { b in
                        Button("Bank \(["A", "B", "C", "D"][b]) (\(b * 32 + 1)–\(b * 32 + 32))…") { Engine.exportBank(b) }
                    }
                }
                Button("Export Current Voice…") { Engine.exportVoice() }.keyboardShortcut("e")
                Divider()
                Button("Save (hold SAVE)") { model.save() }.keyboardShortcut("s")
            }
            CommandMenu("Device") {
                Picker("Finish", selection: $finishName) {
                    ForEach(Finish.all, id: \.name) { Text($0.name).tag($0.name) }
                }
                Toggle("FM-1 V15 Hardware Behaviour", isOn: $quirks)
                    .onChange(of: quirks) { _, v in mf1_global_set(Int32(MF1_G_QUIRKS), v ? 1 : 0) }
                Divider()
                Button("All Notes Off") { mf1_all_notes_off() }.keyboardShortcut(".", modifiers: .command)
                Button("Restore Factory Banks…") {
                    let a = NSAlert()
                    a.messageText = "Restore all 128 factory voices?"
                    a.informativeText = "This overwrites every preset, like the M-VAVE restore tool. Effects settings are reset."
                    a.addButton(withTitle: "Restore")
                    a.addButton(withTitle: "Cancel")
                    if a.runModal() == .alertFirstButtonReturn { Engine.loadFactoryBanks() }
                }
            }
            CommandGroup(replacing: .help) {
                Button("MF-1 Keyboard and Controls") { HelpWindow.show() }
            }
        }
    }
}

struct ContentView: View {
    @ObservedObject var model: DeviceModel
    var finish: Finish
    @State private var dropTarget = false

    var body: some View {
        ZStack {
            Color(hex: 0x151517).ignoresSafeArea()
            PanelView(model: model, finish: finish)
                .padding(EdgeInsets(top: 30, leading: 22, bottom: 22, trailing: 22))
            if dropTarget {
                RoundedRectangle(cornerRadius: 16).stroke(Color.accentColor, lineWidth: 4).padding(8)
            }
        }
        .onDrop(of: [.fileURL], isTargeted: $dropTarget) { providers in
            for p in providers {
                _ = p.loadObject(ofClass: URL.self) { url, _ in
                    guard let url else { return }
                    DispatchQueue.main.async { Engine.importSyx(url) }
                }
            }
            return true
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var model: DeviceModel? {
        didSet {
            startTimer()
            if let model {
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { Snapshot.runIfRequested(model: model) }
            }
        }
    }
    private var timer: Timer?
    private var monitor: Any?
    private var computerKeys: [UInt16: Int] = [:]

    func applicationDidFinishLaunching(_ notification: Notification) {
        Engine.start()
        let quirks = UserDefaults.standard.object(forKey: "quirks") as? Bool ?? true
        mf1_global_set(Int32(MF1_G_QUIRKS), quirks ? 1 : 0)
        monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp]) { [weak self] e in
            self?.handle(e) == true ? nil : e
        }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard mf1_dirty() else { return .terminateNow }
        // Like the hardware, edits that were not saved are lost at power off.
        let a = NSAlert()
        a.messageText = "Save changes to the current preset?"
        a.informativeText = "The FM-1 keeps edits only after SAVE. Unsaved edits are lost when it powers off."
        a.addButton(withTitle: "Save")
        a.addButton(withTitle: "Don't Save")
        a.addButton(withTitle: "Cancel")
        switch a.runModal() {
        case .alertFirstButtonReturn: mf1_save()
        case .alertThirdButtonReturn: return .terminateCancel
        default: break
        }
        return .terminateNow
    }

    // Key-ups are not delivered once another app is active.
    func applicationDidResignActive(_ notification: Notification) {
        computerKeys.removeAll()
        model?.releaseAllKeys()
    }

    func applicationWillTerminate(_ notification: Notification) {
        mf1_stop()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    private func startTimer() {
        guard timer == nil else { return }
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { [weak self] _ in
            self?.model?.poll()
        }
        RunLoop.main.add(timer!, forMode: .common)
    }

    // Mac keyboard as the keybed: A W S E D R F G Y H U J K O L P ; [ ' play
    // chromatically from F, Z and X are OCT- and OCT+.
    static let noteKeys: [Character: Int] = [
        "a": 0, "w": 1, "s": 2, "e": 3, "d": 4, "r": 5, "f": 6, "g": 7, "y": 8, "h": 9, "u": 10, "j": 11,
        "k": 12, "o": 13, "l": 14, "p": 15, ";": 16, "[": 17, "'": 18,
    ]

    private func handle(_ e: NSEvent) -> Bool {
        guard let model else { return false }
        let down = e.type == .keyDown
        // Key-ups always go through, so a key released with Command held
        // does not leave a note stuck.
        if !down, let k = computerKeys.removeValue(forKey: e.keyCode) {
            model.keyUp(k)
            return true
        }
        if e.modifierFlags.contains(.command) { return false }
        if NSApp.keyWindow?.firstResponder is NSTextView { return false }
        if model.renaming != nil, down, let c = e.characters?.first {
            if e.keyCode == 51 {
                model.renameBackspace()
            } else if e.keyCode == 36 {
                model.turnSelect(1)
            } else if e.keyCode == 53 {
                model.turnKnob(1, -1)
            } else if c.isASCII, !c.isNewline {
                model.typeRename(c)
            }
            return true
        }
        guard let ch = e.charactersIgnoringModifiers?.lowercased().first else { return false }
        if ch == "z" || ch == "x" {
            if e.isARepeat { return true }
            let dir = ch == "z" ? -1 : 1
            if down { model.octDown(dir) } else { model.octUp(dir) }
            return true
        }
        guard let semi = Self.noteKeys[ch] else { return false }
        if e.isARepeat { return true }
        // Semitone above F3 to the panel key that plays it.
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

enum HelpWindow {
    static func show() {
        let a = NSAlert()
        a.messageText = "MF-1 Controls"
        a.informativeText = """
        Knobs: drag up/down or use the scroll wheel. Hold Option for fine steps.
        Keys: click or drag across the keybed, or play the Mac keyboard: \
        A W S E D R F G Y H U J K O L P ; [ ' from F3. Z / X are OCT- / OCT+.
        SAVE: press and hold, as on the hardware (or Command-S).
        OCT- + OCT+ together reset octave and transpose. Hold OCT and turn ALGORITHM to transpose.
        EDIT: the upper keys pick OP1–OP6, PIT, GLO, MONO and POLY. Long-press an OP key to mute it.

        Patches: drag a .syx file onto the window, use File > Import, or send SysEx to the \
        MIDI port "\(Engine.portName)". Web librarians that look for an FM-1 port see this one. \
        A 32 voice bank asks which bank (A–D, turn KNOB1–4) to store it in.
        """
        a.runModal()
    }
}

// Development aid: `MF-1 --snapshot out.png [fx|env|arp|seq|lfo|edit|glo|alg|presets]`
// renders the panel offscreen and quits, for checking the look against photos.
enum Snapshot {
    static let active = CommandLine.arguments.contains("--snapshot")
    @MainActor static func runIfRequested(model: DeviceModel) {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count else { return }
        let path = args[i + 1]
        let what = i + 2 < args.count ? args[i + 2] : "home"
        switch what {
        case "fx": model.press("FX")
        case "env": model.mode = .env
        case "arp": model.mode = .arp
        case "seq": model.mode = .seq
        case "lfo": model.press("LFO")
        case "edit": model.press("EDIT")
        case "glo": model.press("GLO")
        case "alg": model.popup = .algorithm
        case "presets": model.popup = .presets
        default: break
        }
        let finish = Finish.all.first { args.contains($0.name.lowercased()) } ?? .black
        let view = PanelView(model: model, finish: finish).frame(width: 1615, height: 965).background(Color(hex: 0x151517))
        let r = ImageRenderer(content: view)
        r.scale = 1
        if let img = r.nsImage, let tiff = img.tiffRepresentation, let rep = NSBitmapImageRep(data: tiff),
           let png = rep.representation(using: .png, properties: [:]) {
            try? png.write(to: URL(fileURLWithPath: path))
        }
        exit(0)
    }
}
