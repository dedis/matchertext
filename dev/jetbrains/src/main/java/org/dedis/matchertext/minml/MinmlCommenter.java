package org.dedis.matchertext.minml;

import com.intellij.codeInsight.generation.CommenterDataHolder;
import com.intellij.codeInsight.generation.SelfManagingCommenter;
import com.intellij.lang.Commenter;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.util.TextRange;
import com.intellij.psi.PsiFile;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/**
 * MinML comments are -[...]. A comment ends at the "]" that balances its "-[", which the
 * platform cannot find from the lexer: it would end the comment at the first "]" and split
 * comments that contain brackets. So this commenter finds and edits comments itself.
 * "Comment with Line Comment" wraps each line in a comment, as MinML has no line comments.
 */
public final class MinmlCommenter implements Commenter, SelfManagingCommenter<CommenterDataHolder> {
    private static final String PREFIX = "-[";
    private static final String SUFFIX = "]";

    @Override
    public String getLineCommentPrefix() {
        return null;
    }

    @Override
    public String getBlockCommentPrefix() {
        return PREFIX;
    }

    @Override
    public String getBlockCommentSuffix() {
        return SUFFIX;
    }

    @Override
    public String getCommentedBlockCommentPrefix() {
        return null;
    }

    @Override
    public String getCommentedBlockCommentSuffix() {
        return null;
    }

    @Override
    public CommenterDataHolder createLineCommentingState(int startLine, int endLine, @NotNull Document document,
                                                         @NotNull PsiFile file) {
        return EMPTY_STATE;
    }

    @Override
    public CommenterDataHolder createBlockCommentingState(int selectionStart, int selectionEnd,
                                                          @NotNull Document document, @NotNull PsiFile file) {
        return EMPTY_STATE;
    }

    @Override
    public void commentLine(int line, int offset, @NotNull Document document, @NotNull CommenterDataHolder data) {
        TextRange r = lineText(line, document);
        if (!r.isEmpty()) {
            insertBlockComment(r.getStartOffset(), r.getEndOffset(), document, data);
        }
    }

    @Override
    public void uncommentLine(int line, int offset, @NotNull Document document, @NotNull CommenterDataHolder data) {
        TextRange r = lineText(line, document);
        uncommentBlockComment(r.getStartOffset(), r.getEndOffset(), document, data);
    }

    @Override
    public boolean isLineCommented(int line, int offset, @NotNull Document document,
                                   @NotNull CommenterDataHolder data) {
        TextRange r = lineText(line, document);
        return isComment(document.getCharsSequence(), r.getStartOffset(), r.getEndOffset());
    }

    @Override
    public @Nullable String getCommentPrefix(int line, @NotNull Document document, @NotNull CommenterDataHolder data) {
        return PREFIX;
    }

    /** The selection, trimmed, if it is one comment; else the comment around an empty selection. */
    @Override
    public @Nullable TextRange getBlockCommentRange(int selectionStart, int selectionEnd, @NotNull Document document,
                                                    @NotNull CommenterDataHolder data) {
        CharSequence text = document.getCharsSequence();
        if (selectionStart < selectionEnd) {
            TextRange r = trim(text, selectionStart, selectionEnd);
            return isComment(text, r.getStartOffset(), r.getEndOffset()) ? r : null;
        }
        return enclosingComment(text, selectionStart);
    }

    @Override
    public @Nullable String getBlockCommentPrefix(int selectionStart, @NotNull Document document,
                                                  @NotNull CommenterDataHolder data) {
        return PREFIX;
    }

    @Override
    public @Nullable String getBlockCommentSuffix(int selectionEnd, @NotNull Document document,
                                                  @NotNull CommenterDataHolder data) {
        return SUFFIX;
    }

    /** Removes "-[" and "]" of the comment from startOffset to endOffset, with one space inside each. */
    @Override
    public void uncommentBlockComment(int startOffset, int endOffset, Document document, CommenterDataHolder data) {
        CharSequence text = document.getCharsSequence();
        int end = endOffset - SUFFIX.length();
        if (end > startOffset + PREFIX.length() && text.charAt(end - 1) == ' ') {
            end--;
        }
        int start = startOffset + PREFIX.length();
        if (start < end && text.charAt(start) == ' ') {
            start++;
        }
        document.deleteString(end, endOffset);
        document.deleteString(startOffset, start);
    }

    @Override
    public @NotNull TextRange insertBlockComment(int startOffset, int endOffset, Document document,
                                                 CommenterDataHolder data) {
        document.insertString(endOffset, " " + SUFFIX);
        document.insertString(startOffset, PREFIX + " ");
        return new TextRange(startOffset, endOffset + PREFIX.length() + SUFFIX.length() + 2);
    }

    /** The text of the line without its leading and trailing whitespace. */
    private static TextRange lineText(int line, Document document) {
        return trim(document.getCharsSequence(), document.getLineStartOffset(line), document.getLineEndOffset(line));
    }

    private static TextRange trim(CharSequence text, int start, int end) {
        while (start < end && Character.isWhitespace(text.charAt(start))) {
            start++;
        }
        while (end > start && Character.isWhitespace(text.charAt(end - 1))) {
            end--;
        }
        return new TextRange(start, end);
    }

    /**
     * Whether text[start, end) is one comment: "-[", text that does not close it, and "]".
     * The text between need not balance, so that commenting lines such as "p[" and
     * uncommenting them restores them.
     */
    private static boolean isComment(CharSequence text, int start, int end) {
        if (end - start < PREFIX.length() + SUFFIX.length() || text.charAt(start) != '-'
            || text.charAt(start + 1) != '[' || text.charAt(end - 1) != ']') {
            return false;
        }
        int depth = 0;
        for (int i = start + 1; i < end - 1; i++) {
            char c = text.charAt(i);
            if (c == '[' || c == '(' || c == '{') {
                depth++;
            } else if ((c == ']' || c == ')' || c == '}') && --depth == 0) {
                return false;
            }
        }
        return true;
    }

    /** The innermost comment that contains offset, found through the matchers around it. */
    private static @Nullable TextRange enclosingComment(CharSequence text, int offset) {
        for (int open = unmatchedOpener(text, offset); open >= 0; open = unmatchedOpener(text, open)) {
            if (text.charAt(open) == '[' && open > 0 && text.charAt(open - 1) == '-' && isNameStart(text, open - 1)) {
                int close = closer(text, open);
                return close < 0 ? null : new TextRange(open - 1, close + 1);
            }
        }
        return null;
    }

    /** The last opener before offset that no matcher between them closes, or -1. */
    private static int unmatchedOpener(CharSequence text, int offset) {
        int depth = 0;
        for (int i = offset - 1; i >= 0; i--) {
            char c = text.charAt(i);
            if (c == ']' || c == ')' || c == '}') {
                depth++;
            } else if (c == '[' || c == '(' || c == '{') {
                if (depth == 0) {
                    return i;
                }
                depth--;
            }
        }
        return -1;
    }

    /** The matcher that balances the opener at open, or -1. */
    private static int closer(CharSequence text, int open) {
        int depth = 0;
        for (int i = open; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c == '[' || c == '(' || c == '{') {
                depth++;
            } else if ((c == ']' || c == ')' || c == '}') && --depth == 0) {
                return i;
            }
        }
        return -1;
    }

    /** Whether a name starts at i: after a line start, whitespace, or a matcher, maybe with a space sucker "<". */
    private static boolean isNameStart(CharSequence text, int i) {
        if (i > 0 && text.charAt(i - 1) == '<') {
            i--;
        }
        return i == 0 || " \t\r\n[](){}".indexOf(text.charAt(i - 1)) >= 0;
    }
}
