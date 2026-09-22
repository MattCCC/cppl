package dev.cppl

import com.intellij.openapi.fileTypes.LanguageFileType
import com.intellij.lang.Language
import javax.swing.Icon

internal object CpplLanguage : Language("C++L")

/**
 * Registers *.cppl as its own file type. Highlighting and diagnostics come
 * from cppl-lsp over LSP rather than from a plugin-side parser, so no
 * ParserDefinition is contributed here.
 */
internal object CpplFileType : LanguageFileType(CpplLanguage) {
    override fun getName(): String = "C++L"

    override fun getDescription(): String = "C++L source file"

    override fun getDefaultExtension(): String = "cppl"

    override fun getIcon(): Icon? = null
}
