package org.dedis.matchertext.minml;

import com.intellij.lang.Language;

public final class MinmlLanguage extends Language {
    private static final long serialVersionUID = 1L;

    public static final MinmlLanguage INSTANCE = new MinmlLanguage();

    private MinmlLanguage() {
        super("MinML");
    }
}
