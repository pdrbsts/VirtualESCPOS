// swift-tools-version:5.5
import PackageDescription

let package = Package(
    name: "VirtualESCPOS",
    platforms: [
        .macOS(.v10_15)
    ],
    targets: [
        .executableTarget(
            name: "VirtualESCPOS",
            path: ".",
            exclude: [
                "Source/Info.plist"
            ],
            sources: [
                "Network.cpp",
                "VirtualPrinter.cpp",
                "Source/main.m",
                "Source/AppDelegate.mm", // Boxed C++ 
                "Source/PrinterView.mm"
            ],
            publicHeadersPath: "include", // We don't really have public headers, but this quiets some warnings or we can omit
            cxxSettings: [
                .define("MACOS_PORT")
            ],
            linkerSettings: [
                .linkedFramework("Cocoa"),
                .unsafeFlags([
                    "-Xlinker", "-sectcreate",
                    "-Xlinker", "__TEXT",
                    "-Xlinker", "__info_plist",
                    "-Xlinker", "Source/Info.plist"
                ])
            ]
        )
    ],
    cxxLanguageStandard: .cxx17
)
