import AppKit
import Foundation
import MF1Core
import RhymesCore
import SwiftUI

// Rhymes' side of the panel: what each knob, button and key does. The songs, the game, its lights and the sounds are
// RhymesCore's; this file only turns the panel into calls on it, keeps each song's settings and stars, and asks for
// redraws.

enum RH {
    static let fields = Int(RH_FIELDS)
    static let song = Int(RH_SONG), level = Int(RH_LEVEL), speed = Int(RH_SPEED), room = Int(RH_ROOM)
    static let lights = Int(RH_LIGHTS)
    static let levels = (0..<Int(RH_LEVEL_COUNT)).map { String(cString: rh_level_name(Int32($0))) }
    static let lightNames = (0..<Int(RH_LIGHTS_COUNT)).map { String(cString: rh_lights_name(Int32($0))) }
    static let sounds = (0..<Int(rh_sound_count())).map { String(cString: rh_sound_name(Int32($0))) }
    static let sound = Int(RH_SOUND)
    static let songCount = Int(rh_song_count())
    static func range(_ field: Int) -> ClosedRange<Int> {
        var lo: Int32 = 0, hi: Int32 = 0
        rh_range(Int32(field), &lo, &hi)
        return Int(lo)...Int(hi)
    }
    static func title(_ song: Int) -> String { String(cString: rh_song_title(Int32(song))) }
    static func blurb(_ song: Int) -> String { String(cString: rh_song_blurb(Int32(song))) }
    static func accent(_ song: Int) -> UInt32 { rh_song_accent(Int32(song)) }
    // What a level asks of the child, under the title.
    static let levelHints = ["Press any key!", "Press the shining key!", "Play it yourself!"]
}

struct RParam {
    var name: String
    var field: Int
    var format: (Int) -> String = { "\($0)" }
    var range: ClosedRange<Int> { RH.range(field) }
}

final class RhymesModel: ObservableObject {
    static let shared = RhymesModel()

    @Published var pill: String? = nil
    @Published var octHeld: Set<Int> = []

    // The song being played, and one row per song, so each keeps how it was left.
    private(set) var settings = [Int32](repeating: 0, count: RH.fields)
    private var table: [[Int32]] = []
    // The most stars each song has earned at each level.
    private(set) var best: [[Int]] = []
    var song: Int { Int(settings[RH.song]) }

    var tick = 0
    // A copy of the game, taken each poll, for the lights and the screen.
    private(set) var game = rh_game_t()
    private(set) var now: UInt32 = 0
    private(set) var bright: UInt32 = 0, dim: UInt32 = 0
    private(set) var led = LedState()
    private var heldKeys = Set<Int>()
    private var lastGeneration: UInt32 = 0
    private var lastRhymes: UInt32 = 0
    private var lastLedSignature = 0
    private var lastVolume: Int32 = -1
    private var scoredAt: UInt32 = 1
    private var pillTimer: Timer?
    private var saveTask: DispatchWorkItem?
    private var transposeTouched = false
    private var masterLevel: Double?

    init() {
        table = (0..<RH.songCount).map { Self.recipe($0) }
        best = Array(repeating: Array(repeating: 0, count: Int(RH_LEVEL_COUNT)), count: RH.songCount)
        var state = Stored()
        if let loaded = Self.readStored() { state = loaded }
        for (i, s) in state.songs.enumerated() where i < table.count && s.count == RH.fields {
            table[i] = s.map { Int32($0) }
        }
        for (i, b) in state.best.enumerated() where i < best.count && b.count == best[i].count { best[i] = b }
        let s = max(0, min(table.count - 1, state.song))
        settings = table[s]
        settings[RH.song] = Int32(s)
    }

    static func recipe(_ song: Int) -> [Int32] {
        var s = [Int32](repeating: 0, count: RH.fields)
        rh_default(Int32(song), &s)
        return s
    }

    // After the engine has started: puts the songs' sounds in the banks and loads the current one.
    func start() {
        rhymes_install()
        mf1_global_set(Int32(MF1_G_QUIRKS), 1)
        mf1_global_set(Int32(MF1_G_EXTENDED), 0)
        load()
    }

    private func load() {
        rh_clamp(&settings)
        rhymes_load(settings)
        table[song] = settings
        refresh()
    }

    // ---------------------------------------------------------------- the song

    func value(_ field: Int) -> Int { Int(settings[field]) }

    func set(_ field: Int, _ v: Int) {
        let r = RH.range(field)
        let c = Int32(max(r.lowerBound, min(r.upperBound, v)))
        guard settings[field] != c else { return }
        settings[field] = c
        rh_clamp(&settings)
        table[song] = settings
        rhymes_apply(settings)
        scheduleSave()
        refresh()
    }

    func selectSong(_ i: Int) {
        let n = RH.songCount
        let next = ((i % n) + n) % n
        guard next != song else { return }
        releaseAllKeys()
        table[song] = settings
        settings = table[next]
        settings[RH.song] = Int32(next)
        load()
        scheduleSave()
        showPill(RH.title(next))
    }

    func restart() {
        rhymes_restart()
        refresh()
        showPill("From the start")
    }

    func listen() {
        rhymes_listen()
        refresh()
        if game.state == Int32(RH_LISTEN) { showPill("Listen…") }
    }

    // Back to the song's own settings.
    func resetSong() {
        releaseAllKeys()
        settings = Self.recipe(song)
        load()
        scheduleSave()
        showPill("\(RH.title(song)) as written")
    }

    func resetStars() {
        best = best.map { $0.map { _ in 0 } }
        scheduleSave()
        refresh()
        showPill("Every star cleared")
    }

    var stars: Int { best[song][value(RH.level)] }

    // ---------------------------------------------------------------- knobs

    func params() -> [RParam] {
        [RParam(name: "Speed", field: RH.speed, format: { "\($0)%" }),
         RParam(name: "Room", field: RH.room),
         RParam(name: "Sound", field: RH.sound, format: { RH.sounds[max(0, min(RH.sounds.count - 1, $0))] }),
         RParam(name: "Lights", field: RH.lights, format: { RH.lightNames[max(0, min(RH.lightNames.count - 1, $0))] })]
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
        let ps = params()
        guard k < ps.count else { return }
        let p = ps[k]
        let step = p.range.count > 12 ? 5 : 1
        set(p.field, value(p.field) + delta * step)
        showPill(p.name, p.format(value(p.field)))
    }

    // A click on a chip on the screen: steps short lists round, longer ranges by ten.
    func clickChip(_ k: Int, back: Bool) {
        let ps = params()
        guard k < ps.count else { return }
        let p = ps[k]
        let r = p.range
        let dir = back ? -1 : 1
        if r.count <= 12 {
            set(p.field, r.lowerBound + ((value(p.field) - r.lowerBound + dir) % r.count + r.count) % r.count)
        } else {
            set(p.field, value(p.field) + dir * 10)
        }
        showPill(p.name, p.format(value(p.field)))
    }

    func cycle(_ field: Int, _ name: String, _ names: [String]) {
        let r = RH.range(field)
        set(field, value(field) >= r.upperBound ? r.lowerBound : value(field) + 1)
        showPill(name, names[value(field)])
    }

    func showPill(_ name: String, _ value: String? = nil) {
        pill = value.map { "\(name): \($0)" } ?? name
        pillTimer?.invalidate()
        pillTimer = Timer.scheduledTimer(withTimeInterval: 1.4, repeats: false) { [weak self] _ in
            self?.pill = nil
            self?.refresh()
        }
        refresh()
    }

    // SELECT moves between the song's lines, to practise one.
    func turnSelect(_ delta: Int) {
        guard delta != 0 else { return }
        rhymes_line(Int32(delta > 0 ? 1 : -1))
        refresh()
        showPill("Line", "\(game.tune.lineAt(Int(game.pos)) + 1) of \(game.tune.lines)")
    }

    func turnPresets(_ delta: Int) {
        guard delta != 0 else { return }
        selectSong(song + (delta > 0 ? 1 : -1))
    }

    // ALGORITHM is the level.
    func turnAlgorithm(_ delta: Int) {
        guard delta != 0 else { return }
        set(RH.level, value(RH.level) + (delta > 0 ? 1 : -1))
        showPill("Level", RH.levels[value(RH.level)])
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

    // FX, SEL and ENV pick a level straight away.
    static let levelButtons = ["FX": Int(RH_MAGIC), "SEL": Int(RH_GUIDE), "ENV": Int(RH_SOLO)]

    func buttonLegend(_ hw: String) -> String {
        ["HOME": "SONG", "FX": "MAGIC", "SEL": "GUIDE", "ENV": "SOLO", "LFO": "LIGHT", "EDIT": "SOUND", "GLO": "",
         "SAVE": "AGAIN", "ARP": "<", "SEQ": ">", "PLAY/STOP": "LISTEN", "REC": ""][hw] ?? hw
    }

    func press(_ button: String) {
        if mf1_bank_pending() && button == "HOME" {
            mf1_bank_cancel()
            return
        }
        if let l = Self.levelButtons[button] {
            set(RH.level, l)
            showPill("Level", RH.levels[l])
            return
        }
        switch button {
        case "HOME": showPill(RH.title(song), RH.blurb(song))
        case "ARP": turnPresets(-1)
        case "SEQ": turnPresets(1)
        case "LFO": cycle(RH.lights, "Lights", RH.lightNames)
        case "EDIT": cycle(RH.sound, "Sound", RH.sounds)
        case "PLAY/STOP": listen()
        case "SAVE": restart()
        default: break
        }
    }

    // The long press on AGAIN.
    func save() { resetSong() }

    func buttonLit(_ b: String) -> Color? {
        if let l = Self.levelButtons[b] { return value(RH.level) == l ? .white : nil }
        switch b {
        case "HOME": return .white
        case "PLAY/STOP": return game.state == Int32(RH_LISTEN) ? .green : nil
        case "LFO": return value(RH.lights) != Int(RH_BLINK) ? Color.white.opacity(0.85) : nil
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

    func keyDown(_ k: Int) {
        guard !heldKeys.contains(k) else { return }
        heldKeys.insert(k)
        rhymes_key_down(Int32(k))
        refresh()
    }

    func keyUp(_ k: Int) {
        guard heldKeys.remove(k) != nil else { return }
        rhymes_key_up(Int32(k))
    }

    func isHeld(_ k: Int) -> Bool { heldKeys.contains(k) }

    func releaseAllKeys() {
        heldKeys.removeAll()
        rhymes_all_keys_up()
    }

    func shownNote(key k: Int) -> Int { Int(rh_note_of(Int32(k))) + led.octave * 12 }

    func keyLed(_ k: Int) -> Double {
        if (bright >> UInt32(k)) & 1 != 0 || heldKeys.contains(k) { return 1 }
        return (dim >> UInt32(k)) & 1 != 0 ? 0.4 : 0.06
    }

    // ---------------------------------------------------------------- polling

    private static let ledButtons = ["FX", "SEL", "ENV", "LFO", "PLAY/STOP", "OCT-", "OCT+"]
    // The picture moves at ten frames a second, as RhymesCore's frames do.
    static let sceneFPS = 10

    // A fresh copy of the game and its lights, and a redraw.
    private func refresh() {
        rhymes_game(&game, &now)
        rh_game_lights(&game, now, &bright, &dim)
        LedClock.shared.frame = tick
        LedClock.screen.frame = tick
        objectWillChange.send()
    }

    func poll() {
        tick &+= 1
        rhymes_game(&game, &now)
        rh_game_lights(&game, now, &bright, &dim)
        led = LedState.read()
        recordStars()
        var h = Hasher()
        for b in Self.ledButtons { h.combine(buttonLit(b) != nil) }
        for b in Self.ledButtons { h.combine(buttonLit(b) == Color.white) }
        for k in 0..<27 { h.combine(keyLed(k)) }
        let sig = h.finalize()
        if sig != lastLedSignature {
            lastLedSignature = sig
            LedClock.shared.frame = tick
        }
        var redraw = tick % (60 / Self.sceneFPS) == 0
        let r = rhymes_generation()
        if r != lastRhymes {
            lastRhymes = r
            redraw = true
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
        if redraw { LedClock.screen.frame = tick }
    }

    // A finished song keeps its best stars.
    private func recordStars() {
        guard game.state == Int32(RH_DONE), game.done_at != scoredAt else { return }
        scoredAt = game.done_at
        let lv = Int(game.level), s = Int(game.tune.song)
        guard s >= 0 && s < best.count && lv >= 0 && lv < best[s].count else { return }
        if Int(game.stars) > best[s][lv] {
            best[s][lv] = Int(game.stars)
            scheduleSave()
        }
    }

    // A program change from a keyboard picks a song; one past the last song is put back.
    private func followOutsideChanges() {
        let p = Int(mf1_current_preset())
        guard p != song, !mf1_bank_pending() else { return }
        if p < RH.songCount {
            selectSong(p)
        } else {
            load()
        }
    }

    // ---------------------------------------------------------------- keeping the songs

    private struct Stored: Codable {
        var version = 1
        var song = 0
        var songs: [[Int]] = []
        var best: [[Int]] = []
    }

    private static var file: URL { Engine.stateDirectory.appendingPathComponent("rhymes.json") }

    private static func readStored() -> Stored? {
        guard let data = try? Data(contentsOf: file) else { return nil }
        if let s = try? JSONDecoder().decode(Stored.self, from: data), s.version == 1 { return s }
        // Unreadable: keep it aside rather than write over it.
        let aside = file.deletingLastPathComponent()
            .appendingPathComponent("rhymes.json.unreadable-\(Int(Date().timeIntervalSince1970))")
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
        table[song] = settings
        let s = Stored(version: 1, song: song, songs: table.map { $0.map { Int($0) } }, best: best)
        if let data = try? JSONEncoder().encode(s) { try? data.write(to: Self.file, options: .atomic) }
    }
}

extension RhymesModel: ModeModel {
    var labels: PanelLabels {
        PanelLabels(select: "LINE", presets: "SONG", algorithm: "LEVEL", knobs: ["SPEED", "ROOM", "SOUND", "LIGHTS"])
    }
    func upperKeyLabel(_ i: Int) -> String? { nil }
    func screenView(frame: Int) -> AnyView { AnyView(RhymesScreen(model: self, frame: frame)) }
    func screenInput() -> AnyView { AnyView(RhymesScreenInput(model: self)) }
}
