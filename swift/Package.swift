// swift-tools-version: 6.2
// The swift-tools-version declares the minimum version of Swift required to build this package.

import Foundation
import PackageDescription

let packageRoot = Context.packageDirectory

let package = Package(
    name: "swift",
    platforms: [.macOS(.v26)],
    targets: [
        .executableTarget(
            name: "test",
            swiftSettings: [
                .unsafeFlags(["-I", "\(packageRoot)/.."])
            ],
            linkerSettings: [
                .linkedLibrary("vmnet-broker"),
                // for finding libvmnet-broker.a
                .unsafeFlags(["-L\(packageRoot)/.."]),
                .linkedFramework("Virtualization"),
                .linkedFramework("vmnet"),
            ]
        )
    ]
)
