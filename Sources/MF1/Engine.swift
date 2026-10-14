import AppKit
import CoreText
import Foundation
import MF1Core
import UniformTypeIdentifiers

// Swift side helpers around the C engine interface.
enum Engine {
    // The virtual MIDI port name contains "FM-1" so the existing web
    // librarians and editors, which match ports by that substring, find it.
    static let portName = "MF-1 (FM-1 Compatible)"

    static var stateDirectory: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("MF-1", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    static func start() {
        _ = mf1_start(stateDirectory.path, portName, UserDefaults.standard.string(forKey: "outputUID") ?? "")
        if mf1_needs_factory() { loadFactoryBanks() }
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
        save(Data(buf), name: "MF-1 Bank \(["A", "B", "C", "D"][bank]).syx")
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
