# Changelog

## Unreleased
- Runs in Moscow on `/bin/ussr`: HTTPS to the models through its OpenSSL, and
  with no shell each test runs as a program and its arguments (`--direct`, the
  default where there is no `/bin/sh`), its output to the log by
  `USSR_EXTERNAL_OUTPUT`.
- From research/refine_and_enrich.md: `--holdout`, a hidden test no model sees;
  `--patience`, `--target` and `--budget` to stop; the handoff carries the
  requirements so far and the known defects of other answers; and enrich refines
  again when it stalls on a failing best. The docs say enrichment is not voting.
- Refining as research/README.md defines it: `--task` and `--tasks FILE` add
  sub-tasks to the prompt, and `--refine N` refines in rounds, each drafting code
  from the prompt, testing it, and adding what the draft shows is missing, until
  a round finds nothing new. `tasks.md` lists them.
- Enrichment: `--score COMMAND`, a "what is better" suite whose last printed number
  ranks answers before size or speed, and `--method enrich`, the chain that stops
  once a cycle finds nothing better. The report has a score column.
- The record: every run adds to `~/.lenin/history.tsv`, and `--stats` reports pass
  rates and wins by model, and how often later cycles win or a baseline is beaten.

## 0.1.0
- First version: refines a prompt, gives it to several models for a number of
  cycles, measures each answer with a test (size, speed) and extracts the best;
  a blind vote of the models without a test; a baseline to compare with; a report.
