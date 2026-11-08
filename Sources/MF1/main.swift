import SwiftUI

// One binary, three front ends: the MF-1 emulator, iScream mode and Almanac mode (View › iScream Mode
// or Almanac Mode, or --iscream and --almanac), each the same engine behind a different face.
switch Engine.mode {
case .mf1: MF1App.main()
case .iscream: IScreamApp.main()
case .almanac: AlmanacApp.main()
}
