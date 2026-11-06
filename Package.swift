// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "MF1",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "MF1", targets: ["MF1"]),
    ],
    targets: [
        .target(
            name: "MF1Core",
            path: "Sources/MF1Core",
            cxxSettings: [
                .unsafeFlags(["-O3", "-ffast-math", "-fobjc-arc"]),
            ],
            linkerSettings: [
                .linkedFramework("CoreAudio"),
                .linkedFramework("AudioToolbox"),
                .linkedFramework("CoreMIDI"),
            ]
        ),
        // iScream: flavors, toppings and how a sundae becomes an FM-1 voice and effects.
        .target(
            name: "IScreamCore",
            dependencies: ["MF1Core"],
            path: "Sources/IScreamCore",
            cxxSettings: [
                .unsafeFlags(["-O2"]),
            ]
        ),
        .executableTarget(
            name: "MF1",
            dependencies: ["MF1Core", "IScreamCore"],
            path: "Sources/MF1",
            resources: [.copy("Resources/Banks"), .copy("Resources/Fonts")]
        ),
        .executableTarget(
            name: "mf1-render",
            dependencies: ["MF1Core", "IScreamCore"],
            path: "Sources/mf1-render"
        ),
    ],
    cxxLanguageStandard: .cxx17
)
