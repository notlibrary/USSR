# Lenin

Lenin is the brain center of the USSR: an agent that enriches and refines
neuroslop code. USSR is made by prompting an AI and refining the result with
the next AI "centrifuge". Lenin is that process as a program.

```bash
ussr src/lenin/lenin.su --models models.conf --dir out --cycles 3 \
     --criteria size --test "python test.py {file}" --ext .py "what you want"
```

Lenin does the two operations [research/README.md](../../research/README.md)
defines: **refining**, on the prompt, and **enrichment**, on the answers.

## What it does

1. **Refines the prompt.** The first model turns your request into a precise
   prompt: language, inputs, outputs, limits, how to tell it works. Assumptions
   it makes are listed as assumptions. It goes to `out/refined.md`, so read it.
   `--no-refine` sends yours as it is. With `--task` and `--refine N` it refines
   further: see [Refining](#refining).
2. **Spins the centrifuge.** Every model answers. Each sees the best answer so
   far and how it measured, or what its test said when it failed, and is asked
   for a better one. `--method` says how often that best changes: see below.
3. **Measures.** Each answer is one file, written to `out/`. `--test` runs on it
   and exits 0 when it works. Size is its bytes. Speed is how many times the
   test runs a second over `--window` seconds (3 by default), start-up included:
   the clock has whole seconds, so keep the window at 3 or more.
4. **Extracts the best.** An answer that fails its test cannot win. Among the
   rest the criterion decides, then the other one. `out/best<ext>` is the winner
   and `out/report.md` has every answer with its numbers.

`--baseline FILE` measures a solution you already have, a classic one, the same
way, and the report says whether anything beat it.

## Refining

Refining is a prompt operation. Prompt A, "make a text editor", and sub-task A,
"it supports bold text", make prompt B, "make a text editor that supports bold
text". It is repeated until the prompt reflects what you really want. Sub-tasks
come two ways:

- **Yours**: `--task "it supports bold text"`, as many as you like, or
  `--tasks FILE` with one per line. They go into the prompt before the first
  model refines it; with `--no-refine` they go after it as they are, under "It
  must also:".
- **Found**: `--refine N` refines in N rounds. Round 1 is the refining above.
  Each round after it has the first model write a draft from the prompt, runs
  your test on the draft, and asks the model what the draft shows the prompt
  leaves out: what the code got wrong or left out because the prompt did not say
  it. Those sub-tasks go into the prompt, and the next round drafts again.
  Refining stops when a round finds nothing new.

```bash
ussr src/lenin/lenin.su --models models.conf --refine 3 \
     --task "it supports bold text" --test "python test.py {file}" --ext .py \
     "make a text editor"
```

`out/tasks.md` lists every sub-task and where it came from, "(yours)" or
"(round 2)". Each round's prompt is `out/refine2.md`, `out/refine3.md` and so on,
with its draft next to it. Drafts only show what is missing from the prompt:
they do not compete with the centrifuge's answers. A round costs two calls.

What it does not do: a model decides what the draft leaves out, and it can be
wrong, or add what you never asked for. It is told not to. Read `tasks.md`.

## Enrichment

Enrichment chooses the better answer from several models, each used more than
once, with a test suite that says what is better. Writing such a suite takes less
intelligence than solving the problem. The current best goes to the next model, or
back to the same one, until no better answer comes that the tests can detect.

```bash
ussr src/lenin/lenin.su --models models.conf --method enrich --cycles 10 \
     --score "python suite.py {file}" --ext .py "what you want"
```

- `--method enrich` is the chain below. It stops at the first cycle that finds
  nothing better than the cycles before it, so `--cycles` is the most it does,
  not what it does. It needs `--test` or `--score`: without one nothing can be
  better.
- `--score COMMAND` is the "what is better" suite. It runs on every answer and
  prints, as the last number of its output, how many of its checks pass:
  `9 checks, passed 7`. More passing ranks first, before size or speed, so an
  answer that passes 7 of 9 beats one that passes 3, and the next model is
  shown the better one and what its suite said. Without `--test`, an answer
  passes when the suite exits 0, that is when all of it passes. With `--test`,
  the test decides what passes and the suite ranks.

With one model, enrich is that model improving its own answer, cycle after
cycle. It stops where its suite stops telling answers apart: an answer can be
better in ways the suite does not check, and Lenin cannot see those.

## Methods

`--method rounds`, the default: every model in a cycle gets the same prompt, with
the best from the cycles before it. They work independently, so you get as many
different ideas as there are models, and the first cycle spreads the widest.

`--method chain`: one centrifuge after another. Each is given the current best, what
the model just before it did included, so each refines the last. With two models and
two cycles that is A, B, A, B, and every step sees everything before it.

```bash
ussr src/lenin/lenin.su --models models.conf --method chain --cycles 2 \
     --test "python test.py {file}" --ext .py "what you want"
```

Which does better is a question for your task. A chain builds on what works; it can
also settle on the first thing that passes, since all after it start from there. Both
cost the same: models times cycles. A model that fails is passed over and the next
gets the same best.

With no `--test` nothing measures an answer, so "best" could only mean "smallest", and
a stub is small. There the chain passes on the latest answer, called "the answer so
far" and not "the best", and the models still vote at the end.

`--method enrich`: the chain, stopping once a cycle finds nothing better. See
[Enrichment](#enrichment).

Each model's prompt is written next to its answer, `cycle1_name.prompt.md`, so you can
read what it was really given.

## What "best" means

Best of what the models gave, by the test you wrote. Not more.

**A weak test picks the wrong winner.** Given the task of printing a sum, a test
that only looks at the exit status, and the canned answer `assert 2+3==5`, Lenin
picks that for its size: it passes and prints nothing. Lenin cannot tell. Check
outputs, on several inputs, with edge cases: see `examples/wordcount/test.py`.

**Without `--test`** nothing is run. The models vote blind on each other's
answers, and the report says that nothing there is shown to work. A vote tells
you what models like, and they like confident code. Use it for a first look.

## The record

One run says little, so every run adds a few lines to a file that outlives it:
what each model answered, how it measured and who won. It answers what a single
report cannot: which model passes most often, whether later cycles win, and how
often anything beats your baseline.

```bash
ussr src/lenin/lenin.su --stats
```

```text
# Lenin's record: 12 runs of 4 tasks

| model | answers | no answer | tested | passed | pass rate | runs won |
|---|---|---|---|---|---|---|
| claude-opus-5-5 | 36 | 0 | 36 | 31 | 86% | 7 |

| method | runs | a passing winner | won after cycle 1 | baseline beaten | baseline stands |
|---|---|---|---|---|---|
| chain | 6 | 5 | 3 | 1 | 2 |
| rounds | 6 | 6 | 2 | 0 | 3 |

Refining: a later cycle won 5 of 11 runs with a passing winner (45%).
Classic code: the best answer beat the baseline in 1 of 6 runs that had one (16%).
```

"Runs won" counts only answers that passed a test: a vote shows nothing works.
The numbers count what happened, on your tasks and tests. They are as good as those
tests, and under 20 measured runs Lenin says they are too few to conclude anything.

The file is `~/.lenin/history.tsv`, or `LENIN_HISTORY`, or `--history FILE`;
`--no-history` keeps a run out of it. It is plain tab-separated text with no keys
and no answers, but it does hold model names and the first 60 bytes of each prompt.

## Models

`--models` is a file of lines `provider model KEY_ENV [url]`:

```text
# three developers
anthropic claude-opus-5-5  ANTHROPIC_API_KEY
openai    gpt-x            OPENAI_API_KEY
# a local one, no key
openai    llama3.2         -  http://localhost:11434/v1/chat/completions
```

The third word names an environment variable, so keys stay out of the file and
off the command line, out of `ps` and your shell history. Lenin writes keys to
no file. `-` means no key: Lenin then sends the word `none`, not your
`OPENAI_API_KEY`, to that URL. A model whose key is not set is skipped, and a
model that fails ends only its own turn.

Models come through `prompt(_):`: Anthropic or anything OpenAI-compatible.

## Before you run it

- **It costs money.** A run is one call to refine, two for each more round of
  `--refine`, models times cycles, and with no test or suite one vote per model. Lenin prints the number first and does not ask.
- **`--test` and `--score` run code nobody has read, with your rights.** So do
  the drafts of `--refine`. A model can write
  anything into it. Use a container, a virtual machine or a throwaway user.
- **A test that hangs stops Lenin**: USSR has no way to kill a command it
  started. Put a limit in the test: `timeout 10 python {file}`.
- It handles **one file per answer**, the first fenced block of the reply.
- Models are asked **one after another**, not at once.
- It runs on a hosted `ussr`. On Moscow, `ussr` has no TLS or curl yet, so it
  reaches no provider.

## Options

| Option | Meaning | Default |
|--------|---------|---------|
| `--models FILE` | the centrifuge | required |
| `--dir DIR` | where everything goes | `lenin-out` |
| `--cycles N` | generation cycles | 2 |
| `--criteria speed\|size` | what decides among those that pass and score the same; speed needs `--test` or `--score` | `size` |
| `--method rounds\|chain\|enrich` | when the best is passed on: after each cycle, or each model; enrich stops when nothing better comes | `rounds` |
| `--test COMMAND` | exits 0 when `{file}` works; `{file}` goes at the end if not given | none: a vote |
| `--score COMMAND` | a suite that prints last how many of its checks `{file}` passes; more ranks first | none |
| `--ext .EXT` | candidates' file extension | `.txt` |
| `--baseline FILE` | a solution to compare with | none |
| `--window SECONDS` | how long to time speed | 3 |
| `--refine N` | refining rounds; after the first, each drafts and adds what it shows is missing | 1 |
| `--no-refine` | send the prompt as it is (`--refine 0`) | refines |
| `--task TEXT` | a sub-task for the prompt; give it more than once | none |
| `--tasks FILE` | sub-tasks, one a line | none |
| `--prompt-file FILE` | the prompt from a file | the arguments |
| `--history FILE` | the record every run adds to | `LENIN_HISTORY`, else `~/.lenin/history.tsv` |
| `--no-history` | keep this run out of the record | records it |
| `--stats` | print what the record says, and run nothing | |

## A benchmark

`examples/wordcount` is a task with a strict test and a classic baseline, for
finding out what a set of models does against code written by hand:

```bash
ussr src/lenin/lenin.su --models models.conf --dir out --cycles 3 --criteria speed \
     --ext .py --baseline src/lenin/examples/wordcount/baseline.py \
     --prompt-file src/lenin/examples/wordcount/prompt.txt \
     --test "python src/lenin/examples/wordcount/test.py {file}"
```

One task says little. Whether enriched neuroslop beats classical software is a
claim that takes many such tasks to back up, and none have been run yet.

## Tests

`tests/lenin_test.su`: the helpers, the ranking and whole runs, with canned model
replies and no network, in `tests/run_all.sh`.
