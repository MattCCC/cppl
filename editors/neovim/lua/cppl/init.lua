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
  -- Extra flags forwarded as repeated `--clang-arg`, after those the file's
  -- compile_commands.json entry gives it, which cppl-lsp reads itself.
  clang_arguments = {},
  format_on_save = true,
  -- Format the statement just finished when `}` or `;` is typed, as the
  -- server's on-type formatting lays it out.
  format_on_type = true,
  -- Show, over each Law, proof and verified function, what became of its
  -- obligations in the last compile.
  code_lens = true,
  -- Neovim's own LSP completion (0.11 and newer), triggered as you type. Off
  -- leaves completion to whatever completion plugin is installed.
  completion = true,
  -- Fold by the server's folding ranges (0.11 and newer), every fold open
  -- when the buffer is shown. Off leaves the window's folding as configured.
  folding = true,
  -- Show each argument's parameter name and each `auto`'s deduced type
  -- inline (0.10 and newer).
  inlay_hints = true,
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

  vim.api.nvim_create_autocmd("LspAttach", {
    group = group,
    callback = function(event)
      local client = vim.lsp.get_client_by_id(event.data.client_id)
      if client == nil or client.name ~= "cppl-lsp" then
        return
      end
      -- Neovim shows code lenses only once asked to fetch them, and again
      -- after each change to the buffer they describe.
      if M.options.code_lens then
        local function refresh()
          vim.lsp.codelens.refresh({ bufnr = event.buf })
        end
        refresh()
        vim.api.nvim_create_autocmd({ "BufEnter", "InsertLeave", "BufWritePost" }, {
          group = group,
          buffer = event.buf,
          callback = refresh,
        })
      end
      if M.options.completion and vim.lsp.completion ~= nil then
        vim.lsp.completion.enable(true, client.id, event.buf, { autotrigger = true })
      end
      -- Neovim sends no on-type formatting request of its own. After a trigger
      -- character the server names is typed, ask for the edits and apply them.
      local on_type = client.server_capabilities.documentOnTypeFormattingProvider
      if M.options.format_on_type and on_type then
        local triggers = { [on_type.firstTriggerCharacter] = true }
        for _, character in ipairs(on_type.moreTriggerCharacter or {}) do
          triggers[character] = true
        end
        vim.api.nvim_create_autocmd("InsertCharPre", {
          group = group,
          buffer = event.buf,
          callback = function()
            local typed = vim.v.char
            if not triggers[typed] then
              return
            end
            -- The character is inserted after this returns.
            vim.schedule(function()
              if not vim.api.nvim_buf_is_valid(event.buf) or vim.api.nvim_get_current_buf() ~= event.buf then
                return
              end
              local params = vim.lsp.util.make_position_params(0, client.offset_encoding)
              params.ch = typed
              params.options = {
                tabSize = vim.fn.shiftwidth(),
                insertSpaces = vim.bo[event.buf].expandtab,
              }
              vim.lsp.buf_request(event.buf, "textDocument/onTypeFormatting", params, function(err, result)
                if err == nil and result ~= nil and vim.api.nvim_buf_is_valid(event.buf) then
                  vim.lsp.util.apply_text_edits(result, event.buf, client.offset_encoding)
                end
              end)
            end)
          end,
        })
      end
      if M.options.inlay_hints and vim.lsp.inlay_hint ~= nil and client.server_capabilities.inlayHintProvider then
        vim.lsp.inlay_hint.enable(true, { bufnr = event.buf })
      end
      if M.options.folding and vim.lsp.foldexpr ~= nil and client.server_capabilities.foldingRangeProvider then
        for _, window in ipairs(vim.fn.win_findbuf(event.buf)) do
          vim.wo[window][0].foldmethod = "expr"
          vim.wo[window][0].foldexpr = "v:lua.vim.lsp.foldexpr()"
          vim.wo[window][0].foldlevel = 99
        end
      end
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
