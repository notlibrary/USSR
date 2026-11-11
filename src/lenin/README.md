# Lenin

Lenin is the brain center of the USSR: an agent that refines and enriches
neuroslop code. USSR itself is made by prompting an AI with a prompt refined
beforehand, then enriching what comes back with the next AI of the "centrifuge".
Lenin is that process as a program.

```bash
ussr src/lenin/lenin.su --models models.conf --dir out --cycles 3 \
     --test "python test.py {file}" --ext .py "what you want"
```

It does the two operations that [research/README.md](../../research/README.md)
defines and [research/refine_and_enrich.md](../../research/refine_and_enrich.md)
sets out at length.
[research/neuroslop_enrichment_and_refining.md](../../research/neuroslop_enrichment_and_refining.md)
has the model behind Lenin's choices, the enrichment window among them:

- **Refining** works on the prompt: it is made more precise until it says what
  you really want.
- **Enrichment** works on the answers: several models, used more than once, each
  given the best answer so far, with a test that says which answer is better.

Best is always best by your test. Lenin measures; it does not judge.

## A run, step by step

1. **Refine the prompt.** The first model turns your request into a precise
   prompt: language, inputs, outputs, limits, and how to tell it works. Any
   assumption it makes is listed as one. Your sub-tasks (`--task`) go in, and
   with `--refine N` it goes round more times: see [Refining](#refining).
   `--no-refine` sends your prompt as it is.
2. **Spin the centrifuge.** Every model answers, for `--cycles` cycles. Each is
   shown the best answer so far and how it measured, and asked for a better
   one. `--method` says when that best is passed on: see [Methods](#methods).
3. **Measure each answer.** The first fenced block of a reply is one file in the
   output directory. `--test` runs on it and exits 0 when it works. Size is its
   bytes; speed is how many times a second the test runs.
4. **Extract the best.** An answer that fails its test cannot win. Among the
   rest the suite score decides first (`--score`), then your criterion
   (`--criteria size` or `speed`), then the other one. The winner is copied to
   `best<ext>`, and `report.md` lists every answer with its numbers.

`--baseline FILE` measures a solution you already have, a classic one written
by hand, the same way, and the report says whether anything beat it.

Lenin exits 0 when there is a winner that passes its test (with no test, any
winner), 1 when nothing passes or nobody answered, and 2 for a bad command line.

## Methods

`--method` decides who sees what, and so how answers build on each other.

| | `rounds` (default) | `chain` | `funnel` | `enrich` |
|---|---|---|---|---|
| Order, 2 models A and B, 2 cycles | A, B; A, B | A, B, A, B | A, B; then A, B | A, B, A, B, ... |
| What a model is shown | the best from the cycles **before** this one | the best so far, the model just before it included | cycle 1: nothing, as `rounds`; after it: as `chain` | the same as `chain` |
| When it stops | after `--cycles` | after `--cycles` | after `--cycles` | when `--patience` cycles in a row find nothing better, or after `--cycles` |
| When the best still fails | goes on | goes on | goes on | refines the prompt once more, then goes on |
| Needs | nothing | nothing | nothing | `--test` or `--score` |

**`rounds`**: every model in a cycle gets the same prompt, so they work
independently. You get as many different ideas as you have models, and the first
cycle spreads the widest.

**`chain`**: one model after another. Each gets the current best, so each
improves on the last. A chain builds on what works, but it can also settle on
the first thing that passes, since everything after starts from there.

**`funnel`**: wide first, then deep. In cycle 1 every model answers on its
own, as in `rounds`, so you get the spread of ideas while nothing passes yet.
From cycle 2 it is a `chain` that starts from the best of them. A chain from
the first call spends several models on variations of whoever answered first;
the funnel does not.

**`enrich`**: the chain, run until it stops improving. `--cycles` is the most it
does, not what it does. It stops after `--patience` cycles in a row (1 by
default) bring nothing better by the test. If at that point the best answer
still fails, Lenin first goes [back to refining](#back-to-refining): asking more
models about an unclear task does not make it clear.

```bash
ussr src/lenin/lenin.su --models models.conf --method rounds --cycles 2 \
     --test "python test.py {file}" --ext .py "what you want"
ussr src/lenin/lenin.su --models models.conf --method chain --cycles 2 \
     --test "python test.py {file}" --ext .py "what you want"
ussr src/lenin/lenin.su --models models.conf --method funnel --cycles 3 \
     --test "python test.py {file}" --ext .py "what you want"
ussr src/lenin/lenin.su --models models.conf --method enrich --cycles 10 --patience 2 \
     --score "python suite.py {file}" --ext .py "what you want"
```

`rounds`, `chain` and `funnel` cost the same: models times cycles. Which one does better
is a question for your task, and [the record](#the-record) keeps the numbers by
method. A model that fails to answer is passed over, and the next one gets the
same best.

With no test or suite, nothing measures an answer, so "best" could only mean
"smallest", and a stub is small. A chain then passes on the latest answer,
called "the answer so far" rather than "the best", and the models vote at the
end. `enrich` refuses to run that way.

Each model's prompt is written next to its answer, as `cycle1_name.prompt.md`,
so you can read exactly what it was given.

## Refining

Refining is an operation on the prompt. Prompt A, "make a text editor", plus
sub-task A, "it supports bold text", make prompt B, "make a text editor that
supports bold text". It repeats until the prompt reflects what you really want.
Sub-tasks come in two ways.

**Yours.** `--task "it supports bold text"`, as many times as you like, or
`--tasks FILE` with one per line. They go into the request before the first
model refines it. With `--no-refine` they are added after your prompt as they
are, under "It must also:".

**Found.** `--refine N` refines in N rounds. Round 1 is the refining above.
Every round after it:

1. The first model writes a draft from the current prompt.
2. Your test runs on the draft.
3. A model is shown the request, the prompt, the draft and what its test said,
   and lists what the draft shows the prompt leaves out. With two or more
   models this critic is the second one, not the draft's author: a model
   favours what it wrote.
4. The new sub-tasks go into the prompt.

Refining stops when a round finds nothing new. A round costs two calls.

```bash
ussr src/lenin/lenin.su --models models.conf --refine 3 \
     --task "it supports bold text" --test "python test.py {file}" --ext .py \
     "make a text editor"
```

`tasks.md` lists every sub-task with where it came from: "(yours)", "(round 2)",
or "(cycle 3, the best still failing)". The prompt of each round is in
`refine2.md`, `refine3.md` and so on, each with its draft beside it. Drafts only
show what is missing from the prompt; they don't compete with the centrifuge.

A model decides what a draft leaves out, and it can be wrong or add what you
never asked for. It is told not to, but read `tasks.md`.

### Back to refining

When `enrich` would stop and its best answer still fails, a model other than
that answer's author (the first one, if there is no other) gets the request,
the prompt, and that best answer with what its test said, and lists what the
prompt leaves out. New sub-tasks go into the prompt (`refine_cycleN.md`,
and `tasks.md`) and the cycles go on. If nothing is new, it stops. This needs
refining on, which is the default.

## Enrichment

Enrichment picks the better answer from several models, each used more than
once, with a test suite that says what is better. Writing such a suite takes
less intelligence than solving the problem. The current best goes to the next
model, or back to the same one, until no better answer comes that the tests can
detect. That is `--method enrich`; the pieces below work with every method.

### Enrichment is not voting

Enrichment picks an answer on evidence: what a test run on it showed. Voting
picks what most models like. One answer that passes beats ten that agree and
fail, and models that share a mistake vote for it together.

So Lenin enriches only with `--test` or `--score`. Without them it falls back on
a blind vote: each model is shown the answers under letters, not whose they are,
and replies with one letter. A vote for its own answer is not counted. The
report then says nothing was shown to work.
Treat a vote as a first look, not a result.

### A score: how much passes

`--score COMMAND` is the "what is better" suite. It runs on every answer and
prints how many of its checks pass, as the **last number** of its output:
`9 checks, passed 7`. More passing ranks first, before size or speed. So an
answer that passes 7 of 9 beats one that passes 3, and the next model is shown
the better one and what its suite said.

- Without `--test`, an answer passes when the suite exits 0, that is, when all
  of it passes.
- With `--test`, the test decides what passes and the suite only ranks.

### The handoff

Besides the task and the best answer with its numbers, the next model gets two
more things, so nothing learnt is lost on the way:

- **Requirements so far**: every sub-task, yours and found.
- **Known defects**: what the test said about the last three answers that
  failed, other than the best, so the next model doesn't repeat them.

### A hidden test

A model is shown what its test said, so it can write code that passes exactly
the inputs it saw. `--holdout COMMAND` is a second test that must pass too, and
no model ever sees what it prints: a model only hears that an answer "fails a
hidden one". Give it inputs the visible test doesn't have.

```bash
ussr src/lenin/lenin.su --models models.conf --method enrich --cycles 10 \
     --test "python test.py {file}" --holdout "python hidden.py {file}" \
     --ext .py "what you want"
```

The report counts the answers that passed the visible test and failed the
hidden one: how many were fitted to the test rather than the task. The record
keeps them apart too, as "fitted", and `--stats` gives their share over all
runs: the tests' leak.

Without a hidden test, when two or more answers pass, the report says so: the
winner is then the best by size or speed among answers a visible test let
through, which is where an answer that only gets past the test wins. More
candidates make that likelier, not less: see
[the enrichment window](../../research/neuroslop_enrichment_and_refining.md#4-a-model-and-the-enrichment-window). The hidden
test's output is in `*.log.holdout`, for you. Keep the hidden test itself out of
the models' reach too: not in the prompt, not in a file they are given.

### When to stop

- `--patience N`: `enrich` stops after N cycles in a row with nothing better.
- `--target SCORE`: any method stops as soon as an answer passes and its
  `--score` reaches SCORE, for instance all the checks of the suite.
- `--budget CALLS`: no more paid calls than that, refining and votes included.
  The calls left over are not made, and the report says so.

## What "best" means

Best of what the models gave, by the test you wrote. Nothing more.

- **A weak test picks the wrong winner.** Given the task of printing a sum, a
  test that only checks the exit status, and the answer `assert 2+3==5`, Lenin
  picks that one for its size: it passes and prints nothing. Lenin cannot tell.
  Check outputs, on several inputs, with edge cases, as
  `examples/wordcount/test.py` does.
- **A visible test can be fitted.** Models see what it said. A hidden one,
  `--holdout`, can't be fitted to.
- **Without a test**, nothing is run. A vote tells you what models like, and
  they like confident code.
- **One task says little.** Whether enriched neuroslop beats classical software
  is a claim that takes many tasks to back up. So far Lenin has run only against
  canned models, to test Lenin itself.

## Models

`--models` is a file of lines `provider model KEY_ENV [url]`, as in
[models.conf.example](models.conf.example):

```text
# three developers
anthropic claude-opus-5-5  ANTHROPIC_API_KEY
openai    gpt-x            OPENAI_API_KEY
# a local one, no key
openai    llama3.2         -  http://localhost:11434/v1/chat/completions
```

The third word names an environment variable, so keys stay out of the file,
off the command line, out of `ps` and out of your shell history. Lenin writes no
key to any file. `-` means no key: Lenin then sends the word `none` to that URL,
never your `OPENAI_API_KEY`. A model whose key isn't set is skipped, and a model
that fails ends only its own turn.

Models are reached through `prompt(_):`: Anthropic, or anything
OpenAI-compatible. Different developers disagree in more useful ways than one
model asked twice.

## Before you run it

- **It costs money.** A run makes up to: one call to refine, two for each extra
  `--refine` round, models times cycles, one per cycle when `enrich` refines
  again, and one vote per model when there is no test or suite. Lenin prints the
  number first and does not ask. `--budget` caps it.
- **`--test`, `--score` and `--holdout` run code nobody has read, with your
  rights**, and so do the drafts of `--refine`. A model can write anything. Use a
  container, a virtual machine or a throwaway user.
- **A test that hangs stops Lenin.** USSR can't kill a command it started, so put
  a limit in the test: `timeout 10 python {file}`.
- One file per answer: the first fenced block of the reply.
- Models are asked one after another, not at once.

## In Moscow

Lenin runs in Moscow, the USSR kernel, on its native `/bin/ussr`. That build has
OpenSSL, so the models' HTTPS works, and it starts programs with `posix_spawn`.
Moscow has no shell, so there a test is a program on `PATH` and its arguments,
such as `ussr test.su`: no quotes, pipes or redirections. That is `--direct`,
the default wherever there is no `/bin/sh`. What the test prints still goes to
its log, through `USSR_EXTERNAL_OUTPUT`.

Start it through `ussr` with a small script that includes Lenin:

```text
# /home/lenin_go.su
$include "/src/ussr/src/lenin/lenin.su"
init(r): n v [
    lenin_main(r): v
]
```

    russia> run(st): "ussr" "/home/lenin_go.su" "--models" "/home/models.conf" "--test" "ussr /home/test.su" "say good"

`run(_): "lenin"` would not do: Moscow gives a `.su` that calls `prompt` to
russia.ru, which has no TLS. Certificates come from `/etc/ssl/certs.pem`; for a
server with a certificate of its own, add
`setenv(_): "RYAZAN_CA_FILE" "/path/to/cert.pem"` before `lenin_main`.

## The record

One run says little, so every run adds a few lines to a file that outlives it:
what each model answered, how it measured, and who won. Over many runs it
answers what one report can't: which model passes most often, whether later
cycles win, which method does better, and how often anything beats your
baseline.

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
The tests' leak: 3 of 40 answers that passed a test the models saw failed the hidden one (7%).
```

The last line comes only from runs with `--holdout`. A leak near zero means
more cycles are safe; a large one means stop early or fix the test.

"Runs won" counts only answers that passed a test. The numbers are as good as
your tests, and under 20 measured runs Lenin says they are too few to conclude
anything.

The file is `~/.lenin/history.tsv`, or `LENIN_HISTORY`, or `--history FILE`;
`--no-history` keeps a run out of it. It is plain tab-separated text with no keys
and no answers, but it does hold model names and the first 60 bytes of each
prompt.

## What it writes

In the output directory (`--dir`, `lenin-out` by default):

| File | What it is |
|---|---|
| `refined.md` | the prompt the centrifuge got |
| `tasks.md` | every sub-task and where it came from |
| `refine2.md`, `refine2_draft<ext>`, ... | each refining round's prompt and draft |
| `refine_cycleN.md` | the prompt after refining again at cycle N (`enrich`) |
| `cycleN_model<ext>` | an answer |
| `cycleN_model.prompt.md` | what that model was sent |
| `cycleN_model.log`, `.log.score`, `.log.holdout` | what its test, suite and hidden test printed |
| `best<ext>` | the winner |
| `baseline.log` | what the test said of `--baseline` |
| `report.md` | every answer: state, bytes, runs a second, score; the winner; why it stopped |

## Options

| Option | Meaning | Default |
|---|---|---|
| **What to ask** | | |
| `prompt ...` | the request, the rest of the command line | |
| `--prompt-file FILE` | the request from a file | |
| `--task TEXT` | a sub-task for the prompt; give it more than once | none |
| `--tasks FILE` | sub-tasks, one a line | none |
| `--refine N` | refining rounds; each after the first drafts and adds what it shows is missing | 1 |
| `--no-refine` | send the prompt as it is (`--refine 0`) | refines |
| **Who answers, how** | | |
| `--models FILE` | the centrifuge | required |
| `--method rounds\|chain\|funnel\|enrich` | when the best is passed on: after each cycle; after each model; after cycle 1 and then each model; or after each model until nothing better comes | `rounds` |
| `--cycles N` | generation cycles; for `enrich`, the most | 2 |
| `--patience N` | `enrich` stops after N cycles in a row with nothing better | 1 |
| **How answers are measured** | | |
| `--test COMMAND` | exits 0 when `{file}` works; `{file}` goes at the end if it isn't there | none: a vote |
| `--score COMMAND` | a suite that prints, last, how many of its checks `{file}` passes; more ranks first | none |
| `--holdout COMMAND` | a hidden test: it must pass too, and no model sees what it says; needs `--test` or `--score` | none |
| `--criteria size\|speed` | what decides among answers that pass and score the same; speed needs `--test` or `--score` | `size` |
| `--window SECONDS` | how long speed is timed; the clock has whole seconds, so 3 or more | 3 |
| `--baseline FILE` | a classic solution to measure against | none |
| `--direct` | run tests with no shell: the first word a program on `PATH`, the rest its arguments | where there is no `/bin/sh` |
| **Limits** | | |
| `--target SCORE` | stop once an answer passes and its score reaches SCORE; needs `--score` | none |
| `--budget CALLS` | the most paid model calls a run makes | no limit |
| **Where things go** | | |
| `--dir DIR` | the output directory | `lenin-out` |
| `--ext .EXT` | the answers' file extension | `.txt` |
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

It has not been run with real models yet.

## Tests

`tests/lenin_test.su` covers the helpers, the ranking and whole runs of every
method, with canned model replies and no network; `tests/run_all.sh` runs it.
`src/moscow/tools/qemu_test.py` runs Lenin inside Moscow against a fake model
over HTTPS, with its tests started with no shell.
