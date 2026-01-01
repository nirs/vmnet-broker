// swift-tools-version: 6.2
// The swift-tools-version declares the minimum version of Swift required to build this package.

import Foundation
import PackageDescription

let package = Package(
    name: "vmnet-broker",
    platforms: [.macOS(.v26)],
    products: [
        .library(
            name: "VmnetBroker",
            targets: ["VmnetBroker"],
        )
    ],
    dependencies: [
        .package(url: "https://github.com/apple/swift-log.git", from: "1.0.0")
    ],
    targets: [
        .binaryTarget(
            name: "vmnet_broker_binary",
            path: "Frameworks/vmnet-broker.xcframework"
        ),
        .target(
            name: "VmnetBroker",
            dependencies: ["vmnet_broker_binary"],
            path: "Sources/VmnetBroker",
        ),
        .executableTarget(
            name: "test",
            dependencies: [
                "VmnetBroker",
                .product(name: "Logging", package: "swift-log"),
            ],
            path: "Sources/test",
        ),
    ]
)
