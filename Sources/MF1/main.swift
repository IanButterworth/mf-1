import SwiftUI

// One binary, two front ends: the MF-1 emulator, and iScream when the app is called iScream
// (or started with --iscream), which is the same engine behind a different face.
if Engine.isIScream {
    IScreamApp.main()
} else {
    MF1App.main()
}
