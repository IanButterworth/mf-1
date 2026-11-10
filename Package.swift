// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "MF1",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "MF1", targets: ["MF1"]),
    ],
    dependencies: [
        // Updates for the released app.
        .package(url: "https://github.com/sparkle-project/Sparkle", exact: "2.10.1"),
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
        // Almanac: twelve months as twelve sounds, with their weather, places and moods.
        .target(
            name: "AlmanacCore",
            dependencies: ["MF1Core"],
            path: "Sources/AlmanacCore",
            cxxSettings: [
                .unsafeFlags(["-O2"]),
            ]
        ),
        // Woof: a dog show, every key a bark.
        .target(
            name: "WoofCore",
            dependencies: ["MF1Core"],
            path: "Sources/WoofCore",
            cxxSettings: [
                .unsafeFlags(["-O2"]),
            ]
        ),
        // Phase: process music, a short cell becoming two or more parts that drift and lock.
        .target(
            name: "PhaseCore",
            dependencies: ["MF1Core"],
            path: "Sources/PhaseCore",
            cxxSettings: [
                .unsafeFlags(["-O2"]),
            ]
        ),
        // Piano: a grand piano from measurements of a real one, played by its own engine in place of the DX7 voices.
        // No fast-math, and no fused multiply-adds, so it computes the same samples as the FM-1 firmware.
        .target(
            name: "PianoCore",
            dependencies: ["MF1Core"],
            path: "Sources/PianoCore",
            exclude: ["tools"],
            cSettings: [
                .unsafeFlags(["-O2", "-ffp-contract=off"]),
            ],
            cxxSettings: [
                .unsafeFlags(["-O2", "-ffp-contract=off"]),
            ]
        ),
        .executableTarget(
            name: "MF1",
            dependencies: ["MF1Core", "IScreamCore", "AlmanacCore", "WoofCore", "PhaseCore", "PianoCore",
                           .product(name: "Sparkle", package: "Sparkle")],
            path: "Sources/MF1",
            resources: [.copy("Resources/Banks"), .copy("Resources/Fonts")]
        ),
        .executableTarget(
            name: "mf1-render",
            dependencies: ["MF1Core", "IScreamCore", "AlmanacCore", "WoofCore", "PhaseCore", "PianoCore"],
            path: "Sources/mf1-render"
        ),
    ],
    cxxLanguageStandard: .cxx17
)
