"""A test for wordcount candidates: python test.py candidate.py exits 0 when it
gives the right output on every input. It checks outputs, on inputs chosen to
catch shortcuts, so a candidate cannot pass by doing less; the last one is big,
which is what speed is timed on. Each run has a time limit: a candidate that
hangs fails."""
import random
import subprocess
import sys

CASES = [
    ("", ""),
    ("one", "one 1\n"),
    ("a b", "a 1\nb 1\n"),
    ("b a b a c d", "a 2\nb 2\nc 1\n"),
    ("The the THE cat, cat! dog", "the 3\ncat 2\ndog 1\n"),
    ("x1y 22 x_y", "x 2\ny 2\n"),
    ("Да да ДА нет\nнет\tкот", "да 3\nнет 2\nкот 1\n"),
    ("   \n\t  ", ""),
    ("z y x w", "w 1\nx 1\ny 1\n"),
]


def big():
    rng = random.Random(1917)
    words = ["w" + chr(97 + i) + chr(97 + j) for i in range(26) for j in range(26)]
    counts = {}
    picks = []
    for _ in range(200000):
        w = words[int(rng.random() ** 3 * len(words))]
        picks.append(w.upper() if rng.random() < 0.5 else w)
        counts[w] = counts.get(w, 0) + 1
    top = sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:3]
    return " ".join(picks), "".join("%s %d\n" % kv for kv in top)


def run(path, text):
    done = subprocess.run([sys.executable, path], input=text.encode("utf-8"),
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    return done.returncode, done.stdout.decode("utf-8", "replace").replace("\r\n", "\n"), done.stderr


def main():
    path = sys.argv[1]
    for text, want in CASES + [big()]:
        try:
            code, got, err = run(path, text)
        except subprocess.TimeoutExpired:
            print("timed out on input of %d characters" % len(text))
            return 1
        if code != 0:
            print("exit status %d on %r: %s" % (code, text[:60], err[-400:]))
            return 1
        if got != want:
            print("on %r\n  wanted %r\n  got    %r" % (text[:60], want, got[:200]))
            return 1
    return 0


sys.exit(main())
