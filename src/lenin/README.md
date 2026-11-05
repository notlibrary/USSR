# Lenin

Lenin is the brain center of the USSR: an agent that enriches and refines
neuroslop code. USSR is made by prompting an AI and refining the result with
the next AI "centrifuge". Lenin is that process as a program.

```bash
ussr src/lenin/lenin.su --models models.conf --dir out --cycles 3 \
     --criteria size --test "python test.py {file}" --ext .py "what you want"
```

## What it does

1. **Refines the prompt.** The first model turns your request into a precise
   prompt: language, inputs, outputs, limits, how to tell it works. Assumptions
   it makes are listed as assumptions. It goes to `out/refined.md`, so read it.
   `--no-refine` sends yours as it is.
2. **Spins the centrifuge.** Every model answers. From the second cycle on,
   each sees the best answer so far and how it measured, or what its test said
   when it failed, and is asked for a better one.
3. **Measures.** Each answer is one file, written to `out/`. `--test` runs on it
   and exits 0 when it works. Size is its bytes. Speed is how many times the
   test runs a second over `--window` seconds (3 by default), start-up included:
   the clock has whole seconds, so keep the window at 3 or more.
4. **Extracts the best.** An answer that fails its test cannot win. Among the
   rest the criterion decides, then the other one. `out/best<ext>` is the winner
   and `out/report.md` has every answer with its numbers.

`--baseline FILE` measures a solution you already have, a classic one, the same
way, and the report says whether anything beat it.

## What "best" means

Best of what the models gave, by the test you wrote. Not more.

**A weak test picks the wrong winner.** Given the task of printing a sum, a test
that only looks at the exit status, and the canned answer `assert 2+3==5`, Lenin
picks that for its size: it passes and prints nothing. Lenin cannot tell. Check
outputs, on several inputs, with edge cases: see `examples/wordcount/test.py`.

**Without `--test`** nothing is run. The models vote blind on each other's
answers, and the report says that nothing there is shown to work. A vote tells
you what models like, and they like confident code. Use it for a first look.

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

- **It costs money.** A run is one call to refine, models times cycles, and with
  no test one vote per model. Lenin prints the number first and does not ask.
- **`--test` runs code nobody has read, with your rights.** A model can write
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
| `--criteria speed\|size` | what decides among those that pass; speed needs `--test` | `size` |
| `--test COMMAND` | exits 0 when `{file}` works; `{file}` goes at the end if not given | none: a vote |
| `--ext .EXT` | candidates' file extension | `.txt` |
| `--baseline FILE` | a solution to compare with | none |
| `--window SECONDS` | how long to time speed | 3 |
| `--no-refine` | send the prompt as it is | refines |
| `--prompt-file FILE` | the prompt from a file | the arguments |

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
