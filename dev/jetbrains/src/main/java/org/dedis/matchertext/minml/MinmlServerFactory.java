package org.dedis.matchertext.minml;

import com.intellij.execution.configurations.GeneralCommandLine;
import com.intellij.ide.plugins.IdeaPluginDescriptor;
import com.intellij.ide.plugins.PluginManagerCore;
import com.intellij.openapi.extensions.PluginId;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.SystemInfo;
import com.redhat.devtools.lsp4ij.LanguageServerFactory;
import com.redhat.devtools.lsp4ij.server.OSProcessStreamConnectionProvider;
import com.redhat.devtools.lsp4ij.server.StreamConnectionProvider;
import java.nio.file.Files;
import java.nio.file.Path;
import org.jetbrains.annotations.NotNull;

public final class MinmlServerFactory implements LanguageServerFactory {
    @Override
    public @NotNull StreamConnectionProvider createConnectionProvider(@NotNull Project project) {
        return new OSProcessStreamConnectionProvider(new GeneralCommandLine(serverPath()));
    }

    /** $MINML_LSP_PATH, then the binary bundled in the plugin, then minml-lsp on PATH. */
    private static String serverPath() {
        String env = System.getenv("MINML_LSP_PATH");
        if (env != null && !env.isEmpty()) {
            return env;
        }
        String name = SystemInfo.isWindows ? "minml-lsp.exe" : "minml-lsp";
        IdeaPluginDescriptor plugin = PluginManagerCore.getPlugin(PluginId.getId("org.dedis.matchertext.minml"));
        if (plugin != null) {
            Path bundled = plugin.getPluginPath().resolve("bin").resolve(name);
            // Plugin installation does not always keep the executable bit.
            if (Files.isRegularFile(bundled) && (Files.isExecutable(bundled) || bundled.toFile().setExecutable(true))) {
                return bundled.toString();
            }
        }
        return name;
    }
}
