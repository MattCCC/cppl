# C++L developer entry point.
#
# This Makefile intentionally contains no compiler flags, source lists,
# dependency configuration, or platform-specific build logic.
# CMake and CMakePresets.json remain the source of truth.

.DEFAULT_GOAL := help

SHELL := /bin/sh

CMAKE ?= cmake
CTEST ?= ctest

PRESET ?= dev
BUILD_DIR ?= build/$(PRESET)

JOBS ?=
VERBOSE ?= 0

ifeq ($(strip $(JOBS)),)
BUILD_JOBS :=
else
BUILD_JOBS := --parallel $(JOBS)
endif

ifeq ($(VERBOSE),1)
BUILD_VERBOSE := --verbose
else
BUILD_VERBOSE :=
endif

.PHONY: \
	help \
	configure \
	build \
	rebuild \
	test \
	test-unit \
	test-integration \
	check \
	format \
	format-check \
	tidy \
	tidy-changed \
	test-mutations \
	spec-rules-check \
	lint \
	lint-changed \
	asan \
	ubsan \
	tsan \
	release \
	install \
	package \
	clean \
	distclean

## help: Show available commands
help:
	@printf '%s\n' \
		'C++L development commands' \
		'' \
		'Usage:' \
		'  make <target> [PRESET=name] [JOBS=n] [VERBOSE=1]' \
		'' \
		'Core:' \
		'  configure         Configure the selected CMake preset' \
		'  build             Build the selected preset' \
		'  rebuild           Clean and rebuild the selected preset' \
		'  test              Run the full test suite' \
		'  check             Run formatting checks, linting, build, and tests' \
		'' \
		'Quality:' \
		'  format            Apply source formatting' \
		'  format-check      Verify source formatting without modifying files' \
		'  tidy            	 Apply source formatting' \
		'  lint              Run static analysis / lint checks (full, CI)' \
		'  lint-changed      Run static analysis on changed files only (fast, local)' \
		'  tidy-changed      Apply lint fixes to changed files only (fast, local)' \
		'  test-mutations    Break each soundness check and confirm a test catches it' \
		'' \
		'Sanitizers:' \
		'  asan              Build and test with AddressSanitizer' \
		'  ubsan             Build and test with UndefinedBehaviorSanitizer' \
		'  tsan              Build and test with ThreadSanitizer' \
		'' \
		'Production:' \
		'  release           Build the release preset' \
		'  install           Install the selected build' \
		'  package           Produce distributable packages' \
		'' \
		'Cleanup:' \
		'  clean             Clean the selected build tree' \
		'  distclean         Remove all generated build trees' \
		'' \
		'Examples:' \
		'  make build' \
		'  make test JOBS=12' \
		'  make build PRESET=release' \
		'  make check' \
		'  make asan'

# CMake's own build-system regeneration (driven by CONFIGURE_DEPENDS/
# add_custom_command DEPENDS) already reconfigures incrementally and quietly
# as part of `cmake --build`. Re-running `cmake --preset` unconditionally on
# every `make` invocation duplicated that work and reprinted the full
# configure banner for every target in a `make check` chain. A stamp file
# (rather than CMakeCache.txt itself) tracks this: CMake only rewrites the
# cache's mtime when its content changes, so it stays permanently older than
# CMakeLists.txt after any unrelated bulk-touch (e.g. a fresh checkout) and
# would force a reconfigure on every invocation forever.
CMAKE_STAMP := $(BUILD_DIR)/.cmake-configured

## configure: Configure selected preset
configure: $(CMAKE_STAMP)

$(CMAKE_STAMP): CMakeLists.txt CMakePresets.json
	$(CMAKE) --preset $(PRESET)
	@mkdir -p "$(BUILD_DIR)"
	@touch "$(CMAKE_STAMP)"

## build: Configure and build selected preset
build: configure
	$(CMAKE) --build $(BUILD_DIR) $(BUILD_JOBS) $(BUILD_VERBOSE)

## rebuild: Clean and rebuild selected preset
rebuild: clean build

## test: Build and run all tests
test: build
	$(CTEST) --test-dir $(BUILD_DIR) \
		--output-on-failure \
		$(if $(JOBS),--parallel $(JOBS),)

## test-unit: Run unit tests
test-unit: build
	$(CTEST) --test-dir $(BUILD_DIR) \
		--output-on-failure \
		-L unit \
		$(if $(JOBS),--parallel $(JOBS),)

## test-integration: Run integration tests
test-integration: build
	$(CTEST) --test-dir $(BUILD_DIR) \
		--output-on-failure \
		-L integration \
		$(if $(JOBS),--parallel $(JOBS),)

## test-mutations: Disable one soundness check at a time and confirm a test fails
#
# Deliberately breaks the kernel in a disposable copy of the tree. A passing
# suite proves nothing on its own; this measures whether it would notice. Kept
# out of `check` because each mutation costs a rebuild and a test run.
test-mutations:
	./scripts/test-mutations.sh $(if $(JOBS),--jobs $(JOBS),)

## check: Run repository validation suitable for CI/pre-merge checks
check: format-check lint spec-rules-check test

## spec-rules-check: Validate normative rule IDs and citations in docs/SPEC.md
spec-rules-check: configure
	$(CMAKE) --build $(BUILD_DIR) --target cppl-spec-rules $(BUILD_JOBS)
	$(BUILD_DIR)/bin/cppl-spec-rules check

## format: Apply formatting
format: configure
	$(CMAKE) --build $(BUILD_DIR) --target format

## format-check: Check formatting without modifying files
format-check: configure
	$(CMAKE) --build $(BUILD_DIR) --target format-check

## lint: Run static analysis
lint: configure
	$(CMAKE) --build $(BUILD_DIR) --target lint

## lint-changed: Run static analysis on files changed vs BASE_REF (fast, local dev)
lint-changed: configure
	$(CMAKE) --build $(BUILD_DIR) --target lint-changed

## tidy: Run clang-tidy with automatic fixes
tidy:
	$(CMAKE) --build $(BUILD_DIR) --target tidy

## tidy-changed: Apply clang-tidy fixes to files changed vs BASE_REF (fast, local dev)
tidy-changed: configure
	$(CMAKE) --build $(BUILD_DIR) --target tidy-changed

## asan: Run AddressSanitizer configuration
asan:
	$(MAKE) test PRESET=asan

## ubsan: Run UndefinedBehaviorSanitizer configuration
ubsan:
	$(MAKE) test PRESET=ubsan

## tsan: Run ThreadSanitizer configuration
tsan:
	$(MAKE) test PRESET=tsan

## release: Build production configuration
release:
	@test -n "$(VERSION)" || (echo "ERROR: VERSION is required (example: make release VERSION=0.3.0)" && exit 1)
	@./tools/release "$(VERSION)"

## install: Install selected build
install: build
	$(CMAKE) --install $(BUILD_DIR)

## package: Build distributable package
package: build
	$(CMAKE) --build $(BUILD_DIR) --target package

## clean: Clean selected build tree
clean:
	$(CMAKE) -E remove_directory "$(BUILD_DIR)"

## distclean: Remove all generated build trees
distclean:
	$(CMAKE) -E remove_directory build
