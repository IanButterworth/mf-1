import SwiftUI

// One binary, four front ends: the MF-1 emulator, iScream mode, Almanac mode and Woof mode (View ›
// iScream Mode, Almanac Mode or Woof Mode, or --iscream, --almanac and --woof), each the same engine
// behind a different face.
switch Engine.mode {
case .mf1: MF1App.main()
case .iscream: IScreamApp.main()
case .almanac: AlmanacApp.main()
case .woof: WoofApp.main()
}
