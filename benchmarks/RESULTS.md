# Results: adaptogen vs established algorithms, and what the bacterial mechanisms add

Full tables (with spreads, timings, sizes and p-values): [baselines.md](baselines.md),
[mechanisms.md](mechanisms.md). Raw per-run data: [baselines.csv](baselines.csv),
[mechanisms.csv](mechanisms.csv). All numbers come from the stage-2 code.

## Setup

- **Problems**
  - Six standard symbolic-regression benchmarks: Koza-1, Nguyen-5/7/10, Pagie-1, Keijzer-6.
    Keijzer-6 tests **extrapolation** (train x = 1..50, test x = 1..120).
  - Three list algorithms (sum, max, count of positives). Training lists have ≤ 10 items, test
    lists ≤ 40, so a program must really iterate to generalise.
- **Budget:** about 50,000 program evaluations per run (population 500 × 100 generations; spatial
  runs use a 23×22 grid = 506). Random search and hill climbing get the same number of
  evaluations. Runs stop early when the *training* set is solved.
- **20 independent runs** per cell. Each run samples its own data.
- **Solved** is judged on the **unseen test set**:
  - formulas: RMSE < 1% of the target's spread (R² > 0.9999);
  - lists: every test list answered exactly.
- **p** = two-sided Fisher exact test on solved counts against the first row of the table.
  With 20 runs only large differences reach p < 0.05.

## 1. adaptogen vs other algorithms (formula benchmarks)

Solved runs (of 20) · median test R²:

| Algorithm | koza1 | nguyen5 | nguyen7 | nguyen10 | pagie1 | keijzer6 | **total** |
|---|---|---|---|---|---|---|---|
| adaptogen (full instruction set, lexicase) | 5 · 0.9988 | 0 · 0.984 | 2 · 0.9993 | 5 · 0.9976 | 0 · **0.892** | 6 · 0.9995 | 18/120 |
| adaptogen-classic (tree-GP function set) | 4 · 0.9990 | 0 · 0.977 | 2 · 0.9995 | **6** · 0.9987 | 0 · 0.701 | **15** · 0.99996 | **27/120** |
| adaptogen-tournament | **6** · 0.9966 | 0 · 0.981 | 0 · 0.9989 | 2 · 0.9908 | 0 · 0.820 | 10 · 0.9999 | 18/120 |
| tree-gp (Koza-style) | **6** · 0.9997 | 0 · 0.995 | **4** · 0.9995 | 5 · 0.9991 | 0 · 0.750 | 10 · 0.9999 | 25/120 |
| hill-climber | 2 · 0.9956 | 0 · 0.987 | 2 · 0.9987 | 3 · 0.9892 | 0 · 0.805 | 9 · 0.9998 | 16/120 |
| random-search | 0 · 0.910 | 0 · 0.766 | 0 · 0.9954 | 0 · 0.869 | 0 · 0.319 | 0 · 0.986 | 0/120 |
| polyfit (not evolutionary) | **20** · 1 | **20** · 1 | **20** · 1 | **20** · 1 | 0 · **0.983** | 0 · ≪ 0 | 80/120 |

**Reading:**
- adaptogen is **on par with classic tree GP**: 27 vs 25 solved with the same function set; no
  per-problem difference is significant. It clearly beats random search and is ahead of hill
  climbing.
- The full, more general instruction set solves fewer runs (18). On the hardest problem
  (pagie1), though, it reaches the best median R² of any evolutionary method (0.892 vs 0.750).
- Polynomial least squares is unbeatable on smooth functions **inside** the training range but
  collapses on extrapolation (Keijzer-6). Evolved programs extrapolate.
- Tree GP is several times faster in wall-clock time.

## 2. Ablation: what each bacterial mechanism adds

Solved runs (of 20) per benchmark. The baseline is the stage-1 engine (crossover + mutation,
lexicase, no mechanisms).

| Variant | koza1 | ng5 | ng7 | ng10 | pagie1 | keij6 | list-sum | list-max | list-count+ | **total** |
|---|---|---|---|---|---|---|---|---|---|---|
| baseline | 5 | 0 | 2 | 5 | 0 | 6 | 2 | 4 | 0 | **24/180** |
| +stress | **9** | 1 | 0 | 4 | 0 | 3 | 2 | 3 | 0 | 22/180 |
| +hypermutation | 4 | 0 | 1 | **6** | 0 | 6 | 1 | 2 | 0 | 20/180 |
| +transfer | 4 | 1 | 1 | 3 | 0 | 6 | 2 | 4 | 0 | 21/180 |
| +duplication | **9** | 1 | 0 | 3 | 0 | 4 | 2 | 3 | 0 | 22/180 |
| +spatial | 7 | 0 | 1 | 1 | 0 | 4 | 1 | **5** | 0 | 19/180 |
| +spatial+curriculum | 3 | 0 | 0 | 0 (p = 0.047) | 0 | 2 | 1 | 3 | 0 | 9/180 |
| all mechanisms | 5 | 0 | 0 | 5 | 0 | 4 | 0 | 4 | 0 | 18/180 |
| bacterial (all, no crossover) | 4 | 0 | 0 | 1 | 0 | 4 | 0 | 0 | 0 | 9/180 |

### Honest findings

1. **No mechanism significantly improves on the baseline yet.** The most promising signals are
   stress and duplication on koza1 (9 vs 5 solved, p = 0.32): suggestive, not proven.
2. **Spatial structure + curriculum is significantly worse on nguyen10** (0 vs 5, p = 0.047),
   and it lowers median quality on pagie1 and the lists. Local competition slows the spread of
   good code. Within 100 generations that cost is not repaid by the extra diversity.
3. **Combining everything does not add up.** "All mechanisms" (18) and "bacterial" (9) are
   below the baseline (24). The pure bacterial setting (no crossover) is the weakest. On these
   tasks, recombining two selected parents is more productive than random horizontal transfer.
4. **Stress is "on" almost all the time with the default settings.** With a patience of 3
   generations, most lineages count as stagnating, so the mean mutation multiplier sits around
   4×. A sharper trigger, such as stress relative to neighbours or a longer patience, is a
   better test of the SOS idea.
5. **Hypermutators behave biologically.** Heritable mutation rates drift *downward* once the
   population is well adapted, the "mutation load" effect from population genetics. Even so,
   self-adapted rates did not beat a fixed rate here.
6. **Spatial runs are 2–3× faster** per run because they keep programs smaller.
7. **The list problems are dominated by overfitting**, not by search. Programs solve the short
   training lists with tricks (test accuracy ≈ 4–9% for sum and count, 20–50% for max) and the
   run stops as soon as training is solved. No mechanism fixes that. It needs evaluation
   changes: longer or more varied training lists, a validation split for choosing the final
   program, and a size or time cost.

### What this means for the project

The core engine is competitive with standard GP. The bacteria-inspired mechanisms, at their
**untuned default settings and this short budget**, do not yet help. That is a legitimate
research result and the starting point for the next experiments:

- **Longer runs:** diversity-preserving mechanisms (spatial, curriculum, transfer) usually pay off
  only over long horizons. Rerun at 5–10× the budget.
- **Sharper stress trigger:** relative to neighbours, or with longer patience.
- **Fix the list benchmark's overfitting**, then retest. Loops, duplication and transfer should
  matter most on algorithmic problems.
- **Parameter sweeps** per mechanism (rates, segment sizes, band counts) instead of single defaults.
- **More runs** (50–100) for the promising signals (stress and duplication on koza1).

## 3. Testing the mechanisms properly: sweep on tuning seeds, then a long run on fresh seeds

Sections 1 and 2 used untuned defaults and a short budget. This section follows up the
suggestions above with a cleaner method:

1. **Benchmark fixes first.**
   - List benchmark v2: 100 training lists up to length 20, test lists up to 50.
   - A new **validation split** (`validation_fraction`): 25% of training cases are held back
     and used only to choose the final program.

   20-run check, seeds 2000+: 3/1/0 solved runs (sum/max/count) without validation vs
   1/4/0 with it, so no measurable effect. The list failures turned out to be mostly a
   *search* problem (training accuracy is low too), not memorisation. Validation stays on for
   list benchmarks as a cheap safeguard.
2. **New stress trigger.** `"trigger": "population"` raises mutation only when the *best*
   program has stopped improving, instead of per lineage.
3. **Parameter sweep on tuning seeds** (3000+): 22 settings across the six mechanisms, 5
   benchmarks where something gets solved (koza1, nguyen10, keijzer6, list-sum, list-max),
   20 runs each, standard budget. Full table: [sweep.md](sweep.md), data: [sweep.csv](sweep.csv).
4. **Final comparison on fresh seeds** (5000+, never used for tuning). Each mechanism uses its
   best tuned setting, at **5× the budget** (500 generations). "combined" = the three settings
   that beat the baseline during tuning. Table: [final.md](final.md), data: [final.csv](final.csv).

### Sweep (tuning seeds, 100 runs per setting = 5 benchmarks × 20)

| Mechanism | Settings tried → solved (of 100) | Baseline |
|---|---|---|
| stress | lineage p3: 24 · lineage p10: 22 · lineage p10 mild: 20 · **population p5: 26 · population p15: 27** | 24 |
| hypermutation | default: 17 · slow: 16 · **start rate 2: 29** | 24 |
| transfer | **rate 0.05: 28** · 0.2: 23 · 0.5: 19 · segments ≤2: 18 · ≤12: 18 | 24 |
| duplication | rate 0.05: 20 · 0.1: 20 · 0.3: 17 · segments ≤12: 20 | 24 |
| spatial | radius 1: 17 · 2: 13 · 3: 15 | 24 |
| curriculum | 2 bands: 13 · 4 bands: 12 | 24 |

### Final (fresh seeds, 5× budget): solved runs of 20

| Variant | koza1 | nguyen10 | keijzer6 | list-sum | list-max | **total (of 100)** | pooled p vs baseline* |
|---|---|---|---|---|---|---|---|
| baseline | 7 | 4 | 6 | **12** | 4 | 33 | – |
| stress (population, patience 15) | 9 | 5 | 8 | 11 | **7** | **40** | 0.37 |
| transfer (rate 0.05) | 10 | **7** | 9 | 7 | **7** | **40** | 0.38 |
| spatial (radius 1) | 9 | 4 | **12** | 3 (p = 0.008, worse) | **7** | 35 | 0.88 |
| duplication (segments ≤ 12) | **11** | 5 | 6 | 8 | 2 | 32 | 1.00 |
| hypermutation (start rate 2) | 8 | 4 | 9 | 7 | 3 | 31 | 0.88 |
| combined (stress + transfer + hypermutation) | 9 | 6 | 6 | 6 | 4 | 31 | 0.88 |
| curriculum (radius 2, 2 bands) | **11** | 4 | 8 | 1 (p < 0.001, worse) | 5 | 29 | 0.64 |

\* Cochran–Mantel–Haenszel test, stratified by benchmark (pools the evidence across problems).

### Findings

1. **Still no statistically significant overall improvement.** The best mechanisms gain +7 solved
   runs out of 100 (40 vs 33), p ≈ 0.37. To confirm an effect of that size at p < 0.05 would
   need roughly 300+ runs per variant.
2. **Two mechanisms are consistently, if modestly, positive: population-level stress and
   low-rate horizontal transfer.** They are the only ones that beat the baseline on *both*
   independent seed sets (tuning: 27 and 28 vs 24; final: 40 and 40 vs 33). Each matches or beats
   the baseline on 4 of 5 problems. This is the most credible signal so far.
3. **The sharper stress trigger was the right fix.** Stress on the whole population when the
   best program stalls (26–27) beat per-lineage stress (20–24) in the sweep. That fits the SOS
   picture better: a strong response when the population is genuinely stuck, not constant
   background mutagenesis.
4. **Horizontal transfer works only at low rates.** Rate 0.05 was best and 0.5 clearly worst.
   Occasional, small exchanges help; frequent exchange homogenises the population.
5. **The winner's curse is real.** Hypermutation with start rate 2 was the *best* setting in
   tuning (29) but slightly below the baseline on fresh seeds (31 vs 33). Without the separate
   final run we would have wrongly declared it the winner.
6. **Spatial structure helps some problems and badly hurts others.** With the longer budget it
   doubled the success on the extrapolation problem (keijzer6: 12 vs 6) and needed fewer
   evaluations. But it was significantly worse at discovering list loops (list-sum: 3 vs 12,
   p = 0.008). The MEGA-plate curriculum was even worse there (1 vs 12, p < 0.001).
   Evidently a rare, fragile discovery like a working loop needs to spread through the whole
   population fast, which local competition prevents.
7. **Combining mechanisms does not add up.** The three tuned "winners" together (31) did worse
   than two of them alone (40). The mechanisms interact: each adds variation, and together they
   overshoot.
8. **More budget matters more than any mechanism.** The baseline rose from 2–3/20 to 12/20 on
   list-sum with 5× the budget.

### Recommendation

- Keep all mechanisms **off by default**. None has earned a default-on place yet.
- For users who want to try them, the evidence favours `stress` with
  `"trigger": "population", "patience": 15` and `transfer` with `"rate": 0.05`, used
  **separately**.
- Avoid `spatial` and `curriculum` for algorithmic (loop-discovery) problems. Consider `spatial`
  for problems that need extrapolation and diversity.
- To get a definite answer on stress and transfer: 100+ runs per variant on these five
  benchmarks (≈ 8 hours on 4 cores), or a faster VM first.

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
./build/adaptogen-compare --suite baselines --runs 20
```

```bash
./build/adaptogen-compare --suite mechanisms --runs 20
```

On 4 cores these took about 15 and 40 minutes. The sweep and the final comparison in section 3
(about 45 and 90 minutes) are driven by `--variant NAME=JSON` config overlays and
`--seed-base`. [sweep.md](sweep.md) and [final.md](final.md) record the budget, seed base and
every variant's exact overlay. The same seeds give identical results on the same platform.

## 4. Strain bank: does reviving a saved strain help? (stage 3)

Five independent donor strains were evolved for **list-sum**, each one generalising perfectly
to the unseen longer test lists. A **control donor**, a koza1 formula strain whose sensors don't
exist on list problems, so its sensing becomes pseudogenes, tests whether "starting from any
evolved code" alone explains a gain. Donors are in [strains/](strains/). Targets were run on
fresh seeds (6000+), 20 runs each, standard budget (50,000 evaluations), with 10% of the starting
population random. Table: [transfer.md](transfer.md), data: [transfer.csv](transfer.csv).

| Variant | list-sum (new data) | list-max | list-count_positive |
|---|---|---|---|
| from scratch (baseline) | 5/20 · median 49,901 evals | 1/20 · median accuracy 0.11 | 0/20 |
| from one sum strain | **20/20 · 500 evals** (p < 0.001) | **5/20** · 0.19 (p = 0.18) | 0/20 |
| from five sum strains (multiple parents) | **20/20 · 500 evals** | 0/20 · 0.07 | 0/20 · 0.035 |
| from one sum strain + retain 0.3 | **20/20 · 500 evals** | 1/20 · 0.13 | 0/20 |
| control: unrelated koza1 strain | 5/20 | 3/20 · 0.10 | 0/20 |

### Findings

1. **Reusing a strain on the same task with new data is a huge win.** Every seeded run solved
   list-sum in the first generation, at **1% of the cost** (500 vs ~50,000 evaluations), against
   5/20 from scratch. The unrelated control did no better than scratch. So the gain comes from
   the strain's actual code, not from seeding as such. This is the core "save a solution, reuse
   it" use case.
2. **Transfer to a *different* task is weak so far.** Starting from a sum strain gave the best
   result on list-max (5/20 vs 1/20, median accuracy 0.19 vs 0.11), but that is not significant
   (p = 0.18). On count_positive nothing worked.
3. **More parents is not automatically better.** Seeding from all five sum strains *hurt* on
   list-max (0/20). With 90% of the population cloned from five near-identical solutions, there was
   less useful diversity than with one strain plus its mutants.
4. **Retention works as intended but costs time.** With retain 0.3 the run also scores on the
   parent task (2–3× slower per run) and did not help the new task here. It is for keeping a
   general ability, not for learning faster.
5. **Why cross-task transfer is weak: bloated donors.** The donor strains carry a lot of
   leftover code (e.g. 128 instructions, ~50 counted effective). Turning "sum" into "max" is
   conceptually one change (`+` → `max`) plus initialising the accumulator. In a messy program it
   takes several coordinated mutations, and the sum strain scores badly as a "max" program, so
   selection discards it early. Simplifying strains before saving (removing dead and redundant
   code, which is stage 6's simplifier) is the most promising fix, together with a
   higher `random_fraction` when the target task differs.

## 5. Simplified strains transfer better

Section 4 suggested bloated donors were the limit. A **verified simplifier** now removes code as
long as no training case scores worse, checked case by case. It reduced the five sum donors from
128/28/61/34/32 to **5/9/7/4/7 instructions**, all still perfect on the unseen test lists.
Simplified versions are saved as new strain versions (`list-sum-v6`..`v10`) whose lineage points
to the originals. The same transfer experiment was then rerun with raw and simplified donors on
**identical fresh seeds** (7000+), 20 runs each. Table: [transfer-simplified.md](transfer-simplified.md),
data: [transfer-simplified.csv](transfer-simplified.csv).

| Starting point → list-max | solved (of 20) | median test accuracy |
|---|---|---|
| from scratch (baseline) | 3 | 0.085 |
| one **raw** sum strain | 0 | 0.053 |
| one **simplified** sum strain | **7** | 0.163 |
| one simplified strain, 50% random start | 2 | 0.205 |
| five **raw** sum strains | 1 | 0.068 |
| five **simplified** sum strains | 5 | **0.818** |
| control: unrelated koza1 strain | 0 | 0.028 |

Head-to-head (Fisher exact on solved counts; Mann–Whitney on the accuracy distributions):

| Comparison | solved | accuracy |
|---|---|---|
| one simplified vs one raw strain | 7 vs 0, **p = 0.008** | **p = 0.032** |
| five simplified vs five raw strains | 5 vs 1, p = 0.18 | **p = 0.007** |
| five simplified strains vs scratch | 5 vs 3, p = 0.70 | **p = 0.021** |
| one simplified strain vs scratch | 7 vs 3, p = 0.27 | p = 0.20 |

### Findings

1. **Simplification is what makes transfer work.** The same donor, simplified, went from 0/20 to
   7/20 on a *different* task (p = 0.008). Raw donors were no better than scratch, sometimes worse.
   Clean code leaves the reusable part (the loop over the list) exposed, so a few mutations can
   repurpose it.
2. **Several simplified parents give the most reliable improvement.** Seeding from all five
   simplified sum strains raised median test accuracy on list-max from 0.085 (scratch) to 0.82
   (p = 0.021), though exact solutions (5/20) were not significantly more frequent than from
   scratch. Several distinct but clean solutions give evolution varied starting points. With raw
   donors, the same multiple-inheritance setup failed (section 4).
3. **Raising the random fraction to 50% did not help** here (2/20). The seeded code is the useful
   part; diluting it costs more than the extra diversity gains.
4. **count_positive is still out of reach** (0/20 for every variant). It needs a new conditional
   inside the loop (`if item > 0`) plus a constant, which is a bigger step than repurposing `+`.
   Stepping-stone strains (e.g. a "count items" strain) or a longer budget are the next things to try.
5. **Practical consequence:** strains are now simplified by default when saved from `evolve`, and
   `strains simplify ID` cleans up older ones.

## 6. Improving the core algorithm: down-sampling nearly triples success

Three generic improvements were added as switches. Budgets are counted in **single case
evaluations** (one program on one case), the fair unit when settings score programs on
different numbers of cases. `--case-budget-gens 100` gives every variant the compute of 100
generations of full evaluation.

- **Down-sampled lexicase** (`downsample`): each generation scores programs on a fresh random
  fraction of the training cases, and the champion is re-judged on all of them. The same
  compute buys many more generations.
- **Constant tuning** (`constant_tuning`): a (1+1) local search on the effective constants of
  the best program(s) each generation, written back into the population.
- **Duplicate cache** (`cache_duplicates`): offspring identical to their mother reuse her scores.

Speed: lexicase selection now works on a case-major score matrix, about 15% faster on problems
with few cases. (A memo for loop/if boundaries in the VM was tried and reverted: no gain.)

### Tuning sweep (seeds 8000+, 7 benchmarks × 20 runs = 140 per setting)

Tables: [improve-sweep.md](improve-sweep.md), [improve-sweep2.md](improve-sweep2.md).

| Setting | solved (of 140) |
|---|---|
| baseline (all cases every generation) | 21 |
| down-sampling 0.5 / 0.25 / **0.1** / 0.05 | 19 / 39 / **57** / 53 |
| constant tuning (top 1 / top 5) | 19 / 18 |
| duplicate cache | 22 (identical results, ~3% fewer evaluations) |
| down-sampling 0.25 / 0.1 / 0.05 + constant tuning | 47 / 48 / **69** |

### Confirmation on fresh seeds (9000+, all 9 benchmarks × 20 runs)

Table: [improve-final.md](improve-final.md), data: [improve-final.csv](improve-final.csv).
Tree GP gets the same budget (100 generations, every case).

| Variant | koza1 | ng5 | ng7 | ng10 | pagie1 | keij6 | list-sum | list-max | list-count+ | **total (of 180)** | formulas (of 120) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline | 6 | 0 | 1 | 2 | 0 | 6 | 4 | 3 | 0 | 22 | 15 |
| tree GP (reference) | 6 | 1 | 6 | 1 | 0 | 8 | – | – | – | – | 22 |
| **down-sampling 0.1** | 11 | 2 | 5 | **12** | 0 | **13** | **12** | **6** | 0 | **61** | 43 |
| down-sampling 0.05 + constant tuning | **14** | 2 | **6** | 9 | 0 | **13** | 11 | 4 | 0 | 59 | **44** |

Pooled Cochran–Mantel–Haenszel tests (stratified by benchmark):

- down-sampling 0.1 vs baseline: **p = 2·10⁻⁷**. vs tree GP on the formula benchmarks: **p = 0.002**.
- down-sampling 0.05 + constant tuning vs baseline: **p = 6·10⁻⁷**. vs tree GP: **p = 0.0009**.

Median test quality also improved on the unsolved problems: pagie1 R² 0.898 → 0.920 (tree GP
0.694); list-max accuracy 0.14 → 0.98.

### Findings

1. **Down-sampling is the single biggest improvement so far.** It nearly triples solved runs at
   equal compute (61 vs 22, p = 2·10⁻⁷). It improves every benchmark that anything solves, and it
   lifts adaptogen from "on par with tree GP" to **significantly better** (43 vs 22 on formulas).
   This matches the program-synthesis literature (Hernandez et al. 2019, "random subsampling
   improves performance in lexicase selection").
2. **Constant tuning adds nothing reliable.** Alone it is slightly worse (it spends compute on
   only the top programs). Combined with down-sampling it was best in tuning (69) but equal to
   plain down-sampling on fresh seeds (59 vs 61). It stays optional and off.
3. **The duplicate cache is correct but small.** Only ~3% of offspring are exact copies of their
   mother. Optional and off.
4. **Very small datasets need a minimum sample.** On the 6-point Kepler data, 10% means one case
   per generation, which hurt (2/8 solved vs 5/8 with all cases, at equal compute). Three cases
   per generation was best (6/8). Hence `downsample_min_cases = 3`.
5. **New defaults:** `downsample = 0.1` (at least 3 cases) and a generation limit of 1000 instead
   of 100, so a default run spends about the same compute as before. Configs that relied on the
   old defaults can set `"downsample": 1`. `adaptogen-compare` pins `downsample = 1` unless a
   variant sets it, so every earlier table in this file stays reproducible.
6. **Still unsolved:** pagie1 and count_positive (0/20 for everything). Both need longer runs or
   further ideas (e.g. stepping-stone strains for count_positive).

## 7. Deriving a new problem from a strain (stage 4 plug-ins)

[examples/plugins/weighted_sum.cpp](../examples/plugins/weighted_sum.cpp) is a plug-in generated with
`adaptogen plugin new weighted_sum --from list-sum-v6`. Its base class fixes the parent strain's
channels (`list.length`, `list.item`) and the plug-in adds `list.weight`. The task is
Σ item × weight, with integer weights 0–5 and test lists up to 2.5× longer than training lists.
"Seeded" runs start from the parent strain (`--seed-parents`); "scratch" runs start from random
programs. Down-sampling 0.1, population 500.

| Budget | from scratch | seeded from parent `list-sum-v6` |
|---|---|---|
| 2,000 generations, 10 runs each (seeds 10000+, [derived.md](derived.md)) | 0/10 solved (median test accuracy 0.000) | 0/10 solved (median 0.005) |
| 5,000 generations, seeds 1–3 | **0/3** solved (training accuracy 0.00–0.03) | **3/3** solved (training accuracy 1.0) |
| 5,000 generations, seeds 1–2 (first try) | – | 2/2 solved (after 1,200 and 4,426 generations; seed 1 also 100% on test) |

**Findings (preliminary, few runs):**

1. **Inheriting from a strain helps on a harder derived task, given enough time.** Every seeded run
   with 5,000 generations solved it (5/5); none of the scratch runs did (0/3). With so few runs
   this is suggestive (Fisher p = 0.1 for 3/3 vs 0/3), not proven. At 2,000 generations neither
   setting solves it.
2. **The solution is new code, not a tweak.** There is no multiply step in the inherited loop to
   repurpose. Evolution nested a second loop that adds each item `weight` times, deferring each
   addition by one step: multiplication by repeated addition. That is valid only because the
   weights are whole numbers, which evolution exploited. The resulting strain `weighted-sum-v1`
   records `list-sum-v6` as its parent (lineage `weighted-sum-v1` ← `list-sum-v6` ← `list-sum-v1`).
3. **Why it takes long:** extending the parent's loop needs new instructions *inside* the loop body,
   but a loop body is "the next N statements". An inserted instruction pushes the last one out of
   the loop. Mutation operators that respect block structure (insertion that grows the enclosing
   body) are a promising next improvement for transfer.
