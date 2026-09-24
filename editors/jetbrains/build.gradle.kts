plugins {
    kotlin("jvm") version "2.0.21"
    id("org.jetbrains.intellij") version "1.17.4"
}

group = "dev.cppl"
version = providers.gradleProperty("pluginVersion").get()

repositories {
    mavenCentral()
}

kotlin {
    jvmToolchain(17)
}

intellij {
    type.set(providers.gradleProperty("platformType"))
    version.set(providers.gradleProperty("platformVersion"))
    // The bundled TextMate support, which colors *.cppl with the shared grammar.
    plugins.set(listOf("org.jetbrains.plugins.textmate"))
}

tasks {
    // The TextMate bundle ships beside the plugin's jars: its manifest from
    // src/main/bundle, and the grammar from editors/shared, the one definition
    // of C++L coloring, rather than a copy kept here.
    prepareSandbox {
        from("src/main/bundle") {
            into("${intellij.pluginName.get()}/bundles/cppl")
        }
        from("../shared/cppl.tmLanguage.json") {
            into("${intellij.pluginName.get()}/bundles/cppl/syntaxes")
        }
    }

    patchPluginXml {
        version.set(providers.gradleProperty("pluginVersion"))
        // 232 is the first build with the platform LSP API this plugin uses.
        sinceBuild.set("232")
        untilBuild.set(provider { null })
    }

    // Credentials come from the environment so CI and a local publish use the
    // same path; see editors/README.md for the required variables.
    publishPlugin {
        token.set(providers.environmentVariable("JETBRAINS_MARKETPLACE_TOKEN"))
    }

    signPlugin {
        certificateChain.set(providers.environmentVariable("JETBRAINS_CERTIFICATE_CHAIN"))
        privateKey.set(providers.environmentVariable("JETBRAINS_PRIVATE_KEY"))
        password.set(providers.environmentVariable("JETBRAINS_PRIVATE_KEY_PASSWORD"))
    }
}
