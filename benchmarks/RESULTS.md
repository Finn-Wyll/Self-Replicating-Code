# Stage 1 comparison: adaptogen vs established algorithms

Raw per-run data: [results.csv](results.csv). Full tables with spreads and timings:
[results.md](results.md).

## Setup

- **Problems:** six standard symbolic-regression benchmarks from the GP literature (Koza-1,
  Nguyen-5/7/10, Pagie-1, Keijzer-6). Keijzer-6 trains on x = 1..50 and tests on x = 1..120, so
  it measures **extrapolation**.
- **Budget:** 50,000 program evaluations per run (population 500 × 100 generations). Random
  search and hill climbing get the same number of evaluations. Runs stop early when the
  training data is solved.
- **20 independent runs** per cell; each run samples its own training data.
- **Solved:** RMSE on the **unseen test set** below 1% of the target's standard deviation
  (R² > 0.9999), i.e. essentially exact recovery.
- adaptogen at stage 1 is the core engine only. **None of the bacteria-inspired mechanisms are in
  yet** (they are stage 2), so this measures the baseline that stage 2 has to beat.

| Algorithm | What it is |
|---|---|
| **adaptogen** | Linear programs on the VM, full default instruction set (incl. `if`, loops, min/max/abs/sqrt), lexicase selection |
| **adaptogen-classic** | Same, but restricted to the tree-GP function set {+, −, ×, ÷, sin, cos, exp, log} |
| **adaptogen-tournament** | Full instruction set, tournament selection instead of lexicase |
| **tree-gp** | Classic Koza-style GP: expression trees, ramped half-and-half, subtree crossover/mutation, tournament 7 |
| **hill-climber** | (1+1) mutation-only search on linear programs with restarts |
| **random-search** | Random programs, keep the best |
| **polyfit** | Polynomial least squares, degree chosen on a hold-out split (not evolutionary) |

## Results: solved runs (of 20) and median test R²

| Algorithm | koza1 | nguyen5 | nguyen7 | nguyen10 | pagie1 | keijzer6 (extrapolation) | **total solved** |
|---|---|---|---|---|---|---|---|
| adaptogen | 4 · 0.9988 | 0 · 0.9940 | 2 · 0.9995 | **6** · 0.9957 | 0 · **0.871** | 5 · 0.9990 | 17 / 120 |
| adaptogen-classic | 4 · 0.9985 | 0 · 0.9674 | 2 · 0.9994 | 3 · 0.9979 | 0 · 0.755 | **17** · 0.99997 | **28 / 120** |
| adaptogen-tournament | 4 · 0.9973 | 1 · 0.9855 | 0 · 0.9988 | 0 · 0.9892 | 0 · 0.816 | 4 · 0.9993 | 9 / 120 |
| tree-gp | **6** · 0.9997 | 0 · 0.9949 | **4** · 0.9995 | 5 · 0.9991 | 0 · 0.750 | 10 · 0.9999 | 25 / 120 |
| hill-climber | 2 · 0.9956 | 0 · 0.9867 | 2 · 0.9987 | 3 · 0.9892 | 0 · 0.805 | 9 · 0.9998 | 16 / 120 |
| random-search | 0 · 0.910 | 0 · 0.766 | 0 · 0.995 | 0 · 0.869 | 0 · 0.319 | 0 · 0.986 | 0 / 120 |
| polyfit | **20** · 1.0000 | **20** · 1.0000 | **20** · 1.0000 | **20** · 1.0000 | 0 · **0.983** | 0 · ≪ 0 (diverges) | 80 / 120 |

## What this says (honestly)

1. **The engine works and is competitive with standard GP.** Restricted to the same function
   set, adaptogen solved 28/120 runs against tree GP's 25/120. It is far ahead of random search
   (0/120) and hill climbing (16/120), which shows that selection and recombination are doing
   real work. With 20 runs per cell, differences of one to three solved runs are **not
   statistically meaningful**. Read this as "on par", not "better".

2. **Generality has a price.** The full instruction set (branches, loops, min/max, ...) solved
   fewer runs than the restricted one (17 vs 28) because it searches a much larger space of
   programs. That is the trade-off DESIGN.md anticipated: minimal prior knowledge means more
   searching. On the hardest problem (pagie1) the full set gave the **best** median R² of every
   evolutionary method (0.871 vs 0.750 for tree GP), but nothing solved it exactly.

3. **Lexicase selection beats tournament selection** for adaptogen on 5 of 6 problems
   (17 vs 9 solved). It stays the default.

4. **Use the right tool.** When the answer is a smooth function inside the training range,
   ordinary polynomial least squares wins outright and instantly (80/120). Evolution is not
   needed for that. But polyfit **fails badly outside its training range** (Keijzer-6 test
   R² ≪ 0), while evolved programs found formulas that extrapolate (17/20 for
   adaptogen-classic, e.g. a logarithm, which is the true asymptotic form of the harmonic
   series). Evolved programs are also readable laws, not opaque coefficient vectors (see the
   Kepler example in the README).

5. **Loops did not help on Keijzer-6 yet.** The harmonic sum is a natural loop, but evolution
   preferred the log approximation. Discovering an exact loop is a longer stepping-stone path.
   It is a good test case for the stage-2 mechanisms (gene duplication, stress-induced
   mutagenesis).

6. **Speed.** Tree GP is roughly 2–10× faster in wall-clock time per run on these problems.
   adaptogen pays for effective-code analysis on every mutation and for a more general VM.
   There is room to optimise later (caching effective masks, specialised fast paths).

## What stage 2 has to show

The point of the project is whether the **bacteria-inspired mechanisms** improve on this
baseline: stress-induced mutagenesis with heritable mutation rates, horizontal transfer, gene
duplication, and spatial structure with a curriculum. Stage 2 will add them as switches and
rerun exactly this comparison as an ablation ("does mechanism X help on problem Y?"), plus
non-regression problems (list algorithms, control, scheduling) where loops and branches matter.

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
./build/adaptogen-compare --runs 20 --out benchmarks/results
```

Same seeds give identical results on the same platform. The full run took about 16 minutes on
4 cores.
