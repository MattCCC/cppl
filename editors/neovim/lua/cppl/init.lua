-- cppl.nvim: a thin Neovim client for cppl-lsp.
--
-- Owns no C++L semantics. It registers the `cppl` filetype, points Neovim's
-- built-in LSP client at cppl-lsp over stdio, and enables format-on-save
-- through the server's own canonical formatter.

local M = {}

local defaults = {
  -- Path to cppl-lsp. Empty means: prefer ./build/dev/bin/cppl-lsp under the
  -- detected project root, else fall back to cppl-lsp on PATH.
  server_path = "",
  -- Clang driver forwarded as `--clang`; empty uses cppl-lsp's own default.
  clang_path = "",
  -- Extra flags forwarded as repeated `--clang-arg`.
  clang_arguments = {},
  format_on_save = true,
}

M.options = vim.deepcopy(defaults)

local function executable_name()
  return vim.fn.has("win32") == 1 and "cppl-lsp.exe" or "cppl-lsp"
end

local function resolve_server_path(root)
  if M.options.server_path ~= "" then
    return M.options.server_path
  end
  if root then
    local built = table.concat({ root, "build", "dev", "bin", executable_name() }, "/")
    if vim.fn.executable(built) == 1 then
      return built
    end
  end
  return executable_name()
end

local function server_arguments()
  local args = {}
  if M.options.clang_path ~= "" then
    table.insert(args, "--clang")
    table.insert(args, M.options.clang_path)
  end
  for _, argument in ipairs(M.options.clang_arguments) do
    table.insert(args, "--clang-arg")
    table.insert(args, argument)
  end
  return args
end

local function project_root(bufnr)
  local markers = { "CMakeLists.txt", ".git" }
  local start = vim.api.nvim_buf_get_name(bufnr)
  if start == "" then
    return vim.uv and vim.uv.cwd() or vim.loop.cwd()
  end
  local found = vim.fs.find(markers, { upward = true, path = vim.fs.dirname(start) })[1]
  return found and vim.fs.dirname(found) or nil
end

-- One client per project root, so several open buffers share a server.
local function start(bufnr)
  local root = project_root(bufnr)
  local command = { resolve_server_path(root) }
  vim.list_extend(command, server_arguments())

  vim.lsp.start({
    name = "cppl-lsp",
    cmd = command,
    root_dir = root,
  }, { bufnr = bufnr })
end

function M.setup(options)
  M.options = vim.tbl_deep_extend("force", vim.deepcopy(defaults), options or {})

  vim.filetype.add({
    extension = { cppl = "cppl" },
  })

  local group = vim.api.nvim_create_augroup("cppl", { clear = true })

  -- cppl-lsp reports as `keyword` tokens the proof statements the syntax file
  -- cannot tell from C++ declarations, such as `exact h;` and `contradiction
  -- name;`. They are colored like the proof words it can see. A colorscheme
  -- clears highlights when it loads, so the link is made again after one.
  local function link_proof_keywords()
    vim.api.nvim_set_hl(0, "@lsp.type.keyword.cppl", { link = "Statement", default = true })
  end
  link_proof_keywords()
  vim.api.nvim_create_autocmd("ColorScheme", {
    group = group,
    callback = link_proof_keywords,
  })

  vim.api.nvim_create_autocmd("FileType", {
    group = group,
    pattern = "cppl",
    callback = function(event)
      start(event.buf)
    end,
  })

  if M.options.format_on_save then
    vim.api.nvim_create_autocmd("BufWritePre", {
      group = group,
      pattern = "*.cppl",
      callback = function(event)
        vim.lsp.buf.format({ bufnr = event.buf, timeout_ms = 2000 })
      end,
    })
  end
end

return M
