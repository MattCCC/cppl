using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.LanguageServer.Client;
using Microsoft.VisualStudio.Threading;
using Microsoft.VisualStudio.Utilities;

namespace Cppl.VisualStudio
{
    /// <summary>
    /// Content type for *.cppl. Diagnostics, formatting and highlighting come
    /// from cppl-lsp; this extension owns no C++L semantics.
    /// </summary>
    internal static class CpplContentDefinition
    {
        [Export]
        [Name("cppl")]
        [BaseDefinition(CodeRemoteContentDefinition.CodeRemoteContentTypeName)]
        internal static ContentTypeDefinition CpplContentType { get; set; }

        [Export]
        [FileExtension(".cppl")]
        [ContentType("cppl")]
        internal static FileExtensionToContentTypeDefinition CpplFileExtension { get; set; }
    }

    [ContentType("cppl")]
    [Export(typeof(ILanguageClient))]
    public sealed class CpplLanguageClient : ILanguageClient
    {
        public string Name => "C++L Language Server";

        public IEnumerable<string> ConfigurationSections => new[] { "cppl" };

        public object InitializationOptions => null;

        public IEnumerable<string> FilesToWatch => null;

        public bool ShowNotificationOnInitializeFailed => true;

        public event AsyncEventHandler<EventArgs> StartAsync;

        public event AsyncEventHandler<EventArgs> StopAsync
        {
            add { }
            remove { }
        }

        /// <summary>
        /// An environment override wins; otherwise prefer the solution's own
        /// build, then fall back to cppl-lsp on PATH.
        /// </summary>
        private static string ResolveServerPath()
        {
            string configured = Environment.GetEnvironmentVariable("CPPL_LSP_PATH");
            if (!string.IsNullOrEmpty(configured))
            {
                return configured;
            }

            string root = Directory.GetCurrentDirectory();
            string built = Path.Combine(root, "build", "dev", "bin", "cppl-lsp.exe");
            if (File.Exists(built))
            {
                return built;
            }

            return "cppl-lsp.exe";
        }

        public async Task<Connection> ActivateAsync(CancellationToken token)
        {
            await Task.Yield();

            var info = new ProcessStartInfo
            {
                FileName = ResolveServerPath(),
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };

            var process = new Process { StartInfo = info };
            if (!process.Start())
            {
                return null;
            }

            return new Connection(process.StandardOutput.BaseStream, process.StandardInput.BaseStream);
        }

        public async Task OnLoadedAsync()
        {
            AsyncEventHandler<EventArgs> handler = StartAsync;
            if (handler != null)
            {
                await handler.InvokeAsync(this, EventArgs.Empty);
            }
        }

        public Task OnServerInitializedAsync() => Task.CompletedTask;

        public Task<InitializationFailureContext> OnServerInitializeFailedAsync(ILanguageClientInitializationInfo info)
        {
            return Task.FromResult(new InitializationFailureContext
            {
                FailureMessage =
                    "cppl-lsp failed to start. Build it with 'make build', or set the CPPL_LSP_PATH " +
                    "environment variable to an existing cppl-lsp executable.",
            });
        }
    }
}
