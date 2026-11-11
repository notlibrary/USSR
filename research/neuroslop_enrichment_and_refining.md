# Neuroslop enrichment and refining

*What the two operations are, a small model of when they work, a proof that the
enrichment window exists in that model, and what follows for Lenin.*

## Summary

Neuroslop is raw model output that nobody has checked. Two operations turn it
into something usable:

- **Refining** works on the prompt: it is made more precise until it says what
  is really wanted.
- **Enrichment** works on the answers: several models, used several times,
  produce candidates; a test says which is better; the best so far goes on to
  the next model.

This article makes three claims and is careful about which kind each is.

1. **Proved here, in a model** (section 4): when answers are also ranked by a
   second criterion such as size or speed, and the test lets some wrong answers
   through, the chance that the winner is correct first rises with the number
   of candidates and then falls to zero. The range where it is high is the
   **enrichment window**. It has a closed form, and a hidden test or a
   stricter test widens it.
2. **Taken from published work** (section 3): the ideas enrichment and
   refining are built from, with references. No figures are quoted from them.
3. **Not shown yet**: that any of this beats software written by hand. Lenin
   has run against canned models only. Section 7 says what to measure.

## 1. Definitions

A **task** is what is wanted. A **prompt** is the text that stands for it. A
**candidate** is one answer of one model to one prompt: here, one file of code.
A **test** is a program that runs a candidate and says pass or fail. A
**suite** says how much passes: a number. A **criterion** ranks the candidates
that pass: fewer bytes, or more runs a second. The **centrifuge** is the set of
models asked, one after another.

**Refining** takes a prompt and feedback and gives a better prompt:

    P(n+1) = R(P(n), F(n))

Prompt A, "make a text editor", and sub-task A, "it supports bold text", give
prompt B, "make a text editor that supports bold text". Feedback comes from the
person, or from running a draft made from the prompt and looking at what it
gets wrong.

**Enrichment** keeps a current best S and replaces it only on evidence:

    S(n+1) = C(n+1)   if the test says C(n+1) is better than S(n)
    S(n+1) = S(n)     otherwise

The claim that makes enrichment worth doing is an asymmetry: **telling which of
two answers is better takes less intelligence than writing either.** A dozen
input and output pairs can reject a matrix multiplication that took real
understanding to write.

Enrichment is not voting. A vote counts opinions; enrichment counts what a run
showed. Models that share a mistake vote for it together.

## 2. Where it applies

Enrichment needs a "what is better" test that is cheap next to the work it
judges. That holds for much of programming: compile it, run it, compare
outputs. It holds less where a test is a field trial or a lab measurement: an
engineering agent can enrich too, but each comparison may take weeks, and the
arithmetic of section 4 turns against it, since every candidate costs a test.

Refining needs less: only that a draft can be looked at. It applies wherever a
prompt can be wrong, which is everywhere.

## 3. What is already known

The parts are not new. What the literature says, in short:

- **Sample many, keep what passes.** Generating many programs and filtering
  them with tests is how AlphaCode reached competition level [1]; pass@k, the
  chance that one of k samples passes, is the usual measure since Codex [2].
- **Tests can come from models too**, and agreement between programs and tests
  picks better than either alone (CodeT) [3]. Agreement between samples alone
  already helps on reasoning tasks (self-consistency) [4].
- **Feedback loops help, with a limit.** Showing a model its own failed test
  and asking again improves code (Self-Refine, Reflexion, self-debugging)
  [5][6][7]. But repair is not free: spending the same budget on fresh,
  different samples is often as good or better, and the quality of the
  feedback is what limits repair [8]. Without an outside check, models asked
  to correct their own reasoning often make it worse [9].
- **Several models.** Models that answer in layers, each seeing the others'
  answers, do better than one (Mixture-of-Agents, debate) [10][11]. Models
  also rate their own output above others' [12], so the judge should not be
  the author.
- **An evaluator plus a population** finds programs no single prompt does
  (FunSearch) [13].
- **Weak tests flatter.** With more and stricter tests, measured correctness
  of generated code drops (EvalPlus) [14]. Any measure that becomes a target
  stops measuring (Goodhart) [15].

The window of section 4 is the last point made exact for enrichment.

## 4. A model, and the enrichment window

### 4.1 The model

Ask for n candidates, independently.

- A candidate is **correct** with probability p.
- The test passes every correct candidate. It passes a wrong one with
  probability e: the test's **leak**.
- A hidden test, one the models never saw, catches a wrong candidate that
  passed with probability h. With no hidden test, h = 0.
- Candidates that pass are ranked by a criterion. A wrong candidate that got
  through beats every correct one on it with probability d. Call such a
  candidate an **exploit**. The stub that prints nothing and passes a test
  that only looks at the exit status is one: nothing is smaller.

So each candidate is an exploit with probability

    x = (1 - p) e (1 - h) d

### 4.2 Yield

The chance that at least one candidate passes is `1 - (1 - q)^n`, where
`q = p + (1 - p) e`. It rises fast and flattens: the tenth candidate adds less
than the second. If a call costs c and a correct answer is worth v, the n-th
call is worth making only while its gain is above c, so there is a last call
worth paying for even with a perfect test. That is the budget's edge of the
window. It moves out as calls get cheaper.

### 4.3 The window

**Proposition.** Let W(n) be the probability that the winner after n
candidates is correct. In the model above

    W(n) = (1 - x)^n - (1 - x - p)^n

If x = 0, W(n) rises to 1. If x > 0, W(n) rises to a single peak at

    n* = ln( ln(1 - x - p) / ln(1 - x) ) / ln( (1 - x) / (1 - x - p) )

and then falls to 0.

**Proof.** The winner is the best passing candidate by the criterion. It is
correct exactly when some candidate is correct and none is an exploit: an
exploit beats every correct candidate, and a wrong candidate that is no
exploit loses to any correct one. Candidates are independent, so the chance of
no exploit among n is `(1 - x)^n`, and the chance of no exploit and no correct
one is `(1 - x - p)^n`. Their difference is W(n). For x > 0 both terms go to 0,
so W does. Write a = 1 - x and b = 1 - x - p, with 0 <= b < a < 1. Then
`W'(n) = a^n ln a - b^n ln b` is zero only where `(a/b)^n = ln b / ln a`, which
has one solution, n*; W is 0 at n = 0, positive after, and 0 in the limit, so
that solution is a maximum. ∎

In words: **more candidates first make a correct winner likelier, because one
of them is finally right, and then less likely, because one of them finally
finds the hole in the test.** The first edge of the window is set by p, the
second by x.

### 4.4 Numbers

`python research/window.py` prints these, with a simulation of the same model
beside each row; they agree to two decimals.

| | p | e | h | peak n* | W(1) | W(7) | W(12) | W(20) | W(50) | W(100) |
|---|---|---|---|---|---|---|---|---|---|---|
| a weak test, no hidden test | 0.3 | 0.05 | 0 | 6.5 | 0.30 | 0.72 | 0.65 | 0.49 | 0.17 | 0.03 |
| the same, a hidden test catching 9 in 10 | 0.3 | 0.05 | 0.9 | 12.9 | 0.30 | 0.90 | 0.95 | 0.93 | 0.84 | 0.70 |
| a strict test | 0.3 | 0.005 | 0 | 12.9 | 0.30 | 0.90 | 0.95 | 0.93 | 0.84 | 0.70 |
| a weak test, a hard task | 0.05 | 0.05 | 0 | 13.8 | 0.05 | 0.22 | 0.27 | 0.25 | 0.08 | 0.01 |
| a test nothing wrong gets past | 0.3 | 0 | 0 | none | 0.30 | 0.92 | 0.99 | 1.00 | 1.00 | 1.00 |

Three things to read off:

1. With a test that leaks one wrong answer in twenty, the best moment to stop
   is after about seven candidates, and a hundred candidates are worse than
   one.
2. A hidden test that catches nine exploits in ten does exactly what a test ten
   times stricter does: it divides x by ten. The peak moves out and up.
3. On a hard task with a weak test the window is low everywhere: the peak is
   0.27. No number of candidates fixes a test that leaks as often as the
   models succeed. The test has to improve first.

### 4.5 What the model leaves out

- **Candidates are not independent.** Models share training and share
  mistakes, so real yield grows slower than `1 - (1 - q)^n`.
- **A chain adapts.** When each model sees the last test's output, p rises
  (repair) and so does e: the answers are fitted to the test they were shown.
  The model has no feedback in it; with feedback the falling edge comes sooner
  for the same test.
- **d is not 1.** Not every wrong answer that passes is small or fast. But
  where the criterion is size, stubs are exactly the small ones.
- **No criterion, no fall.** If passing answers are not ranked at all and the
  first one is kept, the chance it is correct is p/q for every n: it neither
  rises nor falls. The fall comes from optimizing among answers the test
  cannot tell apart.

So the proposition proves the window exists under stated assumptions. Whether
a given set of models and a given test are inside it is a measurement
(section 7).

## 5. Refining

The same model says what refining is for. A prompt that leaves the task open
caps p: a model cannot be right about what it was not told, however many times
it is asked. Enrichment raises the chance of finding a correct answer among
candidates; **refining raises p itself**, and it is the only one of the two
that does. From 4.3, a higher p moves the rising edge in and lifts the peak.

Two ways to refine, from the definitions:

- **Before**: write the specification out: inputs, outputs, limits, edge
  cases, how to tell it works.
- **From output**: make a draft, run it, and ask what the draft shows the
  prompt leaves out. A failing test is a better source of sub-tasks than
  thinking about the prompt, because it is evidence.

Two cautions:

- What a draft "leaves out" is a model's opinion, unlike a test's result. It
  can add what nobody asked for. A person should read the list.
- The model that wrote the draft should not be the one that says what is wrong
  with it: models favour their own output [12]. A different model is a better
  critic.

When enrichment stalls and the best answer still fails, more candidates will
not help if the cause is the prompt. Then the right move is back to refining.

## 6. Wide, then deep

Two ways to spend calls:

- **Wide**: every model answers the same prompt, independently. This is where
  yield comes from, and independence is what 4.2 counts on.
- **Deep**: each model is shown the current best and asked to better it. This
  is repair. It builds on what works, and it anchors: everything after the
  first passing answer starts from that answer.

The literature's finding that fresh samples often match repair [8], and the
window's rising edge, both say the same: **go wide first, while nothing passes
yet, and deep after**, when there is something worth improving. Deep from the
first call spends the diversity of several models on variations of whichever
answered first.

## 7. What follows for Lenin

Lenin (`src/lenin`) is these operations as a program. What the article implies,
and where Lenin stands:

| Implication | In Lenin |
|---|---|
| An answer that fails cannot win; evidence, not votes | from the start; without a test the report says nothing was shown to work |
| The best so far goes on to the next model | `--method chain`, `--method enrich` |
| Rank by how much passes, before size or speed | `--score` |
| A hidden test divides x | `--holdout` |
| Stop: the window has a far edge | `--patience`, `--target`, `--budget` |
| Back to refining when the best still fails | `--method enrich` |
| Wide first, then deep (section 6) | **new: `--method funnel`** |
| The critic is not the author (section 5) | **new: with two or more models, another model than the draft's author lists what is missing; a model's vote for its own answer is not counted** |
| Say when the winner may be an exploit (4.3) | **new: with no hidden test and two or more passing answers, the report says the winner was picked by a test the models were shown** |
| Measure e, the leak (4.5) | **new: the record keeps answers that passed the visible test and failed the hidden one apart, and `--stats` gives their share** |
| Fresh restarts when a chain stalls on a passing answer | not done |
| A test must pass more than once (flaky tests) | only when speed is timed |
| Tests written by models (CodeT) | not done |

### What to measure

The record Lenin keeps makes the model's quantities observable:

- **q**, the pass rate, per model: already in `--stats`.
- **e(1 - h)... the leak**: among answers that pass the visible test, the share
  that fail the hidden one. This is the new line of `--stats`. If it is near
  zero, the window is wide and more cycles are safe. If it is not, stop early
  or fix the test.
- **Does deep beat wide?** The share of runs won after cycle 1, by method.
- **Does any of it beat a person?** The share of runs whose winner beat a
  `--baseline`.

None of these has been measured with real models. Until they are, this article
is a method and a proof about a model, not a result.

## References

1. Li et al., "Competition-level code generation with AlphaCode", Science, 2022.
2. Chen et al., "Evaluating Large Language Models Trained on Code", 2021.
3. Chen et al., "CodeT: Code Generation with Generated Tests", ICLR 2023.
4. Wang et al., "Self-Consistency Improves Chain of Thought Reasoning in
   Language Models", ICLR 2023.
5. Madaan et al., "Self-Refine: Iterative Refinement with Self-Feedback",
   NeurIPS 2023.
6. Shinn et al., "Reflexion: Language Agents with Verbal Reinforcement
   Learning", NeurIPS 2023.
7. Chen et al., "Teaching Large Language Models to Self-Debug", ICLR 2024.
8. Olausson et al., "Is Self-Repair a Silver Bullet for Code Generation?",
   ICLR 2024.
9. Huang et al., "Large Language Models Cannot Self-Correct Reasoning Yet",
   ICLR 2024.
10. Wang et al., "Mixture-of-Agents Enhances Large Language Model
    Capabilities", 2024.
11. Du et al., "Improving Factuality and Reasoning in Language Models through
    Multiagent Debate", 2023.
12. Panickssery et al., "LLM Evaluators Recognize and Favor Their Own
    Generations", NeurIPS 2024.
13. Romera-Paredes et al., "Mathematical discoveries from program search with
    large language models", Nature, 2024.
14. Liu et al., "Is Your Code Generated by ChatGPT Really Correct? Rigorous
    Evaluation of Large Language Models for Code Generation", NeurIPS 2023.
15. Goodhart, "Problems of Monetary Management: The U.K. Experience", 1975.

The references are given for their ideas, from memory of the papers; check
them before citing this article's summary of any one.
