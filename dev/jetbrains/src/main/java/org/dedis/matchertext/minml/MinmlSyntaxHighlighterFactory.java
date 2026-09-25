package org.dedis.matchertext.minml;

import com.intellij.lexer.Lexer;
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors;
import com.intellij.openapi.editor.colors.TextAttributesKey;
import com.intellij.openapi.fileTypes.SyntaxHighlighter;
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase;
import com.intellij.openapi.fileTypes.SyntaxHighlighterFactory;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.psi.tree.IElementType;
import org.jetbrains.annotations.NotNull;

/**
 * Colors matchers and gives brace matching its tokens.
 * The language server colors everything else; it sends no bracket tokens.
 */
public final class MinmlSyntaxHighlighterFactory extends SyntaxHighlighterFactory {
    private static final TextAttributesKey[] BRACKETS = {DefaultLanguageHighlighterColors.BRACKETS};
    private static final TextAttributesKey[] BRACES = {DefaultLanguageHighlighterColors.BRACES};
    private static final TextAttributesKey[] PARENTHESES = {DefaultLanguageHighlighterColors.PARENTHESES};

    private static final SyntaxHighlighter HIGHLIGHTER = new SyntaxHighlighterBase() {
        @Override
        public @NotNull Lexer getHighlightingLexer() {
            return new MinmlLexer(true);
        }

        @Override
        public TextAttributesKey @NotNull [] getTokenHighlights(IElementType tokenType) {
            if (tokenType == MinmlLexer.LBRACKET || tokenType == MinmlLexer.RBRACKET) {
                return BRACKETS;
            }
            if (tokenType == MinmlLexer.LBRACE || tokenType == MinmlLexer.RBRACE) {
                return BRACES;
            }
            if (tokenType == MinmlLexer.LPAREN || tokenType == MinmlLexer.RPAREN) {
                return PARENTHESES;
            }
            return TextAttributesKey.EMPTY_ARRAY;
        }
    };

    @Override
    public @NotNull SyntaxHighlighter getSyntaxHighlighter(Project project, VirtualFile virtualFile) {
        return HIGHLIGHTER;
    }
}
