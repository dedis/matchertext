package org.dedis.matchertext.minml;

import com.intellij.lang.Language;

public final class MinmlLanguage extends Language {
    public static final MinmlLanguage INSTANCE = new MinmlLanguage();

    private MinmlLanguage() {
        super("MinML");
    }
}
