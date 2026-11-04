# Results: custom

Budget per run: population 500 x 2000 generations = 1000000 program evaluations. 10 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table. Seeds start at 10000. List benchmarks hold back 25% of the training cases to choose the final program. Common config overlay: `{"downsample":0.1}`.

### Algorithms

- **baseline**: stage-1 engine: no mechanisms (crossover + mutation, lexicase)
- **from-parent-list-sum**: config overlay: {"seeding":{"library":"benchmarks/strains","strains":["list-sum-v6"]}}

## weighted-sum: Weighted sum of a list (sum of item * weight); tested on lists up to 2.5x longer than in training

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 0/10 | - | 0 | 0 .. 0.005 | 1000000 | 8200000 | 152.44 | 44 |
| from-parent-list-sum | 0/10 | 1.000 | 0.005 | 0 .. 0.005 | 1000000 | 8200000 | 171.39 | 46 |

