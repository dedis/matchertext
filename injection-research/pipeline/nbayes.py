"""Word unigram and bigram multinomial naive Bayes, stdlib only."""
import math
import re
from collections import Counter

WORD = re.compile(r"[a-z0-9_]+")


def features(text):
    w = WORD.findall(text.lower())
    return w + [a + " " + b for a, b in zip(w, w[1:])]


class WordNB:
    def fit(self, texts, labels):
        self.classes = sorted(set(labels))
        docs = Counter(labels)
        feat = {c: Counter() for c in self.classes}
        for text, y in zip(texts, labels):
            feat[y].update(features(text))
        self.vocab = set().union(*feat.values())
        n = len(labels)
        self.log_prior = {c: math.log(docs[c] / n) for c in self.classes}
        self.log_lik, self.log_default = {}, {}
        for c in self.classes:
            denom = sum(feat[c].values()) + len(self.vocab)
            self.log_lik[c] = {g: math.log((k + 1) / denom) for g, k in feat[c].items()}
            self.log_default[c] = math.log(1 / denom)
        return self

    def predict(self, text):
        """Returns (label, margin) where margin is the log-odds gap to the runner-up."""
        grams = [g for g in features(text) if g in self.vocab]
        best = second = -math.inf
        label = self.classes[0]
        for c in self.classes:
            lik, dflt = self.log_lik[c], self.log_default[c]
            s = self.log_prior[c] + sum(lik.get(g, dflt) for g in grams)
            if s > best:
                best, second, label = s, best, c
            elif s > second:
                second = s
        return label, best - second
