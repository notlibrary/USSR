# Changelog

## Unreleased
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
