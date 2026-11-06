import SwiftUI

// What the panel needs from whatever runs behind it. The MF-1 device model is one; iScream's
// sundae model is the other. The panel draws knobs, buttons, keys and the screen, and hands
// every touch to the driver.
protocol PanelDriver: ObservableObject {
    var labels: PanelLabels { get }
    // The legend on a button, given the name it has on the hardware.
    func buttonLegend(_ hardwareName: String) -> String
    // The small label on one of the 11 upper keys, if any.
    func upperKeyLabel(_ i: Int) -> String?

    func turnMaster(by delta: Double)
    func turnSelect(_ delta: Int)
    func turnPresets(_ delta: Int)
    func turnAlgorithm(_ delta: Int)
    func turnKnob(_ k: Int, _ delta: Int)

    func press(_ button: String)
    // A long press on SAVE.
    func save()
    func buttonLit(_ button: String) -> Color?
    func octDown(_ dir: Int)
    func octUp(_ dir: Int)

    func keyDown(_ k: Int)
    func keyUp(_ k: Int)
    func keyLed(_ k: Int) -> Double
    func isHeld(_ k: Int) -> Bool
    func shownNote(key k: Int) -> Int

    func screenView(frame: Int) -> AnyView
    func screenInput() -> AnyView
}

struct PanelLabels {
    var master = "MASTER"
    var select = "SELECT"
    var presets = "PRESETS"
    var algorithm = "ALGORITHM"
    var knobs = ["KNOB1", "KNOB2", "KNOB3", "KNOB4"]
}

extension DeviceModel: PanelDriver {
    var labels: PanelLabels { PanelLabels() }
    func buttonLegend(_ hardwareName: String) -> String { hardwareName }
    func upperKeyLabel(_ i: Int) -> String? {
        ["OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO", "MONO", "POLY", nil][i]
    }
    func screenView(frame: Int) -> AnyView { AnyView(ScreenView(model: self, frame: frame)) }
    func screenInput() -> AnyView { AnyView(ScreenInput(model: self)) }
}
