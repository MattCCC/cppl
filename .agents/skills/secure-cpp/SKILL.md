---
name: secure-cpp
description: >
  Secure C++ and C++L engineering rules. Use whenever writing, modifying,
  reviewing, refactoring, or debugging C++/C++L code, especially code involving
  memory, ownership, parsing, hostile input, arithmetic, concurrency, files,
  network data, serialization, FFI/ABI boundaries, cryptography, or security.
---

# Secure C++ Engineering

## 1. Mission

Write C++ as if every reachable input boundary may eventually receive malicious,
malformed, truncated, oversized, adversarial, or intentionally confusing data.

The goal is not merely "valid C++".

The goal is code that is:

* memory safe by construction where practical;
* free of undefined behavior;
* explicit about ownership and lifetime;
* bounds checked at trust boundaries;
* resistant to integer overflow and truncation;
* resistant to injection and path attacks;
* bounded against resource-exhaustion attacks;
* race-free;
* safe across ABI and FFI boundaries;
* conservative with secrets;
* mechanically checked by sanitizers, static analysis, tests, and fuzzing.

Security takes precedence over convenience, cleverness, or reducing line count.

Never weaken a security property merely to make a test pass.

---

## 2. Security mindset

For every change, identify:

1. What data enters this code?
2. Which of it can be attacker-controlled?
3. Which allocations depend on that data?
4. Which indexes, offsets, lengths, counts, or arithmetic depend on it?
5. Which objects own memory?
6. Which references borrow memory?
7. Can anything outlive its owner?
8. Can concurrency invalidate the assumptions?
9. Can malformed input create excessive CPU, memory, stack, disk, or thread use?
10. Does this code cross a process, file, network, C ABI, JNI, Swift/ObjC, plugin,
    serialization, or other trust boundary?
11. Could an attacker alter control flow, paths, commands, filenames, format strings,
    sizes, indexes, or state transitions?

Do this before implementation, not after.

---

## 3. Fundamental rule: invalid state must not become trusted state

External data is untrusted.

Examples include:

* network input;
* IPC;
* files;
* database contents;
* command-line arguments;
* environment variables;
* OCR output;
* parser input;
* serialized structures;
* plugin input;
* JNI/Swift/ObjC input;
* C ABI structures;
* data loaded from caches;
* persisted state created by an older application version.

Validate untrusted data before allowing it into trusted domain types.

Prefer:

```cpp
auto value = Percentage::parse(raw);
if (!value) {
    return std::unexpected(value.error());
}

process_verified(*value);
```

over propagating primitive values throughout the program.

A type that represents validated data should not be constructible in an invalid state.

---

## 4. Ownership and lifetime

### 4.1 RAII is mandatory

Resources must be represented by objects whose lifetime controls the resource.

This applies to:

* heap memory;
* file descriptors;
* handles;
* sockets;
* locks;
* mapped memory;
* database resources;
* threads;
* temporary files;
* native API resources.

Cleanup must occur automatically on every exit path.

---

### 4.2 Owning raw pointers are forbidden

Do not introduce raw owning pointers.

Prefer, in order:

```cpp
T value;
std::optional<T>
std::unique_ptr<T>
std::shared_ptr<T>
```

Use `std::shared_ptr` only when ownership is genuinely shared.

Do not use `shared_ptr` merely because lifetime is unclear.

Unclear lifetime must be designed, not hidden.

Use `std::weak_ptr` where shared ownership would otherwise create cycles.

---

### 4.3 Manual allocation is exceptional

Do not normally introduce:

```cpp
new
delete
new[]
delete[]
malloc
calloc
realloc
free
```

If low-level allocation is genuinely required, isolate it behind a small RAII abstraction.

The unsafe implementation must not leak raw ownership into normal application code.

---

### 4.4 Borrowing must be obvious

Raw pointers, references, `std::span`, `std::string_view`, and iterators are borrowing
constructs unless explicitly documented otherwise.

A borrowed object must never outlive its owner.

Pay special attention to:

```cpp
std::string_view
std::span
T&
T*
vector::iterator
lambda captures
callbacks
coroutines
async tasks
```

Do not store a view or reference unless the lifetime relationship is guaranteed by
the architecture.

Do not return views into temporary or local objects.

---

### 4.5 Container invalidation must be considered

Never retain pointers, references, or iterators across operations that may invalidate
them.

Examples include:

```cpp
vector.push_back(...)
vector.emplace_back(...)
vector.reserve(...)
vector.resize(...)
vector.erase(...)
string.append(...)
unordered_map.rehash(...)
```

Check the container's invalidation guarantees before retaining aliases.

---

### 4.6 Async capture rules

Do not capture local variables by reference into work that may outlive the current
scope.

Be particularly suspicious of:

```cpp
[&]
[&foo]
this
std::string_view
std::span
raw pointers
```

inside asynchronous callbacks, tasks, threads, or coroutines.

Ownership must be explicit.

---

## 5. Memory access and bounds

### 5.1 Pointer + length pairs should become `std::span`

Prefer:

```cpp
void parse(std::span<const std::byte> data);
```

over:

```cpp
void parse(const void* data, std::size_t length);
```

at internal C++ boundaries.

C ABI boundaries may use pointer + length, but convert them into checked C++ abstractions
immediately.

---

### 5.2 Attacker-controlled indexes require validation

Never index an object using untrusted input before proving:

```text
index < size
```

For ranges prove:

```text
offset <= size
length <= size - offset
```

The second form is preferred because:

```cpp
offset + length <= size
```

can itself overflow.

---

### 5.3 Do not assume null termination

Input buffers are not C strings merely because their element type is `char`.

Never call C string functions on arbitrary external buffers unless termination is
separately guaranteed.

Prefer length-aware APIs.

---

### 5.4 Raw memory operations require proof

Operations such as:

```cpp
memcpy
memmove
memset
memcmp
```

must have explicitly valid sizes and object types.

Never use raw memory operations to copy or initialize non-trivial C++ objects.

Prefer normal construction, assignment, algorithms, or `std::bit_cast` where the
semantics actually require object-representation conversion.

---

## 6. Integer and arithmetic safety

Integer mistakes can become memory corruption.

Treat arithmetic involving any of the following as security-sensitive:

* allocation sizes;
* offsets;
* indexes;
* lengths;
* counts;
* multiplication of dimensions;
* serialized lengths;
* file sizes;
* pointer offsets;
* timestamps;
* money;
* enum conversions.

---

### 6.1 Never rely on signed overflow

Signed integer overflow is undefined behavior.

Never write code whose correctness requires it.

---

### 6.2 Check arithmetic before allocation

Dangerous:

```cpp
auto bytes = count * sizeof(Item);
```

when `count` is external.

The multiplication must be proven representable before use.

The same applies to:

```text
a + b
a * b
offset + length
width * height
rows * columns
count * stride
```

Use checked arithmetic helpers where the operation is security-sensitive.

---

### 6.3 Narrowing conversions require proof

Do not silently convert between integer domains where values may be lost.

Examples requiring explicit validation:

```cpp
uint64_t -> uint32_t
size_t   -> int
int64_t  -> int
signed   -> unsigned
unsigned -> signed
```

Validate representability first.

Do not use a cast merely to silence a compiler warning.

---

### 6.4 Signed/unsigned comparisons require care

Do not casually mix signed and unsigned arithmetic.

Choose a domain deliberately.

Values that may logically be negative must not be represented as unsigned merely for
convenience.

---

### 6.5 Shifts must be validated

Before shifting, ensure the shift count is valid for the operand width.

Do not shift negative signed values.

Do not depend on undefined or implementation-dependent shift behavior.

---

## 7. Parsing hostile input

Parsers are security boundaries.

Every parser must be designed for adversarial input.

A parser must:

* validate all lengths;
* validate all offsets;
* reject truncated structures;
* reject impossible states;
* place limits on nesting;
* place limits on collection sizes;
* place limits on string/buffer sizes;
* place limits on total allocations;
* make forward progress;
* avoid unbounded recursion;
* avoid quadratic or exponential behavior on attacker-selected input;
* reject arithmetic overflow while calculating positions or sizes.

Never trust a count merely because it appeared in a structurally valid header.

A 10-byte input claiming to contain 4 billion records remains invalid.

---

## 8. Resource-exhaustion resistance

Memory safety alone is insufficient.

Attackers must not be able to trivially exhaust:

* memory;
* CPU;
* stack;
* file descriptors;
* threads;
* disk;
* queue capacity;
* recursion depth.

Untrusted sizes require reasonable hard limits.

Avoid:

```text
unbounded recursion
unbounded queues
unbounded retries
unbounded decompression
unbounded regex work
unbounded container growth
one thread per attacker-controlled object
```

Any loop processing hostile data must either:

* consume input;
* reduce remaining work; or
* have an explicit bound.

---

## 9. Command and code injection

Never construct shell commands using attacker-controlled strings.

Do not introduce:

```cpp
system(...)
popen(...)
```

for operations that can be performed through a direct API.

Prefer APIs that pass executable arguments separately rather than through shell
parsing.

Never concatenate untrusted data into executable source, scripts, SQL, shell commands,
regular expressions, or other interpreted languages without an appropriate safe API.

---

## 10. Format-string safety

Never allow external input to become the format string.

Bad:

```cpp
printf(user_text);
```

The format must be controlled by the program.

The same principle applies to logging frameworks and other formatting systems.

---

## 11. Filesystem security

Treat paths originating outside the trusted core as attacker-controlled.

Defend against:

```text
../ traversal
absolute-path escape
symlink attacks
path canonicalization confusion
special device paths
unexpected Unicode/path normalization
TOCTOU races
```

When access is intended to remain underneath a root directory, prove that the final
resolved target remains underneath that root.

Avoid:

```text
check path
close/check state
later reopen path
```

when the resource could be replaced between those operations.

Prefer operating on already-open handles/descriptors when platform APIs permit it.

Temporary files must use secure creation primitives rather than predictable names.

---

## 12. Concurrency

A data race is a correctness and potentially security defect.

Shared mutable state requires synchronization.

Prefer:

* immutable state;
* message passing;
* ownership transfer;
* scoped locking;
* RAII lock objects.

Avoid detached threads unless the architecture explicitly guarantees their lifetime.

Document lock ordering when multiple locks may be acquired.

Do not invoke arbitrary callbacks while holding internal locks unless the API is
specifically designed for reentrancy.

Atomics are not a replacement for understanding synchronization.

Use the simplest correct memory ordering. If ordering weaker than sequential consistency
is chosen, document the proof/rationale.

---

## 13. ABI and FFI boundaries

Treat every foreign-language or C ABI call as a trust boundary.

At entry:

* validate pointers;
* validate lengths;
* validate enums;
* validate integer conversions;
* validate nullability;
* establish ownership;
* establish lifetime.

Do not allow C++ exceptions to cross a C ABI boundary.

Catch and translate them before returning.

Do not retain foreign buffers unless the API explicitly transfers ownership or
guarantees sufficient lifetime.

Do not expose C++ implementation-specific layouts as stable wire formats.

Serialized representations require explicit types, sizes, byte order, and versioning.

---

## 14. Type punning and casts

Avoid:

```cpp
reinterpret_cast
const_cast
C-style casts
```

in ordinary code.

`reinterpret_cast` belongs only in tightly controlled low-level boundaries where the
representation and alignment requirements are understood and tested.

Prefer:

```cpp
static_cast
std::bit_cast
```

where their semantics are actually correct.

A cast must not be used to hide an ownership, lifetime, alignment, or integer-domain
problem.

---

## 15. Secrets and cryptography

Never invent cryptographic algorithms or protocols.

Use established, maintained cryptographic libraries.

Security-sensitive randomness must come from a cryptographically secure random source.

Never use:

```cpp
std::rand()
rand()
```

for keys, nonces, tokens, salts, session identifiers, or security decisions.

Never log:

* passwords;
* authentication tokens;
* refresh tokens;
* private keys;
* encryption keys;
* session secrets;
* full credentials.

Avoid unnecessary copies of secret material.

Secret-bearing buffers should have narrowly scoped lifetime.

Use constant-time comparison where timing leakage matters, such as authentication tags
or secret authentication values.

Do not hard-code production secrets into source code or binaries.

---

## 16. Error handling

Security failures must fail closed.

Do not silently convert:

```text
parse failure -> default valid value
permission failure -> allowed
verification failure -> success
overflow -> truncated value
unknown state -> trusted state
```

Every ignored error must be intentional.

Return values carrying security significance must be checked.

Do not suppress an error merely because a caller currently ignores it.

---

## 17. Exceptions

Destructors must not allow exceptions to escape.

Do not throw through:

* C ABI boundaries;
* destructors during stack unwinding;
* foreign-language ABI boundaries that do not support C++ exceptions.

Exception handling must preserve resource ownership through RAII.

---

## 18. Safer API design

Prefer APIs that make unsafe usage difficult.

Prefer:

```cpp
ValidatedPath
ByteCount
FileOffset
Percentage
NonEmptyString
std::span<const std::byte>
std::expected<T, Error>
enum class
```

over passing unrelated primitive values everywhere.

Do not use `bool` parameters when multiple security-relevant modes become ambiguous.

Prefer explicit types or enums.

Construct trusted domain objects only after validation.

---

## 19. Dangerous constructs

Do not introduce the following without strong justification:

```text
owning raw pointers
manual new/delete
manual malloc/free
reinterpret_cast
const_cast
C-style casts
system()
popen()
setjmp/longjmp
detached threads
unchecked pointer arithmetic
unchecked attacker-controlled operator[]
unchecked integer narrowing
variable-size stack allocations
security-sensitive std::rand()/rand()
raw memory operations on non-trivial C++ objects
```

If one is genuinely necessary:

1. isolate it;
2. minimize its surface;
3. explain the safety invariants;
4. add targeted tests;
5. run sanitizer coverage over it.

---

## 20. Forbidden agent behavior

An agent MUST NOT solve failures by:

* removing validation;
* weakening bounds checks;
* disabling compiler warnings;
* disabling sanitizers;
* adding sanitizer suppressions without proving a tool false positive;
* adding `NOLINT` merely to silence analysis;
* adding unchecked casts;
* replacing checked arithmetic with unchecked arithmetic;
* deleting adversarial tests;
* weakening assertions or invariants;
* increasing arbitrary limits until a failing test succeeds;
* changing malformed input into accepted input without specification authority;
* catching and ignoring security-relevant errors;
* replacing ownership with raw pointers;
* introducing `unsafe` or `trusted` merely to bypass proof/checking requirements.

Never hide a security defect.

Fix its cause.

---

## 21. C++L-specific requirements

When C++L is available, use the language's proof facilities to strengthen these
guarantees.

Use laws/contracts/refinements for properties that are statically expressible.

Examples include:

```text
index < size
offset <= size
length <= size - offset
amount is representable
state transition is permitted
validated type satisfies its invariant
```

However:

```text
compile-time proof != validation of arbitrary runtime input
```

Attacker-controlled runtime data must first be checked before it can enter a refined or
verified domain.

Conceptually:

```text
hostile bytes
    ↓
runtime parser / validator
    ↓
validated domain value
    ↓
verified C++L code
```

The check that admits the value is `RUNTIME-CHECKED`, not a compile-time proof, and is
reported as such (`AGENTS.md` §17).

Treat `unsafe` and `trusted` as security-critical escape hatches.

Every new use must:

* be minimal;
* have a precise justification;
* state the invariant being relied upon;
* have tests around the boundary;
* be reviewable independently.

Never introduce `unsafe`, `trusted`, or an axiom simply to silence an obligation.

Proofs supplement runtime validation. They do not replace validation of unknowable
runtime values.

---

## 22. Testing hostile behavior

Do not test only success paths.

For every input-facing component consider tests for:

* empty input;
* one-byte input;
* truncated input;
* maximum accepted input;
* one over maximum;
* zero;
* negative values where representable;
* maximum integer;
* integer overflow boundaries;
* malformed encoding;
* invalid enum values;
* duplicated fields;
* unexpected field ordering;
* deeply nested input;
* huge declared length with tiny actual buffer;
* invalid offset;
* overlapping ranges;
* malformed Unicode;
* embedded NUL;
* repeated records;
* adversarial repetition;
* partial reads;
* interrupted operations;
* concurrent access;
* cancellation;
* allocation failure where testable.

Every discovered security bug should receive a regression test.

---

## 23. Fuzzing

Any non-trivial parser or decoder accepting untrusted bytes should have a fuzz target
unless there is a strong reason it cannot.

High-value fuzz targets include:

* parsers;
* protocol decoders;
* binary readers;
* source readers;
* serialization;
* image metadata;
* file formats;
* FFI validators;
* canonicalization logic;
* arithmetic recovery logic.

Fuzz harnesses must impose practical resource limits.

A fuzzer-discovered crash or sanitizer finding becomes a regression fixture before the
bug is considered closed.

Prefer sanitizer-enabled fuzzing.

---

## 24. Required dynamic analysis

For substantial C++ changes, run the repository's sanitizer configurations where
available.

At minimum, security-sensitive C++ should regularly be tested under:

```text
AddressSanitizer
UndefinedBehaviorSanitizer
```

Concurrency-sensitive code should additionally be tested separately under:

```text
ThreadSanitizer
```

MemorySanitizer may be used where the complete relevant dependency stack can be
instrumented.

Sanitizer findings are defects until demonstrated otherwise.

Do not simply suppress them.

Sanitizer runtimes are primarily development/testing tools; do not assume that linking
the ordinary sanitizer runtime into production is itself a production hardening
strategy.

---

## 25. Static analysis

Run the repository's configured clang-tidy/static-analysis suite.

For new security-sensitive code, relevant families include:

```text
clang-analyzer-*
bugprone-*
cert-*
cppcoreguidelines-*
concurrency-*
performance-*
portability-*
```

Do not blindly enable every check and then suppress hundreds of findings.

Configure a meaningful enforced set and keep new code clean.

Warnings involving:

* lifetime;
* bounds;
* narrowing;
* ownership;
* uninitialized state;
* use-after-move;
* suspicious raw memory use;
* unchecked return values;
* concurrency;
* command execution;

receive security priority.

---

## 26. Compiler warnings

New code must not introduce compiler warnings.

Use the repository's strict warning profile.

Where compatible with the project, security-oriented development profiles should
consider warnings for:

```text
conversion
sign conversion
shadowing
format misuse
implicit narrowing
unused results
unreachable code
```

Do not add casts simply to silence warnings without first proving the conversion safe.

---

## 27. Production hardening

Use the repository's platform-specific hardened release configuration.

Consider, where supported and appropriate:

* stack protection;
* fortified libc interfaces;
* position-independent executables;
* non-executable memory;
* RELRO;
* control-flow protection;
* platform signing and hardened runtime mechanisms.

Do not blindly copy compiler/linker security flags between operating systems or
toolchains.

Hardening is defense in depth.

It does not replace correct C++.

---

## 28. Security review procedure

Before declaring a C++ task complete, inspect the diff specifically for:

### Memory

* Who owns every allocated object?
* Can anything dangle?
* Can reallocation invalidate an alias?
* Can an index leave the object?
* Can alignment be wrong?
* Can object lifetime rules be violated?

### Arithmetic

* Can any addition overflow?
* Can any multiplication overflow?
* Can narrowing alter the value?
* Are signed and unsigned domains mixed incorrectly?

### Input

* What happens with malformed input?
* What happens with enormous input?
* What happens with truncated input?
* Is recursion bounded?
* Are declared lengths checked against actual lengths?

### Injection

* Can input influence a command?
* Can input influence a path?
* Can input become a format string?
* Can input become executable/interpreted content?

### Concurrency

* Is mutable state shared?
* Is lifetime guaranteed across threads?
* Can callbacks reenter while locks are held?
* Are atomics actually sufficient?

### Secrets

* Can credentials enter logs?
* Are security tokens unnecessarily copied?
* Is cryptographic randomness actually cryptographic?

### Boundaries

* Are FFI pointers and lengths validated?
* Can exceptions cross the boundary?
* Is ownership transfer explicit?

---

## 29. Required completion gate

A security-sensitive change is not complete until all applicable items are satisfied:

* [ ] ownership is explicit;
* [ ] no new dangling lifetime possibility exists;
* [ ] untrusted indexes and lengths are validated;
* [ ] allocation arithmetic is checked;
* [ ] narrowing conversions are proven safe;
* [ ] malformed input fails safely;
* [ ] work and allocation from hostile input are bounded;
* [ ] no new injection surface exists;
* [ ] filesystem paths are handled safely;
* [ ] concurrency assumptions are explicit;
* [ ] FFI/ABI boundaries validate their inputs;
* [ ] secrets are not exposed;
* [ ] adversarial tests exist where applicable;
* [ ] parser/decoder fuzzing exists where applicable;
* [ ] ordinary tests pass;
* [ ] ASan/UBSan pass where available;
* [ ] TSan passes for affected concurrent code where available;
* [ ] static analysis produces no unexplained new findings;
* [ ] no sanitizer/static-analysis suppression was added to hide the issue;
* [ ] no security invariant or test was weakened.

If a required check cannot be executed because the environment lacks the necessary
tooling, state exactly which check was not executed.

Never claim the code is memory-safe or security-verified solely because normal unit
tests pass.

---

## 30. Agent completion report

For security-relevant C++ work, summarize:

```text
Security boundaries touched:
- ...

Memory/lifetime decisions:
- ...

Hostile-input protections:
- ...

Integer/bounds protections:
- ...

Concurrency considerations:
- ...

Tests added:
- ...

Verification run:
- normal tests:
- ASan:
- UBSan:
- TSan:
- static analysis:
- fuzzing:

Residual risks / checks not run:
- ...
```

Do not call the implementation "secure", "memory-safe", or "verified" unless the claim
is justified by the actual guarantees and checks performed.

---

## 31. Repository entry points

Sections 24–27 refer to "the repository's" configurations. In C++L they are:

```text
warnings        CMakeLists.txt: -Wall -Wextra -Wpedantic -Wshadow -Wconversion
                -Wsign-conversion, errors under every preset except ci-linux-gcc
static analysis make lint-changed (fast), make lint (whole tree),
                make ci-quality (Linux, as the Quality job runs it)
ASan            make asan                   CI: ci-asan, every run
UBSan           make ubsan                  CI: ci-ubsan, every run
TSan            make tsan on Linux,         CI: ci-tsan, nightly
                tools/ci/linux.sh tsan elsewhere (Docker)
bounded runs    tests/support/bounded.sh caps each file a test writes and the CPU
                time of each process it runs
```

`.clang-tidy` enforces `clang-analyzer-*`, `bugprone-*`, `cert-*`, `concurrency-*`,
`performance-*` and `portability-*` with every finding an error. Of
`cppcoreguidelines-*` it enables only the cast, slicing and virtual-destructor checks,
and it disables `bugprone-exception-escape`. What those leave out, the review in §28
covers by hand.

The tree has no fuzz harness and the `release` preset sets no hardening flags. Until
they exist, §23 and §27 cannot be satisfied by a command: report fuzzing as not run,
and make no hardening claim.

Related skills:

* `.agents/skills/cppl-security-fix` for a soundness or security defect, with
  `SECURITY.md` for disclosure;
* `.agents/skills/cppl-fuzz-verifier` for verifier fuzzing oracles;
* `.agents/skills/cppl-ffi-model` and `.agents/skills/cppl-runtime-validation` for §13
  and §21 boundaries.
