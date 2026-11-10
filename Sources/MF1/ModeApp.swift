import AppKit
import MF1Core
import SwiftUI

// What iScream and Almanac share: a model behind the panel, and an app delegate that owns the panel window,
// the Mac keyboard and the 60 Hz poll. MF-1 itself has its own delegate.
protocol ModeModel: PanelDriver {
    func poll()
    func writeNow()
    func releaseAllKeys()
    func releaseOct()
}

class ModeDelegate: NSObject, NSApplicationDelegate {
    private(set) var window: PanelWindow?
    private var timer: Timer?
    private var monitor: Any?
    private var computerKeys: [UInt16: Int] = [:]
    private var octKeys: [UInt16: Int] = [:]
    private var seenDown: Set<UInt16> = []

    // Each mode fills these in.
    var model: any ModeModel { fatalError("a mode delegate names its model") }
    var title: String { "" }
    var frameName: String { "" }
    var titledColor: NSColor { .black }
    func startEngine() {}
    func makeContent() -> NSView { NSView() }
    // After launch: snapshots and self checks.
    @MainActor func launched() {}

    func applicationDidFinishLaunching(_ notification: Notification) {
        startEngine()
        let w = PanelWindow(contentRect: NSRect(x: 0, y: 0, width: 1180, height: 1180 * PanelMetrics.height / PanelMetrics.width),
                            styleMask: [.borderless], backing: .buffered, defer: false)
        w.contentView = makeContent()
        w.title = Engine.slot == 1 ? title : "\(title) \(Engine.slot)"
        w.isMovableByWindowBackground = false
        w.contentAspectRatio = NSSize(width: PanelMetrics.width, height: PanelMetrics.height)
        w.isReleasedWhenClosed = false
        window = w
        setFrameless(UserDefaults.standard.object(forKey: "frameless") as? Bool ?? true)
        w.placeAndRemember(frameName)
        w.makeKeyAndOrderFront(nil)
        monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp]) { [weak self] e in
            self?.handle(e) == true ? nil : e
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { [weak self] _ in
            self?.releaseLiftedKeys()
            self?.model.poll()
        }
        RunLoop.main.add(timer!, forMode: .common)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { self.launched() }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        model.writeNow()
        return .terminateNow
    }

    func applicationWillTerminate(_ notification: Notification) { mf1_stop() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        window?.makeKeyAndOrderFront(nil)
        return false
    }

    func applicationDidResignActive(_ notification: Notification) {
        computerKeys.removeAll()
        octKeys.removeAll()
        seenDown.removeAll()
        model.releaseAllKeys()
        model.releaseOct()
        KnobInputView.showCursor()
    }

    func setFrameless(_ frameless: Bool) {
        guard let w = window else { return }
        let frame = w.frame
        w.styleMask = frameless ? [.borderless, .miniaturizable, .resizable]
                                : [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView]
        if !frameless {
            w.titlebarAppearsTransparent = true
            w.titleVisibility = .hidden
        }
        w.isOpaque = !frameless
        w.backgroundColor = frameless ? .clear : titledColor
        w.hasShadow = true
        w.setFrame(frame, display: true)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { w.invalidateShadow() }
    }

    // ---------------------------------------------------------------- Mac keyboard

    @discardableResult
    private func release(_ code: UInt16) -> Bool {
        seenDown.remove(code)
        if let k = computerKeys.removeValue(forKey: code) {
            model.keyUp(k)
            return true
        }
        if let dir = octKeys.removeValue(forKey: code) {
            model.octUp(dir)
            return true
        }
        return false
    }

    private func releaseLiftedKeys() {
        for code in Array(computerKeys.keys) + Array(octKeys.keys) {
            if CGEventSource.keyState(.combinedSessionState, key: code) {
                seenDown.insert(code)
            } else if seenDown.contains(code) {
                release(code)
            }
        }
    }

    private func handle(_ e: NSEvent) -> Bool {
        let down = e.type == .keyDown
        if !down && release(e.keyCode) { return true }
        if e.modifierFlags.contains(.command) { return false }
        if let dir = AppDelegate.octaveKeys[e.keyCode] {
            if down && !e.isARepeat && octKeys[e.keyCode] == nil {
                octKeys[e.keyCode] = dir
                model.octDown(dir)
            }
            return true
        }
        guard let semi = AppDelegate.noteKeys[e.keyCode] else { return false }
        if e.isARepeat { return true }
        let k: Int
        if let i = DeviceModel.lowerOffsets.firstIndex(of: semi) {
            k = i
        } else if let i = DeviceModel.upperOffsets.firstIndex(of: semi) {
            k = 16 + i
        } else {
            return true
        }
        if down {
            computerKeys[e.keyCode] = k
            model.keyDown(k)
        }
        return true
    }
}
