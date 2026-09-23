# C++L for Neovim

A thin Neovim client for [`cppl-lsp`](../../tools/cppl-lsp/README.md). It
registers the `cppl` filetype, starts the server through Neovim's built-in LSP
client, and enables format-on-save. It owns no C++L semantics.

Requires Neovim 0.10 or newer (for `vim.lsp.start` and `vim.fs`).

## Install

With `lazy.nvim`:

```lua
{
  "cppl-lang/cppl",
  config = function()
    require("cppl").setup()
  end,
}
```

With `packer.nvim`:

```lua
use({
  "cppl-lang/cppl",
  config = function()
    require("cppl").setup()
  end,
})
```

Or, without a plugin manager, put this directory on your `runtimepath`:

```lua
vim.opt.runtimepath:append("/path/to/cppl/editors/neovim")
require("cppl").setup()
```

## Configuration

All options are optional; the defaults work in a checkout that has been built.

```lua
require("cppl").setup({
  -- Empty prefers <project>/build/dev/bin/cppl-lsp, then cppl-lsp on PATH.
  server_path = "",
  -- Forwarded as `--clang`; empty uses cppl-lsp's own default.
  clang_path = "",
  -- Forwarded as repeated `--clang-arg`.
  clang_arguments = { "-std=c++20" },
  format_on_save = true,
  -- Verification status over each Law, proof and verified function.
  code_lens = true,
  -- Neovim's own LSP completion as you type (0.11 and newer); false leaves
  -- completion to a completion plugin.
  completion = true,
})
```

The project root is detected by searching upward for `CMakeLists.txt` or
`.git`, and one server is shared by all buffers under the same root.

## What works

Diagnostics, formatting (`vim.lsp.buf.format`), format-on-save and code
actions, all from the server. Syntax coloring comes from
[`syntax/cppl.vim`](syntax/cppl.vim), which sources the bundled C++ syntax and
adds only the C++L contextual words. Proof statements spelled like C++
declarations, such as `exact h;` and `contradiction name;`, come from the
server's semantic tokens instead, as `@lsp.type.keyword.cppl`, which
`setup()` links to `Statement` like the syntax file's own proof words.

`vim.lsp.buf.hover()` (`K`) shows what Clang knows of a name, or a C++L
declaration as written with what became of its obligations; inside a `cases`
or `decompose` arm block it shows the subject's states, and completion offers
the arms still owed. Elsewhere completion offers what Clang would accept, and
C++L's declarations, proof statements and clauses where the grammar admits
them. On Neovim 0.11 and newer `setup()` turns on Neovim's own LSP completion
for C++L buffers (`completion = false` leaves it to a completion plugin, which
reads the same answers through `vim.lsp`).

Each Law, proof and verified function shows its verification status as a code
lens over its name -- `PROVEN`, `TRUSTED`, `UNRESOLVED` and why -- refreshed
when the buffer is entered, left insert mode or written; `code_lens = false`
turns this off.

`vim.lsp.buf.definition()`, `declaration()`, `type_definition()` and
`implementation()` are answered by Clang over the file's projection; with the
client attached, `CTRL-]` jumps to the definition too, through Neovim's LSP
`tagfunc`. `vim.lsp.buf.references()` (`grr` in Neovim 0.11) covers every open
C++L buffer and the headers each includes, and `vim.lsp.buf.document_highlight()`
marks each occurrence of the name under the cursor. Rename is not implemented by
the server yet — see "Currently unsupported" in the
[`cppl-lsp` README](../../tools/cppl-lsp/README.md).

## Troubleshooting

`:checkhealth vim.lsp` shows whether the client attached. If it did not, the
server binary was not found: build it with `make build`, or set `server_path`.
`:LspLog` has the server's stderr.
