package dev.cppl

import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspServerSupportProvider
import com.intellij.platform.lsp.api.LspServerSupportProvider.LspServerStarter
import com.intellij.platform.lsp.api.ProjectWideLspServerDescriptor
import java.io.File

private fun executableName(): String = if (SystemInfo.isWindows) "cppl-lsp.exe" else "cppl-lsp"

/**
 * Starts cppl-lsp for *.cppl files. The plugin owns no C++L semantics: it
 * locates the server and lets the platform's LSP client drive diagnostics and
 * formatting.
 */
internal class CpplLspServerDescriptor(project: Project) :
    ProjectWideLspServerDescriptor(project, "C++L") {

    override fun isSupportedFile(file: VirtualFile): Boolean = file.extension == "cppl"

    override fun createCommandLine(): com.intellij.execution.configurations.GeneralCommandLine {
        val settings = CpplSettings.getInstance()
        val executable = settings.resolveServerPath(project.basePath)
        val command = com.intellij.execution.configurations.GeneralCommandLine(executable)
        command.addParameters(settings.serverArguments())
        project.basePath?.let { command.withWorkDirectory(File(it)) }
        return command
    }

    // The server's canonical formatter is the only C++L formatter, so route
    // IDE format actions to it rather than to the C++ engine.
    override val lspFormattingSupport: com.intellij.platform.lsp.api.customization.LspFormattingSupport
        get() = object : com.intellij.platform.lsp.api.customization.LspFormattingSupport() {
            override fun shouldFormatThisFileExclusivelyByServer(
                file: VirtualFile,
                ideCanFormatThisFileItself: Boolean,
                serverExplicitlyWantsToFormatThisFile: Boolean,
            ): Boolean = file.extension == "cppl"
        }
}

internal class CpplLspSupportProvider : LspServerSupportProvider {
    override fun fileOpened(project: Project, file: VirtualFile, serverStarter: LspServerStarter) {
        if (file.extension == "cppl") {
            serverStarter.ensureServerStarted(CpplLspServerDescriptor(project))
        }
    }
}
