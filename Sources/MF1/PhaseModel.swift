import AppKit
import Foundation
import MF1Core
import PhaseCore
import SwiftUI

// Phase's side of the panel: what each knob, button and key does, the piece it all edits, and the phrase the keys
// write. The processes and the player are PhaseCore's; this file decides which number a turn changes, and when a
// phrase becomes the cell.

enum PH {
    static let fields = Int(PH_FIELDS)
    static let piece = Int(PH_PIECE), process = Int(PH_PROCESS), motion = Int(PH_MOTION), parts = Int(PH_PARTS)
    static let repeats = Int(PH_REPEATS), length = Int(PH_LENGTH), density = Int(PH_DENSITY)
    static let transpose = Int(PH_TRANSPOSE), gate = Int(PH_GATE), sound = Int(PH_SOUND), bright = Int(PH_BRIGHT)
    static let decay = Int(PH_DECAY), room = Int(PH_ROOM), tempo = Int(PH_TEMPO), echo = Int(PH_ECHO)
    static let echoSteps = Int(PH_ECHO_STEPS), spread = Int(PH_SPREAD)
    static let maxSteps = Int(PH_MAX_STEPS)
    static func range(_ field: Int) -> ClosedRange<Int> {
        var lo: Int32 = 0, hi: Int32 = 0
        phase_range(Int32(field), &lo, &hi)
        return Int(lo)...Int(hi)
    }
    static let processes = (0..<Int(PH_PROCESS_COUNT)).map { String(cString: phase_process_name(Int32($0))) }
    static let sounds = (0..<Int(PH_SOUND_COUNT)).map { String(cString: phase_sound_name(Int32($0))) }
    static let spreads = (0..<Int(PH_SPREAD_COUNT)).map { String(cString: phase_spread_name(Int32($0))) }
    // What the Motion knob is called for each process.
    static let motionNames = ["Drift", "Jump", "Offset", "Gap", "Swell", "Growth", "Drift", "Drift"]
}

struct PieceInfo {
    var name: String, title: String, blurb: String

    static let all: [PieceInfo] = (0..<Int(phase_piece_count())).map { i in
        var p = PhasePiece()
        _ = phase_piece(Int32(i), &p)
        func text<T>(_ t: inout T, _ n: Int) -> String {
            withUnsafePointer(to: &t) { $0.withMemoryRebound(to: CChar.self, capacity: n) { String(cString: $0) } }
        }
        return PieceInfo(name: text(&p.name, 11), title: text(&p.title, 20), blurb: text(&p.blurb, 40))
    }
}

// The four pages the knobs can be on.
enum PhasePage: Int, CaseIterable {
    case process, cell, sound, time
    var title: String { ["Process", "Cell", "Sound", "Time"][rawValue] }
}

struct PParam {
    var name: String
    var field: Int
    var format: (Int) -> String = { "\($0)" }
    var range: ClosedRange<Int> { PH.range(field) }
}

extension PhaseCell {
    var notes: [Int] {
        withUnsafeBytes(of: note) { raw in raw.bindMemory(to: Int8.self).prefix(Int(length)).map { Int($0) } }
    }

    init(_ notes: [Int]) {
        self.init()
        withUnsafeMutableBytes(of: &note) { raw in
            let n = raw.bindMemory(to: Int8.self)
            for i in 0..<n.count { n[i] = i < notes.count ? Int8(clamping: notes[i]) : -1 }
        }
        length = Int8(min(notes.count, PH.maxSteps))
    }
}

final class PhaseModel: ObservableObject {
    static let shared = PhaseModel()
    static let pieces = PieceInfo.all
    // How long the keys must rest before a phrase becomes the cell.
    static let phraseSeconds = 1.5

    @Published var page: PhasePage = .process
    @Published var pill: String? = nil
    @Published var octHeld: Set<Int> = []
    // Keys write a new cell; off, they play along over the process.
    @Published var writes = true
    @Published private(set) var playing = false

    // The piece being played, and one per piece, so each keeps how it was left, with its cell.
    private(set) var settings = [Int32](repeating: 0, count: PH.fields)
    private var table: [[Int32]] = []
    private var cells: [PhaseCell] = []
    var piece: Int { Int(settings[PH.piece]) }
    var info: PieceInfo { Self.pieces[piece] }
    var cell: PhaseCell { cells[piece] }

    // The phrase being written: each key's note, octave removed, and when it was pressed.
    private(set) var phrase: [(note: Int, time: Double)] = []
    var writing: Bool { !phrase.isEmpty }
    private(set) var lastPress = 0.0
    // A snapshot draws the process at this tick instead of the player's.
    var snapshotTick: Int64? = nil

    var tick = 0
    private(set) var led = LedState()
    private var heldKeys: [Int: Int] = [:]
    private var lastGeneration: UInt32 = 0
    private var lastLedSignature = 0
    private var lastVolume: Int32 = -1
    private var pillTimer: Timer?
    private var saveTask: DispatchWorkItem?
    private var transposeTouched = false
    private var masterLevel: Double?

    init() {
        table = (0..<Self.pieces.count).map { Self.recipe($0) }
        cells = (0..<Self.pieces.count).map { Self.writtenCell($0) }
        var state = Stored()
        if let loaded = Self.readStored() { state = loaded }
        for (i, s) in state.pieces.enumerated() where i < table.count && s.count == PH.fields {
            table[i] = s.map { Int32($0) }
        }
        for (i, c) in state.cells.enumerated() where i < cells.count && (2...PH.maxSteps).contains(c.count) {
            cells[i] = PhaseCell(c)
        }
        let p = max(0, min(table.count - 1, state.piece))
        settings = table[p]
        settings[PH.piece] = Int32(p)
        page = PhasePage(rawValue: state.page) ?? .process
        writes = state.writes
    }

    static func recipe(_ piece: Int) -> [Int32] {
        var s = [Int32](repeating: 0, count: PH.fields)
        phase_default_piece(Int32(piece), &s)
        return s
    }

    static func writtenCell(_ piece: Int) -> PhaseCell {
        var c = PhaseCell()
        phase_default_cell(Int32(piece), &c)
        return c
    }

    // After the engine has started: puts the sounds in the banks and loads the current piece, ready to play.
    func start() {
        phase_install()
        mf1_global_set(Int32(MF1_G_QUIRKS), 1)
        mf1_global_set(Int32(MF1_G_EXTENDED), 0)
        load()
    }

    private func load() {
        phase_clamp(&settings)
        phase_load(settings)
        table[piece] = settings
    }

    // The process the settings, the cell and the octave make.
    func process() -> PhaseProc {
        var p = PhaseProc()
        var c = cells[piece]
        phase_proc_make(settings, &c, &p)
        p.transpose += Int32(octave * 12)
        return p
    }

    // ---------------------------------------------------------------- playing

    func play() {
        guard !Snapshot.active else { return }
        var p = process()
        phase_start(&p, 0.03)
        playing = true
    }

    func stop() {
        phase_stop()
        playing = false
    }

    func togglePlay() {
        if playing {
            stop()
            showPill("Stopped")
        } else {
            play()
            showPill(info.title)
        }
    }

    // Back to the start of the process, all parts together.
    func restart() {
        play()
        showPill("From the beginning")
    }

    private func changeProcess() {
        guard playing else { return }
        var p = process()
        phase_change(&p)
    }

    // Shuts the player down for good, before the engine stops.
    func shutdown() {
        phase_shutdown()
        playing = false
    }

    // ---------------------------------------------------------------- the piece

    func value(_ field: Int) -> Int { Int(settings[field]) }

    func set(_ field: Int, _ v: Int) {
        let r = PH.range(field)
        let c = Int32(max(r.lowerBound, min(r.upperBound, v)))
        guard settings[field] != c else { return }
        settings[field] = c
        commit()
    }

    private func commit() {
        phase_clamp(&settings)
        table[piece] = settings
        phase_apply(settings)
        changeProcess()
        scheduleSave()
        objectWillChange.send()
    }

    func selectPiece(_ i: Int) {
        let n = Self.pieces.count
        let next = ((i % n) + n) % n
        guard next != piece else { return }
        let wasPlaying = playing
        stop()
        phrase.removeAll()
        table[piece] = settings
        settings = table[next]
        settings[PH.piece] = Int32(next)
        load()
        if wasPlaying { play() }
        scheduleSave()
        objectWillChange.send()
    }

    // Back to the piece as written: its settings and its cell.
    func resetPiece() {
        settings = Self.recipe(piece)
        cells[piece] = Self.writtenCell(piece)
        phrase.removeAll()
        load()
        if playing { play() }
        scheduleSave()
        showPill("\(info.title) as written")
        objectWillChange.send()
    }

    func resetAll() {
        let p = piece
        table = (0..<Self.pieces.count).map { Self.recipe($0) }
        cells = (0..<Self.pieces.count).map { Self.writtenCell($0) }
        settings = table[p]
        phrase.removeAll()
        load()
        if playing { play() }
        scheduleSave()
        showPill("Every piece as written")
        objectWillChange.send()
    }

    // ---------------------------------------------------------------- knob pages

    func motionText(_ v: Int) -> String {
        let p = value(PH.process)
        let n = Int(phase_motion_steps(Int32(p), Int32(v), Int32(value(PH.length))))
        switch p {
        case PH_PHASE, PH_RESULT, PH_TAPE: return "1/\(n)"
        case PH_SHIFT: return "+\(n)"
        case PH_BUILD, PH_CANON: return "−\(n)"
        default: return "\(n)"
        }
    }

    func params(_ p: PhasePage) -> [PParam] {
        switch p {
        case .process:
            return [PParam(name: "Process", field: PH.process, format: { PH.processes[max(0, min(PH.processes.count - 1, $0))] }),
                    PParam(name: PH.motionNames[max(0, min(7, value(PH.process)))], field: PH.motion, format: motionText),
                    PParam(name: "Parts", field: PH.parts),
                    PParam(name: "Repeats", field: PH.repeats, format: { "×\($0)" })]
        case .cell:
            return [PParam(name: "Length", field: PH.length),
                    PParam(name: "Density", field: PH.density, format: { "\($0)%" }),
                    PParam(name: "Transpose", field: PH.transpose, format: { $0 > 0 ? "+\($0)" : $0 < 0 ? "−\(-$0)" : "0" }),
                    PParam(name: "Gate", field: PH.gate, format: { "\($0)%" })]
        case .sound:
            return [PParam(name: "Sound", field: PH.sound, format: { PH.sounds[max(0, min(PH.sounds.count - 1, $0))] }),
                    PParam(name: "Bright", field: PH.bright), PParam(name: "Decay", field: PH.decay),
                    PParam(name: "Room", field: PH.room)]
        case .time:
            return [PParam(name: "Tempo", field: PH.tempo), PParam(name: "Echo", field: PH.echo),
                    PParam(name: "Echo Steps", field: PH.echoSteps),
                    PParam(name: "Span", field: PH.spread, format: { PH.spreads[max(0, min(PH.spreads.count - 1, $0))] })]
        }
    }

    func turnKnob(_ k: Int, _ delta: Int) {
        guard delta != 0 else { return }
        if mf1_bank_pending() {
            // A bank was sent to the port: KNOB1..4 choose where it goes, as on the hardware.
            if delta > 0 {
                mf1_bank_commit(Int32(k))
                showPill("Saved to Bank \(["A", "B", "C", "D"][k & 3])")
            }
            return
        }
        let ps = params(page)
        guard k < ps.count else { return }
        let p = ps[k]
        let step = p.range.count > 40 ? 2 : 1
        set(p.field, value(p.field) + delta * step)
        showPill(p.name, p.format(value(p.field)))
    }

    // A click on a chip on the screen: steps short lists round, longer ranges by five.
    func clickChip(_ k: Int, back: Bool) {
        let ps = params(page)
        guard k < ps.count else { return }
        let p = ps[k]
        let r = p.range
        let dir = back ? -1 : 1
        if r.count <= 16 {
            set(p.field, r.lowerBound + ((value(p.field) - r.lowerBound + dir) % r.count + r.count) % r.count)
        } else {
            set(p.field, value(p.field) + dir * 5)
        }
        showPill(p.name, p.format(value(p.field)))
    }

    func cycle(_ field: Int, _ name: String, _ format: (Int) -> String) {
        let r = PH.range(field)
        set(field, value(field) >= r.upperBound ? r.lowerBound : value(field) + 1)
        showPill(name, format(value(field)))
    }

    func showPill(_ name: String, _ value: String? = nil) {
        pill = value.map { "\(name): \($0)" } ?? name
        pillTimer?.invalidate()
        pillTimer = Timer.scheduledTimer(withTimeInterval: 1.2, repeats: false) { [weak self] _ in self?.pill = nil }
    }

    // ---------------------------------------------------------------- panel knobs

    func turnSelect(_ delta: Int) {
        guard delta != 0 else { return }
        page = PhasePage(rawValue: max(0, min(PhasePage.allCases.count - 1, page.rawValue + delta))) ?? page
        scheduleSave()
    }

    func turnPresets(_ delta: Int) {
        guard delta != 0 else { return }
        selectPiece(piece + (delta > 0 ? 1 : -1))
    }

    // ALGORITHM is the process.
    func turnAlgorithm(_ delta: Int) {
        guard delta != 0 else { return }
        set(PH.process, value(PH.process) + (delta > 0 ? 1 : -1))
        showPill("Process", PH.processes[value(PH.process)])
    }

    func turnMaster(by delta: Double) {
        let cur = Int(mf1_global_get(Int32(MF1_G_VOLUME)))
        var level = masterLevel ?? Double(cur)
        if Int(level.rounded()) != cur { level = Double(cur) }
        level = max(0, min(100, level + delta))
        masterLevel = level
        let value = Int(level.rounded())
        if value != cur { mf1_global_set(Int32(MF1_G_VOLUME), Int32(value)) }
        showPill("Volume", "\(value)")
    }

    // ---------------------------------------------------------------- buttons

    static let pageButtons: [String: PhasePage] = ["HOME": .process, "FX": .cell, "SEL": .sound, "ENV": .time]

    func buttonLegend(_ hw: String) -> String {
        ["HOME": "PROC", "FX": "CELL", "SEL": "SOUND", "ENV": "TIME", "LFO": "PARTS", "EDIT": "SPAN", "GLO": "BEGIN",
         "SAVE": "RESET", "ARP": "<", "SEQ": ">", "REC": "WRITE"][hw] ?? hw
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
        case "LFO": cycle(PH.parts, "Parts") { "\($0)" }
        case "EDIT": cycle(PH.spread, "Span") { PH.spreads[$0] }
        case "GLO": restart()
        case "PLAY": togglePlay()
        case "REC":
            writes.toggle()
            if !writes { phrase.removeAll() }
            scheduleSave()
            showPill(writes ? "Keys write the cell" : "Keys play along")
        case "SAVE": showPill("Hold RESET for the piece as written")
        default: break
        }
    }

    // The long press on RESET.
    func save() { resetPiece() }

    func buttonLit(_ b: String) -> Color? {
        if let p = Self.pageButtons[b] { return page == p ? .white : nil }
        switch b {
        case "PLAY":
            // Lit while playing, with a blink at the top of each repeat of part 1.
            guard playing else { return nil }
            return beatLit ? .white : Color.white.opacity(0.75)
        case "REC":
            guard writes else { return nil }
            if writing { return (tick / 8) % 2 == 0 ? .white : nil }
            return Color.white.opacity(0.85)
        case "OCT-": return octaveLit(-1)
        case "OCT+": return octaveLit(1)
        default: return nil
        }
    }

    private var beatLit = false

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

    // The octave moves the keys and the piece together.
    private func setOctave(_ o: Int) {
        mf1_global_set(Int32(MF1_G_OCTAVE), Int32(o))
        changeProcess()
    }

    func octDown(_ dir: Int) {
        octHeld.insert(dir)
        transposeTouched = false
        if octHeld.count == 2 {
            setOctave(0)
            mf1_global_set(Int32(MF1_G_TRANSPOSE), 0)
            transposeTouched = true
            showPill("Octave", "0")
        }
    }

    func octUp(_ dir: Int) {
        octHeld.remove(dir)
        if !transposeTouched && octHeld.isEmpty {
            let o = max(-3, min(3, octave + dir))
            setOctave(o)
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
        if writes {
            // The first key of a phrase stops the process, so the phrase is heard alone.
            if phrase.isEmpty && playing { stop() }
            if phrase.count < PH.maxSteps { phrase.append((note - octave * 12, now())) }
            lastPress = now()
            objectWillChange.send()
        }
        heldKeys[k] = note
        phase_key_down(Int32(note))
    }

    func keyUp(_ k: Int) {
        guard let note = heldKeys.removeValue(forKey: k) else { return }
        phase_key_up(Int32(note))
        // The wait for the phrase to end starts again when the last key lets go.
        if writing { lastPress = max(lastPress, now()) }
    }

    func isHeld(_ k: Int) -> Bool { heldKeys[k] != nil }

    func releaseAllKeys() {
        for k in Array(heldKeys.keys) { keyUp(k) }
    }

    func shownNote(key k: Int) -> Int { 53 + DeviceModel.semitone(forKey: k) + led.octave * 12 }

    func keyLed(_ k: Int) -> Double {
        let note = shownNote(key: k)
        guard (0...127).contains(note) else { return 0.05 }
        let lit = note < 64 ? (led.soundingLo >> UInt64(note)) & 1 : (led.soundingHi >> UInt64(note - 64)) & 1
        return lit != 0 || heldKeys[k] != nil ? 1 : 0.06
    }

    private func now() -> Double { ProcessInfo.processInfo.systemUptime }

    // How far the wait for the phrase to end has gone, 0 .. 1.
    var phraseWait: Double {
        guard writing, heldKeys.isEmpty else { return 0 }
        return max(0, min(1, (now() - lastPress) / Self.phraseSeconds))
    }

    // The phrase as a cell. The step is the time between most presses, a little under the middle one, and each gap
    // is that many steps, so a pause becomes a rest or two.
    static func cell(from phrase: [(note: Int, time: Double)]) -> [Int] {
        guard let first = phrase.first else { return [] }
        let gaps = zip(phrase.dropFirst(), phrase).map { $0.time - $1.time }
        let sorted = gaps.sorted()
        let unit = sorted.isEmpty ? 0.25 : max(0.06, min(1.0, sorted[(sorted.count - 1) * 3 / 10]))
        var notes = [first.note]
        for (i, g) in gaps.enumerated() {
            let steps = max(1, min(4, Int((g / unit).rounded())))
            for _ in 1..<steps where notes.count < PH.maxSteps { notes.append(-1) }
            if notes.count < PH.maxSteps { notes.append(phrase[i + 1].note) }
        }
        if notes.count < 2 { notes.append(-1) }
        return notes
    }

    // For snapshots: a phrase as if just played, with part of the wait gone.
    func snapshotPhrase(_ notes: [Int]) {
        let t = now()
        phrase = notes.enumerated().map { ($0.element, t - 1.2 + Double($0.offset) * 0.1) }
        lastPress = t - 0.5
    }

    func finishPhrase() {
        let notes = Self.cell(from: phrase)
        phrase.removeAll()
        guard !notes.isEmpty else { return }
        cells[piece] = PhaseCell(notes)
        settings[PH.length] = Int32(notes.count)
        settings[PH.density] = 100
        phase_clamp(&settings)
        table[piece] = settings
        phase_apply(settings)
        scheduleSave()
        play()
        showPill("New cell", "\(notes.filter { $0 >= 0 }.count) notes, \(notes.count) steps")
        objectWillChange.send()
    }

    // ---------------------------------------------------------------- polling

    private static let ledButtons = ["HOME", "FX", "SEL", "ENV", "PLAY", "REC", "OCT-", "OCT+"]

    // What the screen draws: the player's process while playing, else the process at its start.
    func view() -> PhaseView {
        var v = PhaseView()
        if playing, phase_now(&v) { return v }
        var p = process()
        phase_view(&p, snapshotTick ?? 0, &v)
        return v
    }

    func poll() {
        tick &+= 1
        if writing && heldKeys.isEmpty && now() - lastPress >= Self.phraseSeconds { finishPhrase() }
        led = LedState.read()
        if playing {
            var v = PhaseView()
            if phase_now(&v) { beatLit = v.part.0.pos_milli < 400 }
        }
        var h = Hasher()
        for b in Self.ledButtons { h.combine(buttonLit(b) != nil) }
        for b in Self.ledButtons { h.combine(buttonLit(b) == Color.white) }
        for k in 0..<27 { h.combine(keyLed(k)) }
        let sig = h.finalize()
        if sig != lastLedSignature {
            lastLedSignature = sig
            LedClock.shared.frame = tick
        }
        // The rings turn smoothly while playing; at rest the screen only changes when something else does.
        var redraw = (playing || writing) ? tick % 2 == 0 : tick % 15 == 0
        let g = mf1_generation()
        if g != lastGeneration {
            lastGeneration = g
            var buf = [CChar](repeating: 0, count: 64)
            mf1_take_message(&buf)
            let msg = String(cString: buf)
            if !msg.isEmpty { showPill(msg) }
            followOutsideChanges()
            let v = mf1_global_get(Int32(MF1_G_VOLUME))
            if v != lastVolume {
                lastVolume = v
                objectWillChange.send()
            }
            redraw = true
        }
        if redraw { LedClock.screen.frame = tick }
    }

    // A program change from a keyboard picks a piece; one past the last piece is put back.
    private func followOutsideChanges() {
        let p = Int(mf1_current_preset())
        guard p != piece, !mf1_bank_pending() else { return }
        if p < Self.pieces.count {
            selectPiece(p)
        } else {
            load()
        }
    }

    // ---------------------------------------------------------------- keeping the pieces

    private struct Stored: Codable {
        var version = 1
        var piece = 0
        var page = 0
        var writes = true
        var pieces: [[Int]] = []
        var cells: [[Int]] = []
    }

    private static var file: URL { Engine.stateDirectory.appendingPathComponent("pieces.json") }

    private static func readStored() -> Stored? {
        guard !Snapshot.active, let data = try? Data(contentsOf: file) else { return nil }
        if let s = try? JSONDecoder().decode(Stored.self, from: data), s.version == 1 { return s }
        // Unreadable: keep it aside rather than write over it.
        let aside = file.deletingLastPathComponent()
            .appendingPathComponent("pieces.json.unreadable-\(Int(Date().timeIntervalSince1970))")
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
        table[piece] = settings
        let s = Stored(version: 1, piece: piece, page: page.rawValue, writes: writes,
                       pieces: table.map { $0.map { Int($0) } }, cells: cells.map { $0.notes })
        if let data = try? JSONEncoder().encode(s) { try? data.write(to: Self.file, options: .atomic) }
    }
}

extension PhaseModel: ModeModel {
    var labels: PanelLabels {
        PanelLabels(select: "PAGE", presets: "PIECE", algorithm: "PROCESS")
    }
    func upperKeyLabel(_ i: Int) -> String? { nil }
    func screenView(frame: Int) -> AnyView { AnyView(PhaseScreen(model: self, frame: frame)) }
    func screenInput() -> AnyView { AnyView(PhaseScreenInput(model: self)) }
}
