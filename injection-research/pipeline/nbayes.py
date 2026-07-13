"""Character-trigram multinomial naive Bayes, stdlib only."""
import math
from collections import Counter


def trigrams(text):
    t = " ".join(text.lower().split())
    return [t[i:i + 3] for i in range(len(t) - 2)]


class TrigramNB:
    def fit(self, texts, labels):
        self.classes = sorted(set(labels))
        docs = Counter(labels)
        feat = {c: Counter() for c in self.classes}
        for text, y in zip(texts, labels):
            feat[y].update(trigrams(text))
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
        grams = [g for g in trigrams(text) if g in self.vocab]
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
