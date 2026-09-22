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
})
```

The project root is detected by searching upward for `CMakeLists.txt` or
`.git`, and one server is shared by all buffers under the same root.

## What works

Diagnostics, formatting (`vim.lsp.buf.format`), format-on-save and code
actions, all from the server. Syntax coloring comes from
[`syntax/cppl.vim`](syntax/cppl.vim), which sources the bundled C++ syntax and
adds only the C++L contextual words.

Hover, completion and navigation are not implemented by the server yet, so
they are unavailable here — see "Currently unsupported" in the
[`cppl-lsp` README](../../tools/cppl-lsp/README.md).

## Troubleshooting

`:checkhealth vim.lsp` shows whether the client attached. If it did not, the
server binary was not found: build it with `make build`, or set `server_path`.
`:LspLog` has the server's stderr.
