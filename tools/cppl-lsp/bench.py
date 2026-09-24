#!/usr/bin/env python3
"""Measures a language server over one workspace, as an editor would use it.

    python3 tools/cppl-lsp/bench.py --name cppl-lsp --workspace DIR \
        --file src/a.cpp --at 'needle' [--at 'needle'...] \
        --complete-after 'object.' --query Server -- COMMAND [ARGS...]

The server is started with COMMAND over stdio. It is initialized with the
workspace as its root, and one file of it is opened. What is recorded:

  initialize      until the server answers `initialize`
  first hover     from opening the file until a hover sent at once, as an
                  editor sends one, is answered
  diagnostics     from opening the file until its first diagnostics
  hover, definition, references, documentSymbol, completion
                  each request's latency once the file is open, over --runs
                  repetitions at each --at needle (median and 95th percentile)
  indexed         from `initialized` until the server reports its workspace
                  indexing done, as work-done progress whose title says index
  workspace/symbol
                  latency of --query once indexing is done
  memory          what the system charges the server process for once
                  indexing is done, and at most over the run: the physical
                  footprint on macOS, the resident set on Linux; and the
                  largest resident set, which on macOS also counts memory
                  freed and kept by the allocator

Only the Python standard library is used. Results are printed as a table and,
with --json, appended to a file as one JSON object per run.
"""

import argparse
import json
import os
import pathlib
import resource
import statistics
import subprocess
import sys
import threading
import time
import urllib.parse


def uri_of(path):
    return "file://" + urllib.parse.quote(str(pathlib.Path(path).resolve()))


class Server:
    """A language server on the other end of a pipe."""

    def __init__(self, command, cwd):
        self.process = subprocess.Popen(command, cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL)
        self.lock = threading.Condition()
        self.responses = {}
        self.notifications = []  # (time, method, params)
        self.next_id = 0
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _write(self, message):
        body = json.dumps(message).encode()
        self.process.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.process.stdin.flush()

    def _read(self):
        stream = self.process.stdout
        while True:
            length = None
            while True:
                line = stream.readline()
                if not line:
                    return
                line = line.strip()
                if not line:
                    break
                name, _, value = line.partition(b":")
                if name.lower() == b"content-length":
                    length = int(value)
            if length is None:
                continue
            message = json.loads(stream.read(length))
            now = time.monotonic()
            if "method" in message and "id" in message:
                self._answer(message)
            elif "method" in message:
                with self.lock:
                    self.notifications.append((now, message["method"], message.get("params")))
                    self.lock.notify_all()
            else:
                with self.lock:
                    self.responses[message["id"]] = (now, message)
                    self.lock.notify_all()

    def _answer(self, request):
        # What an editor answers a server that asks it something.
        method = request["method"]
        if method == "workspace/configuration":
            result = [None] * len(request.get("params", {}).get("items", []))
        else:
            result = None
        self._write({"jsonrpc": "2.0", "id": request["id"], "result": result})

    def notify(self, method, params):
        self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method, params, timeout=600.0):
        """Sends a request and returns (seconds until answered, the answer)."""
        with self.lock:
            self.next_id += 1
            identity = self.next_id
        start = time.monotonic()
        self._write({"jsonrpc": "2.0", "id": identity, "method": method, "params": params})
        with self.lock:
            if not self.lock.wait_for(lambda: identity in self.responses, timeout):
                raise TimeoutError(method)
            answered, message = self.responses.pop(identity)
        return answered - start, message

    def wait_for(self, predicate, timeout):
        """Waits for a notification `predicate` accepts; returns its time or None."""
        deadline = time.monotonic() + timeout
        seen = 0
        with self.lock:
            while True:
                for when, method, params in self.notifications[seen:]:
                    if predicate(method, params):
                        return when
                seen = len(self.notifications)
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not self.lock.wait(remaining):
                    if remaining <= 0:
                        return None

    def finish(self):
        try:
            self.request("shutdown", None, timeout=30)
            self.notify("exit", None)
        except (TimeoutError, BrokenPipeError, OSError):
            pass
        try:
            self.process.stdin.close()
        except OSError:
            pass
        _, status, usage = os.wait4(self.process.pid, 0)
        # ru_maxrss is in bytes on macOS and in kibibytes on Linux.
        scale = 1 if sys.platform == "darwin" else 1024
        return usage.ru_maxrss * scale


def memory_of(pid):
    """(now, peak) in MiB: what the system charges the process for, freed
    memory an allocator keeps for reuse left out. On macOS that is the
    physical footprint, which the activity monitor shows; on Linux, the
    resident set."""
    if sys.platform == "darwin":
        summary = subprocess.run(["vmmap", "--summary", str(pid)], capture_output=True, text=True).stdout
        found = {}
        for line in summary.splitlines():
            if line.startswith("Physical footprint"):
                label, _, value = line.partition(":")
                found["peak" if "peak" in label else "now"] = value.strip()
        return tuple(to_mib(found.get(key, "0K")) for key in ("now", "peak"))
    status = pathlib.Path(f"/proc/{pid}/status").read_text()
    fields = dict(line.split(":", 1) for line in status.splitlines() if ":" in line)
    return tuple(round(int(fields[key].split()[0]) / 1024, 1) for key in ("VmRSS", "VmHWM"))


def to_mib(value):
    scale = {"K": 1 / 1024, "M": 1, "G": 1024}
    return round(float(value[:-1]) * scale.get(value[-1], 1 / (1024 * 1024)), 1)


def position_of(text, needle, into):
    at = text.find(needle)
    if at < 0:
        raise SystemExit(f"'{needle}' is not in the file")
    at += into
    line = text.count("\n", 0, at)
    column = at - (text.rfind("\n", 0, at) + 1)
    # Characters count in UTF-16 units; the needles are ASCII.
    return {"line": line, "character": column}


def answered(message):
    """Whether a response carries something: a fast empty answer is no answer."""
    result = message.get("result")
    if isinstance(result, dict):
        if "items" in result:
            return bool(result["items"])
        if "contents" in result:
            return bool(result["contents"])
    return bool(result)


def summary(samples):
    """samples: (seconds, response) pairs."""
    ordered = sorted(seconds for seconds, _ in samples)
    p95 = ordered[min(len(ordered) - 1, int(round(0.95 * (len(ordered) - 1))))]
    return {"median_ms": round(statistics.median(ordered) * 1000, 1), "p95_ms": round(p95 * 1000, 1),
            "n": len(ordered), "answered": sum(1 for _, message in samples if answered(message))}


def indexing(title):
    return title is not None and "index" in title.lower()


def measure(arguments):
    workspace = pathlib.Path(arguments.workspace).resolve()
    path = workspace / arguments.file
    text = path.read_text()
    uri = uri_of(path)
    results = {"server": arguments.name, "workspace": str(workspace), "file": arguments.file}

    spawned = time.monotonic()
    server = Server(arguments.command, workspace)
    capabilities = {
        "window": {"workDoneProgress": True},
        "workspace": {"workspaceFolders": True, "configuration": True, "symbol": {}},
        "textDocument": {
            "hover": {"contentFormat": ["markdown", "plaintext"]},
            "completion": {"completionItem": {"snippetSupport": True}},
            "documentSymbol": {"hierarchicalDocumentSymbolSupport": True},
            "publishDiagnostics": {},
            "rename": {"prepareSupport": True},
        },
    }
    elapsed, _ = server.request("initialize", {
        "processId": os.getpid(), "rootUri": uri_of(workspace), "capabilities": capabilities,
        "workspaceFolders": [{"uri": uri_of(workspace), "name": workspace.name}],
    })
    results["initialize_ms"] = round((time.monotonic() - spawned) * 1000, 1)
    initialized = time.monotonic()
    server.notify("initialized", {})

    # Which progress tokens are indexing, as each begins.
    tokens = {}

    def progress(method, params):
        if method != "$/progress":
            return False
        value = params.get("value", {})
        if value.get("kind") == "begin":
            tokens[json.dumps(params.get("token"))] = value.get("title")
        return False

    # An editor asks about a file as soon as it opens it, without waiting for
    # its diagnostics.
    points = [position_of(text, needle, 0) for needle in arguments.at]
    document = {"uri": uri}
    opened = time.monotonic()
    server.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "cpp", "version": 1, "text": text}})
    server.request("textDocument/hover", {"textDocument": document, "position": points[0]})
    results["first_hover_ms"] = round((time.monotonic() - opened) * 1000, 1)
    diagnosed = server.wait_for(
        lambda method, params: method == "textDocument/publishDiagnostics" and params.get("uri") == uri,
        arguments.timeout)
    results["diagnostics_ms"] = None if diagnosed is None else round((diagnosed - opened) * 1000, 1)

    samples = {name: [] for name in ("hover", "definition", "references", "documentSymbol", "completion")}
    completion = position_of(text, arguments.complete_after, len(arguments.complete_after))
    for _ in range(arguments.runs):
        for point in points:
            samples["hover"].append(server.request("textDocument/hover", {
                "textDocument": document, "position": point}))
            samples["definition"].append(server.request("textDocument/definition", {
                "textDocument": document, "position": point}))
            samples["references"].append(server.request("textDocument/references", {
                "textDocument": document, "position": point, "context": {"includeDeclaration": True}}))
        samples["documentSymbol"].append(server.request("textDocument/documentSymbol", {
            "textDocument": document}))
        samples["completion"].append(server.request("textDocument/completion", {
            "textDocument": document, "position": completion}))
    for name, values in samples.items():
        results[name] = summary(values)

    # Indexing ends when a progress titled with indexing ends and no other
    # begins for a while after.
    def ended(method, params):
        progress(method, params)
        if method != "$/progress" or params.get("value", {}).get("kind") != "end":
            return False
        return indexing(tokens.get(json.dumps(params.get("token"))))

    last_end = None
    while True:
        when = server.wait_for(ended, arguments.timeout)
        if when is None:
            break
        last_end = when
        with server.lock:
            server.notifications = [n for n in server.notifications if n[0] > when]
        quiet = server.wait_for(
            lambda method, params: method == "$/progress" and params.get("value", {}).get("kind") == "begin" and
            indexing(params.get("value", {}).get("title")), arguments.settle)
        if quiet is None:
            break
    results["indexed_ms"] = None if last_end is None else round((last_end - initialized) * 1000, 1)

    samples = [server.request("workspace/symbol", {"query": arguments.query}) for _ in range(arguments.runs)]
    results["workspace_symbol"] = summary(samples)
    results["memory_mib"], results["peak_memory_mib"] = memory_of(server.process.pid)
    results["peak_resident_mib"] = round(server.finish() / (1024 * 1024), 1)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--name", required=True)
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--file", required=True, help="the file to open, relative to the workspace")
    parser.add_argument("--at", action="append", required=True, help="text whose first letter a request names")
    parser.add_argument("--complete-after", required=True)
    parser.add_argument("--query", default="Server")
    parser.add_argument("--runs", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=1800.0)
    parser.add_argument("--settle", type=float, default=10.0,
                        help="seconds without a new indexing pass for indexing to count as done")
    parser.add_argument("--json", help="a file to append the results to")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    if arguments.command and arguments.command[0] == "--":
        arguments.command = arguments.command[1:]
    if not arguments.command:
        parser.error("no server command after --")
    results = measure(arguments)
    if arguments.json:
        with open(arguments.json, "a") as out:
            out.write(json.dumps(results) + "\n")
    width = max(len(key) for key in results)
    for key, value in results.items():
        print(f"{key:<{width}}  {value}")


if __name__ == "__main__":
    main()
