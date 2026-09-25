package org.dedis.matchertext.minml;

import com.intellij.lang.Commenter;

/** MinML comments are -[...]. */
public final class MinmlCommenter implements Commenter {
    @Override
    public String getLineCommentPrefix() {
        return null;
    }

    @Override
    public String getBlockCommentPrefix() {
        return "-[";
    }

    @Override
    public String getBlockCommentSuffix() {
        return "]";
    }

    @Override
    public String getCommentedBlockCommentPrefix() {
        return null;
    }

    @Override
    public String getCommentedBlockCommentSuffix() {
        return null;
    }
}
