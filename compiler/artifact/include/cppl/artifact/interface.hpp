#pragma once

#include "cppl/source/digest.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::artifact {

// A verification interface: what one translation unit proved about the
// functions it defines, recorded so that another unit may use those contracts
// without their bodies (SPEC.md TUBOUND-002, RFC 0017).
//
// To every unit that reads one it is untrusted input. This component only
// writes the canonical text and reads it back strictly: a read establishes that
// the bytes are an intact, well-formed artifact of this format version, and
// nothing more. Whether the artifact was produced under the reader's
// configuration, whether its sources are unchanged, and whether the reader's
// own declaration states the contract recorded are separate checks, made by the
// driver and by the obligation layer, and every one of them must pass before a
// recorded contract is used (SPEC.md TUBOUND-004, TUBOUND-005, TRUST.md TCB-XTU-*).
//
// The text is line oriented. Every field is one token, with each byte outside
// printable ASCII, a space and `%` written as `%XX` in upper-case hex, so a
// path, a Clang USR or a version string with spaces in it is still one field
// and has exactly one spelling. A repeated item is written in the byte order of
// its line, which is what makes the text of a given content unique, and the
// last line is the SHA-256 of every byte before it.

inline constexpr std::string_view kMagic = "cppl-verification-interface";
// Version 2 records the models of code outside the program an entry rests on
// (`Model`), and version 3 the runtime validation sites (`RuntimeCheck`). A
// reader refuses an interface of an earlier version, which could not say
// whether a contract rested on either (SPEC.md TUBOUND-005).
inline constexpr std::uint32_t kFormatVersion = 3;

// Bounds on what a reader accepts and a writer produces. A reader never scans or
// allocates past them, whatever the input claims.
inline constexpr std::size_t kMaxBytes = std::size_t{16} << 20U;
inline constexpr std::size_t kMaxLineBytes = std::size_t{64} << 10U;
inline constexpr std::size_t kMaxEntries = std::size_t{1} << 16U;
inline constexpr std::size_t kMaxSources = std::size_t{1} << 16U;
inline constexpr std::size_t kMaxFlags = std::size_t{1} << 12U;
inline constexpr std::size_t kMaxEntryItems = std::size_t{1} << 14U;

// What must be the same where an interface is produced and where it is used for
// a recorded contract to mean the same thing in both places (SPEC.md TUBOUND-005).
// Each field is compared exactly; a consumer names the first that differs.
struct Configuration {
    std::string compiler;    // the C++L compiler version
    std::string semantics;   // the declared verification-semantics version
    source::Digest verifier; // the digest of the verifier's semantics-bearing sources
    std::string kernel;      // kernel::kKernelVersion
    std::string core;        // kernel::kFormalCoreVersion
    std::string clang;       // the Clang that resolved C++ semantics
    std::string language;    // the selected -std, or `default`
    std::string target;      // the effective target triple
    // The options that can change C++ meaning, in command-line order.
    std::vector<std::string> flags;

    friend bool operator==(const Configuration&, const Configuration&) = default;
};

// A file the producing unit was preprocessed from, and its content then. A
// consumer refuses the interface when any of them has changed since (SPEC.md
// TUBOUND-005), which is what makes an interface older than its unit stale.
struct SourceFile {
    std::string path; // absolute
    source::Digest digest;

    friend bool operator==(const SourceFile&, const SourceFile&) = default;
};

// A trusted law a recorded contract rests on, as the producing unit's trust
// report names it (SPEC.md TUBOUND-006).
struct Premise {
    source::Digest identity;
    std::string name;
    std::string file;
    std::uint32_t line = 0;

    friend bool operator==(const Premise&, const Premise&) = default;
};

// A trusted model a recorded contract rests on that is not a law of the
// program: what the producing compiler takes code outside the program, such as
// the standard library the program runs with, to do, which nothing verified
// (SPEC.md TUBOUND-006, STDMODEL-018). The format knows nothing of what is
// modeled: the identity is the producing compiler's, and the name is what a
// report shows. Like a premise, it is carried and reported by every unit whose
// claims rest on the entry, never re-affirmed.
struct Model {
    source::Digest identity;
    std::string name;

    friend bool operator==(const Model&, const Model&) = default;
};

// An unsafe block a recorded contract rests on (SPEC.md TUBOUND-006).
struct UnsafeBlock {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    friend bool operator==(const UnsafeBlock&, const UnsafeBlock&) = default;
};

// A runtime validation site a recorded contract rests on: a place in a
// verified body where a value entered a refinement type because the runtime
// conditions of its path held, rather than by anything proven of every
// execution (SPEC.md RUNTIMECHECK-011, RUNTIMECHECK-015). Its identity is where
// it is and what it enters; the predicate is recorded only to be shown.
struct RuntimeCheck {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string refinement;
    std::string predicate;

    friend bool operator==(const RuntimeCheck&, const RuntimeCheck&) = default;
};

// A contract another interface recorded that a recorded contract rests on,
// directly or through what it calls: the callable, and the identity of the
// entry that recorded it (SPEC.md TUBOUND-006, TUBOUND-008).
struct Dependency {
    std::string symbol;
    source::Digest entry;

    friend bool operator==(const Dependency&, const Dependency&) = default;
};

enum class Correctness : std::uint8_t {
    Total,   // established, and so is the function's termination
    Partial, // holds if the function returns (SPEC.md CORRECT-006)
};

// One contract the producing unit proved, and what its proof rests on, each
// kind of dependency apart: trusted laws, library models, unsafe code, runtime
// validation and contracts of further units are different premises and are
// reported as such wherever the contract is used (SPEC.md TUBOUND-002,
// TUBOUND-006, RUNTIMECHECK-015).
struct Entry {
    // Clang's USR for the function: its qualified declaration, overload,
    // parameter types, qualifiers and template arguments (SPEC.md TUBOUND-004).
    std::string symbol;
    std::string name; // the qualified name, for diagnostics
    // The canonical identity of the contract statement, as the obligation layer
    // builds it from a declaration. A consumer rebuilds it from its own.
    source::Digest statement;
    // The statement as the producer described it. Diagnostics only: nothing
    // is ever read back out of it.
    std::string contract;
    Correctness correctness = Correctness::Partial;
    std::vector<Premise> premises;
    std::vector<Model> models;
    std::vector<UnsafeBlock> unsafe;
    std::vector<RuntimeCheck> runtime;
    std::vector<Dependency> depends;

    friend bool operator==(const Entry&, const Entry&) = default;
};

struct Interface {
    Configuration configuration;
    std::string unit; // the producing unit's main input, absolute
    std::vector<SourceFile> sources;
    std::vector<Entry> entries;

    friend bool operator==(const Interface&, const Interface&) = default;
};

// The verification-result identity of one entry (SPEC.md TUBOUND-009): the
// function, the statement, whether it is total, and the identity of every
// dependency of every kind, runtime validation sites included, each dependency
// on another unit's contract by that contract's own result identity. Two
// entries share it exactly when a caller may conclude the same from either.
// What is recorded only to be shown -- the function's name, the statement as
// text, where a trusted law is written, the predicate a runtime check
// established -- takes no part, so the identity is not that of the entry's
// bytes.
[[nodiscard]] source::Digest identify(const Entry& entry);

// Whether a name is one a library model is reported under.
[[nodiscard]] bool is_library_model(std::string_view name);

// The canonical text. Repeated items are put in canonical order and exact
// duplicates dropped; an empty field, two sources at one path with different
// content, two entries for one symbol, or a result past the bounds above is an
// error, because it could not be read back as written.
[[nodiscard]] std::expected<std::string, std::string> serialize(const Interface& recorded);

struct ParseError {
    std::string reason;
    std::size_t line = 0; // 1-based, 0 when the failure is not one line's
};

// A text read from an interface, as a report or a diagnostic shows it:
// printable ASCII as itself, and every other byte and `%` as `%XX`. A name
// decoded from a hostile interface may hold any byte, and shown raw, a newline
// in it could forge a line of the report that shows it (SPEC.md TUBOUND-005,
// TRUST.md TCB-XTU-007).
[[nodiscard]] std::string displayed(std::string_view text);

// Reads an interface. Anything that is not exactly the canonical text of some
// interface of this format version is refused with the reason: a different
// magic or version, a size or line past the bounds, a checksum that does not
// match, a missing, unknown, repeated or misplaced field, an out-of-order or
// duplicated item, a non-canonical token or number, and a status other than
// `proven`.
[[nodiscard]] std::expected<Interface, ParseError> parse(std::string_view text);

} // namespace cppl::artifact
