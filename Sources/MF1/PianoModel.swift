import AppKit
import Foundation
import MF1Core
import PianoCore
import SwiftUI

// Piano's side of the panel: what each knob, button and key does, and the voicing it all edits. The sound is made
// in PianoCore. This file only decides which number a turn changes.

enum PF {
    static let fields = Int(PN_FIELDS)
    static let piano = Int(PN_F_PIANO), tone = Int(PN_F_TONE), sustain = Int(PN_F_SUSTAIN), room = Int(PN_F_ROOM)
    static let space = Int(PN_F_SPACE), volume = Int(PN_F_VOLUME), unison = Int(PN_F_UNISON), knock = Int(PN_F_KNOCK)
    static let stretch = Int(PN_F_STRETCH), damper = Int(PN_F_DAMPER), touch = Int(PN_F_TOUCH)
    static func range(_ field: Int) -> ClosedRange<Int> {
        var lo: Int32 = 0, hi: Int32 = 0
        piano_range(Int32(field), &lo, &hi)
        return Int(lo)...Int(hi)
    }
    static let spaces = ["Room", "Hall", "Plate"]
    static let names = (0..<Int(piano_count())).map { String(cString: piano_name(Int32($0))) }
    static let blurbs = (0..<Int(piano_count())).map { String(cString: piano_blurb(Int32($0))) }
}

// The two pages the knobs can be on.
enum PianoPage: Int, CaseIterable {
    case tone, strings
    var title: String { ["Tone", "Strings"][rawValue] }
}

struct PianoParam {
    var name: String
    var field: Int
    var format: (Int) -> String = { "\($0)" }
    var range: ClosedRange<Int> { PF.range(field) }
}

final class PianoModel: ObservableObject {
    static let shared = PianoModel()

    @Published var page: PianoPage = .tone
    @Published var pill: String? = nil
    @Published var octHeld: Set<Int> = []
    @Published private(set) var pedal = false

    // The voicing being played, and one per piano, so each keeps how it was left.
    private(set) var settings = [Int32](repeating: 0, count: PF.fields)
    private var table: [[Int32]] = []
    var piano: Int { Int(settings[PF.piano]) }

    var tick = 0
    private(set) var led = LedState()
    // Each string's sound now, for the screen.
    private(set) var levels = [Float](repeating: 0, count: 128)
    private var heldKeys: [Int: Int] = [:]
    private var lastGeneration: UInt32 = 0
    private var lastLedSignature = 0
    private var pillTimer: Timer?
    private var saveTask: DispatchWorkItem?
    private var transposeTouched = false
    private var masterLevel: Double?

    init() {
        table = (0..<PF.names.count).map { Self.recipe($0) }
        var state = Stored()
        if let loaded = Self.readStored() { state = loaded }
        for (i, s) in state.pianos.enumerated() where i < table.count && s.count == PF.fields {
            table[i] = s.map { Int32($0) }
        }
        let p = max(0, min(table.count - 1, state.piano))
        settings = table[p]
        settings[PF.piano] = Int32(p)
        page = PianoPage(rawValue: state.page) ?? .tone
    }

    static func recipe(_ i: Int) -> [Int32] {
        var s = [Int32](repeating: 0, count: PF.fields)
        piano_default(Int32(i), &s)
        return s
    }

    // After the engine has started: the piano takes over from the DX7 voices.
    func start() {
        // The FM-1's own line-out filter and velocity scaling belong to the stock firmware's path, not to this one.
        mf1_global_set(Int32(MF1_G_QUIRKS), 0)
        mf1_global_set(Int32(MF1_G_EXTENDED), 0)
        mf1_arp_enable(false)
        mf1_seq_enable(false)
        piano_install()
        load()
    }

    private func load() {
        piano_clamp(&settings)
        piano_apply(settings)
        table[piano] = settings
    }

    // ---------------------------------------------------------------- the piano

    func value(_ field: Int) -> Int { Int(settings[field]) }

    func set(_ field: Int, _ v: Int) {
        let r = PF.range(field)
        let c = Int32(max(r.lowerBound, min(r.upperBound, v)))
        guard settings[field] != c else { return }
        settings[field] = c
        piano_clamp(&settings)
        table[piano] = settings
        piano_apply(settings)
        scheduleSave()
        objectWillChange.send()
    }

    func selectPiano(_ i: Int) {
        let n = PF.names.count
        let next = ((i % n) + n) % n
        guard next != piano else { return }
        table[piano] = settings
        settings = table[next]
        settings[PF.piano] = Int32(next)
        load()
        scheduleSave()
        showPill(PF.names[next])
        objectWillChange.send()
    }

    func resetPiano() {
        settings = Self.recipe(piano)
        load()
        scheduleSave()
        showPill("\(PF.names[piano]) as voiced")
        objectWillChange.send()
    }

    func resetAll() {
        let p = piano
        table = (0..<PF.names.count).map { Self.recipe($0) }
        settings = table[p]
        load()
        scheduleSave()
        showPill("Every piano as voiced")
        objectWillChange.send()
    }

    func setPedal(_ down: Bool) {
        guard down != pedal else { return }
        pedal = down
        mf1_sustain(down)
        showPill("Pedal", down ? "Down" : "Up")
    }

    // ---------------------------------------------------------------- knob pages

    static func percent(_ v: Int) -> String { "\(v)%" }
    static func signed(_ v: Int) -> String { v > 50 ? "+\(v - 50)" : "\(v - 50)" }

    func params(_ p: PianoPage) -> [PianoParam] {
        switch p {
        case .tone:
            return [PianoParam(name: "Tone", field: PF.tone, format: Self.signed),
                    PianoParam(name: "Sustain", field: PF.sustain, format: Self.percent),
                    PianoParam(name: "Room", field: PF.room),
                    PianoParam(name: "Volume", field: PF.volume)]
        case .strings:
            return [PianoParam(name: "Unison", field: PF.unison, format: Self.percent),
                    PianoParam(name: "Knock", field: PF.knock, format: Self.percent),
                    PianoParam(name: "Stretch", field: PF.stretch, format: Self.percent),
                    PianoParam(name: "Damper", field: PF.damper, format: Self.percent)]
        }
    }

    func turnKnob(_ k: Int, _ delta: Int) {
        guard delta != 0 else { return }
        let ps = params(page)
        guard k < ps.count else { return }
        let p = ps[k]
        let step = p.range.count > 150 ? 5 : p.range.count > 12 ? 2 : 1
        set(p.field, value(p.field) + delta * step)
        showPill(p.name, p.format(value(p.field)))
    }

    // A click on a chip on the screen: steps by five, or a short list round.
    func clickChip(_ k: Int, back: Bool) {
        let ps = params(page)
        guard k < ps.count else { return }
        let p = ps[k]
        let r = p.range
        let dir = back ? -1 : 1
        if r.count <= 12 {
            set(p.field, r.lowerBound + ((value(p.field) - r.lowerBound + dir) % r.count + r.count) % r.count)
        } else {
            set(p.field, value(p.field) + dir * (r.count > 150 ? 10 : 5))
        }
        showPill(p.name, p.format(value(p.field)))
    }

    func showPill(_ name: String, _ value: String? = nil) {
        pill = value.map { "\(name): \($0)" } ?? name
        pillTimer?.invalidate()
        pillTimer = Timer.scheduledTimer(withTimeInterval: 1.2, repeats: false) { [weak self] _ in self?.pill = nil }
    }

    // ---------------------------------------------------------------- panel knobs

    func turnSelect(_ delta: Int) {
        guard delta != 0 else { return }
        page = PianoPage(rawValue: max(0, min(PianoPage.allCases.count - 1, page.rawValue + delta))) ?? page
        scheduleSave()
    }

    func turnPresets(_ delta: Int) {
        guard delta != 0 else { return }
        selectPiano(piano + (delta > 0 ? 1 : -1))
    }

    // ALGORITHM is the touch: how hard the panel's keys play, since they sense no velocity.
    func turnAlgorithm(_ delta: Int) {
        guard delta != 0 else { return }
        set(PF.touch, value(PF.touch) + delta * 3)
        showPill("Touch", touchName(value(PF.touch)))
    }

    func touchName(_ v: Int) -> String {
        let marks = [(16, "ppp"), (33, "pp"), (49, "p"), (64, "mp"), (80, "mf"), (96, "f"), (112, "ff")]
        return "\(v) \(marks.first(where: { v <= $0.0 })?.1 ?? "fff")"
    }

    func turnMaster(by delta: Double) {
        let cur = Int(mf1_global_get(Int32(MF1_G_VOLUME)))
        var level = masterLevel ?? Double(cur)
        if Int(level.rounded()) != cur { level = Double(cur) }
        level = max(0, min(100, level + delta))
        masterLevel = level
        let value = Int(level.rounded())
        if value != cur { mf1_global_set(Int32(MF1_G_VOLUME), Int32(value)) }
        showPill("Master", "\(value)")
    }

    // ---------------------------------------------------------------- buttons

    static let pageButtons: [String: PianoPage] = ["HOME": .tone, "FX": .strings]

    func buttonLegend(_ hw: String) -> String {
        ["HOME": "TONE", "FX": "STRING", "SEL": "SPACE", "ENV": "", "LFO": "", "EDIT": "", "GLO": "PEDAL",
         "SAVE": "RESET", "ARP": "<", "SEQ": ">", "PLAY/STOP": "", "REC": ""][hw] ?? hw
    }

    func press(_ button: String) {
        if mf1_bank_pending() && button == "HOME" {
            mf1_bank_cancel()
            return
        }
        if let p = Self.pageButtons[button] {
            page = p
            scheduleSave()
            return
        }
        switch button {
        case "ARP": turnPresets(-1)
        case "SEQ": turnPresets(1)
        case "SEL":
            set(PF.space, (value(PF.space) + 1) % PF.spaces.count)
            showPill("Space", PF.spaces[value(PF.space)])
        case "GLO": setPedal(!pedal)
        case "SAVE": showPill("Hold RESET for the piano as voiced")
        default: break
        }
    }

    // The long press on RESET.
    func save() { resetPiano() }

    func buttonLit(_ b: String) -> Color? {
        if let p = Self.pageButtons[b] { return page == p ? .white : nil }
        switch b {
        case "GLO": return pedal ? .white : nil
        case "OCT-": return octaveLit(-1)
        case "OCT+": return octaveLit(1)
        default: return nil
        }
    }

    private func octaveLit(_ side: Int) -> Color? {
        let o = led.octave
        guard o != 0, (o > 0) == (side > 0) else { return nil }
        switch abs(o) {
        case 1: return (tick / 30) % 2 == 0 ? .white : nil
        case 2: return (tick / 8) % 2 == 0 ? .white : nil
        default: return .white
        }
    }

    var octave: Int { Int(mf1_global_get(Int32(MF1_G_OCTAVE))) }

    func octDown(_ dir: Int) {
        octHeld.insert(dir)
        transposeTouched = false
        if octHeld.count == 2 {
            mf1_global_set(Int32(MF1_G_OCTAVE), 0)
            mf1_global_set(Int32(MF1_G_TRANSPOSE), 0)
            transposeTouched = true
            showPill("Octave", "0")
        }
    }

    func octUp(_ dir: Int) {
        octHeld.remove(dir)
        if !transposeTouched && octHeld.isEmpty {
            let o = max(-3, min(3, octave + dir))
            mf1_global_set(Int32(MF1_G_OCTAVE), Int32(o))
            showPill("Octave", o > 0 ? "+\(o)" : "\(o)")
        }
        if octHeld.isEmpty { transposeTouched = false }
    }

    func releaseOct() {
        octHeld.removeAll()
        transposeTouched = false
    }

    // ---------------------------------------------------------------- keys

    func noteFor(key k: Int) -> Int { 53 + DeviceModel.semitone(forKey: k) + octave * 12 }

    func keyDown(_ k: Int) {
        guard heldKeys[k] == nil else { return }
        let note = noteFor(key: k)
        guard (0...127).contains(note) else { return }
        heldKeys[k] = note
        mf1_key_down_velocity(Int32(note), Int32(value(PF.touch)))
    }

    func keyUp(_ k: Int) {
        guard let note = heldKeys.removeValue(forKey: k) else { return }
        mf1_key_up(Int32(note))
    }

    func isHeld(_ k: Int) -> Bool { heldKeys[k] != nil }

    func releaseAllKeys() {
        for k in Array(heldKeys.keys) { keyUp(k) }
    }

    func shownNote(key k: Int) -> Int { 53 + DeviceModel.semitone(forKey: k) + led.octave * 12 }

    func keyLed(_ k: Int) -> Double {
        let note = shownNote(key: k)
        guard (0...127).contains(note) else { return 0.05 }
        if heldKeys[k] != nil { return 1 }
        return Double(max(0.06, min(1, ringing(note))))
    }

    // A string's sound on a 0 to 1 scale, about 50 dB from barely audible to ff.
    func ringing(_ note: Int) -> Float {
        let a = levels[note]
        guard a > 1e-5 else { return 0 }
        return max(0, min(1, (20 * log10(a) + 56) / 50))
    }

    // ---------------------------------------------------------------- polling

    private static let ledButtons = ["HOME", "FX", "GLO", "OCT-", "OCT+"]

    func poll() {
        tick &+= 1
        led = LedState.read()
        piano_levels(&levels)
        var h = Hasher()
        for b in Self.ledButtons { h.combine(buttonLit(b) != nil) }
        for k in 0..<27 { h.combine(Int(keyLed(k) * 8)) }
        let sig = h.finalize()
        if sig != lastLedSignature {
            lastLedSignature = sig
            LedClock.shared.frame = tick
        }
        // The strings glow as they ring, so the screen moves while any sounds.
        var redraw = tick % 2 == 0 && levels.contains(where: { $0 > 1e-5 })
        if tick % 30 == 0 { redraw = true }
        let g = mf1_generation()
        if g != lastGeneration {
            lastGeneration = g
            var buf = [CChar](repeating: 0, count: 64)
            mf1_take_message(&buf)
            let msg = String(cString: buf)
            if !msg.isEmpty { showPill(msg) }
            redraw = true
        }
        if redraw { LedClock.screen.frame = tick }
    }

    // ---------------------------------------------------------------- keeping the pianos

    private struct Stored: Codable {
        var version = 2
        var piano = 0
        var page = 0
        var pianos: [[Int]] = []
    }

    private static var file: URL { Engine.stateDirectory.appendingPathComponent("pianos.json") }

    private static func readStored() -> Stored? {
        guard let data = try? Data(contentsOf: file) else { return nil }
        if let s = try? JSONDecoder().decode(Stored.self, from: data), s.version == 2 { return s }
        // Unreadable: keep it aside rather than write over it.
        let aside = file.deletingLastPathComponent()
            .appendingPathComponent("pianos.json.unreadable-\(Int(Date().timeIntervalSince1970))")
        try? FileManager.default.moveItem(at: file, to: aside)
        return nil
    }

    private func scheduleSave() {
        guard !Snapshot.active else { return }
        saveTask?.cancel()
        let t = DispatchWorkItem { [weak self] in self?.writeNow() }
        saveTask = t
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4, execute: t)
    }

    func writeNow() {
        guard !Snapshot.active else { return }
        table[piano] = settings
        let s = Stored(version: 2, piano: piano, page: page.rawValue, pianos: table.map { $0.map { Int($0) } })
        if let data = try? JSONEncoder().encode(s) { try? data.write(to: Self.file, options: .atomic) }
    }
}

extension PianoModel: ModeModel {
    var labels: PanelLabels {
        PanelLabels(select: "PAGE", presets: "PIANO", algorithm: "TOUCH")
    }
    func upperKeyLabel(_ i: Int) -> String? { nil }
    func screenView(frame: Int) -> AnyView { AnyView(PianoScreen(model: self, frame: frame)) }
    func screenInput() -> AnyView { AnyView(PianoScreenInput(model: self)) }
}
