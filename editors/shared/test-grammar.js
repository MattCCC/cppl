// Grammar regression test.
//
// The C++L contextual words are ordinary C++ identifiers outside their
// grammatical scopes (docs/GRAMMAR.md section 1). A grammar that colors every
// occurrence would mis-highlight valid C++, so these cases pin the boundary.
// Run with: node editors/shared/test-grammar.js

const fs = require("fs");
const path = require("path");

const grammarPath = path.join(__dirname, "cppl.tmLanguage.json");
const grammar = JSON.parse(fs.readFileSync(grammarPath, "utf8"));

// Flatten the top-level pattern list, in order, resolving `include`s into the
// rules they name. Order is what makes the model faithful: a TextMate engine
// scans left to right and lets the first matching rule consume the region, so
// an earlier rule (a comment, a string) masks every later one.
function resolveRules(patterns, seen = new Set()) {
  const out = [];
  for (const entry of patterns || []) {
    if (entry.include) {
      const name = entry.include.replace(/^#/, "");
      // `source.cpp` is the C++ grammar: not ours to model, and reaching it
      // means no C++L rule claimed the text, which is the answer we want.
      if (entry.include.startsWith("source.")) {
        out.push({ cpp: true });
        continue;
      }
      if (seen.has(name)) continue;
      seen.add(name);
      const target = grammar.repository[name];
      if (target) out.push(...resolveRules(target.patterns, seen));
      continue;
    }
    if (typeof entry.match === "string") {
      const scope = entry.name || (entry.captures && entry.captures["1"] && entry.captures["1"].name);
      out.push({ regex: new RegExp(entry.match, "g"), scope: scope || "(captures)" });
      continue;
    }
    if (typeof entry.begin === "string") {
      out.push({
        regex: new RegExp(entry.begin, "g"),
        end: new RegExp(entry.end, "g"),
        scope: entry.name || "(block)",
        inner: entry.patterns || [],
      });
    }
  }
  return out;
}

const rules = resolveRules(grammar.patterns);

// Walk the line the way an engine would and report which C++L scopes end up
// claiming any of its text.
function scopesFor(line) {
  const hits = [];
  let position = 0;

  while (position < line.length) {
    let best = null;
    for (const rule of rules) {
      if (rule.cpp) continue;
      rule.regex.lastIndex = position;
      const found = rule.regex.exec(line);
      if (found && (best === null || found.index < best.found.index)) {
        best = { rule, found };
      }
    }

    if (!best) break;

    const { rule, found } = best;

    // A comment or string swallows the rest of its region, so nothing inside
    // it can be claimed as C++L. That is the whole point of matching them
    // first.
    if (rule.end) {
      rule.end.lastIndex = found.index + Math.max(found[0].length, 1);
      const closing = rule.end.exec(line);
      const regionEnd = closing ? closing.index + closing[0].length : line.length;

      if (!/^(comment|string)\./.test(rule.scope)) {
        hits.push(rule.scope);
        for (const nested of resolveRules(rule.inner)) {
          if (nested.cpp) continue;
          const body = line.slice(found.index + found[0].length, regionEnd);
          nested.regex.lastIndex = 0;
          if (nested.regex.test(body)) hits.push(nested.scope);
        }
      }

      position = regionEnd;
      continue;
    }

    hits.push(rule.scope);
    position = found.index + Math.max(found[0].length, 1);
  }

  return hits;
}

const cases = [
  // Real C++L syntax must be recognized.
  { line: "law identity(int x)", expect: true, why: "law declaration head" },
  { line: "proof identity_holds(int x)", expect: true, why: "proof declaration head" },
  { line: "    proves identity(x);", expect: true, why: "proves clause" },
  { line: "verified int fifty(int x)", expect: true, why: "verified modifier" },
  { line: "    ensures (result == 50)", expect: true, why: "ensures clause" },
  { line: "    expects (x > 0)", expect: true, why: "expects clause" },
  { line: "type Percentage = int where (self >= 0);", expect: true, why: "refinement type" },
  { line: "    decreases (n);", expect: true, why: "decreases clause" },
  { line: "    invariant (i <= n)", expect: true, why: "invariant clause" },
  { line: "    refl;", expect: true, why: "refl proof statement" },
  { line: "    induction (n);", expect: true, why: "induction proof statement" },
  { line: "    forall (int i)", expect: true, why: "quantifier" },
  { line: "    ensures (result == old(x));", expect: true, why: "old snapshot" },

  // Ordinary C++ using contextual words as identifiers must stay untouched
  // (tests/fixtures/contextual_identifiers.cpp).
  { line: "int law = 1;", expect: false, why: "law as a variable name" },
  { line: "    return law - 1;", expect: false, why: "law in an expression" },
  { line: "int type = 2;", expect: false, why: "type as a variable name" },
  { line: "    int where = 3;", expect: false, why: "where as a variable name" },
  { line: "    result = compute();", expect: false, why: "result as an ordinary assignment target" },

  // Every line of tests/fixtures/contextual_identifiers.cpp is ordinary C++.
  // C++L reserves nothing, so outside a C++L construct the C++ reading wins
  // (SPEC.md 3.1) and the highlighter must follow that same precedence.
  { line: "void proof() {}", expect: false, why: "proof as an ordinary function name" },
  { line: "struct ghost {};", expect: false, why: "ghost as a struct name" },
  { line: "int verified = 0;", expect: false, why: "verified as a variable name" },
  { line: "int trusted = 0;", expect: false, why: "trusted as a variable name" },
  { line: "    proof();", expect: false, why: "calling a function named proof" },
  { line: "    ghost value;", expect: false, why: "declaring a variable of type ghost" },
  { line: "    (void)verified;", expect: false, why: "verified in a cast expression" },
  { line: "    int ghost = 1;", expect: false, why: "ghost shadowed by an ordinary identifier" },
  { line: "int pure = 0;", expect: false, why: "pure as a variable name" },
  { line: "int expects = 0;", expect: false, why: "expects as a variable name" },
  { line: "int ensures = 0;", expect: false, why: "ensures as a variable name" },
  { line: "int invariant = 0;", expect: false, why: "invariant as a variable name" },
  { line: "int decreases = 0;", expect: false, why: "decreases as a variable name" },
  { line: "int forall = 0;", expect: false, why: "forall as a variable name" },
  { line: "int exists = 0;", expect: false, why: "exists as a variable name" },
  { line: "int old = 0;", expect: false, why: "old as a variable name" },
  { line: "int self = 0;", expect: false, why: "self as a variable name" },
  { line: "int refl = 0;", expect: false, why: "refl as a variable name" },
  { line: "int induction = 0;", expect: false, why: "induction as a variable name" },
  { line: "int decompose = 0;", expect: false, why: "decompose as a variable name" },

  // C++ constructs that share a spelling with C++L.
  { line: "using type = int;", expect: false, why: "using-alias named type" },
  { line: "    std::vector<int> cases;", expect: false, why: "cases as a variable name" },
  { line: "    switch (x) { case 1: break; }", expect: false, why: "ordinary switch label" },
  { line: "template <typename T> struct apply {};", expect: false, why: "apply as a template name" },

  // A ghost local with an explicit type is unambiguous and is colored; a bare
  // `ghost value;` is not, and the grammar deliberately resolves that in
  // favour of the C++ reading. See the modifier rule's comment.
  { line: "    ghost int shadow = x;", expect: true, why: "ghost local with a type" },

  // Comments and strings must never be reinterpreted as C++L.
  { line: "// law identity(int x) in a comment", expect: false, why: "C++L syntax inside a line comment" },
  { line: '    const char* s = "law identity(int x)";', expect: false, why: "C++L syntax inside a string" },
];

let failures = 0;
for (const testCase of cases) {
  const hits = scopesFor(testCase.line);
  const matched = hits.length > 0;
  if (matched !== testCase.expect) {
    failures += 1;
    const wanted = testCase.expect ? "to be C++L syntax" : "to stay ordinary C++";
    process.stderr.write(
      `FAIL (${testCase.why}): expected ${JSON.stringify(testCase.line)} ${wanted}, ` +
        `got [${hits.join(", ") || "no match"}]\n`
    );
  }
}

if (failures > 0) {
  process.stderr.write(`\n${failures} of ${cases.length} grammar cases failed.\n`);
  process.exit(1);
}

process.stdout.write(`grammar: ${cases.length} cases passed\n`);
