// cppl-vscode: a thin client for cppl-lsp.
//
// This extension owns no C++L semantics. It locates the cppl-lsp
// executable, starts it as a child process speaking LSP over stdio, and
// registers the client for the `cppl` language ID. It attaches to ordinary
// `cpp` documents only when `cppl.serveCpp` asks it to (tools/cppl-lsp/
// README.md, "Editor architecture"). Diagnostics, formatting and code actions
// all come from the server; nothing here re-derives them.

const path = require("path");
const fs = require("fs");
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;
let starting;
let outputChannel;

function servesCpp() {
  return vscode.workspace.getConfiguration("cppl").get("serveCpp") === true;
}

function served(document) {
  return document.uri.scheme === "file" && (document.languageId === "cppl" || (servesCpp() && document.languageId === "cpp"));
}

function documentSelector() {
  const selector = [{ scheme: "file", language: "cppl" }];
  if (servesCpp()) {
    selector.push({ scheme: "file", language: "cpp" });
  }
  return selector;
}

function builtServerPath(workspaceRoot) {
  const executable = process.platform === "win32" ? "cppl-lsp.exe" : "cppl-lsp";
  return path.join(workspaceRoot, "build", "dev", "bin", executable);
}

// An explicit setting wins; otherwise prefer this workspace's own build so a
// contributor's edits take effect without configuration, and fall back to the
// bare name so an installed cppl-lsp is found on PATH.
function resolveServerPath(workspaceRoot) {
  const configured = vscode.workspace.getConfiguration("cppl").get("serverPath");
  if (configured && configured.length > 0) {
    return configured;
  }
  const built = builtServerPath(workspaceRoot);
  if (fs.existsSync(built)) {
    return built;
  }
  return process.platform === "win32" ? "cppl-lsp.exe" : "cppl-lsp";
}

function serverArguments() {
  const config = vscode.workspace.getConfiguration("cppl");
  const args = [];
  const clangPath = config.get("clangPath");
  if (clangPath && clangPath.length > 0) {
    args.push("--clang", clangPath);
  }
  const clangArguments = config.get("clangArguments") || [];
  for (const argument of clangArguments) {
    args.push("--clang-arg", argument);
  }
  return args;
}

function workspaceRoot() {
  const folders = vscode.workspace.workspaceFolders;
  return folders && folders.length > 0 ? folders[0].uri.fsPath : process.cwd();
}

async function startClient() {
  const serverPath = resolveServerPath(workspaceRoot());

  const run = {
    command: serverPath,
    args: serverArguments(),
    transport: TransportKind.stdio,
  };

  const clientOptions = {
    documentSelector: documentSelector(),
    outputChannel,
    // A half-typed buffer is the normal state of a file being edited, so a
    // failed start must report itself rather than surface as an unhandled
    // rejection.
    initializationFailedHandler: (error) => {
      outputChannel.appendLine(`cppl-lsp failed to initialize: ${error}`);
      vscode.window.showErrorMessage(
        `cppl-lsp failed to start from '${serverPath}'. Build it with 'make build', or set 'cppl.serverPath'.`
      );
      return false;
    },
  };

  client = new LanguageClient("cppl", "C++L Language Server", { run, debug: run }, clientOptions);
  await client.start();
}

async function stopClient() {
  if (starting) {
    await starting;
  }
  if (!client) {
    return;
  }
  const stopping = client.stop();
  client = undefined;
  await stopping;
}

// The server starts once a document it serves is open, so a C++ workspace
// that never opens a C++L file never runs it unless `cppl.serveCpp` is on.
async function ensureStarted() {
  if (client || starting) {
    return starting;
  }
  if (!vscode.workspace.textDocuments.some(served)) {
    return undefined;
  }
  starting = startClient().finally(() => {
    starting = undefined;
  });
  return starting;
}

async function activate(context) {
  outputChannel = vscode.window.createOutputChannel("C++L Language Server");
  context.subscriptions.push(outputChannel);

  context.subscriptions.push(
    vscode.commands.registerCommand("cppl.restartServer", async () => {
      await stopClient();
      await startClient();
      vscode.window.showInformationMessage("cppl-lsp restarted.");
    }),
    vscode.commands.registerCommand("cppl.showOutput", () => {
      outputChannel.show();
    }),
    vscode.workspace.onDidOpenTextDocument((document) => {
      if (served(document)) {
        ensureStarted();
      }
    }),
    // Which documents the client serves is fixed when it starts.
    vscode.workspace.onDidChangeConfiguration(async (event) => {
      if (event.affectsConfiguration("cppl.serveCpp")) {
        await stopClient();
        await ensureStarted();
      }
    })
  );

  await ensureStarted();
}

function deactivate() {
  return stopClient();
}

module.exports = { activate, deactivate };
