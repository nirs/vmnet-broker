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
        .package(url: "https://github.com/apple/swift-log.git", from: "1.0.0"),
        .package(url: "https://github.com/apple/swift-docc-plugin", from: "1.0.0"),
    ],
    targets: [
        .target(
            name: "vmnet_broker",
        ),
        .target(
            name: "VmnetBroker",
            dependencies: ["vmnet_broker"],
        ),
        .testTarget(
            name: "VmnetBrokerTests",
            dependencies: ["VmnetBroker", "vmnet_broker"],
        ),
        .executableTarget(
            name: "test",
            dependencies: [
                "VmnetBroker",
                .product(name: "Logging", package: "swift-log"),
            ],
        ),
    ]
)
