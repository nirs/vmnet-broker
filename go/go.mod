module github.com/nirs/vmnet-broker/go

go 1.24.0

require github.com/Code-Hex/vz/v3 v3.7.1

require golang.org/x/sys v0.36.0 // indirect

// https://github.com/Code-Hex/vz/pull/205
replace github.com/Code-Hex/vz/v3 => ../../norio-nomura/vz
