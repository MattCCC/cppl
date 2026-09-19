# Straight-line locals and assignments

Status: implemented by this slice; normative rules are SPEC.md 12.8.

A verified body may declare locals and assign to them. Each write gives the
local its next logical version; a read denotes the version current where it
stands. Identity is the declaration Clang resolved, so shadowing, nested scopes
and same-spelled declarations follow C++ name lookup, and no name is ever looked
up by spelling. The value a version denotes is the modeled expression that
established it, so a local is never an unknown and nothing about one is assumed.

The bridge now takes statements in program order, carrying the versions. It
lowers what follows a branch once per arm, under the versions that arm
established. A local's value after a branch is therefore path-sensitive by
construction: there is no merge operation, no `select` over versions, and no new
kernel capability. `vir::LocalVersion` and `vir::LocalRef` carry the model; the
runtime program keeps its own statements, and erasure stays deletion-only.

Obligation generation walks a path's steps in order. A guard contributes its
condition; a version contributes the value it binds. Both contribute their calls
where the body evaluates them. This anchoring is the slice's load-bearing rule:
a call written before a branch is proven without that branch's condition, and a
call bound to a local is proven on every path that reaches its statement, even
where the local is never read. Without it, binding a call to a local would move
its precondition to the paths that happen to read the result.

Supported declarations are `T x = e;`, `T x{e};`, `T x(e);`, with `auto` or
`const`, at automatic storage and a modeled type. Assignment names a local of
the same body. Uninitialized, `static`, `extern`, `register`, `thread_local`,
reference, pointer, `volatile`, aggregate and empty-braced declarations are
refused, as are compound assignment, increment, assignment to a parameter, and
unmodeled initializer conversions. `volatile` is now unmodeled by name rather
than incidentally, because a volatile read is an effect, not a value.
Qualification alone is not a conversion: reading a `const` local yields the
value it holds. C++ puts a local in scope inside its own initializer, so
`unsigned y = y;` is refused by name rather than trusted to scoping.

Replaying a value at every read has a cost the core cannot share: locals that
each read the previous one twice double the stated term per statement. Lowered
terms are therefore bounded at 16384 core nodes and a path at 128 statements,
and a body beyond either is refused. Term lowering also scopes each version to
the body beneath it and replays a read only below the version being replayed,
so a sibling arm's version or a cycle is refused even from malformed VIR.

Assignment to a parameter is refused rather than modeled. A postcondition names
the value the caller passed, and C++ contracts do not settle whether a parameter
mentioned in `ensures` denotes that value or the one at return. The refusal
keeps the question open instead of answering it silently.

Kernel rules, logical assumptions, axioms and runtime checks: zero. Kernel and
core versions are unchanged, because the accepted calculus is unchanged.

Validation covers chained initializers, multiple declarators, `auto`, `const`,
braced and parenthesized initialization, assignment in one arm and in both,
nested branch assignments, shadowing and nested scopes, a declaration as an
unbraced arm, `bool` and call-bound locals as guards, preserved earlier values,
calls in initializers and in assigned values, erasure, and all three target
standards. Negative cases cover stale versions, sibling and inner arms'
versions, guard polarity through a local, false arms, moved call
preconditions, self-initialization, assignments hidden in expressions, chained
or comma-sequenced, captures, structured bindings, globals, exponential
expansion, every refused declaration and mutation, and malformed VIR that
rebinds a version, reads a sibling arm's version, or reads itself.

Next: arithmetic normalization, then loops with explicit invariants. The
`result == x + 2u` form of a chained increment needs the former: `(x + 1) + 1`
and `x + 2` are not definitionally equal, and this slice adds no reassociation.
