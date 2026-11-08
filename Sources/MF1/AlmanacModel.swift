import AlmanacCore
import AppKit
import Foundation
import MF1Core
import SwiftUI

// Almanac's side of the panel: what each knob, button and key does, and the day it all edits.
// The sound is made in AlmanacCore. This file only decides which number a turn changes, and which
// notes a key plays.

enum AL {
    static let fields = Int(AL_FIELDS)
    static let month = Int(AL_MONTH), mood = Int(AL_MOOD), warmth = Int(AL_WARMTH), linger = Int(AL_LINGER)
    static let shimmer = Int(AL_SHIMMER), sky = Int(AL_SKY), weather = Int(AL_WEATHER), elements = Int(AL_ELEMENTS)
    static let gusts = Int(AL_GUSTS), place = Int(AL_PLACE), distance = Int(AL_DISTANCE), echo = Int(AL_ECHO)
    static let space = Int(AL_SPACE), play = Int(AL_PLAY), voicing = Int(AL_VOICING), tempo = Int(AL_TEMPO)
    static let step = Int(AL_STEP)
    static func range(_ field: Int) -> ClosedRange<Int> {
        var lo: Int32 = 0, hi: Int32 = 0
        almanac_range(Int32(field), &lo, &hi)
        return Int(lo)...Int(hi)
    }
    static let skies = (0..<Int(AL_SKY_COUNT)).map { String(cString: almanac_sky_name(Int32($0))) }
    static let places = (0..<Int(AL_PLACE_COUNT)).map { String(cString: almanac_place_name(Int32($0))) }
    static let styles = ["Single", "Chord", "Split", "Pattern"]
    static let voicings = ["Close", "Open", "Wide"]
    static let steps = ["1/4", "1/8", "1/8T", "1/16", "1/16T"]
    static func moodName(_ v: Int) -> String { String(cString: almanac_mood_name(Int32(v))) }
}

struct MonthInfo {
    var name: String, month: String, title: String, blurb: String
    var skyTop: UInt32, skyBottom: UInt32, ground: UInt32, far: UInt32, accent: UInt32
    var season: Int

    static let all: [MonthInfo] = (0..<Int(almanac_month_count())).map { i in
        var m = AlmanacMonth()
        _ = almanac_month(Int32(i), &m)
        func text<T>(_ t: inout T, _ n: Int) -> String {
            withUnsafePointer(to: &t) { $0.withMemoryRebound(to: CChar.self, capacity: n) { String(cString: $0) } }
        }
        return MonthInfo(name: text(&m.name, 11), month: text(&m.month, 12), title: text(&m.title, 24),
                         blurb: text(&m.blurb, 40), skyTop: m.skyTop, skyBottom: m.skyBottom, ground: m.ground,
                         far: m.far, accent: m.accent, season: Int(m.season))
    }
}

// The four pages the knobs can be on.
enum Page: Int, CaseIterable {
    case mood, weather, land, play
    var title: String { ["Mood", "Weather", "Land", "Play"][rawValue] }
}

struct AParam {
    var name: String
    var field: Int
    var format: (Int) -> String = { "\($0)" }
    var range: ClosedRange<Int> { AL.range(field) }
}

final class AlmanacModel: ObservableObject {
    static let shared = AlmanacModel()
    static let months = MonthInfo.all

    @Published var page: Page = .mood
    @Published var pill: String? = nil
    @Published var octHeld: Set<Int> = []

    // The day being played, and one per month, so each month keeps how it was left.
    private(set) var day = [Int32](repeating: 0, count: AL.fields)
    private var table: [[Int32]] = []
    var month: Int { Int(day[AL.month]) }
    var info: MonthInfo { Self.months[month] }

    var tick = 0
    private(set) var led = LedState()
    // The notes each held key plays, and how many held keys play each note, so a note two chords share
    // stops only when both keys are up.
    private var heldKeys: [Int: [Int]] = [:]
    private var noteRefs = [Int](repeating: 0, count: 128)
    private var lastGeneration: UInt32 = 0
    private var lastLedSignature = 0
    private var lastVolume: Int32 = -1
    private var pillTimer: Timer?
    private var saveTask: DispatchWorkItem?
    private var transposeTouched = false
    private var masterLevel: Double?

    init() {
        table = (0..<Self.months.count).map { Self.recipe($0) }
        var state = Stored()
        if let loaded = Self.readStored() { state = loaded }
        for (i, s) in state.days.enumerated() where i < table.count && s.count == AL.fields {
            table[i] = s.map { Int32($0) }
        }
        let m = max(0, min(table.count - 1, state.month))
        day = table[m]
        day[AL.month] = Int32(m)
        page = Page(rawValue: state.page) ?? .mood
    }

    static func recipe(_ month: Int) -> [Int32] {
        var s = [Int32](repeating: 0, count: AL.fields)
        almanac_default_day(Int32(month), &s)
        return s
    }

    // After the engine has started: puts the months in the banks and plays the current day.
    func start() {
        almanac_install()
        mf1_global_set(Int32(MF1_G_QUIRKS), 1)
        mf1_global_set(Int32(MF1_G_EXTENDED), 0)
        load()
    }

    private func load() {
        almanac_clamp(&day)
        almanac_load(day)
        table[month] = day
    }

    // ---------------------------------------------------------------- the day

    func value(_ field: Int) -> Int { Int(day[field]) }

    func set(_ field: Int, _ v: Int) {
        let r = AL.range(field)
        let c = Int32(max(r.lowerBound, min(r.upperBound, v)))
        guard day[field] != c else { return }
        // A new playing style must not leave notes of the old one hanging.
        if field == AL.play || field == AL.voicing { releaseAllKeys() }
        day[field] = c
        commit()
    }

    private func commit() {
        almanac_clamp(&day)
        table[month] = day
        almanac_apply(day)
        scheduleSave()
        objectWillChange.send()
    }

    func selectMonth(_ i: Int) {
        let n = Self.months.count
        let next = ((i % n) + n) % n
        guard next != month else { return }
        releaseAllKeys()
        table[month] = day
        day = table[next]
        day[AL.month] = Int32(next)
        load()
        scheduleSave()
        objectWillChange.send()
    }

    // Back to the month as composed.
    func resetMonth() {
        releaseAllKeys()
        day = Self.recipe(month)
        load()
        scheduleSave()
        showPill("\(info.month) as composed")
        objectWillChange.send()
    }

    func resetAll() {
        releaseAllKeys()
        let m = month
        table = (0..<Self.months.count).map { Self.recipe($0) }
        day = table[m]
        load()
        scheduleSave()
        showPill("Every month as composed")
        objectWillChange.send()
    }

    // ---------------------------------------------------------------- knob pages

    func params(_ p: Page) -> [AParam] {
        switch p {
        case .mood:
            return [AParam(name: "Mood", field: AL.mood, format: AL.moodName), AParam(name: "Warmth", field: AL.warmth),
                    AParam(name: "Linger", field: AL.linger), AParam(name: "Shimmer", field: AL.shimmer)]
        case .weather:
            return [AParam(name: "Sky", field: AL.sky, format: { AL.skies[max(0, min(AL.skies.count - 1, $0))] }),
                    AParam(name: "Weather", field: AL.weather), AParam(name: "Elements", field: AL.elements),
                    AParam(name: "Gusts", field: AL.gusts)]
        case .land:
            return [AParam(name: "Place", field: AL.place, format: { AL.places[max(0, min(AL.places.count - 1, $0))] }),
                    AParam(name: "Distance", field: AL.distance), AParam(name: "Echo", field: AL.echo),
                    AParam(name: "Space", field: AL.space)]
        case .play:
            return [AParam(name: "Style", field: AL.play, format: { AL.styles[max(0, min(3, $0))] }),
                    AParam(name: "Voicing", field: AL.voicing, format: { AL.voicings[max(0, min(2, $0))] }),
                    AParam(name: "Tempo", field: AL.tempo),
                    AParam(name: "Notes", field: AL.step, format: { AL.steps[max(0, min(4, $0))] })]
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
        let step = p.range.count > 12 ? 2 : 1
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
        if r.count <= 12 {
            set(p.field, r.lowerBound + ((value(p.field) - r.lowerBound + dir) % r.count + r.count) % r.count)
        } else {
            set(p.field, value(p.field) + dir * 5)
        }
        showPill(p.name, p.format(value(p.field)))
    }

    // Steps a short list round, for the buttons that cycle sky, style and voicing.
    func cycle(_ field: Int, _ name: String, _ names: [String]) {
        let r = AL.range(field)
        set(field, value(field) >= r.upperBound ? r.lowerBound : value(field) + 1)
        showPill(name, names[value(field)])
    }

    func showPill(_ name: String, _ value: String? = nil) {
        pill = value.map { "\(name): \($0)" } ?? name
        pillTimer?.invalidate()
        pillTimer = Timer.scheduledTimer(withTimeInterval: 1.2, repeats: false) { [weak self] _ in self?.pill = nil }
    }

    // ---------------------------------------------------------------- panel knobs

    func turnSelect(_ delta: Int) {
        guard delta != 0 else { return }
        page = Page(rawValue: max(0, min(Page.allCases.count - 1, page.rawValue + delta))) ?? page
        scheduleSave()
    }

    func turnPresets(_ delta: Int) {
        guard delta != 0 else { return }
        selectMonth(month + (delta > 0 ? 1 : -1))
    }

    // ALGORITHM is the sky.
    func turnAlgorithm(_ delta: Int) {
        guard delta != 0 else { return }
        set(AL.sky, value(AL.sky) + (delta > 0 ? 1 : -1))
        showPill("Sky", AL.skies[value(AL.sky)])
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

    static let pageButtons: [String: Page] = ["HOME": .mood, "FX": .weather, "SEL": .land, "ENV": .play]

    func buttonLegend(_ hw: String) -> String {
        ["HOME": "MOOD", "FX": "WTHR", "SEL": "LAND", "ENV": "PLAY", "LFO": "STYLE", "EDIT": "VOICE", "GLO": "SKY",
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
        case "LFO": cycle(AL.play, "Style", AL.styles)
        case "EDIT": cycle(AL.voicing, "Voicing", AL.voicings)
        case "GLO": cycle(AL.sky, "Sky", AL.skies)
        case "SAVE": showPill("Hold RESET for the month as composed")
        default: break
        }
    }

    // The long press on RESET.
    func save() { resetMonth() }

    func buttonLit(_ b: String) -> Color? {
        if let p = Self.pageButtons[b] { return page == p ? .white : nil }
        switch b {
        case "LFO": return value(AL.play) != Int(AL_SINGLE) ? Color.white.opacity(0.85) : nil
        case "GLO": return value(AL.sky) != Int(AL_CLEAR) ? Color.white.opacity(0.85) : nil
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

    // The lowest twelve keys are the left hand, which a split gives the drone.
    func noteFor(key k: Int) -> Int { 53 + DeviceModel.semitone(forKey: k) + octave * 12 }
    func isLeft(key k: Int) -> Bool { DeviceModel.semitone(forKey: k) < 12 }

    func keyDown(_ k: Int) {
        guard heldKeys[k] == nil else { return }
        let note = noteFor(key: k)
        guard (0...127).contains(note) else { return }
        var out = [Int32](repeating: 0, count: 8)
        let n = Int(almanac_key_notes(day, Int32(note), isLeft(key: k), &out))
        let notes = out.prefix(n).map { Int($0) }
        heldKeys[k] = notes
        for x in notes {
            if noteRefs[x] == 0 { mf1_key_down(Int32(x)) }
            noteRefs[x] += 1
        }
    }

    func keyUp(_ k: Int) {
        guard let notes = heldKeys.removeValue(forKey: k) else { return }
        for x in notes {
            noteRefs[x] -= 1
            if noteRefs[x] == 0 { mf1_key_up(Int32(x)) }
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

    // How many notes are sounding, for the scene.
    var soundingCount: Int { led.soundingLo.nonzeroBitCount + led.soundingHi.nonzeroBitCount }

    // ---------------------------------------------------------------- polling

    private static let ledButtons = ["HOME", "FX", "SEL", "ENV", "LFO", "GLO", "OCT-", "OCT+"]
    // The scene is pixel art and moves at a few frames a second, as a small screen would.
    static let sceneFPS = 6

    func poll() {
        tick &+= 1
        led = LedState.read()
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

    // A program change from a keyboard picks a month; one past the last month is put back.
    private func followOutsideChanges() {
        let p = Int(mf1_current_preset())
        guard p != month, !mf1_bank_pending() else { return }
        if p < Self.months.count {
            selectMonth(p)
        } else {
            load()
        }
    }

    // ---------------------------------------------------------------- keeping the days

    private struct Stored: Codable {
        var version = 1
        var month = 0
        var page = 0
        var days: [[Int]] = []
    }

    private static var file: URL { Engine.stateDirectory.appendingPathComponent("days.json") }

    private static func readStored() -> Stored? {
        guard let data = try? Data(contentsOf: file) else { return nil }
        if let s = try? JSONDecoder().decode(Stored.self, from: data), s.version == 1 { return s }
        // Unreadable: keep it aside rather than write over it.
        let aside = file.deletingLastPathComponent()
            .appendingPathComponent("days.json.unreadable-\(Int(Date().timeIntervalSince1970))")
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
        table[month] = day
        let s = Stored(version: 1, month: month, page: page.rawValue, days: table.map { $0.map { Int($0) } })
        if let data = try? JSONEncoder().encode(s) { try? data.write(to: Self.file, options: .atomic) }
    }
}

extension AlmanacModel: ModeModel {
    var labels: PanelLabels {
        PanelLabels(select: "PAGE", presets: "MONTH", algorithm: "SKY")
    }
    func upperKeyLabel(_ i: Int) -> String? { nil }
    func screenView(frame: Int) -> AnyView { AnyView(AlmanacScreen(model: self, frame: frame)) }
    func screenInput() -> AnyView { AnyView(AlmanacScreenInput(model: self)) }
}
