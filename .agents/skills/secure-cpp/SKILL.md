---
name: secure-cpp
description: Release-blocking security rules for writing, reviewing, refactoring or debugging C++/C++L here - memory, lifetime, parsing, hostile input, arithmetic, concurrency, files, serialization, FFI/ABI, crypto, unsafe/trusted.
---

# Secure C++ / C++L

`AGENTS.md` §41 applies; this adds the rules and the commands. Assume every reachable input is hostile, malformed, truncated or oversized. Security beats convenience and line count, and no security property is weakened to make anything pass.

Before writing, answer: what enters and which of it an attacker controls; which allocations, indexes, lengths and arithmetic depend on it; who owns each object and what borrows it; what concurrency can invalidate; what CPU, memory, stack or disk hostile input can consume; which boundaries (process, file, network, C ABI, plugin, serialization) it crosses.

## Rules

- **Trust.** Network, IPC, files, caches, argv, environment, OCR, serialized or old persisted state and FFI input are validated before becoming a domain type, and a validated type cannot be constructed invalid (`Percentage::parse(raw)` returning `std::expected`). A count in a header proves nothing: 10 bytes claiming 4 billion records are invalid.
- **Ownership.** RAII for every resource: memory, descriptors, handles, locks, mappings, threads, temp files. No owning raw pointers; prefer a value, `optional`, `unique_ptr`, and `shared_ptr` only for genuinely shared ownership (`weak_ptr` against cycles), never to hide an unclear lifetime. `new`/`delete`/`malloc`/`free` live only inside a small RAII type.
- **Lifetime.** Views (`string_view`, `span`, `T&`, `T*`, iterators, captures, coroutine parameters) never outlive their owner and are never returned into temporaries or locals. No alias survives `push_back`, `reserve`, `resize`, `erase`, `append` or `rehash`. No `[&]`, `this`, view or raw pointer in work that may outlive its scope.
- **Bounds.** Buffers cross internal APIs as `std::span<const std::byte>`; a C pointer and length become one at the boundary. Prove `index < size`, and for a range `offset <= size && length <= size - offset` (`offset + length` can overflow). Never assume NUL termination. `memcpy`/`memset`/`memcmp` only with proven sizes on trivial types; otherwise construct, assign or `std::bit_cast`.
- **Arithmetic.** Sizes, offsets, counts, dimensions, file sizes, timestamps, money and enum conversions are security-sensitive. Never rely on signed overflow. Check `count * size`, `a + b`, `width * height` before allocating. Prove a narrowing (`uint64_t` to `uint32_t`, `size_t` to `int`, any sign change) before doing it, and never cast to silence a warning. Choose signed or unsigned deliberately. Validate shift counts; never shift a negative.
- **Parsers and resources.** A parser checks every length and offset, rejects truncation, impossible states and overflow, bounds nesting, collection, string and total allocation sizes, always makes progress, and does no unbounded recursion or super-linear work on chosen input. Every loop over hostile data consumes input, shrinks the work, or has a bound. No unbounded queue, retry, decompression, regex, growth, or thread per attacker-controlled object.
- **Injection.** No `system`/`popen` where an API exists; arguments go separately, never through a shell. Untrusted data never becomes a command, SQL, script, regex or format string; the program owns every format string, logging included.
- **Filesystem.** Paths from outside are hostile: `..`, absolute escape, symlinks, canonicalization and Unicode confusion, special devices, embedded NUL, TOCTOU. Prove the resolved target stays under its root, act on open handles instead of re-opening paths, and create temp files with secure primitives.
- **Concurrency.** Shared mutable state is synchronized: prefer immutability, message passing, ownership transfer and scoped RAII locks. No detached thread without a guaranteed lifetime, no arbitrary callback under an internal lock; document lock order. Atomics are not a design, and ordering weaker than `seq_cst` states its rationale.
- **FFI and ABI.** Each foreign call is a trust boundary: validate pointers, lengths, enums, nullability and integer conversions, settle ownership and lifetime, keep no foreign buffer without a transfer. No exception crosses a C or foreign ABI or escapes a destructor or `main`. A wire format states types, sizes, byte order and version, never a C++ layout.
- **Casts.** No `reinterpret_cast`, `const_cast` or C cast in ordinary code; `reinterpret_cast` only at a tested low-level boundary. A cast never hides an ownership, lifetime, alignment or range problem.
- **Secrets.** Established crypto libraries only, and a CSPRNG (never `rand`) for keys, nonces, tokens, salts and session IDs. Never log credentials, tokens or keys; keep secret buffers few and short-lived; compare tags and secrets in constant time; no production secret in source.
- **Errors.** Fail closed: never parse failure to a default, permission failure to allowed, verification failure to success, overflow to truncation, unknown to trusted. Check every security-relevant result; ignore an error only on purpose.
- **APIs.** Prefer types that make misuse hard (`ValidatedPath`, `ByteCount`, `span`, `expected`, `enum class`) to bare primitives and ambiguous `bool` parameters.
- **Escape hatches.** Owning raw pointers, manual allocation, `reinterpret_cast`/`const_cast`/C casts, `system`/`popen`, `setjmp`, detached threads, unchecked pointer arithmetic, unchecked `operator[]` on a hostile index, unchecked narrowing, VLAs, `rand` for security, raw memory operations on non-trivial types: only when necessary, then isolated and minimal, with the invariant stated, targeted tests and sanitizer coverage. A `NOLINT` names its check and its reason.

## C++L

A proof does not validate runtime input: `hostile bytes -> runtime validator -> validated or refined value -> verified code`. The admitting check is `RUNTIME-CHECKED` (`AGENTS.md` §17), never `PROVEN`. State with laws, contracts and refinements what is statically expressible: `index < size`, `length <= size - offset`, representability, permitted transitions, type invariants. `unsafe`, `trusted` and axioms are security-critical: each new one is minimal, states its invariant, is tested at its boundary and reviewable alone, and is never added to silence an obligation.

## Forbidden fixes

Beyond `AGENTS.md` §41: no sanitizer suppression without a proven false positive, no `NOLINT` merely to silence, no checked arithmetic made unchecked, no limit raised until a test passes, no malformed input accepted without specification authority, no security-relevant error caught and dropped. Fix the cause.

## Tests

For hostile input: empty, one byte, truncated, maximum and one over, zero, negative, integer extremes and overflow edges, malformed encoding and Unicode, invalid enums, duplicate and reordered fields, deep nesting, a huge declared length over a tiny buffer, invalid offsets, overlapping ranges, embedded NUL, adversarial repetition, partial reads, interruption, concurrency, cancellation. Each security bug gets a regression test shown to fail without the fix. A non-trivial parser or decoder gets a fuzz target, and a fuzzer finding becomes a corpus regression before it is closed.

## Commands

- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`, errors in every preset but `ci-linux-gcc`.
- Lint: `make lint-changed`, `make lint`, and `make ci-quality` for Linux headers. `.clang-tidy` enforces the analyzer, `bugprone` (with `exception-escape`), `cert`, `concurrency` and the ownership, lifetime, bounds, initialization and vararg `cppcoreguidelines` checks; every finding is an error, and its header says what stays off and why.
- ASan, UBSan: `make asan`, `make ubsan`; CI `ci-asan` and `ci-ubsan` on every run. Every finding aborts.
- TSan: `make tsan` on Linux, `tools/ci/linux.sh tsan` elsewhere; CI nightly.
- Fuzzing: `tests/fuzz`; `make ci-fuzz` or `tools/ci/linux.sh fuzz`; CI on every run; every build replays each corpus (`docs/CI.md`, "Fuzzing").
- Hardening: `cmake/Hardening.cmake`, on in `release`, `dev` and the `ci-*` build presets; `architecture_hardening` reads the linked binaries (`docs/CI.md`, "Hardening").
- Before merging platform code: `make ci-quality`, `make ci-linux-gcc`, `tools/ci/linux.sh asan|ubsan|fuzz`. Tests run under `tests/support/bounded.sh`.
- Related skills: `cppl-security-fix` (with `SECURITY.md`), `cppl-fuzz-verifier`, `cppl-ffi-model`, `cppl-runtime-validation`.

## Done

Ownership explicit; nothing new can dangle; untrusted indexes, lengths and allocation arithmetic checked; narrowing proven; malformed input fails safely with bounded work; no new injection or path surface; concurrency explicit; FFI validated; secrets unexposed; adversarial tests and fuzzing where they apply; tests, ASan, UBSan, TSan for concurrent code, and lint pass; nothing suppressed or weakened. Name each check you could not run. Passing unit tests never show memory safety.

Report security-relevant work as: boundaries touched; memory and lifetime decisions; hostile-input, integer and bounds protections; concurrency; tests added; verification run (tests, ASan, UBSan, TSan, lint, fuzzing); residual risks and checks not run. Call code "secure", "memory-safe" or "verified" only when the checks run justify it.
