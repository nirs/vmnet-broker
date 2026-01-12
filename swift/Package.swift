// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

// swift-tools-version: 6.2
// The swift-tools-version declares the minimum version of Swift required to build this package.

import Foundation
import PackageDescription

var deps: [Package.Dependency] = [
    .package(url: "https://github.com/apple/swift-log.git", from: "1.0.0"),
    .package(url: "https://github.com/apple/swift-testing.git", branch: "main"),
]

// DocC plugin is only needed for documentation generation, skip in CI
if ProcessInfo.processInfo.environment["SKIP_DOCC_PLUGIN"] == nil {
    deps.append(.package(url: "https://github.com/apple/swift-docc-plugin", from: "1.0.0"))
}

let package = Package(
    name: "vmnet-broker",
    platforms: [.macOS(.v26)],
    products: [
        .library(
            name: "VmnetBroker",
            targets: ["VmnetBroker"],
        )
    ],
    dependencies: deps,
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
            dependencies: [
                "VmnetBroker",
                "vmnet_broker",
                .product(name: "Testing", package: "swift-testing"),
            ],
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
