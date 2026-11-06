import AppKit
import Foundation
import IScreamCore
import MF1Core
import SwiftUI

// iScream's side of the panel: what each knob, button and key does, and the sundae it all edits.
// The sound is made in IScreamCore. This file only decides which number a turn changes.

enum IS {
    static let fields = Int(IS_FIELDS)
    static let flavor = Int(IS_FLAVOR), scoops = Int(IS_SCOOPS), sweet = Int(IS_SWEET), melt = Int(IS_MELT)
    static let fizz = Int(IS_FIZZ), coneOn = Int(IS_CONE_ON), coneStyle = Int(IS_CONE_STYLE)
    static let toast = Int(IS_TOAST), crunch = Int(IS_CRUNCH)
    // Toppings, in the order they are stacked.
    static let cream = 0, fudge = 1, caramel = 2, chips = 3, sprinkles = 4
    static func top(_ topping: Int, _ knob: Int) -> Int { Int(IS_TOPPING_BASE) + topping * 4 + knob }
    static func range(_ field: Int) -> ClosedRange<Int> {
        var lo: Int32 = 0, hi: Int32 = 0
        iscream_range(Int32(field), &lo, &hi)
        return Int(lo)...Int(hi)
    }
}

struct FlavorInfo {
    var name: String, title: String, blurb: String
    var color: UInt32, accent: UInt32, bits: UInt32
    var pattern: Int
    var mono: Bool

    static let all: [FlavorInfo] = (0..<Int(iscream_flavor_count())).map { i in
        var f = IScreamFlavor()
        _ = iscream_flavor(Int32(i), &f)
        func text<T>(_ t: inout T, _ n: Int) -> String {
            withUnsafePointer(to: &t) { $0.withMemoryRebound(to: CChar.self, capacity: n) { String(cString: $0) } }
        }
        return FlavorInfo(name: text(&f.name, 11), title: text(&f.title, 24), blurb: text(&f.blurb, 40),
                          color: f.color, accent: f.accent, bits: f.bits, pattern: Int(f.pattern), mono: f.mono)
    }
}

// What KNOB1..4 and the screen chips change. The scoop and cone are things on their own; each
// topping has three knobs and an on/off switch, as the FM-1's effect pages do.
enum Focus: Int, CaseIterable {
    case scoop, cone, cream, fudge, caramel, chips, sprinkles

    var topping: Int? { rawValue >= 2 ? rawValue - 2 : nil }
    var title: String { ["Scoop", "Cone", "Whipped Cream", "Hot Fudge", "Caramel", "Choc Chips", "Sprinkles"][rawValue] }
    var short: String { ["Scoop", "Cone", "Cream", "Fudge", "Caramel", "Chips", "Sprinkles"][rawValue] }
}

struct IParam {
    var name: String
    var field: Int
    var format: (Int) -> String = { "\($0)" }
    var isSwitch = false
    var range: ClosedRange<Int> { IS.range(field) }
}

final class IScreamModel: ObservableObject {
    static let shared = IScreamModel()
    static let flavors = FlavorInfo.all

    @Published var focus: Focus = .scoop
    @Published var pill: String? = nil
    @Published var octHeld: Set<Int> = []

    // The sundae being played, and one per flavor, so each flavor keeps how it was left.
    private(set) var sundae = [Int32](repeating: 0, count: IS.fields)
    private var table: [[Int32]] = []
    var flavor: Int { Int(sundae[IS.flavor]) }
    var info: FlavorInfo { Self.flavors[flavor] }

    var tick = 0
    private(set) var led = LedState()
    private var heldKeys: [Int: Int] = [:]
    private var lastGeneration: UInt32 = 0
    private var lastLedSignature = 0
    private var lastVolume: Int32 = -1
    private var wasSounding = false
    private var pillTimer: Timer?
    private var saveTask: DispatchWorkItem?
    private var transposeTouched = false
    private var masterLevel: Double?

    init() {
        table = (0..<Self.flavors.count).map { Self.recipe($0) }
        var state = Stored()
        if let loaded = Self.readStored() { state = loaded }
        for (i, s) in state.sundaes.enumerated() where i < table.count && s.count == IS.fields {
            table[i] = s.map { Int32($0) }
        }
        let f = max(0, min(table.count - 1, state.flavor))
        sundae = table[f]
        sundae[IS.flavor] = Int32(f)
        focus = Focus(rawValue: state.focus) ?? .scoop
    }

    static func recipe(_ flavor: Int) -> [Int32] {
        var s = [Int32](repeating: 0, count: IS.fields)
        iscream_default_sundae(Int32(flavor), &s)
        return s
    }

    // After the engine has started: puts the flavors in the banks and plays the current sundae.
    func start() {
        iscream_install()
        mf1_global_set(Int32(MF1_G_QUIRKS), 1)
        mf1_global_set(Int32(MF1_G_EXTENDED), 0)
        load()
    }

    private func load() {
        iscream_clamp(&sundae)
        iscream_load(sundae)
        table[flavor] = sundae
    }

    // ---------------------------------------------------------------- the sundae

    func value(_ field: Int) -> Int { Int(sundae[field]) }

    func set(_ field: Int, _ v: Int) {
        let r = IS.range(field)
        let c = Int32(max(r.lowerBound, min(r.upperBound, v)))
        guard sundae[field] != c else { return }
        sundae[field] = c
        commit()
    }

    private func commit() {
        iscream_clamp(&sundae)
        table[flavor] = sundae
        iscream_apply(sundae)
        scheduleSave()
        objectWillChange.send()
    }

    func selectFlavor(_ i: Int) {
        let n = Self.flavors.count
        let next = ((i % n) + n) % n
        guard next != flavor else { return }
        table[flavor] = sundae
        sundae = table[next]
        sundae[IS.flavor] = Int32(next)
        load()
        scheduleSave()
        objectWillChange.send()
    }

    // Back to how the flavor was designed.
    func freshScoop() {
        sundae = Self.recipe(flavor)
        load()
        scheduleSave()
        showPill("Fresh scoop")
        objectWillChange.send()
    }

    func resetAll() {
        let f = flavor
        table = (0..<Self.flavors.count).map { Self.recipe($0) }
        sundae = table[f]
        load()
        scheduleSave()
        showPill("All flavors fresh")
        objectWillChange.send()
    }

    // ---------------------------------------------------------------- knob pages

    func params(_ f: Focus) -> [IParam] {
        func p(_ name: String, _ field: Int, _ format: @escaping (Int) -> String = { "\($0)" }) -> IParam {
            IParam(name: name, field: field, format: format)
        }
        let pct: (Int) -> String = { "\($0)%" }
        switch f {
        case .scoop:
            return [p("Sweet", IS.sweet), p("Melt", IS.melt), p("Fizz", IS.fizz), p("Scoops", IS.scoops)]
        case .cone:
            let styles = ["Waffle", "Sugar", "Cup"]
            return [p("Style", IS.coneStyle, { styles[max(0, min(2, $0))] }),
                    p("Toast", IS.toast, { v in
                        let hz = 100.0 * pow(200.0, Double(v) / 107.0)
                        return hz >= 1000 ? String(format: "%.1fk", hz / 1000) : String(format: "%.0f", hz)
                    }),
                    p("Crunch", IS.crunch), IParam(name: "Cone", field: IS.coneOn, format: IScreamModel.onOff, isSwitch: true)]
        default:
            let t = f.topping!
            let names = [["Mix", "Rate", "Depth"], ["Gain", "Tone", "Level"], ["Drizzle", "Rate", "Depth"],
                         ["Mix", "Echo", "Speed"], ["Mix", "Decay", "Room"]][t]
            let rooms = ["Room", "Hall", "Plate"]
            var list = (0..<3).map { k in
                IParam(name: names[k], field: IS.top(t, k + 1), format: pct)
            }
            if t == IS.sprinkles { list[2].format = { rooms[max(0, min(2, $0))] } }
            list.append(IParam(name: f.short, field: IS.top(t, 0), format: IScreamModel.onOff, isSwitch: true))
            return list
        }
    }

    static let onOff: (Int) -> String = { $0 != 0 ? "On" : "Off" }

    // Whether the focused cone or topping is on.
    func isOn(_ f: Focus) -> Bool {
        switch f {
        case .scoop: return true
        case .cone: return value(IS.coneOn) != 0
        default: return value(IS.top(f.topping!, 0)) != 0
        }
    }

    func switchField(_ f: Focus) -> Int? {
        switch f {
        case .scoop: return nil
        case .cone: return IS.coneOn
        default: return IS.top(f.topping!, 0)
        }
    }

    func toggle(_ f: Focus) {
        guard let field = switchField(f) else { return }
        let on = value(field) == 0
        set(field, on ? 1 : 0)
        showPill(f.title, on ? "On" : "Off")
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
        let ps = params(focus)
        guard k < ps.count else { return }
        let p = ps[k]
        if p.isSwitch {
            let want = delta > 0 ? 1 : 0
            if value(p.field) != want {
                set(p.field, want)
                showPill(focus.title, want != 0 ? "On" : "Off")
            }
            return
        }
        // A knob on something that is off switches it on, so there is no menu to dive for.
        if let sw = switchField(focus), value(sw) == 0 { set(sw, 1) }
        let step = p.range.count > 12 ? 2 : 1
        set(p.field, value(p.field) + delta * step)
        showPill(p.name, p.format(value(p.field)))
    }

    // A click on a chip on the screen: steps short lists round, toggles switches.
    func clickChip(_ k: Int, back: Bool) {
        let ps = params(focus)
        guard k < ps.count else { return }
        let p = ps[k]
        if p.isSwitch { toggle(focus); return }
        if let sw = switchField(focus), value(sw) == 0 { set(sw, 1) }
        let r = p.range
        let dir = back ? -1 : 1
        if r.count <= 12 {
            let v = r.lowerBound + ((value(p.field) - r.lowerBound + dir) % r.count + r.count) % r.count
            set(p.field, v)
        } else {
            set(p.field, value(p.field) + dir * 5)
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
        focus = Focus(rawValue: max(0, min(Focus.allCases.count - 1, focus.rawValue + delta))) ?? focus
        scheduleSave()
    }

    func turnPresets(_ delta: Int) {
        guard delta != 0 else { return }
        selectFlavor(flavor + (delta > 0 ? 1 : -1))
    }

    func turnAlgorithm(_ delta: Int) {
        guard delta != 0 else { return }
        let before = value(IS.scoops)
        set(IS.scoops, before + (delta > 0 ? 1 : -1))
        showPill("Scoops", "\(value(IS.scoops))")
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

    // The top row of buttons picks the cone or a topping; HOME picks the scoop.
    static let focusButtons: [String: Focus] = ["HOME": .scoop, "FX": .cone, "SEL": .cream, "ENV": .fudge,
                                                 "LFO": .caramel, "EDIT": .chips, "GLO": .sprinkles]

    func buttonLegend(_ hw: String) -> String {
        ["HOME": "SCOOP", "FX": "CONE", "SEL": "CREAM", "ENV": "FUDGE", "LFO": "CARML", "EDIT": "CHIPS", "GLO": "SPRNK",
         "SAVE": "RESET", "ARP": "<", "SEQ": ">", "PLAY/STOP": "", "REC": ""][hw] ?? hw
    }

    func press(_ button: String) {
        if mf1_bank_pending() && button == "HOME" {
            mf1_bank_cancel()
            return
        }
        if let f = Self.focusButtons[button] {
            // A second press on the focused cone or topping switches it.
            if focus == f { toggle(f) } else { focus = f }
            scheduleSave()
            return
        }
        switch button {
        case "ARP": turnPresets(-1)
        case "SEQ": turnPresets(1)
        case "SAVE": showPill("Hold RESET for a fresh scoop")
        default: break
        }
    }

    // The long press on RESET.
    func save() { freshScoop() }

    func buttonLit(_ b: String) -> Color? {
        if let f = Self.focusButtons[b] {
            if focus == f { return .white }
            return f != .scoop && isOn(f) ? Color.white.opacity(0.85) : nil
        }
        switch b {
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
        mf1_key_down(Int32(note))
    }

    func keyUp(_ k: Int) {
        guard let note = heldKeys.removeValue(forKey: k) else { return }
        mf1_key_up(Int32(note))
    }

    func isHeld(_ k: Int) -> Bool { heldKeys[k] != nil }

    func releaseAllKeys() {
        for (_, n) in heldKeys { mf1_key_up(Int32(n)) }
        heldKeys.removeAll()
    }

    func shownNote(key k: Int) -> Int { 53 + DeviceModel.semitone(forKey: k) + led.octave * 12 }

    func keyLed(_ k: Int) -> Double {
        let note = shownNote(key: k)
        guard (0...127).contains(note) else { return 0.05 }
        let lit = note < 64 ? (led.soundingLo >> UInt64(note)) & 1 : (led.soundingHi >> UInt64(note - 64)) & 1
        return lit != 0 || heldKeys[k] != nil ? 1 : 0.06
    }

    // ---------------------------------------------------------------- polling

    private static let ledButtons = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "OCT-", "OCT+"]

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
        // The screen moves while a note sounds, and gently when idle; the rest of the time it is still.
        let sounding = mf1_peak() > 0.0002
        var redraw = (sounding && tick % 2 == 0) || (wasSounding && !sounding) || (!sounding && tick % 6 == 0)
        wasSounding = sounding
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

    // A program change from a keyboard picks a flavor; one past the last flavor is put back.
    private func followOutsideChanges() {
        let p = Int(mf1_current_preset())
        guard p != flavor, !mf1_bank_pending() else { return }
        if p < Self.flavors.count {
            selectFlavor(p)
        } else {
            load()
        }
    }

    // ---------------------------------------------------------------- keeping the sundaes

    private struct Stored: Codable {
        var version = 1
        var flavor = 0
        var focus = 0
        var sundaes: [[Int]] = []
    }

    private static var file: URL { Engine.stateDirectory.appendingPathComponent("sundaes.json") }

    private static func readStored() -> Stored? {
        guard let data = try? Data(contentsOf: file) else { return nil }
        if let s = try? JSONDecoder().decode(Stored.self, from: data), s.version == 1 { return s }
        // Unreadable: keep it aside rather than write over it.
        let aside = file.deletingLastPathComponent()
            .appendingPathComponent("sundaes.json.unreadable-\(Int(Date().timeIntervalSince1970))")
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
        table[flavor] = sundae
        let s = Stored(version: 1, flavor: flavor, focus: focus.rawValue, sundaes: table.map { $0.map { Int($0) } })
        if let data = try? JSONEncoder().encode(s) { try? data.write(to: Self.file, options: .atomic) }
    }
}

extension IScreamModel: PanelDriver {
    var labels: PanelLabels {
        PanelLabels(select: "TOPPING", presets: "FLAVOR", algorithm: "SCOOPS")
    }
    func upperKeyLabel(_ i: Int) -> String? { nil }
    func screenView(frame: Int) -> AnyView { AnyView(IScreamScreen(model: self, frame: frame)) }
    func screenInput() -> AnyView { AnyView(IScreamScreenInput(model: self)) }
}
