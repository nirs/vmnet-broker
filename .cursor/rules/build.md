# Build Rules

## Always use Makefile targets

When testing compilation or building, always use Makefile targets instead of direct compilation commands:

- ✅ Use `make vmnet-broker` to build the broker
- ✅ Use `make test-c` to build the test client
- ✅ Use `make clean` to clean build artifacts
- ❌ Never use direct `clang` or `gcc` commands that would create `.o` or `.d` files in the source directory

This ensures all build artifacts go to the `build/` directory and keeps the source tree clean.
