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
        .executableTarget(
            name: "MF1",
            dependencies: ["MF1Core"],
            path: "Sources/MF1",
            resources: [.copy("Resources/Banks"), .copy("Resources/Fonts")]
        ),
        .executableTarget(
            name: "mf1-render",
            dependencies: ["MF1Core"],
            path: "Sources/mf1-render"
        ),
    ],
    cxxLanguageStandard: .cxx17
)
