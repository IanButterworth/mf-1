import AppKit
import Foundation
import MF1Core
import SwiftUI
import WoofCore

// Woof's side of the panel: what each knob, button and key does, and the dog it all edits. The barks are made
// in WoofCore. This file only decides which number a turn changes, and which notes a key plays.

enum WF {
    static let fields = Int(WF_FIELDS)
    static let dog = Int(WF_DOG), size = Int(WF_SIZE), gruff = Int(WF_GRUFF), length = Int(WF_LENGTH)
    static let yelp = Int(WF_YELP), mood = Int(WF_MOOD), howl = Int(WF_HOWL), growl = Int(WF_GROWL)
    static let breath = Int(WF_BREATH), place = Int(WF_PLACE), distance = Int(WF_DISTANCE), echo = Int(WF_ECHO)
    static let space = Int(WF_SPACE), play = Int(WF_PLAY), pack = Int(WF_PACK), tempo = Int(WF_TEMPO)
    static let step = Int(WF_STEP)
    static func range(_ field: Int) -> ClosedRange<Int> {
        var lo: Int32 = 0, hi: Int32 = 0
        woof_range(Int32(field), &lo, &hi)
        return Int(lo)...Int(hi)
    }
    static let places = (0..<Int(WF_PLACE_COUNT)).map { String(cString: woof_place_name(Int32($0))) }
    static let styles = ["Single", "Repeat", "Chorus", "Chase"]
    static let packs = ["Pair", "Trio", "Litter"]
    static let steps = ["1/4", "1/8", "1/8T", "1/16", "1/16T"]
    static func moodName(_ v: Int) -> String { String(cString: woof_mood_name(Int32(v))) }
}

struct DogInfo {
    var name: String, breed: String, word: String, blurb: String
    var accent: UInt32

    static let all: [DogInfo] = (0..<Int(woof_dog_count())).map { i in
        var d = WoofDog()
        _ = woof_dog(Int32(i), &d)
        func text<T>(_ t: inout T, _ n: Int) -> String {
            withUnsafePointer(to: &t) { $0.withMemoryRebound(to: CChar.self, capacity: n) { String(cString: $0) } }
        }
        return DogInfo(name: text(&d.name, 11), breed: text(&d.breed, 20), word: text(&d.word, 8),
                       blurb: text(&d.blurb, 40), accent: d.accent)
    }
}

// The four pages the knobs can be on.
enum WoofPage: Int, CaseIterable {
    case bark, mood, place, play
    var title: String { ["Bark", "Mood", "Place", "Play"][rawValue] }
}

struct WParam {
    var name: String
    var field: Int
    var format: (Int) -> String = { "\($0)" }
    var range: ClosedRange<Int> { WF.range(field) }
}

final class WoofModel: ObservableObject {
    static let shared = WoofModel()
    static let dogs = DogInfo.all

    @Published var page: WoofPage = .bark
    @Published var pill: String? = nil
    @Published var octHeld: Set<Int> = []
    // The dog show: the next dog comes on every few seconds.
    @Published var parade = false

    // The dog being played, and one per breed, so each keeps how it was left.
    private(set) var settings = [Int32](repeating: 0, count: WF.fields)
    private var table: [[Int32]] = []
    var dog: Int { Int(settings[WF.dog]) }
    var info: DogInfo { Self.dogs[dog] }

    var tick = 0
    // The tick the current dog came on, so it trots in.
    private(set) var arrivedTick = -1000
    private var paradeTick = 0
    private(set) var led = LedState()
    // The notes each held key plays, and how many held keys play each note, so a note two keys share stops only
    // when both are up.
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
        table = (0..<Self.dogs.count).map { Self.recipe($0) }
        var state = Stored()
        if let loaded = Self.readStored() { state = loaded }
        for (i, s) in state.dogs.enumerated() where i < table.count && s.count == WF.fields {
            table[i] = s.map { Int32($0) }
        }
        let d = max(0, min(table.count - 1, state.dog))
        settings = table[d]
        settings[WF.dog] = Int32(d)
        page = WoofPage(rawValue: state.page) ?? .bark
        parade = state.parade
    }

    static func recipe(_ dog: Int) -> [Int32] {
        var s = [Int32](repeating: 0, count: WF.fields)
        woof_default_dog(Int32(dog), &s)
        return s
    }

    // After the engine has started: puts the dogs in the banks and plays the current one.
    func start() {
        woof_install()
        mf1_global_set(Int32(MF1_G_QUIRKS), 1)
        mf1_global_set(Int32(MF1_G_EXTENDED), 0)
        load()
        arrivedTick = tick
    }

    private func load() {
        woof_clamp(&settings)
        woof_load(settings)
        table[dog] = settings
    }

    // ---------------------------------------------------------------- the dog

    func value(_ field: Int) -> Int { Int(settings[field]) }

    func set(_ field: Int, _ v: Int) {
        let r = WF.range(field)
        let c = Int32(max(r.lowerBound, min(r.upperBound, v)))
        guard settings[field] != c else { return }
        // A new playing style must not leave notes of the old one hanging.
        if field == WF.play || field == WF.pack { releaseAllKeys() }
        settings[field] = c
        commit()
    }

    private func commit() {
        woof_clamp(&settings)
        table[dog] = settings
        woof_apply(settings)
        scheduleSave()
        objectWillChange.send()
    }

    func selectDog(_ i: Int) {
        let n = Self.dogs.count
        let next = ((i % n) + n) % n
        guard next != dog else { return }
        releaseAllKeys()
        table[dog] = settings
        settings = table[next]
        settings[WF.dog] = Int32(next)
        load()
        arrivedTick = tick
        paradeTick = tick
        scheduleSave()
        objectWillChange.send()
    }

    // Back to the breed as shown.
    func resetDog() {
        releaseAllKeys()
        settings = Self.recipe(dog)
        load()
        scheduleSave()
        showPill("\(info.breed) as bred")
        objectWillChange.send()
    }

    func resetAll() {
        releaseAllKeys()
        let d = dog
        table = (0..<Self.dogs.count).map { Self.recipe($0) }
        settings = table[d]
        load()
        scheduleSave()
        showPill("Every dog as bred")
        objectWillChange.send()
    }

    func toggleParade() {
        parade.toggle()
        paradeTick = tick
        scheduleSave()
        showPill("Dog show", parade ? "On" : "Off")
    }

    // ---------------------------------------------------------------- knob pages

    func params(_ p: WoofPage) -> [WParam] {
        switch p {
        case .bark:
            return [WParam(name: "Size", field: WF.size), WParam(name: "Gruff", field: WF.gruff),
                    WParam(name: "Length", field: WF.length), WParam(name: "Yelp", field: WF.yelp)]
        case .mood:
            return [WParam(name: "Mood", field: WF.mood, format: WF.moodName), WParam(name: "Howl", field: WF.howl),
                    WParam(name: "Growl", field: WF.growl), WParam(name: "Breath", field: WF.breath)]
        case .place:
            return [WParam(name: "Place", field: WF.place, format: { WF.places[max(0, min(WF.places.count - 1, $0))] }),
                    WParam(name: "Distance", field: WF.distance), WParam(name: "Echo", field: WF.echo),
                    WParam(name: "Space", field: WF.space)]
        case .play:
            return [WParam(name: "Style", field: WF.play, format: { WF.styles[max(0, min(3, $0))] }),
                    WParam(name: "Pack", field: WF.pack, format: { WF.packs[max(0, min(2, $0))] }),
                    WParam(name: "Tempo", field: WF.tempo),
                    WParam(name: "Barks", field: WF.step, format: { WF.steps[max(0, min(4, $0))] })]
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

    // Steps a short list round, for the buttons that cycle style, pack and place.
    func cycle(_ field: Int, _ name: String, _ names: [String]) {
        let r = WF.range(field)
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
        page = WoofPage(rawValue: max(0, min(WoofPage.allCases.count - 1, page.rawValue + delta))) ?? page
        scheduleSave()
    }

    func turnPresets(_ delta: Int) {
        guard delta != 0 else { return }
        selectDog(dog + (delta > 0 ? 1 : -1))
    }

    // ALGORITHM is the place.
    func turnAlgorithm(_ delta: Int) {
        guard delta != 0 else { return }
        set(WF.place, value(WF.place) + (delta > 0 ? 1 : -1))
        showPill("Place", WF.places[value(WF.place)])
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

    static let pageButtons: [String: WoofPage] = ["HOME": .bark, "FX": .mood, "SEL": .place, "ENV": .play]

    func buttonLegend(_ hw: String) -> String {
        ["HOME": "BARK", "FX": "MOOD", "SEL": "PLACE", "ENV": "PLAY", "LFO": "STYLE", "EDIT": "PACK", "GLO": "SHOW",
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
        case "LFO": cycle(WF.play, "Style", WF.styles)
        case "EDIT": cycle(WF.pack, "Pack", WF.packs)
        case "GLO": toggleParade()
        case "SAVE": showPill("Hold RESET for the dog as bred")
        default: break
        }
    }

    // The long press on RESET.
    func save() { resetDog() }

    func buttonLit(_ b: String) -> Color? {
        if let p = Self.pageButtons[b] { return page == p ? .white : nil }
        switch b {
        case "LFO": return value(WF.play) != Int(WF_SINGLE) ? Color.white.opacity(0.85) : nil
        case "GLO": return parade ? Color.white.opacity(0.85) : nil
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
        var out = [Int32](repeating: 0, count: 8)
        let n = Int(woof_key_notes(settings, Int32(note), &out))
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
    // How long each dog stays on in a parade.
    static let paradeSeconds = 10

    // Scene frames since the dog came on.
    var arrivedFrames: Int { (tick - arrivedTick) / (60 / Self.sceneFPS) }

    func poll() {
        tick &+= 1
        if parade && heldKeys.isEmpty && tick - paradeTick >= Self.paradeSeconds * 60 { selectDog(dog + 1) }
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

    // A program change from a keyboard picks a dog; one past the last dog is put back.
    private func followOutsideChanges() {
        let p = Int(mf1_current_preset())
        guard p != dog, !mf1_bank_pending() else { return }
        if p < Self.dogs.count {
            selectDog(p)
        } else {
            load()
        }
    }

    // ---------------------------------------------------------------- keeping the dogs

    private struct Stored: Codable {
        var version = 1
        var dog = 0
        var page = 0
        var parade = false
        var dogs: [[Int]] = []
    }

    private static var file: URL { Engine.stateDirectory.appendingPathComponent("dogs.json") }

    private static func readStored() -> Stored? {
        guard let data = try? Data(contentsOf: file) else { return nil }
        if let s = try? JSONDecoder().decode(Stored.self, from: data), s.version == 1 { return s }
        // Unreadable: keep it aside rather than write over it.
        let aside = file.deletingLastPathComponent()
            .appendingPathComponent("dogs.json.unreadable-\(Int(Date().timeIntervalSince1970))")
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
        table[dog] = settings
        let s = Stored(version: 1, dog: dog, page: page.rawValue, parade: parade,
                       dogs: table.map { $0.map { Int($0) } })
        if let data = try? JSONEncoder().encode(s) { try? data.write(to: Self.file, options: .atomic) }
    }
}

extension WoofModel: ModeModel {
    var labels: PanelLabels {
        PanelLabels(select: "PAGE", presets: "DOG", algorithm: "PLACE")
    }
    func upperKeyLabel(_ i: Int) -> String? { nil }
    func screenView(frame: Int) -> AnyView { AnyView(WoofScreen(model: self, frame: frame)) }
    func screenInput() -> AnyView { AnyView(WoofScreenInput(model: self)) }
}
