import org.gradle.internal.os.OperatingSystem

plugins {
    java
    id("org.jetbrains.intellij.platform") version "2.19.0"
}

group = "org.dedis.matchertext"
version = "0.0.1"

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }
}

dependencies {
    intellijPlatform {
        intellijIdeaCommunity("2025.3.6.1") { useInstaller = false }
        plugin("com.redhat.devtools.lsp4ij:0.21.0")
    }
}

java.toolchain.languageVersion = JavaLanguageVersion.of(21)

intellijPlatform {
    pluginConfiguration.ideaVersion.sinceBuild = "252"
    buildSearchableOptions = false
}

// The plugin bundles the language server built from this repository, like the VS Code extension.
val serverName = if (OperatingSystem.current().isWindows) "minml-lsp.exe" else "minml-lsp"
val buildServer = tasks.register<Exec>("buildServer") {
    val out = layout.buildDirectory.file("server/$serverName")
    workingDir = rootDir.resolve("../..")
    commandLine("go", "build", "-o", out.get().asFile.path, "./go/markup/minml/cmd/lsp/")
    inputs.dir(rootDir.resolve("../../go"))
    outputs.file(out)
}

tasks.prepareSandbox {
    from(buildServer) { into(intellijPlatform.projectName.map { "$it/bin" }) }
}
