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

On 4 cores these took about 15 and 40 minutes. The same seeds give identical results on the
same platform.
