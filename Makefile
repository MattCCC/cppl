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
	lint \
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
		'  lint              Run static analysis / lint checks' \
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

## configure: Configure selected preset
configure:
	$(CMAKE) --preset $(PRESET)

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

## check: Run repository validation suitable for CI/pre-merge checks
check: format-check lint test

## format: Apply formatting
format: configure
	$(CMAKE) --build $(BUILD_DIR) --target format

## format-check: Check formatting without modifying files
format-check: configure
	$(CMAKE) --build $(BUILD_DIR) --target format-check

## lint: Run static analysis
lint: configure
	$(CMAKE) --build $(BUILD_DIR) --target lint

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
	$(MAKE) build PRESET=release

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
