import SwiftUI

// One binary, two front ends: the MF-1 emulator, and iScream mode (View › iScream Mode, or
// --iscream), which is the same engine behind a different face.
if Engine.isIScream {
    IScreamApp.main()
} else {
    MF1App.main()
}
