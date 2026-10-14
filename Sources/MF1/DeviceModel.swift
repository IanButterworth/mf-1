import Foundation
import MF1Core
import SwiftUI

enum Mode { case home, fx, env, lfo, edit, glo, arp, seq }

// Upper row keys in EDIT mode.
enum EditTarget: Int { case op1, op2, op3, op4, op5, op6, pit, glo }

enum FxEffect: Int, CaseIterable {
    case filter, reverb, delay, distortion, chorus, phaser
    var title: String {
        ["Filter", "Reverb", "Delay", "Distortion", "Chorus", "Phaser"][rawValue]
    }
    // CC numbers for KNOB1..3 and the on/off switch on KNOB4.
    var ccs: [Int32] {
        [[1, 2, 3, 0], [5, 6, 7, 4], [9, 10, 11, 8], [13, 14, 15, 12], [17, 18, 19, 16], [21, 22, 23, 20]][rawValue]
    }
}

// One knob slot on a screen page.
struct Param {
    var name: String
    var get: () -> Int = { 0 }
    var set: (Int) -> Void = { _ in }
    var range: ClosedRange<Int> = 0...99
    var format: (Int) -> String = { "\($0)" }
    // Actions fire on a turn instead of changing a value.
    var action: ((Int) -> Void)? = nil
    var bars = false
    var showName = true

    static let empty = Param(name: "")
}

enum Popup: Equatable {
    case presets
    case algorithm
    case value(String, String)
    case text(String)
}

final class DeviceModel: ObservableObject {
    @Published var mode: Mode = .home
    @Published var pages: [Mode: Int] = [:]
    @Published var editTarget: EditTarget = .op1
    @Published var editPages: [EditTarget: Int] = [:]
    @Published var fxSelected = 0
    @Published var fxGrabbed = false
    @Published var pill: String? = nil
    @Published var popup: Popup? = nil
    @Published var renaming: [Character]? = nil
    @Published var renameCursor = 0
    @Published var renameClass = 0
    @Published var copying: Int? = nil
    @Published var resetting = false
    @Published var resetBank = 0
    @Published var octHeld: Set<Int> = []
    @Published var saving = false
    @Published var tick = 0  // drives LED blinking and redraws

    private var pillTimer: Timer?
    private var popupTimer: Timer?
    private var heldKeys: [Int: Int] = [:]  // key index -> note sent
    private var lastGeneration: UInt32 = 0
    private var transposeTouched = false

    // ---------------------------------------------------------------- state

    var preset: Int { Int(mf1_current_preset()) }
    var presetTitle: String { String(format: "%03d %@", preset + 1, Engine.editName()) }
    var octave: Int { Int(mf1_global_get(Int32(MF1_G_OCTAVE))) }
    var transpose: Int { Int(mf1_global_get(Int32(MF1_G_TRANSPOSE))) }
    var arpOn: Bool { mf1_arp_on() }
    var seqOn: Bool { mf1_seq_on() }

    func page(_ m: Mode) -> Int { pages[m] ?? 0 }

    var pageCount: Int {
        switch mode {
        case .lfo, .glo, .arp: return 2
        case .seq: return 3
        case .edit:
            switch editTarget {
            case .pit, .glo: return 2
            default: return 6
            }
        default: return 1
        }
    }

    var currentPage: Int {
        mode == .edit ? (editPages[editTarget] ?? 0) : page(mode)
    }

    var pageTitle: String {
        let n = pageCount
        let name: String
        switch mode {
        case .home: return "OSC"
        case .fx: name = "FX"
        case .env: return "ENV"
        case .lfo: name = "LFO"
        case .glo: name = "Global"
        case .arp: name = "Arpeggio"
        case .seq: name = "Sequencer"
        case .edit:
            switch editTarget {
            case .pit: name = "Pitch EG"
            case .glo: name = "Voice"
            default: name = "OP\(editTarget.rawValue + 1)"
            }
        }
        return n > 1 ? "\(currentPage + 1)/\(n) \(name)" : name
    }

    // ---------------------------------------------------------------- pages

    private func voice(_ index: Int, _ name: String, _ range: ClosedRange<Int> = 0...99,
                       format: @escaping (Int) -> String = { "\($0)" }) -> Param {
        Param(name: name, get: { Int(mf1_edit_get(Int32(index))) },
              set: { mf1_edit_set(Int32(index), Int32($0)) }, range: range, format: format)
    }

    private func global(_ id: Int32, _ name: String, _ range: ClosedRange<Int>,
                        format: @escaping (Int) -> String = { "\($0)" }) -> Param {
        Param(name: name, get: { Int(mf1_global_get(id)) }, set: { mf1_global_set(id, Int32($0)) },
              range: range, format: format)
    }

    private func arp(_ id: Int32, _ name: String, _ range: ClosedRange<Int>,
                     format: @escaping (Int) -> String = { "\($0)" }) -> Param {
        Param(name: name, get: { Int(mf1_arp_get(id)) }, set: { mf1_arp_set(id, Int32($0)) },
              range: range, format: format)
    }

    private func seq(_ id: Int32, _ name: String, _ range: ClosedRange<Int>,
                     format: @escaping (Int) -> String = { "\($0)" }) -> Param {
        Param(name: name, get: { Int(mf1_seq_get(id)) }, set: { mf1_seq_set(id, Int32($0)) },
              range: range, format: format)
    }

    static let onOff: (Int) -> String = { $0 != 0 ? "On" : "Off" }
    static let percent: (Int) -> String = { "\($0)%" }
    static let rate: (Int) -> String = { String(cString: mf1_rate_name(Int32($0))) }

    // Yamaha octave numbering, where middle C (MIDI 60) is C3.
    static func noteName(_ n: Int) -> String {
        let names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        return "\(names[(n % 12 + 12) % 12])\(n / 12 - 2)"
    }

    var params: [Param] {
        switch mode {
        case .home: return []
        case .fx: return fxParams
        case .env:
            return ["Attack", "Decay", "Sustain", "Release"].enumerated().map { i, n in
                Param(name: n, get: { Int(mf1_env_get(Int32(i + 1))) }, set: { mf1_env_set(Int32(i + 1), Int32($0)) },
                      range: 0...100, format: DeviceModel.percent, bars: true)
            }
        case .lfo:
            if page(.lfo) == 0 {
                let waves = ["Triangle", "Saw Down", "Saw Up", "Square", "Sine", "S&Hold"]
                return [voice(142, "Wave", 0...5, format: { waves[$0] }), voice(137, "Speed"),
                        voice(139, "PMD"), voice(140, "AMD")]
            }
            return [voice(138, "Delay"), voice(143, "Pitch Sens", 0...7),
                    voice(141, "Lfo Sync", 0...1, format: DeviceModel.onOff),
                    voice(136, "Osc Sync", 0...1, format: DeviceModel.onOff)]
        case .glo:
            if page(.glo) == 0 {
                return [global(Int32(MF1_G_MIDI_CH), "MIDI Ch", 0...16, format: { $0 == 0 ? "ALL" : "\($0)" }),
                        global(Int32(MF1_G_FX_CH), "Effect Ch", 1...16),
                        global(Int32(MF1_G_BEND_UP), "Pitch Up", 1...24),
                        global(Int32(MF1_G_BEND_DOWN), "Pitch Dn", 1...24)]
            }
            return [Param(name: "Reset Patches", action: { [weak self] d in if d > 0 { self?.beginReset() } }),
                    global(Int32(MF1_G_VELOCITY), "Velocity", 1...127),
                    global(Int32(MF1_G_GLIDE_MODE), "Glide", 0...1, format: { $0 == 0 ? "Full Time" : "Fingered" }),
                    global(Int32(MF1_G_GLIDE_TIME), "Glide Val", 0...100)]
        case .arp:
            if page(.arp) == 0 {
                return [arp(Int32(MF1_A_MODE), "Mode", 0...6, format: { String(cString: mf1_arp_mode_name(Int32($0))) })
                            .with { $0.showName = false },
                        arp(Int32(MF1_A_OCTAVES), "Octave", 1...4),
                        arp(Int32(MF1_A_RATE), "Rate", 0...(Int(mf1_rate_count()) - 1), format: DeviceModel.rate),
                        global(Int32(MF1_G_TEMPO), "Tempo", 30...300)]
            }
            return [arp(Int32(MF1_A_GATE), "Gate", 20...100, format: DeviceModel.percent),
                    arp(Int32(MF1_A_SWING), "Swing", 50...75, format: DeviceModel.percent),
                    arp(Int32(MF1_A_LATCH), "Latch", 0...1, format: DeviceModel.onOff),
                    arp(Int32(MF1_A_SYNC), "Sync", 0...1, format: DeviceModel.onOff)]
        case .seq:
            switch page(.seq) {
            case 0:
                return [seq(Int32(MF1_S_PATTERN), "Pattern", 0...15, format: { "\($0 + 1)" }),
                        Param(name: "Clear", action: { [weak self] d in
                            if d > 0 {
                                mf1_seq_clear_pattern()
                                self?.flash("Pattern Cleared")
                            }
                        }).with { $0.showName = true },
                        seq(Int32(MF1_S_CHAIN), "Chain", 1...16),
                        seq(Int32(MF1_S_STEPS), "Step", 1...16)]
            case 1:
                return [seq(Int32(MF1_S_VOICE), "Voice", 0...127, format: { String(format: "%03d", $0 + 1) }),
                        seq(Int32(MF1_S_RATE), "Rate", 0...(Int(mf1_rate_count()) - 1), format: DeviceModel.rate),
                        global(Int32(MF1_G_TEMPO), "Tempo", 30...300),
                        seq(Int32(MF1_S_GATE), "Gate", 20...100, format: DeviceModel.percent)]
            default:
                return [seq(Int32(MF1_S_SWING), "Swing", 50...75, format: DeviceModel.percent),
                        seq(Int32(MF1_S_SYNC), "Sync", 0...1, format: DeviceModel.onOff),
                        seq(Int32(MF1_S_TRANSPOSE), "Transpose", -24...24, format: { $0 > 0 ? "+\($0)" : "\($0)" }),
                        .empty]
            }
        case .edit: return editParams
        }
    }

    private var fxParams: [Param] {
        let effect = FxEffect(rawValue: Int(mf1_fx_order(Int32(fxSelected))))!
        let c = effect.ccs
        func p(_ cc: Int32, _ name: String, _ range: ClosedRange<Int>, _ f: @escaping (Int) -> String) -> Param {
            Param(name: name, get: { Int(mf1_fx_get(cc)) }, set: { mf1_fx_set(cc, Int32($0)) }, range: range, format: f)
        }
        let onOff = Param(name: "Switch", get: { Int(mf1_fx_get(c[3])) },
                          action: { [weak self] d in
                              mf1_fx_set(c[3], d > 0 ? 1 : 0)
                              self?.showPill(effect.title, d > 0 ? "On" : "Off")
                          })
        switch effect {
        case .filter:
            return [p(c[0], "Type", 0...2, { ["Low Pass", "Band Pass", "High Pass"][$0] }),
                    p(c[1], "CutOff", 0...107, { v in
                        let hz = 100.0 * pow(200.0, Double(v) / 107.0)
                        return hz >= 1000 ? String(format: "%.1fkHz", hz / 1000) : String(format: "%.0fHz", hz)
                    }),
                    p(c[2], "Q", 0...100, { "\($0)" }), onOff]
        case .reverb:
            return [p(c[0], "Type", 0...2, { ["Room", "Hall", "Plate"][$0] }),
                    p(c[1], "Decay", 0...100, DeviceModel.percent), p(c[2], "Mix", 0...100, DeviceModel.percent), onOff]
        case .delay:
            return [p(c[0], "Decay", 0...100, DeviceModel.percent), p(c[1], "Rate", 0...100, DeviceModel.percent),
                    p(c[2], "Mix", 0...100, DeviceModel.percent), onOff]
        case .distortion:
            return [p(c[0], "Gain", 0...100, DeviceModel.percent), p(c[1], "Tone", 0...100, DeviceModel.percent),
                    p(c[2], "Level", 0...100, DeviceModel.percent), onOff]
        case .chorus, .phaser:
            return [p(c[0], "Freq", 0...100, DeviceModel.percent), p(c[1], "Depth", 0...100, DeviceModel.percent),
                    p(c[2], "Mix", 0...100, DeviceModel.percent), onOff]
        }
    }

    private var editParams: [Param] {
        let page = editPages[editTarget] ?? 0
        switch editTarget {
        case .pit:
            let base = page == 0 ? 126 : 130
            let n = page == 0 ? "Rate" : "Level"
            return (0..<4).map { voice(base + $0, "\(n) \($0 + 1)") }
        case .glo:
            if page == 0 {
                return [voice(134, "Algorithm", 0...31, format: { "\($0 + 1)" }), voice(135, "Feedback", 0...7),
                        voice(136, "Osc Sync", 0...1, format: DeviceModel.onOff),
                        voice(144, "Transpose", 0...48, format: { DeviceModel.noteName($0 + 36) })]
            }
            return [Param(name: "Init Voice", action: { [weak self] d in
                        if d > 0 {
                            mf1_init_voice()
                            self?.flash("Init Voice")
                        }
                    }),
                    Param(name: "Rename", action: { [weak self] d in if d > 0 { self?.beginRename() } }),
                    Param(name: "Copy Voice", action: { [weak self] d in
                        if d > 0 { self?.copying = self?.preset } else { self?.copying = nil }
                    }),
                    .empty]
        default:
            let base = (5 - editTarget.rawValue) * 21
            switch page {
            case 0:
                return [voice(base + 20, "Detune", 0...14, format: { v in v > 7 ? "+\(v - 7)" : "\(v - 7)" }),
                        voice(base + 18, "FreqCoarse", 0...31), voice(base + 19, "FreqFine"),
                        voice(base + 16, "Out Level")]
            case 1: return (0..<4).map { voice(base + $0, "Rate \($0 + 1)") }
            case 2: return (0..<4).map { voice(base + 4 + $0, "Level \($0 + 1)") }
            case 3:
                return [voice(base + 14, "A ModSens", 0...3), voice(base + 15, "KeyVelocity", 0...7),
                        voice(base + 17, "OscMode", 0...1, format: { $0 == 0 ? "Ratio" : "Fixed" }), .empty]
            case 4:
                return [voice(base + 9, "L Depth"), voice(base + 10, "R Depth"),
                        voice(base + 8, "BreakPoint", format: { DeviceModel.noteName($0 + 21) }), .empty]
            default:
                let curves = ["-LN", "-EX", "+EX", "+LN"]
                return [voice(base + 11, "L Curve", 0...3, format: { curves[$0] }),
                        voice(base + 12, "R Curve", 0...3, format: { curves[$0] }),
                        voice(base + 13, "RateScale", 0...7), .empty]
            }
        }
    }

    // ---------------------------------------------------------------- feedback

    func showPill(_ name: String, _ value: String) {
        pill = "\(name): \(value)"
        pillTimer?.invalidate()
        pillTimer = Timer.scheduledTimer(withTimeInterval: 1.2, repeats: false) { [weak self] _ in
            self?.pill = nil
        }
    }

    func show(_ p: Popup, for seconds: Double = 1.0) {
        popup = p
        popupTimer?.invalidate()
        popupTimer = Timer.scheduledTimer(withTimeInterval: seconds, repeats: false) { [weak self] _ in
            self?.popup = nil
        }
    }

    func flash(_ text: String) { show(.text(text), for: 1.0) }

    // ---------------------------------------------------------------- knobs

    func turnKnob(_ k: Int, _ delta: Int) {
        guard delta != 0 else { return }
        if mf1_bank_pending() {
            // The bank picker after a bulk dump: KNOB1..4 choose A..D.
            if delta > 0 {
                mf1_bank_commit(Int32(k))
                flash("Saved to Bank \(["A", "B", "C", "D"][k])")
            }
            return
        }
        if resetting {
            resetBank = k
            return
        }
        if var name = renaming {
            switch k {
            case 0: renameCursor = max(0, min(9, renameCursor + delta))
            case 1: if delta < 0 { renaming = nil; flash("Rename Cancelled") }
            case 2:
                let set = Self.renameSets[renameClass]
                let cur = name[renameCursor]
                let ix = set.firstIndex(of: cur) ?? 0
                name[renameCursor] = set[((ix + delta) % set.count + set.count) % set.count]
                renaming = name
            default: break
            }
            return
        }
        if copying != nil {
            if k == 3 {
                copying = ((copying! + delta) % 128 + 128) % 128
            } else if k == 2 && delta < 0 {
                copying = nil
            }
            return
        }
        let ps = params
        guard k < ps.count else { return }
        let p = ps[k]
        if p.name.isEmpty { return }
        if let a = p.action {
            a(delta)
            objectWillChange.send()
            return
        }
        let v = max(p.range.lowerBound, min(p.range.upperBound, p.get() + delta))
        p.set(v)
        showPill(p.name, p.format(v))
        if mode == .edit && editTarget == .glo && currentPage == 0 && k == 0 { show(.algorithm) }
        objectWillChange.send()
    }

    func turnSelect(_ delta: Int) {
        guard delta != 0 else { return }
        if resetting {
            if delta > 0 {
                mf1_reset_bank(Int32(resetBank))
                flash("Reset Success")
            }
            resetting = false
            return
        }
        if var name = renaming {
            if delta > 0 {
                while name.last == " " && name.count > 0 { name.removeLast() }
                mf1_rename(String(name))
                renaming = nil
                flash("Renamed")
            }
            return
        }
        if let src = copying {
            if delta > 0 {
                mf1_copy_voice(Int32(src))
                flash("Voice Copied")
            }
            copying = nil
            return
        }
        if seqOn && mf1_seq_recording() {
            mf1_seq_select(Int32(delta))
            return
        }
        switch mode {
        case .fx:
            let next = max(0, min(5, fxSelected + delta))
            if fxGrabbed && next != fxSelected { mf1_fx_swap(Int32(fxSelected), Int32(next)) }
            fxSelected = next
        case .edit:
            let n = pageCount
            editPages[editTarget] = max(0, min(n - 1, (editPages[editTarget] ?? 0) + delta))
        default:
            pages[mode] = max(0, min(pageCount - 1, page(mode) + delta))
        }
    }

    func turnPresets(_ delta: Int) {
        guard delta != 0, !seqOn else { return }
        mf1_load_preset(Int32(((preset + delta) % 128 + 128) % 128))
        show(.presets, for: 1.2)
        objectWillChange.send()
    }

    func turnAlgorithm(_ delta: Int) {
        guard delta != 0 else { return }
        if !octHeld.isEmpty {
            // Hold OCT and turn ALGORITHM for transpose.
            let t = max(-12, min(12, transpose + delta))
            mf1_global_set(Int32(MF1_G_TRANSPOSE), Int32(t))
            transposeTouched = true
            show(.value("Transpose", t > 0 ? "+\(t)" : "\(t)"))
            return
        }
        let a = max(0, min(31, Int(mf1_edit_get(134)) + delta))
        mf1_edit_set(134, Int32(a))
        show(.algorithm, for: 1.2)
    }

    func turnMaster(_ value: Int) {
        mf1_global_set(Int32(MF1_G_VOLUME), Int32(value))
        show(.value("Volume", "\(value)"))
    }

    // ---------------------------------------------------------------- buttons

    func press(_ button: String) {
        if mf1_bank_pending() && button == "HOME" {
            mf1_bank_cancel()
            return
        }
        renaming = nil
        copying = nil
        resetting = false
        switch button {
        case "HOME": mode = .home
        case "FX": toggle(.fx)
        case "LFO": toggle(.lfo)
        case "EDIT": toggle(.edit)
        case "GLO": toggle(.glo)
        case "ENV":
            // ENV switches the envelope on and shows it; pressing it again
            // switches it off.
            if mode == .env {
                mf1_env_set(0, 0)
                mode = .home
            } else {
                mf1_env_set(0, 1)
                mode = .env
            }
        case "SEL":
            if mode == .fx { fxGrabbed.toggle() }
        case "ARP":
            if seqOn {
                flash("Disable SEQ to enter")
                return
            }
            if mode == .arp {
                mf1_arp_enable(false)
                mode = .home
            } else {
                mf1_arp_enable(true)
                mode = .arp
            }
        case "SEQ":
            if arpOn {
                flash("Disable ARP to enter")
                return
            }
            if mode == .seq || seqOn {
                mf1_seq_enable(false)
                mode = .home
            } else {
                mf1_seq_enable(true)
                mode = .seq
            }
        case "PLAY":
            guard seqOn else { flash("Enable SEQ first"); return }
            if mf1_seq_playing() || mf1_seq_recording() {
                mf1_seq_stop()
            } else {
                mf1_seq_play()
            }
        case "REC":
            guard seqOn else { flash("Enable SEQ first"); return }
            mf1_seq_record(!mf1_seq_recording())
        default: break
        }
        objectWillChange.send()
    }

    private func toggle(_ m: Mode) {
        mode = mode == m ? .home : m
        if m != .fx { fxGrabbed = false }
    }

    func save() {
        mf1_save()
        saving = true
        flash("Preset Saved")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) { self.saving = false }
    }

    func octDown(_ dir: Int) {
        octHeld.insert(dir)
        transposeTouched = false
        if octHeld.count == 2 {
            mf1_global_set(Int32(MF1_G_OCTAVE), 0)
            mf1_global_set(Int32(MF1_G_TRANSPOSE), 0)
            transposeTouched = true
            show(.value("Octave", "0"))
        }
    }

    func octUp(_ dir: Int) {
        octHeld.remove(dir)
        if !transposeTouched && octHeld.isEmpty {
            let o = max(-3, min(3, octave + dir))
            mf1_global_set(Int32(MF1_G_OCTAVE), Int32(o))
            show(.value("Octave", o > 0 ? "+\(o)" : "\(o)"))
        }
        if octHeld.isEmpty { transposeTouched = false }
    }

    // ---------------------------------------------------------------- keys

    // Key index 0..26: lower row is 0..15, upper row 16..26.
    static let upperOffsets = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
    static let lowerOffsets = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]

    static func semitone(forKey k: Int) -> Int {
        k < 16 ? lowerOffsets[k] : upperOffsets[k - 16]
    }

    func noteFor(key k: Int) -> Int { 53 + DeviceModel.semitone(forKey: k) + octave * 12 + transpose }

    func keyDown(_ k: Int) {
        if mode == .edit && k >= 16 {
            editKey(k - 16)
            return
        }
        if let name = renaming, k >= 16, k - 16 < 5 {
            if k - 16 == 4 {
                var n = name
                n[renameCursor] = " "
                renaming = n
            } else {
                renameClass = k - 16
            }
            return
        }
        guard heldKeys[k] == nil else { return }
        let note = noteFor(key: k)
        guard (0...127).contains(note) else { return }
        heldKeys[k] = note
        mf1_key_down(Int32(note))
    }

    func keyUp(_ k: Int) {
        if let s = editKeyDownAt, k >= 16, s.key == k - 16 {
            editKeyDownAt = nil
        }
        guard let note = heldKeys.removeValue(forKey: k) else { return }
        mf1_key_up(Int32(note))
    }

    func isHeld(_ k: Int) -> Bool { heldKeys[k] != nil }

    func releaseAllKeys() {
        for (_, n) in heldKeys { mf1_key_up(Int32(n)) }
        heldKeys.removeAll()
    }

    private var editKeyDownAt: (key: Int, time: Date)?

    private func editKey(_ i: Int) {
        switch i {
        case 0...5:
            editTarget = EditTarget(rawValue: i)!
            editKeyDownAt = (i, Date())
            // A long press mutes the operator until the preset changes.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.7) { [weak self] in
                guard let self, let d = self.editKeyDownAt, d.key == i else { return }
                let m = Int(mf1_op_mutes()) ^ (1 << i)
                mf1_set_op_mutes(Int32(m))
                self.flash(m & (1 << i) != 0 ? "OP\(i + 1) Muted" : "OP\(i + 1) On")
            }
        case 6: editTarget = .pit
        case 7: editTarget = .glo
        case 8:
            mf1_edit_set_mono(true)
            flash("Mono")
        case 9:
            mf1_edit_set_mono(false)
            flash("Poly")
        default: break
        }
    }

    // ---------------------------------------------------------------- rename, reset

    static let renameSets: [[Character]] = [
        Array("ABCDEFGHIJKLMNOPQRSTUVWXYZ "),
        Array("abcdefghijklmnopqrstuvwxyz "),
        Array(" !\"#$%&'()*+,-./:;<=>?@[]^_`{|}~"),
        Array("0123456789 "),
    ]

    func beginRename() {
        var chars = Array(Engine.editName())
        while chars.count < 10 { chars.append(" ") }
        renaming = Array(chars.prefix(10))
        renameCursor = 0
        renameClass = 0
    }

    func beginReset() {
        resetting = true
        resetBank = preset / 32
    }

    // Typing on the Mac keyboard while renaming.
    func typeRename(_ c: Character) {
        guard var name = renaming else { return }
        name[renameCursor] = c
        renaming = name
        renameCursor = min(9, renameCursor + 1)
    }

    func renameBackspace() {
        guard var name = renaming else { return }
        renameCursor = max(0, renameCursor - 1)
        name[renameCursor] = " "
        renaming = name
    }

    // ---------------------------------------------------------------- polling

    func poll() {
        tick &+= 1
        let g = mf1_generation()
        if g != lastGeneration {
            lastGeneration = g
            var buf = [CChar](repeating: 0, count: 64)
            mf1_take_message(&buf)
            let msg = String(cString: buf)
            if !msg.isEmpty { flash(msg) }
            objectWillChange.send()
        }
    }

    // ---------------------------------------------------------------- LEDs

    func buttonLit(_ b: String) -> Color? {
        let blink = (tick / 15) % 2 == 0
        switch b {
        case "FX": return mode == .fx ? .white : nil
        case "ENV": return mf1_env_get(0) != 0 ? .white : nil
        case "LFO": return mode == .lfo ? .white : nil
        case "EDIT": return mode == .edit ? .white : nil
        case "GLO": return mode == .glo ? .white : nil
        case "HOME": return mode == .home ? .white : nil
        case "SEL": return fxGrabbed && mode == .fx ? .white : nil
        case "SAVE": return saving ? .white : (mf1_dirty() && blink ? Color.white.opacity(0.35) : nil)
        case "ARP": return arpOn ? .white : nil
        case "SEQ": return seqOn ? .white : nil
        case "PLAY":
            guard seqOn else { return nil }
            return mf1_seq_playing() ? Color(red: 0.3, green: 1, blue: 0.45) : Color(red: 1, green: 0.42, blue: 0.2)
        case "REC": return mf1_seq_recording() ? Color(red: 1, green: 0.2, blue: 0.2) : nil
        case "OCT-":
            return octaveLed(-1)
        case "OCT+":
            return octaveLed(1)
        default: return nil
        }
    }

    private func octaveLed(_ side: Int) -> Color? {
        let o = octave
        guard o != 0, (o > 0) == (side > 0) else { return nil }
        // 1 = slow blink, 2 = fast blink, 3 = solid.
        switch abs(o) {
        case 1: return (tick / 30) % 2 == 0 ? .white : nil
        case 2: return (tick / 8) % 2 == 0 ? .white : nil
        default: return .white
        }
    }

    // Brightness 0...1 for the LED in key k.
    func keyLed(_ k: Int, sounding: (UInt64, UInt64)) -> Double {
        let blinkOn = (tick / 15) % 2 == 0
        if mode == .edit && k >= 16 {
            let i = k - 16
            switch i {
            case 0...7:
                let selected = editTarget.rawValue == i
                if i < 6 && Int(mf1_op_mutes()) & (1 << i) != 0 { return selected ? 0.5 : 0.08 }
                return selected ? 1 : (blinkOn ? 0.8 : 0.1)
            case 8: return mf1_edit_mono() ? 1 : (blinkOn ? 0.8 : 0.1)
            case 9: return mf1_edit_mono() ? (blinkOn ? 0.8 : 0.1) : 1
            default: return 0.05
            }
        }
        if seqOn && k < 16 {
            let mask = Int(mf1_seq_step_mask())
            let steps = Int(mf1_seq_get(Int32(MF1_S_STEPS)))
            if k >= steps { return 0.04 }
            let playing = Int(mf1_seq_play_step())
            if mf1_seq_playing() && playing == k { return blinkOn ? 1 : 0.3 }
            if mf1_seq_recording() && !mf1_seq_playing() && Int(mf1_seq_rec_step()) == k {
                return (tick / 6) % 2 == 0 ? 1 : 0.25
            }
            return mask & (1 << k) != 0 ? 0.9 : 0.12
        }
        let note = noteFor(key: k)
        guard (0...127).contains(note) else { return 0.05 }
        let lit = note < 64 ? (sounding.0 >> UInt64(note)) & 1 : (sounding.1 >> UInt64(note - 64)) & 1
        if lit != 0 || heldKeys[k] != nil { return 1 }
        return 0.06
    }
}

extension Param {
    func with(_ f: (inout Param) -> Void) -> Param {
        var c = self
        f(&c)
        return c
    }
}
