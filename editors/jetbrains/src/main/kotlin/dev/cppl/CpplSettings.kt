package dev.cppl

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.PersistentStateComponent
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.State
import com.intellij.openapi.components.Storage
import com.intellij.openapi.util.SystemInfo
import java.nio.file.Files
import java.nio.file.Path

/** Where cppl-lsp lives and how it should invoke Clang. */
@Service(Service.Level.APP)
@State(name = "CpplSettings", storages = [Storage("cppl.xml")])
internal class CpplSettings : PersistentStateComponent<CpplSettings.State> {

    internal class State {
        @JvmField var serverPath: String = ""
        @JvmField var clangPath: String = ""
        @JvmField var clangArguments: MutableList<String> = mutableListOf()
    }

    private var state = State()

    override fun getState(): State = state

    override fun loadState(state: State) {
        this.state = state
    }

    /**
     * An explicit setting wins; otherwise prefer the project's own build so a
     * contributor's edits take effect without configuration, and fall back to
     * the bare name so an installed cppl-lsp is found on PATH.
     */
    fun resolveServerPath(projectBasePath: String?): String {
        if (state.serverPath.isNotEmpty()) {
            return state.serverPath
        }
        val executable = if (SystemInfo.isWindows) "cppl-lsp.exe" else "cppl-lsp"
        if (projectBasePath != null) {
            val built = Path.of(projectBasePath, "build", "dev", "bin", executable)
            if (Files.isExecutable(built)) {
                return built.toString()
            }
        }
        return executable
    }

    fun serverArguments(): List<String> {
        val arguments = mutableListOf<String>()
        if (state.clangPath.isNotEmpty()) {
            arguments += "--clang"
            arguments += state.clangPath
        }
        for (argument in state.clangArguments) {
            arguments += "--clang-arg"
            arguments += argument
        }
        return arguments
    }

    companion object {
        fun getInstance(): CpplSettings = ApplicationManager.getApplication().getService(CpplSettings::class.java)
    }
}
