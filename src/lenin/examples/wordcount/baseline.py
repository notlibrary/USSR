# The classic solution, written by hand, to measure candidates against.
import collections
import re
import sys

text = sys.stdin.buffer.read().decode("utf-8").lower()
counts = collections.Counter(re.findall(r"[^\W\d_]+", text))
for word, count in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:3]:
    print(word, count)
