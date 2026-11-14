# Lenin — refinement ideas

**Today:** an agent that refines a prompt and then spins a "centrifuge"
of AI models from different vendors over cycles. The methods are
`rounds`, `chain`, `funnel` and `enrich`. Each answer is measured by
`--test` (pass/fail), size, speed and a `--score` suite. Options include
a `--holdout` hidden test, `--baseline`, `--patience`, `--target` and
`--budget`. `--refine N` refines in rounds and `--task` adds sub-tasks.
A model vote is used when there is no test. Each run writes
`report.md`, appends to `~/.lenin/history.tsv`, and `--stats` reports on
the history. It runs in Moscow with no shell.

## Centrifuge and search
- Parallel model calls through async tasks, where today they run one
  after another
- A tournament or beam search: keep the top K answers per cycle, not
  only the single best
- Crossover: give a model two passing answers and ask it to merge their
  strengths
- Mutation prompts: "make it shorter", "make it faster", "remove
  dependencies", chosen by the criterion
- Temperature or seed schedules per cycle (wide early, narrow late)
- Adaptive model choice: pick models by their past win rate from
  `history.tsv` (a bandit)
- Diversity pressure: penalise answers too similar to ones already seen
  (token diff)
- Early abort of a slow test with a timeout per run

## Measuring
- More criteria: memory peak, cyclomatic complexity, lint warnings,
  dependency count, readability score from a model judge
- Weighted multi-criteria ranking (`--criteria size:0.3,speed:0.7`) and a
  Pareto front in the report
- Benchmark statistics: several runs, median and spread, not one figure
- Coverage measured by the test suite, so answers that skip code paths
  are flagged
- Mutation testing of the test suite itself, to find weak tests that
  everything passes
- Sandboxing: run tests under a scratch `HOME`, with no network, CPU and
  memory limits, and a container where one exists

## Multi-file and real projects
- Answers made of several files (a whole fenced tree), not only the
  first block
- Patch mode: give the models an existing repository file and take back
  a unified diff, applied and tested
- A `--repo DIR` mode: run the project's own test command against each
  candidate in a worktree
- Language presets (`--lang python|c|ussr`) setting `--ext`, `--test`
  and the system prompt

## Refining
- Clarifying questions asked to the human before the run
  (`--interactive`)
- Generate tests from the refined prompt, then let the human approve
  them as `--holdout`
- Track requirement coverage: which sub-task each test checks
- A prompt library with versioning: reuse refined prompts across runs

## Cost and providers
- A token and price accounting per model and per run, printed in the
  report, with `--budget` in money as well as calls
- Prompt caching for the shared prefix of every cycle
- Local models (OpenAI-compatible servers, llama.cpp) next to paid ones
- Retries with backoff on 429/5xx, and failover to another model
- Response streaming with a live progress line

## Reports and record
- An HTML report with diffs between cycles and charts of score per
  cycle (through Ukraine or a static file)
- Explain-the-win: a short model-written summary of why the winner beat
  the runner-up
- Export of the history to KGB facts (model, task, wins)
- Reproducible runs: record seeds, model versions, prompts; `--replay
  RUN`

## Integration
- A USSR REPL skill: the AGENTS.md "AI and history based autocomplete
  agent" could reuse Lenin's model plumbing
- Run Lenin on Lenin: a self-improvement preset with its own test suite
- A Novgorod board bot that posts run reports (opt-in)

## Tests
- A fake-provider mode with canned replies (tests/ai/*.json already
  exist) covering every method and stop rule
- Property tests for ranking: a failing answer never wins, a holdout
  never leaks

## Workflow
- Resume: continue a stopped run from `--dir` (answers and measurements
  already on disk), not from zero
- A dry run that prints the plan: models, calls, the most it can cost,
  and the prompts, without calling anyone
- A config file (`lenin.conf`) for the options, so a task is one short
  command; today everything is flags
- A watch mode for the human: a live table of answers, scores and the
  current best in the terminal
- Human in the loop: pause after a cycle, show the best, take a note
  that goes into the next handoff
- Several tasks in one run (`--tasks` as a batch of independent
  problems) with a combined report
- Keep every answer's conversation, so a model can be asked a follow-up
  about its own answer

## Models and prompts
- Roles: separate models for refining, drafting, criticising and
  judging, set in `models.conf`
- Reasoning or effort settings and max-tokens per model in
  `models.conf`
- Tool use: let a model run the test itself in a sandbox and iterate
  before it answers
- Structured output: ask for a JSON envelope (files, notes,
  assumptions) in place of "the first fenced block"
- Prompt-injection guard: text from one model's answer shown to another
  is fenced and marked as data
- A context budget: trim the handoff (best answer, defects) to fit a
  model's window, which `--window` hints at today
- Model ensembles for the vote: weighted by history, with ties broken
  by the test, never by the author

## Analysis
- Significance: say when a "better" answer is within the noise of the
  measurement
- Curves per method (rounds, chain, funnel, enrich) on the same task,
  to choose a method from data
- Detect answers copied from the baseline or from each other
- A leak estimate for `--score` suites, as `--stats` gives for tests
