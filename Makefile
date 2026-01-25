# SPDX-FileCopyrightText: The vmnet-broker authors
# SPDX-License-Identifier: Apache-2.0

BUILD = build
CC = clang

# -arch: build universal binary for Intel and Apple Silicon
# -Wall -Wextra: enable warnings
# -O2: optimization level
# -Iinclude: include directory for headers
# -MMD: generate dependency files (.d) automatically
# -MP: add phony targets for headers to avoid errors if headers are deleted
CFLAGS = -arch x86_64 -arch arm64 -Wall -Wextra -O2 -Iinclude -MMD -MP

LDFLAGS = -arch x86_64 -arch arm64 -framework CoreFoundation -framework vmnet

broker_sources = $(wildcard broker/*.c) lib/common.c
test_sources = test/test.c client/client.c lib/common.c
broker_objects = $(patsubst %.c,$(BUILD)/%.o,$(broker_sources))
test_objects = $(patsubst %.c,$(BUILD)/%.o,$(test_sources))

.PHONY: all test install uninstall clean test-swift test-go fmt lint scripts dist

all: vmnet-broker test-c test-swift test-go scripts

test: test-c
	bats test
	cd go && go test -v ./vmnet_broker -count 1
	cd swift && swift test

# Generate new version.h if needed to avoid unnecessary rebuilds.
include/version.h: FORCE
	@./scripts/gen-version.sh

# Empty target to force recipe execution (see GNU Make manual)
FORCE:

# broker/broker.c includes version.h
$(BUILD)/broker/broker.o: include/version.h

vmnet-broker: $(broker_objects)
	$(CC) $(LDFLAGS) $(broker_objects) -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

test-c: $(test_objects)
	$(CC) $(LDFLAGS) $(test_objects) -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(broker_objects:.o=.d)
-include $(test_objects:.o=.d)

test-swift:
	cd swift && swift build
	ln -fs $(shell cd swift && swift build --show-bin-path)/test $@
	codesign -f -v --entitlements entitlements.plist -s - $@

test-go:
	cd go && go build -o ../$@ cmd/test.go
	codesign -f -v --entitlements entitlements.plist -s - $@

install: dist
	sudo ./install.sh "$(BUILD)/vmnet-broker.tar.gz"

uninstall: uninstall.sh
	sudo ./uninstall.sh

clean:
	rm -f vmnet-broker test-c test-swift test-go install.sh uninstall.sh include/version.h
	rm -rf $(BUILD)
	cd swift && swift package clean
	cd go && go clean

fmt:
	clang-format -i broker/*.c client/*.c lib/*.c test/*.c include/*.h

lint: scripts
	shellcheck -x install.sh uninstall.sh scripts/dist.sh scripts/gen-version.sh
	clang-format --dry-run --Werror broker/*.c client/*.c lib/*.c test/*.c include/*.h

scripts: install.sh uninstall.sh

install.sh: scripts/install.sh.in scripts/common.sh
	@echo "Building $@"
	@sed -e '/#@INCLUDE_COMMON@/r scripts/common.sh' -e '/#@INCLUDE_COMMON@/d' $< > $@
	@chmod +x $@

uninstall.sh: scripts/uninstall.sh.in scripts/common.sh
	@echo "Building $@"
	@sed -e '/#@INCLUDE_COMMON@/r scripts/common.sh' -e '/#@INCLUDE_COMMON@/d' $< > $@
	@chmod +x $@

dist: vmnet-broker scripts
	./scripts/dist.sh
