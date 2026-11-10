import SwiftUI

// One binary, six front ends: the MF-1 emulator and the iScream, Almanac, Woof, Phase and Piano modes (View ›
// iScream Mode and the rest, or --iscream, --almanac, --woof, --phase and --piano), each the same engine behind a
// different face.
Updates.start()
switch Engine.mode {
case .mf1: MF1App.main()
case .iscream: IScreamApp.main()
case .almanac: AlmanacApp.main()
case .woof: WoofApp.main()
case .phase: PhaseApp.main()
case .piano: PianoApp.main()
}
