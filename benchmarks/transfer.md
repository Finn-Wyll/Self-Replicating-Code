# Results: custom

Budget per run: population 500 x 100 generations = 50000 program evaluations. 20 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table. Seeds start at 6000. List benchmarks hold back 25% of the training cases to choose the final program.

### Algorithms

- **baseline**: stage-1 engine: no mechanisms (crossover + mutation, lexicase)
- **from-sum**: config overlay: {"seeding":{"library":"benchmarks/strains","strains":["list-sum-v1"]}}
- **from-5-sums**: config overlay: {"seeding":{"library":"benchmarks/strains","strains":["list-sum-v1","list-sum-v2","list-sum-v3","list-sum-v4","list-sum-v5"]}}
- **from-sum+retain0.3**: config overlay: {"seeding":{"library":"benchmarks/strains","retain_weight":0.3,"strains":["list-sum-v1"]}}
- **control:from-koza1**: config overlay: {"seeding":{"library":"benchmarks/strains","strains":["koza1-v1"]}}

## list-sum: sum of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 5/20 | - | 0.09 | 0.035 .. 0.995 | 49901 | 4.07 | 40 |
| from-sum | 20/20 | 0.000 | 1 | 1 .. 1 | 500 | 0.03 | 51 |
| from-5-sums | 20/20 | 0.000 | 1 | 1 .. 1 | 500 | 0.04 | 51 |
| from-sum+retain0.3 | 20/20 | 0.000 | 1 | 1 .. 1 | 500 | 0.07 | 51 |
| control:from-koza1 | 5/20 | 1.000 | 0.045 | 0.03 .. 0.08 | 49901 | 3.67 | 36 |

## list-max: max of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 1/20 | - | 0.11 | 0.05 .. 0.27 | 49901 | 4.46 | 44 |
| from-sum | 5/20 | 0.182 | 0.1925 | 0.085 .. 0.69 | 49901 | 4.90 | 43 |
| from-5-sums | 0/20 | 1.000 | 0.0725 | 0.035 .. 0.185 | 49901 | 9.23 | 51 |
| from-sum+retain0.3 | 1/20 | 1.000 | 0.125 | 0.05 .. 0.25 | 49901 | 12.53 | 42 |
| control:from-koza1 | 3/20 | 0.605 | 0.1025 | 0.05 .. 0.89 | 49901 | 3.80 | 50 |

## list-count_positive: count_positive of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.015 | 0 .. 0.03 | 49901 | 5.23 | 38 |
| from-sum | 0/20 | 1.000 | 0.005 | 0 .. 0.02 | 49901 | 4.39 | 42 |
| from-5-sums | 0/20 | 1.000 | 0.035 | 0.01 .. 0.16 | 49901 | 9.55 | 48 |
| from-sum+retain0.3 | 0/20 | 1.000 | 0.01 | 0.005 .. 0.025 | 49901 | 14.78 | 46 |
| control:from-koza1 | 0/20 | 1.000 | 0 | 0 .. 0.015 | 49901 | 3.93 | 48 |

