package org.dedis.matchertext.minml;

import com.intellij.icons.AllIcons;
import com.intellij.openapi.fileTypes.LanguageFileType;
import javax.swing.Icon;
import org.jetbrains.annotations.NotNull;

public final class MinmlFileType extends LanguageFileType {
    public static final MinmlFileType INSTANCE = new MinmlFileType();

    private MinmlFileType() {
        super(MinmlLanguage.INSTANCE);
    }

    @Override
    public @NotNull String getName() {
        return "MinML";
    }

    @Override
    public @NotNull String getDescription() {
        return "MinML markup";
    }

    @Override
    public @NotNull String getDefaultExtension() {
        return "minml";
    }

    @Override
    public Icon getIcon() {
        return AllIcons.FileTypes.Text;
    }
}
