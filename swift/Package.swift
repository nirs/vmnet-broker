// swift-tools-version: 6.2
// The swift-tools-version declares the minimum version of Swift required to build this package.

import Foundation
import PackageDescription

let packageRoot = Context.packageDirectory

let package = Package(
    name: "swift",
    platforms: [.macOS(.v26)],
    dependencies: [
        .package(url: "https://github.com/apple/swift-log.git", from: "1.0.0")
    ],
    targets: [
        .executableTarget(
            name: "test",
            dependencies: [
                .product(name: "Logging", package: "swift-log")
            ],
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
