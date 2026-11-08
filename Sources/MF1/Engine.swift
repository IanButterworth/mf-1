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
    static var portName: String { "\(mode.title) (FM-1 Compatible)" }

    // One app, four faces: the MF-1 emulator, iScream, Almanac and Woof. The mode is picked at launch,
    // because each has its own engine state and MIDI port. Snapshots render MF-1 unless --iscream,
    // --almanac or --woof is given, whatever mode was last used.
    enum Mode: String, CaseIterable {
        case mf1, iscream, almanac, woof
        var title: String { ["MF-1", "iScream", "Almanac", "Woof"][Self.allCases.firstIndex(of: self)!] }
    }

    static let mode: Mode = {
        let args = CommandLine.arguments
        if args.contains("--iscream") { return .iscream }
        if args.contains("--almanac") { return .almanac }
        if args.contains("--woof") { return .woof }
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
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent(mode.title, isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    static func start() {
        _ = mf1_start(stateDirectory.path, portName, UserDefaults.standard.string(forKey: "outputUID") ?? "")
        if mf1_needs_factory() { loadFactoryBanks() }
    }

    // iScream's, Almanac's and Woof's banks are their own sounds, not the factory voices.
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
        let dir = Engine.stateDirectory.appendingPathComponent("Songs", isDirectory: true)
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
