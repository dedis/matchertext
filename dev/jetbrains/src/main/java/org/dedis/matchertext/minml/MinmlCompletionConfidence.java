package org.dedis.matchertext.minml;

import com.intellij.codeInsight.completion.CompletionConfidence;
import com.intellij.openapi.editor.Editor;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiFile;
import com.intellij.util.ThreeState;
import org.jetbrains.annotations.NotNull;

/**
 * MinML is mostly prose, so completion opens only after '{' (attribute names) or on request,
 * like the VS Code extension. Otherwise typing any word would query the language server.
 */
public final class MinmlCompletionConfidence extends CompletionConfidence {
    @Override
    public @NotNull ThreeState shouldSkipAutopopup(@NotNull Editor editor, @NotNull PsiElement contextElement,
                                                   @NotNull PsiFile psiFile, int offset) {
        CharSequence text = editor.getDocument().getCharsSequence();
        return offset > 0 && text.charAt(offset - 1) == '{' ? ThreeState.UNSURE : ThreeState.YES;
    }
}
