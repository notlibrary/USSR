# Changelog

## Unreleased
- The record: every run adds to `~/.lenin/history.tsv`, and `--stats` reports pass
  rates and wins by model, and how often later cycles win or a baseline is beaten.

## 0.1.0
- First version: refines a prompt, gives it to several models for a number of
  cycles, measures each answer with a test (size, speed) and extracts the best;
  a blind vote of the models without a test; a baseline to compare with; a report.
