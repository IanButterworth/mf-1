import AppKit
import ArpeggiCore
import Foundation
import MF1Core
import SwiftUI

// Arpeggi's side of the panel: what each knob, button and key does, and the scene it all edits. The pool, the
// lanes and the player are ArpeggiCore's; this file decides which number a turn changes.

enum AP {
    static let fields = Int(AP_FIELDS)
    static let scene = Int(AP_SCENE), relation = Int(AP_RELATION), duo = Int(AP_DUO)
    static let aShape = Int(AP_A_SHAPE), aRate = Int(AP_A_RATE), aRhythm = Int(AP_A_RHYTHM), aGate = Int(AP_A_GATE)
    static let bShape = Int(AP_B_SHAPE), bRate = Int(AP_B_RATE), bRhythm = Int(AP_B_RHYTHM), bGate = Int(AP_B_GATE)
    static let hold = Int(AP_HOLD_MODE), ring = Int(AP_RING_SIZE), span = Int(AP_SPAN), pitch = Int(AP_B_PITCH)
    static let sound = Int(AP_SOUND), bright = Int(AP_BRIGHT), decay = Int(AP_DECAY), room = Int(AP_ROOM)
    static let tempo = Int(AP_TEMPO), swing = Int(AP_SWING), echo = Int(AP_ECHO), echoSteps = Int(AP_ECHO_STEPS)
    static let maxPool = Int(AP_MAX_POOL)
    static func range(_ field: Int) -> ClosedRange<Int> {
        var lo: Int32 = 0, hi: Int32 = 0
        arpeggi_range(Int32(field), &lo, &hi)
        return Int(lo)...Int(hi)
    }
    private static func names(_ n: Int, _ f: (Int32) -> UnsafePointer<CChar>?) -> [String] {
        (0..<n).map { String(cString: f(Int32($0))!) }
    }
    static let shapes = names(AP_SHAPE_COUNT, arpeggi_shape_name)
    static let rates = names(AP_RATE_COUNT, arpeggi_rate_name)
    static let rhythms = names(AP_RHYTHM_COUNT, arpeggi_rhythm_name)
    static let relations = names(AP_RELATION_COUNT, arpeggi_relation_name)
    static let holds = names(AP_HOLD_COUNT, arpeggi_hold_name)
    static let sounds = names(AP_SOUND_COUNT, arpeggi_sound_name)
    static func pick(_ list: [String], _ i: Int) -> String { list[max(0, min(list.count - 1, i))] }
    static let noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    static func noteName(_ n: Int) -> String { "\(noteNames[((n % 12) + 12) % 12])\(n / 12 - 1)" }
}

struct SceneInfo {
    var name: String, title: String, blurb: String

    static let all: [SceneInfo] = (0..<Int(arpeggi_scene_count())).map { i in
        var c = ApScene()
        _ = arpeggi_scene(Int32(i), &c)
        func text<T>(_ t: inout T, _ n: Int) -> String {
            withUnsafePointer(to: &t) { $0.withMemoryRebound(to: CChar.self, capacity: n) { String(cString: $0) } }
        }
        return SceneInfo(name: text(&c.name, 11), title: text(&c.title, 20), blurb: text(&c.blurb, 48))
    }
}

// The five pages the knobs can be on.
enum ArpPage: Int, CaseIterable {
    case a, b, notes, sound, time
    var title: String { ["A", "B", "Notes", "Sound", "Time"][rawValue] }
}

struct ArpParam {
    var name: String
    var field: Int
    var format: (Int) -> String = { "\($0)" }
    var range: ClosedRange<Int> { AP.range(field) }
}

final class ArpeggiModel: ObservableObject {
    static let shared = ArpeggiModel()
    static let scenes = SceneInfo.all

    @Published var page: ArpPage = .a
    @Published var pill: String? = nil
    @Published var octHeld: Set<Int> = []
    // Keys feed the pool; off, they play along over the lanes.
    @Published var writes = true
    @Published private(set) var playing = false

    // The scene being played, and one per scene, so each keeps how it was left. The pool is not a scene's: it
    // stays when the scene changes, so the same notes can be heard through every scene.
    private(set) var settings = [Int32](repeating: 0, count: AP.fields)
    private var table: [[Int32]] = []
    var scene: Int { Int(settings[AP.scene]) }
    var info: SceneInfo { Self.scenes[scene] }

    // A snapshot draws the lanes this far in, with the scene's seed, instead of the player's.
    var snapshotTick: Int64? = nil

    var tick = 0
    private(set) var led = LedState()
    // Each key's note: the pool's (octave removed) when it writes, the sounding note when it plays along.
    private var heldKeys: [Int: (note: Int, writes: Bool)] = [:]
    private var lastGeneration: UInt32 = 0
    private var lastLedSignature = 0
    private var lastVolume: Int32 = -1
    private var pillTimer: Timer?
    private var saveTask: DispatchWorkItem?
    private var transposeTouched = false
    private var masterLevel: Double?
    private var lastPoolSignature = 0

    init() {
        table = (0..<Self.scenes.count).map { Self.recipe($0) }
        var state = Stored()
        if let loaded = Self.readStored() { state = loaded }
        for (i, s) in state.scenes.enumerated() where i < table.count && s.count == AP.fields {
            table[i] = s.map { Int32($0) }
        }
        let c = max(0, min(table.count - 1, state.scene))
        settings = table[c]
        settings[AP.scene] = Int32(c)
        page = ArpPage(rawValue: state.page) ?? .a
        writes = state.writes
    }

    static func recipe(_ scene: Int) -> [Int32] {
        var s = [Int32](repeating: 0, count: AP.fields)
        arpeggi_default_scene(Int32(scene), &s)
        return s
    }

    static func seed(_ scene: Int) -> [Int8] {
        var n = [Int8](repeating: -1, count: AP.maxPool)
        let k = Int(arpeggi_default_seed(Int32(scene), &n))
        return Array(n.prefix(k))
    }

    // After the engine has started: puts the sounds in the banks and loads the current scene, ready to play.
    func start() {
        arpeggi_install()
        mf1_global_set(Int32(MF1_G_QUIRKS), 1)
        mf1_global_set(Int32(MF1_G_EXTENDED), 0)
        load()
    }

    private func load() {
        arpeggi_clamp(&settings)
        arpeggi_load(settings)
        table[scene] = settings
        sendProc()
    }

    // The lanes as the settings and the octave make them.
    func proc() -> ApProc {
        var p = ApProc()
        ap_proc_make(settings, &p)
        p.transpose = Int32(octave * 12)
        return p
    }

    private func sendProc() {
        guard !Snapshot.active else { return }
        var p = proc()
        arpeggi_set(&p)
        arpeggi_pool_mode(settings[AP.hold], settings[AP.ring])
    }

    // ---------------------------------------------------------------- playing

    // With an empty pool, PLAY starts the scene with its own chord.
    func play() {
        guard !Snapshot.active else { return }
        sendProc()
        if !arpeggi_start() {
            var s = Self.seed(scene)
            arpeggi_seed(&s, Int32(s.count))
        }
        playing = arpeggi_playing()
    }

    func stop() {
        arpeggi_stop()
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

    func clear() {
        arpeggi_clear()
        playing = false
        showPill("Notes cleared")
        objectWillChange.send()
    }

    // Shuts the player down for good, before the engine stops.
    func shutdown() {
        arpeggi_shutdown()
        playing = false
    }

    func pool() -> ApPool {
        var p = ApPool()
        if Snapshot.active || snapshotTick != nil {
            ap_pool_init(&p, settings[AP.hold], settings[AP.ring])
            var s = Self.seed(scene)
            ap_pool_seed(&p, &s, Int32(s.count))
            return p
        }
        arpeggi_pool(&p)
        return p
    }

    func poolNotes(_ p: ApPool) -> [Int] {
        withUnsafeBytes(of: p.note) { Array($0.bindMemory(to: Int8.self).prefix(Int(p.count))).map { Int($0) } }
    }

    // ---------------------------------------------------------------- the scene

    func value(_ field: Int) -> Int { Int(settings[field]) }

    func set(_ field: Int, _ v: Int) {
        let r = AP.range(field)
        let c = Int32(max(r.lowerBound, min(r.upperBound, v)))
        guard settings[field] != c else { return }
        settings[field] = c
        commit()
    }

    private func commit() {
        arpeggi_clamp(&settings)
        table[scene] = settings
        arpeggi_apply(settings)
        sendProc()
        scheduleSave()
        objectWillChange.send()
    }

    func selectScene(_ i: Int) {
        let n = Self.scenes.count
        let next = ((i % n) + n) % n
        guard next != scene else { return }
        table[scene] = settings
        settings = table[next]
        settings[AP.scene] = Int32(next)
        load()
        scheduleSave()
        objectWillChange.send()
    }

    func resetScene() {
        settings = Self.recipe(scene)
        load()
        scheduleSave()
        showPill("\(info.title) as written")
        objectWillChange.send()
    }

    func resetAll() {
        let c = scene
        table = (0..<Self.scenes.count).map { Self.recipe($0) }
        settings = table[c]
        load()
        scheduleSave()
        showPill("Every scene as written")
        objectWillChange.send()
    }

    // ---------------------------------------------------------------- knob pages

    // Whether a value does anything in the scene as set: B's settings without B, B's own time in Hocket and
    // Canon, B's shape in Canon and Mirror, the ring outside Ring.
    func applies(_ field: Int) -> Bool {
        let duo = value(AP.duo) != 0, rel = value(AP.relation)
        switch field {
        case AP.bShape: return duo && rel != Int(AP_CANON) && rel != Int(AP_MIRROR)
        case AP.bRate, AP.bRhythm: return duo && rel != Int(AP_HOCKET) && rel != Int(AP_CANON)
        case AP.bGate, AP.pitch: return duo
        case AP.ring: return value(AP.hold) == Int(AP_RING)
        case AP.echoSteps: return value(AP.echo) > 0
        default: return true
        }
    }

    static func signed(_ v: Int) -> String { v > 0 ? "+\(v)" : v < 0 ? "−\(-v)" : "0" }

    func params(_ p: ArpPage) -> [ArpParam] {
        switch p {
        case .a, .b:
            let b = p == .b
            return [ArpParam(name: "Shape", field: b ? AP.bShape : AP.aShape, format: { AP.pick(AP.shapes, $0) }),
                    ArpParam(name: "Rate", field: b ? AP.bRate : AP.aRate, format: { AP.pick(AP.rates, $0) }),
                    ArpParam(name: "Rhythm", field: b ? AP.bRhythm : AP.aRhythm, format: { AP.pick(AP.rhythms, $0) }),
                    ArpParam(name: "Gate", field: b ? AP.bGate : AP.aGate, format: { "\($0)%" })]
        case .notes:
            return [ArpParam(name: "Hold", field: AP.hold, format: { AP.pick(AP.holds, $0) }),
                    ArpParam(name: "Ring", field: AP.ring),
                    ArpParam(name: "Span", field: AP.span, format: { $0 == 1 ? "1 oct" : "\($0) oct" }),
                    ArpParam(name: "B Pitch", field: AP.pitch, format: Self.signed)]
        case .sound:
            return [ArpParam(name: "Sound", field: AP.sound, format: { AP.pick(AP.sounds, $0) }),
                    ArpParam(name: "Bright", field: AP.bright), ArpParam(name: "Decay", field: AP.decay),
                    ArpParam(name: "Room", field: AP.room)]
        case .time:
            return [ArpParam(name: "Tempo", field: AP.tempo), ArpParam(name: "Swing", field: AP.swing, format: { "\($0)%" }),
                    ArpParam(name: "Echo", field: AP.echo),
                    ArpParam(name: "Echo Steps", field: AP.echoSteps, format: { "\($0)/16" })]
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
        let step = p.range.count > 60 ? 2 : 1
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
        let r = AP.range(field)
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
        page = ArpPage(rawValue: max(0, min(ArpPage.allCases.count - 1, page.rawValue + delta))) ?? page
        scheduleSave()
    }

    func turnPresets(_ delta: Int) {
        guard delta != 0 else { return }
        selectScene(scene + (delta > 0 ? 1 : -1))
        showPill(info.title)
    }

    // ALGORITHM is the relation between the lanes.
    func turnAlgorithm(_ delta: Int) {
        guard delta != 0 else { return }
        set(AP.relation, value(AP.relation) + (delta > 0 ? 1 : -1))
        if value(AP.duo) == 0 { set(AP.duo, 1) }
        showPill("Relation", AP.pick(AP.relations, value(AP.relation)))
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

    static let pageButtons: [String: ArpPage] = ["HOME": .a, "FX": .b, "SEL": .notes, "ENV": .sound, "LFO": .time]

    func buttonLegend(_ hw: String) -> String {
        ["HOME": "A", "FX": "B", "SEL": "NOTES", "ENV": "SOUND", "LFO": "TIME", "EDIT": "HOLD", "GLO": "DUO",
         "SAVE": "CLEAR", "ARP": "<", "SEQ": ">", "REC": "WRITE"][hw] ?? hw
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
        case "EDIT": cycle(AP.hold, "Hold") { AP.holds[$0] }
        case "GLO":
            set(AP.duo, value(AP.duo) == 0 ? 1 : 0)
            showPill(value(AP.duo) != 0 ? "A and B, \(AP.pick(AP.relations, value(AP.relation)).lowercased())" : "A alone")
        case "PLAY": togglePlay()
        case "REC":
            writes.toggle()
            scheduleSave()
            showPill(writes ? "Keys feed the arps" : "Keys play along")
        case "SAVE": clear()
        default: break
        }
    }

    // The long press on CLEAR puts the scene back as written.
    func save() { resetScene() }

    func buttonLit(_ b: String) -> Color? {
        if let p = Self.pageButtons[b] { return page == p ? .white : nil }
        switch b {
        case "PLAY":
            // Lit while playing, brighter on each beat.
            guard playing else { return nil }
            return beatLit ? .white : Color.white.opacity(0.7)
        case "REC": return writes ? Color.white.opacity(0.85) : nil
        case "GLO": return value(AP.duo) != 0 ? .white : nil
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

    // The octave moves the keys and the lanes together.
    private func setOctave(_ o: Int) {
        mf1_global_set(Int32(MF1_G_OCTAVE), Int32(o))
        sendProc()
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
            // The pool keeps notes without the octave, so OCT- and OCT+ move what is playing.
            let n = note - octave * 12
            heldKeys[k] = (n, true)
            arpeggi_key_down(Int32(n))
            playing = arpeggi_playing()
        } else {
            heldKeys[k] = (note, false)
            arpeggi_play_down(Int32(note))
        }
        objectWillChange.send()
    }

    func keyUp(_ k: Int) {
        guard let h = heldKeys.removeValue(forKey: k) else { return }
        if h.writes {
            arpeggi_key_up(Int32(h.note))
            playing = arpeggi_playing()
        } else {
            arpeggi_play_up(Int32(h.note))
        }
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

    // ---------------------------------------------------------------- what the screen draws

    // The lanes' notes lately, newest first: the player's, or for a snapshot, what the lanes would have played.
    func recent() -> [ApHit] {
        if let at = snapshotTick {
            var p = proc()
            var pl = pool()
            let msPerTick = 60000.0 / Double(value(AP.tempo)) / Double(AP_PPQ)
            let from = max(0, at - Int64(2.5 * 1000 / msPerTick))
            var out: [ApHit] = []
            var buf = [ApNote](repeating: ApNote(), count: Int(AP_MAX_OUT))
            var t = from
            while t < at {
                let k = Int(ap_next_notes(&p, &pl, t, at, &buf))
                if k == 0 { break }
                for n in buf.prefix(k) {
                    out.append(ApHit(lane: n.lane, note: n.note, ago_ms: Int32(Double(at - n.tick) * msPerTick),
                                     length_ms: Int32(Double(n.length) * msPerTick)))
                }
                t = buf[0].tick + 1
            }
            return out.reversed()
        }
        var buf = [ApHit](repeating: ApHit(), count: 96)
        let n = Int(arpeggi_recent(&buf, 96))
        return Array(buf.prefix(n))
    }

    // The clock's tick now, for the beat dots.
    func nowTick() -> Int64? {
        if let t = snapshotTick { return t }
        var t: Int64 = 0
        return arpeggi_now(&t) ? t : nil
    }

    // ---------------------------------------------------------------- polling

    private static let ledButtons = ["HOME", "FX", "SEL", "ENV", "LFO", "PLAY", "REC", "GLO", "OCT-", "OCT+"]

    func poll() {
        tick &+= 1
        led = LedState.read()
        let wasPlaying = playing
        playing = arpeggi_playing()
        if playing, let t = nowTick() { beatLit = t % Int64(AP_PPQ) < Int64(AP_PPQ) / 5 }
        var h = Hasher()
        for b in Self.ledButtons { h.combine(buttonLit(b) != nil) }
        for b in Self.ledButtons { h.combine(buttonLit(b) == Color.white) }
        for k in 0..<27 { h.combine(keyLed(k)) }
        let sig = h.finalize()
        if sig != lastLedSignature {
            lastLedSignature = sig
            LedClock.shared.frame = tick
        }
        var pool = self.pool()
        var ph = Hasher()
        ph.combine(poolNotes(pool))
        ph.combine(Int(pool.next))
        let poolSig = ph.finalize()
        // The trails move smoothly while the lanes play, or while their last notes fade.
        var redraw = playing || wasPlaying ? tick % 2 == 0 : tick % 15 == 0
        if poolSig != lastPoolSignature || wasPlaying != playing {
            lastPoolSignature = poolSig
            redraw = true
            objectWillChange.send()
        }
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
        if !redraw, let last = recent().first, last.ago_ms < 2600 { redraw = tick % 2 == 0 }
        if redraw { LedClock.screen.frame = tick }
    }

    // A program change from a keyboard picks a scene; one past the last scene is put back.
    private func followOutsideChanges() {
        let p = Int(mf1_current_preset())
        guard p != scene, !mf1_bank_pending() else { return }
        if p < Self.scenes.count {
            selectScene(p)
        } else {
            load()
        }
    }

    // ---------------------------------------------------------------- keeping the scenes

    private struct Stored: Codable {
        var version = 1
        var scene = 0
        var page = 0
        var writes = true
        var scenes: [[Int]] = []
    }

    private static var file: URL { Engine.stateDirectory.appendingPathComponent("scenes.json") }

    private static func readStored() -> Stored? {
        guard !Snapshot.active, let data = try? Data(contentsOf: file) else { return nil }
        if let s = try? JSONDecoder().decode(Stored.self, from: data), s.version == 1 { return s }
        // Unreadable: keep it aside rather than write over it.
        let aside = file.deletingLastPathComponent()
            .appendingPathComponent("scenes.json.unreadable-\(Int(Date().timeIntervalSince1970))")
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
        table[scene] = settings
        let s = Stored(version: 1, scene: scene, page: page.rawValue, writes: writes,
                       scenes: table.map { $0.map { Int($0) } })
        if let data = try? JSONEncoder().encode(s) { try? data.write(to: Self.file, options: .atomic) }
    }
}

extension ArpeggiModel: ModeModel {
    var labels: PanelLabels {
        PanelLabels(select: "PAGE", presets: "SCENE", algorithm: "RELATION")
    }
    func upperKeyLabel(_ i: Int) -> String? { nil }
    func screenView(frame: Int) -> AnyView { AnyView(ArpeggiScreen(model: self, frame: frame)) }
    func screenInput() -> AnyView { AnyView(ArpeggiScreenInput(model: self)) }
}
