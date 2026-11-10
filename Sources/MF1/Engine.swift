import AppKit
import CoreAudio
import CoreText
import Foundation
import MF1Core
import UniformTypeIdentifiers

// Swift side helpers around the C engine interface.
enum Engine {
    // The virtual MIDI port name contains "FM-1" so the existing web
    // librarians and editors, which match ports by that substring, find it.
    static var portName: String { "\(name) (FM-1 Compatible)" }

    // Each window is an app of its own, because the engine is one per process. A second window of the same mode
    // is numbered ("MF-1 2"), with its own saved state and MIDI port, so the two never write the same files.
    static var name: String { slot == 1 ? mode.title : "\(mode.title) \(slot)" }

    static let slot: Int = {
        // Snapshots and self checks run beside an open app and use the first slot's state without claiming it.
        let args = CommandLine.arguments
        if args.contains("--snapshot") || args.contains("--selfcheck") { return 1 }
        for n in 1...9 where claim(n) { return n }
        return 1
    }()

    private static var claimed: Int32 = -1

    // A lock held for the app's life marks a slot as taken. The system lets it go when the app quits or crashes.
    private static func claim(_ n: Int) -> Bool {
        let dir = directory(slot: n)
        let fresh = !FileManager.default.fileExists(atPath: dir.path)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let fd = open(dir.appendingPathComponent(".open").path, O_RDWR | O_CREAT, 0o644)
        guard fd >= 0 else { return false }
        // Other windows look at the lock for a moment once a second, so a busy lock is tried again before the
        // slot counts as taken.
        var held = false
        for _ in 0..<3 {
            if flock(fd, LOCK_EX | LOCK_NB) == 0 { held = true; break }
            usleep(5000)
        }
        guard held else {
            close(fd)
            return false
        }
        claimed = fd
        // A new window starts from what the first one last saved.
        if fresh && n > 1 { copySavedState(from: directory(slot: 1), to: dir) }
        return true
    }

    // How many windows are open, in every mode: the slots whose lock is held, this one's included.
    static func openWindows() -> Int {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        var n = 0
        for m in Mode.allCases {
            for s in 1...9 {
                let path = base.appendingPathComponent(s == 1 ? m.title : "\(m.title) \(s)")
                    .appendingPathComponent(".open").path
                let fd = open(path, O_RDONLY)
                guard fd >= 0 else { continue }
                if flock(fd, LOCK_SH | LOCK_NB) != 0 { n += 1 } else { flock(fd, LOCK_UN) }
                close(fd)
            }
        }
        return n
    }

    private static func copySavedState(from src: URL, to dst: URL) {
        let fm = FileManager.default
        guard let files = try? fm.contentsOfDirectory(at: src, includingPropertiesForKeys: nil) else { return }
        for f in files where f.lastPathComponent != ".open" && f.lastPathComponent != "Songs" {
            try? fm.copyItem(at: f, to: dst.appendingPathComponent(f.lastPathComponent))
        }
    }

    private static func directory(slot n: Int) -> URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return base.appendingPathComponent(n == 1 ? mode.title : "\(mode.title) \(n)", isDirectory: true)
    }

    // Opens another window: a new instance of the app in the given mode.
    static func openWindow(_ m: Mode) {
        let app = Bundle.main.bundleURL
        guard app.pathExtension == "app" else {
            alert("New windows open only from the built app.")
            return
        }
        let config = NSWorkspace.OpenConfiguration()
        config.createsNewApplicationInstance = true
        config.arguments = ["--\(m.rawValue)"]
        NSWorkspace.shared.openApplication(at: app, configuration: config) { _, error in
            if let error { DispatchQueue.main.async { alert(error.localizedDescription) } }
        }
    }

    // One app, six faces: the MF-1 emulator, iScream, Almanac, Woof, Phase and Piano. The mode is picked at launch,
    // because each has its own engine state and MIDI port. Snapshots render MF-1 unless --iscream,
    // --almanac, --woof, --phase or --piano is given, whatever mode was last used.
    enum Mode: String, CaseIterable {
        case mf1, iscream, almanac, woof, phase, piano
        var title: String { ["MF-1", "iScream", "Almanac", "Woof", "Phase", "Piano"][Self.allCases.firstIndex(of: self)!] }
    }

    static let mode: Mode = {
        let args = CommandLine.arguments
        if args.contains("--mf1") { return .mf1 }
        if args.contains("--iscream") { return .iscream }
        if args.contains("--almanac") { return .almanac }
        if args.contains("--woof") { return .woof }
        if args.contains("--phase") { return .phase }
        if args.contains("--piano") { return .piano }
        if args.contains("--snapshot") { return .mf1 }
        let d = UserDefaults.standard
        if let saved = d.string(forKey: "appMode"), let m = Mode(rawValue: saved) { return m }
        return d.bool(forKey: "iscreamMode") ? .iscream : .mf1
    }()

    // Saves the mode and relaunches the app in it.
    static func switchMode(to next: Mode) {
        guard next != mode else { return }
        UserDefaults.standard.set(next.rawValue, forKey: "appMode")
        UserDefaults.standard.set(next == .iscream, forKey: "iscreamMode")
        let app = Bundle.main.bundleURL
        guard app.pathExtension == "app" else {
            alert("The mode changes the next time MF-1 starts.")
            return
        }
        let config = NSWorkspace.OpenConfiguration()
        config.createsNewApplicationInstance = true
        NSWorkspace.shared.openApplication(at: app, configuration: config) { _, error in
            DispatchQueue.main.async {
                if let error { alert(error.localizedDescription) } else { NSApp.terminate(nil) }
            }
        }
    }

    static var stateDirectory: URL {
        let dir = directory(slot: slot)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    // What every window of a mode shares: the user's song files.
    static var sharedDirectory: URL {
        let dir = directory(slot: 1)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    static func start() {
        _ = mf1_start(stateDirectory.path, portName, UserDefaults.standard.string(forKey: "outputUID") ?? "")
        if mf1_needs_factory() { loadFactoryBanks() }
    }

    // The modes' banks are their own sounds, not the factory voices.
    static func startMode(_ install: () -> Void) {
        _ = mf1_start(stateDirectory.path, portName, UserDefaults.standard.string(forKey: "outputUID") ?? "")
        install()
    }

    static func loadFactoryBanks() {
        guard let dir = Bundle.module.url(forResource: "Banks", withExtension: nil) else { return }
        for b in 0..<4 {
            let url = dir.appendingPathComponent("FM-1_factory_bank\(b + 1).syx")
            guard let data = try? Data(contentsOf: url) else { continue }
            data.withUnsafeBytes { raw in
                _ = mf1_import_factory_bank(Int32(b), raw.bindMemory(to: UInt8.self).baseAddress, data.count)
            }
        }
    }

    static func registerFonts() {
        guard let dir = Bundle.module.url(forResource: "Fonts", withExtension: nil),
              let files = try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)
        else { return }
        for f in files where f.pathExtension == "ttf" {
            CTFontManagerRegisterFontsForURL(f as CFURL, .process, nil)
        }
    }

    // Output devices as (name, UID).
    static func outputs() -> [(name: String, uid: String)] {
        (0..<Int(mf1_output_count())).map { i in
            var name = [CChar](repeating: 0, count: 256), uid = [CChar](repeating: 0, count: 256)
            mf1_output_info(Int32(i), &name, &uid)
            return (String(cString: name), String(cString: uid))
        }
    }

    static func bankTitle(_ bank: Int) -> String {
        bank == 4 ? "Bank E (Extended)" : "Bank \(["A", "B", "C", "D"][max(0, min(3, bank))])"
    }

    static func editName() -> String {
        var buf = [CChar](repeating: 0, count: 11)
        mf1_edit_name(&buf)
        return String(cString: buf).trimmingCharacters(in: .whitespaces)
    }

    static func presetName(_ i: Int) -> String {
        var buf = [CChar](repeating: 0, count: 11)
        mf1_preset_name(Int32(i), &buf)
        return String(cString: buf).trimmingCharacters(in: .whitespaces)
    }

    static func pendingName(_ i: Int) -> String {
        var buf = [CChar](repeating: 0, count: 11)
        mf1_bank_pending_name(Int32(i), &buf)
        return String(cString: buf).trimmingCharacters(in: .whitespaces)
    }

    // ---------------------------------------------------------------- files

    @discardableResult
    static func importSyx(_ url: URL) -> Int {
        guard let data = try? Data(contentsOf: url) else { return 0 }
        return data.withUnsafeBytes { raw in
            Int(mf1_import_syx(raw.bindMemory(to: UInt8.self).baseAddress, data.count))
        }
    }

    static func openImportPanel() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "syx") ?? .data, .data]
        panel.allowsMultipleSelection = false
        panel.message = "Choose a DX7 SysEx file: a 32 voice bank or a single voice"
        if panel.runModal() == .OK, let url = panel.url {
            if importSyx(url) == 0 { alert("Not a DX7 voice or bank file.") }
        }
    }

    static func exportBank(_ bank: Int) {
        var buf = [UInt8](repeating: 0, count: 4104)
        mf1_export_bank(Int32(bank), &buf)
        save(Data(buf), name: "MF-1 \(bankTitle(bank)).syx")
    }

    static func exportVoice() {
        var buf = [UInt8](repeating: 0, count: 163)
        mf1_export_voice(&buf)
        save(Data(buf), name: "\(editName()).syx")
    }

    private static func save(_ data: Data, name: String) {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "syx") ?? .data]
        panel.nameFieldStringValue = name
        if panel.runModal() == .OK, let url = panel.url {
            do { try data.write(to: url) } catch { alert(error.localizedDescription) }
        }
    }

    static func alert(_ text: String) {
        let a = NSAlert()
        a.messageText = text
        a.runModal()
    }
}

// The output device list, kept current as devices are plugged in or removed.
final class AudioOutputs: ObservableObject {
    static let shared = AudioOutputs()
    struct Device: Equatable {
        var name: String
        var uid: String
    }
    @Published private(set) var devices: [Device] = []

    private init() {
        reload()
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &address, .main) { [weak self] _, _ in
            self?.reload()
        }
    }

    private func reload() {
        let list = Engine.outputs().map { Device(name: $0.name, uid: $0.uid) }
        if list != devices { devices = list }
    }
}

// The Songs menu: the built-in songs, then MIDI files in the Songs folder.
// A MIDI file named like a built-in song replaces its notes but keeps its
// voice and effects; other files play with the current voice.
final class SongLibrary: ObservableObject {
    static let shared = SongLibrary()

    struct Entry: Identifiable {
        var name: String
        var builtIn: Int?   // index of the built-in song whose voice it uses
        var file: URL?      // MIDI file with the notes, if any
        var id: String { name }
    }

    @Published private(set) var songs: [Entry] = []

    static var folder: URL {
        let dir = Engine.sharedDirectory.appendingPathComponent("Songs", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    private init() {
        refresh()
        NotificationCenter.default.addObserver(forName: NSApplication.didBecomeActiveNotification, object: nil,
                                               queue: .main) { [weak self] _ in self?.refresh() }
    }

    func refresh() {
        let files = ((try? FileManager.default.contentsOfDirectory(at: Self.folder, includingPropertiesForKeys: nil)) ?? [])
            .filter { ["mid", "midi"].contains($0.pathExtension.lowercased()) }
            .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
        var byName: [String: URL] = [:]
        for f in files { byName[f.deletingPathExtension().lastPathComponent.lowercased()] = f }
        var list: [Entry] = []
        for i in 0..<Int(mf1_song_count()) {
            let name = String(cString: mf1_song_name(Int32(i)))
            list.append(Entry(name: name, builtIn: i, file: byName.removeValue(forKey: name.lowercased())))
        }
        for f in files where byName[f.deletingPathExtension().lastPathComponent.lowercased()] != nil {
            list.append(Entry(name: f.deletingPathExtension().lastPathComponent, builtIn: nil, file: f))
        }
        if list.map(\.name) != songs.map(\.name) || list.map(\.file) != songs.map(\.file) { songs = list }
    }

    // Returns false if a MIDI file could not be read.
    @discardableResult
    func play(_ song: Entry) -> Bool {
        guard let file = song.file else {
            if let i = song.builtIn { mf1_song_play(Int32(i)) }
            return true
        }
        guard let data = try? Data(contentsOf: file) else { return false }
        return data.withUnsafeBytes { raw in
            mf1_song_play_midi(raw.bindMemory(to: UInt8.self).baseAddress, data.count, Int32(song.builtIn ?? -1))
        }
    }

    func addFromPanel() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "mid") ?? .midi, .midi]
        panel.allowsMultipleSelection = true
        panel.message = "Choose MIDI files to add to the Songs menu"
        guard panel.runModal() == .OK else { return }
        for url in panel.urls {
            let dest = Self.folder.appendingPathComponent(url.lastPathComponent)
            try? FileManager.default.removeItem(at: dest)
            do { try FileManager.default.copyItem(at: url, to: dest) } catch { Engine.alert(error.localizedDescription) }
        }
        refresh()
    }
}
