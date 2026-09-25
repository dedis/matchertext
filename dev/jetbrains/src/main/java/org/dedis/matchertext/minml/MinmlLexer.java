package org.dedis.matchertext.minml;

import com.intellij.lexer.LexerBase;
import com.intellij.psi.tree.IElementType;
import org.jetbrains.annotations.NotNull;

/**
 * Splits text into matcher tokens and the text between them, for brace matching.
 * The language server does all real parsing; the PSI lexer returns the whole file
 * as one token, so the PSI tree stays one node.
 */
final class MinmlLexer extends LexerBase {
    static final IElementType TEXT = new IElementType("TEXT", MinmlLanguage.INSTANCE);
    static final IElementType LBRACKET = new IElementType("[", MinmlLanguage.INSTANCE);
    static final IElementType RBRACKET = new IElementType("]", MinmlLanguage.INSTANCE);
    static final IElementType LBRACE = new IElementType("{", MinmlLanguage.INSTANCE);
    static final IElementType RBRACE = new IElementType("}", MinmlLanguage.INSTANCE);
    static final IElementType LPAREN = new IElementType("(", MinmlLanguage.INSTANCE);
    static final IElementType RPAREN = new IElementType(")", MinmlLanguage.INSTANCE);

    private final boolean matchers;
    private CharSequence buffer;
    private int start, end, bufferEnd;
    private IElementType type;

    MinmlLexer(boolean matchers) {
        this.matchers = matchers;
    }

    @Override
    public void start(@NotNull CharSequence buffer, int startOffset, int endOffset, int initialState) {
        this.buffer = buffer;
        this.bufferEnd = endOffset;
        this.end = startOffset;
        advance();
    }

    @Override
    public void advance() {
        start = end;
        if (start >= bufferEnd) {
            type = null;
            return;
        }
        type = matchers ? matcher(buffer.charAt(start)) : null;
        if (type != null) {
            end = start + 1;
            return;
        }
        type = TEXT;
        end = start + 1;
        while (end < bufferEnd && (!matchers || matcher(buffer.charAt(end)) == null)) {
            end++;
        }
    }

    private static IElementType matcher(char c) {
        return switch (c) {
            case '[' -> LBRACKET;
            case ']' -> RBRACKET;
            case '{' -> LBRACE;
            case '}' -> RBRACE;
            case '(' -> LPAREN;
            case ')' -> RPAREN;
            default -> null;
        };
    }

    @Override
    public int getState() {
        return 0;
    }

    @Override
    public IElementType getTokenType() {
        return type;
    }

    @Override
    public int getTokenStart() {
        return start;
    }

    @Override
    public int getTokenEnd() {
        return end;
    }

    @Override
    public @NotNull CharSequence getBufferSequence() {
        return buffer;
    }

    @Override
    public int getBufferEnd() {
        return bufferEnd;
    }
}
