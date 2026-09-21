// cppl-vscode: a thin client for cppl-lsp.
//
// This extension owns no C++L semantics. It locates the cppl-lsp
// executable, starts it as a child process speaking LSP over stdio, and
// registers the client for the `cppl` language ID only -- it never attaches
// to ordinary `cpp` documents (tools/cppl-lsp/README.md, "Editor
// architecture").

const path = require("path");
const fs = require("fs");
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

function defaultServerPath(workspaceRoot) {
  const executable = process.platform === "win32" ? "cppl-lsp.exe" : "cppl-lsp";
  return path.join(workspaceRoot, "build", "dev", "bin", executable);
}

function resolveServerPath(workspaceRoot) {
  const configured = vscode.workspace.getConfiguration("cppl").get("serverPath");
  if (configured && configured.length > 0) {
    return configured;
  }
  return defaultServerPath(workspaceRoot);
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

function activate(context) {
  const workspaceRoot =
    vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length > 0
      ? vscode.workspace.workspaceFolders[0].uri.fsPath
      : process.cwd();

  const serverPath = resolveServerPath(workspaceRoot);
  if (!fs.existsSync(serverPath)) {
    vscode.window.showErrorMessage(
      `cppl-lsp: server executable not found at '${serverPath}'. Build it with 'make build', or set ` +
        "'cppl.serverPath' to point at an existing build."
    );
    return;
  }

  const run = {
    command: serverPath,
    args: serverArguments(),
    transport: TransportKind.stdio,
  };

  const serverOptions = { run, debug: run };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "cppl" }],
  };

  client = new LanguageClient("cppl", "C++L Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client.start());
}

function deactivate() {
  if (!client) {
    return undefined;
  }
  return client.stop();
}

module.exports = { activate, deactivate };
