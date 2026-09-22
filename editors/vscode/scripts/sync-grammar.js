// Copy the shared C++L TextMate grammar into this extension.
//
// editors/shared/cppl.tmLanguage.json is the single source of truth, but a
// VSIX can only ship files under its own root, so the grammar is copied in at
// package time rather than duplicated in the repository.

const fs = require("fs");
const path = require("path");

const source = path.join(__dirname, "..", "..", "shared", "cppl.tmLanguage.json");
const destinationDirectory = path.join(__dirname, "..", "syntaxes");
const destination = path.join(destinationDirectory, "cppl.tmLanguage.json");

// Fail loudly: a silently missing grammar would ship an extension with no
// highlighting at all.
JSON.parse(fs.readFileSync(source, "utf8"));

fs.mkdirSync(destinationDirectory, { recursive: true });
fs.copyFileSync(source, destination);

process.stdout.write(`synced grammar -> ${path.relative(process.cwd(), destination)}\n`);
