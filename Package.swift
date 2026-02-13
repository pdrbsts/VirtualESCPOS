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
                "main.cpp", // Windows entry point, excluded
                "build.bat",
                "icon.ico",
                "resource.h",
                "version.rc",
                "bin",
                ".git",
                ".gitignore",
                "README.md",
                "Mac/Info.plist" // Will be handled if we use an actual Xcode project, but for SwiftPM executable we just compile sources
            ],
            sources: [
                "Network.cpp",
                "VirtualPrinter.cpp",
                "Mac/main.m",
                "Mac/AppDelegate.mm", // Boxed C++ 
                "Mac/PrinterView.mm"
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
                    "-Xlinker", "Mac/Info.plist"
                ])
            ]
        )
    ],
    cxxLanguageStandard: .cxx17
)
