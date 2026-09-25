package org.dedis.matchertext.minml;

import com.intellij.openapi.editor.XmlHighlighterColors;
import com.intellij.openapi.editor.colors.TextAttributesKey;
import com.intellij.psi.PsiFile;
import com.redhat.devtools.lsp4ij.features.semanticTokens.DefaultSemanticTokensColorsProvider;
import java.util.List;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/**
 * Colors element names ("type" tokens) as HTML tag names: LSP4IJ's color for "type"
 * looks like plain text in the default themes. The other token types keep LSP4IJ's colors.
 */
public final class MinmlSemanticTokensColorsProvider extends DefaultSemanticTokensColorsProvider {
    @Override
    public @Nullable TextAttributesKey getTextAttributesKey(@NotNull String tokenType,
                                                           @NotNull List<String> tokenModifiers,
                                                           @NotNull PsiFile file) {
        return tokenType.equals("type")
            ? XmlHighlighterColors.HTML_TAG_NAME
            : super.getTextAttributesKey(tokenType, tokenModifiers, file);
    }
}
