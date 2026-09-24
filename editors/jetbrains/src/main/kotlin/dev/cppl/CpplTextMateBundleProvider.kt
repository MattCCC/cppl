package dev.cppl

import com.intellij.ide.plugins.PluginManagerCore
import com.intellij.openapi.extensions.PluginId
import java.nio.file.Files
import org.jetbrains.plugins.textmate.api.TextMateBundleProvider
import org.jetbrains.plugins.textmate.api.TextMateBundleProvider.PluginBundle

/**
 * Colors *.cppl files with the shared C++L TextMate grammar
 * (editors/shared/cppl.tmLanguage.json), which the build copies into the
 * plugin's `bundles/cppl` directory beside a manifest naming the extension.
 * The IDE's TextMate support then owns the file type; cppl-lsp is started for
 * the same files by their extension.
 */
internal class CpplTextMateBundleProvider : TextMateBundleProvider {
    override fun getBundles(): List<PluginBundle> {
        val plugin = PluginManagerCore.getPlugin(PluginId.getId("dev.cppl.jetbrains")) ?: return emptyList()
        val bundle = plugin.pluginPath.resolve("bundles").resolve("cppl")
        return if (Files.isDirectory(bundle)) listOf(PluginBundle("C++L", bundle)) else emptyList()
    }
}
