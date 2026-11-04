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
