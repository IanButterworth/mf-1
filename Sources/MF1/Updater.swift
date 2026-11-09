import Combine
import Sparkle
import SwiftUI

// Updates come from the appcast on the latest GitHub release, through Sparkle. Only release builds carry a
// feed, so a local build never offers to replace itself, and snapshot runs never check.
enum Updates {
    static let controller: SPUStandardUpdaterController? = {
        let args = CommandLine.arguments
        guard Bundle.main.object(forInfoDictionaryKey: "SUFeedURL") != nil,
              !args.contains("--snapshot"), !args.contains("--selfcheck") else { return nil }
        return SPUStandardUpdaterController(startingUpdater: true, updaterDelegate: nil, userDriverDelegate: nil)
    }()

    fileprivate static let state = controller.map { UpdaterState($0.updater) }

    static func start() { _ = controller }
}

fileprivate final class UpdaterState: ObservableObject {
    @Published var canCheck = false

    init(_ updater: SPUUpdater) {
        updater.publisher(for: \.canCheckForUpdates).assign(to: &$canCheck)
    }
}

// The app menu's "Check for Updates…", shared by every mode.
struct UpdateCommands: Commands {
    var body: some Commands {
        CommandGroup(after: .appInfo) {
            if let state = Updates.state {
                CheckForUpdatesButton(state: state)
            }
        }
    }
}

private struct CheckForUpdatesButton: View {
    @ObservedObject var state: UpdaterState

    var body: some View {
        Button("Check for Updates…") { Updates.controller?.checkForUpdates(nil) }
            .disabled(!state.canCheck)
    }
}
