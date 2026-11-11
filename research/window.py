"""The enrichment window of research/neuroslop_enrichment_and_refining.md, in
numbers: the model's formula next to a simulation of the same model.

    python research/window.py

A candidate is correct with probability p. The test passes every correct one,
and a wrong one with probability e. A wrong one that passes is caught by the
hidden test with probability h. One that is not caught beats every correct
answer on the criterion (it is smaller, or faster) with probability d: an
exploit. The winner after n candidates is correct when there is a correct one
and no exploit:

    W(n) = (1 - x)^n - (1 - x - p)^n,    x = (1 - p) e (1 - h) d
"""
import math
import random


def window(p, e, h=0.0, d=1.0):
    """x, the n where W peaks (None: it never falls), and W as a function"""
    x = (1 - p) * e * (1 - h) * d
    w = lambda n: (1 - x) ** n - (1 - x - p) ** n
    if x == 0:
        return x, None, w
    peak = math.log(math.log(1 - x - p) / math.log(1 - x)) / math.log((1 - x) / (1 - x - p))
    return x, peak, w


def simulate(p, e, h, d, n, runs, rng):
    """the share of runs whose winner after n candidates is correct"""
    good = 0
    for _ in range(runs):
        correct = exploit = False
        for _ in range(n):
            if rng.random() < p:
                correct = True
            elif rng.random() < e and rng.random() >= h and rng.random() < d:
                exploit = True
        good += correct and not exploit
    return good / runs


def main():
    rng = random.Random(1917)
    ns = [1, 2, 4, 7, 12, 20, 50, 100]
    cases = [
        ("a weak test, no hidden test", 0.3, 0.05, 0.0, 1.0),
        ("the same, a hidden test that catches 9 in 10", 0.3, 0.05, 0.9, 1.0),
        ("a strict test", 0.3, 0.005, 0.0, 1.0),
        ("a weak test, a hard task", 0.05, 0.05, 0.0, 1.0),
        ("a test nothing wrong gets past", 0.3, 0.0, 0.0, 1.0),
    ]
    for label, p, e, h, d in cases:
        x, peak, w = window(p, e, h, d)
        where = "never falls" if peak is None else "peaks at n = %.1f" % peak
        print("%s: p = %g, e = %g, h = %g -> x = %.4f, %s" % (label, p, e, h, x, where))
        print("  n      " + " ".join("%6d" % n for n in ns))
        print("  W(n)   " + " ".join("%6.3f" % w(n) for n in ns))
        print("  sim    " + " ".join("%6.3f" % simulate(p, e, h, d, n, 20000, rng) for n in ns))
        print()


if __name__ == "__main__":
    main()
