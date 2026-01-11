# SPDX-FileCopyrightText: The vmnet-broker authors
# SPDX-License-Identifier: Apache-2.0

CC = clang

# -Wall -Wextra: enable warnings
# -O2: optimization level
# -Iinclude: include directory for headers
# -MMD: generate dependency files (.d) automatically
# -MP: add phony targets for headers to avoid errors if headers are deleted
CFLAGS = -Wall -Wextra -O2 -Iinclude -MMD -MP

LDFLAGS = -framework CoreFoundation -framework vmnet

build_dir = build
broker_sources = $(wildcard broker/*.c) lib/common.c
test_sources = test/test.c client/client.c lib/common.c
broker_objects = $(patsubst %.c,$(build_dir)/%.o,$(broker_sources))
test_objects = $(patsubst %.c,$(build_dir)/%.o,$(test_sources))

.PHONY: all test install uninstall clean test-swift test-go fmt scripts

all: vmnet-broker test-c test-swift test-go scripts

test: test-c
	bats test
	cd go && go test -v ./vmnet_broker -count 1
	cd swift && swift test

vmnet-broker: $(broker_objects)
	$(CC) $(LDFLAGS) $(broker_objects) -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

test-c: $(test_objects)
	$(CC) $(LDFLAGS) $(test_objects) -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

$(build_dir)/%.o: %.c
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

install:
	sudo ./install.sh

uninstall:
	sudo ./uninstall.sh

clean:
	rm -f vmnet-broker test-c test-swift test-go install.sh uninstall.sh
	rm -rf $(build_dir)
	cd swift && swift package clean
	cd go && go clean

fmt:
	clang-format -i broker/*.c client/*.c lib/*.c test/*.c include/*.h

scripts: install.sh uninstall.sh

install.sh: scripts/install.sh.in scripts/common.sh
	@echo "Building $@"
	@sed -e '/#@INCLUDE_COMMON@/r scripts/common.sh' -e '/#@INCLUDE_COMMON@/d' $< > $@
	@chmod +x $@

uninstall.sh: scripts/uninstall.sh.in scripts/common.sh
	@echo "Building $@"
	@sed -e '/#@INCLUDE_COMMON@/r scripts/common.sh' -e '/#@INCLUDE_COMMON@/d' $< > $@
	@chmod +x $@
