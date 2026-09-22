// cppl-vscode: a thin client for cppl-lsp.
//
// This extension owns no C++L semantics. It locates the cppl-lsp
// executable, starts it as a child process speaking LSP over stdio, and
// registers the client for the `cppl` language ID only -- it never attaches
// to ordinary `cpp` documents (tools/cppl-lsp/README.md, "Editor
// architecture"). Diagnostics, formatting and code actions all come from the
// server; nothing here re-derives them.

const path = require("path");
const fs = require("fs");
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;
let outputChannel;

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
    documentSelector: [{ scheme: "file", language: "cppl" }],
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
  if (!client) {
    return;
  }
  const stopping = client.stop();
  client = undefined;
  await stopping;
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
    })
  );

  await startClient();
}

function deactivate() {
  return stopClient();
}

module.exports = { activate, deactivate };
