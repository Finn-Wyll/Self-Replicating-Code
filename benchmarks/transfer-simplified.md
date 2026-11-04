# Results: custom

Budget per run: population 500 x 100 generations = 50000 program evaluations. 20 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table. Seeds start at 7000. List benchmarks hold back 25% of the training cases to choose the final program.

### Algorithms

- **baseline**: stage-1 engine: no mechanisms (crossover + mutation, lexicase)
- **raw-sum**: config overlay: {"seeding":{"library":"benchmarks/strains","strains":["list-sum-v1"]}}
- **simplified-sum**: config overlay: {"seeding":{"library":"benchmarks/strains","strains":["list-sum-v6"]}}
- **simplified-sum,random0.5**: config overlay: {"seeding":{"library":"benchmarks/strains","random_fraction":0.5,"strains":["list-sum-v6"]}}
- **5-raw-sums**: config overlay: {"seeding":{"library":"benchmarks/strains","strains":["list-sum-v1","list-sum-v2","list-sum-v3","list-sum-v4","list-sum-v5"]}}
- **5-simplified-sums**: config overlay: {"seeding":{"library":"benchmarks/strains","strains":["list-sum-v6","list-sum-v7","list-sum-v8","list-sum-v9","list-sum-v10"]}}
- **control:from-koza1**: config overlay: {"seeding":{"library":"benchmarks/strains","strains":["koza1-v1"]}}

## list-max: max of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 3/20 | - | 0.085 | 0.03 .. 0.26 | 49901 | 4.85 | 34 |
| raw-sum | 0/20 | 0.231 | 0.0525 | 0.025 .. 0.18 | 49901 | 4.97 | 42 |
| simplified-sum | 7/20 | 0.273 | 0.1625 | 0.035 .. 1 | 49901 | 6.66 | 30 |
| simplified-sum,random0.5 | 2/20 | 1.000 | 0.205 | 0.03 .. 0.685 | 49901 | 5.96 | 45 |
| 5-raw-sums | 1/20 | 0.605 | 0.0675 | 0.025 .. 0.2 | 49901 | 7.85 | 46 |
| 5-simplified-sums | 5/20 | 0.695 | 0.8175 | 0.21 .. 0.995 | 49901 | 5.49 | 50 |
| control:from-koza1 | 0/20 | 0.231 | 0.0275 | 0 .. 0.165 | 49901 | 3.86 | 52 |

## list-count_positive: count_positive of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.0075 | 0 .. 0.055 | 49901 | 4.82 | 39 |
| raw-sum | 0/20 | 1.000 | 0.005 | 0 .. 0.025 | 49901 | 5.02 | 45 |
| simplified-sum | 0/20 | 1.000 | 0.0375 | 0.015 .. 0.115 | 49901 | 13.89 | 56 |
| simplified-sum,random0.5 | 0/20 | 1.000 | 0.0325 | 0.02 .. 0.11 | 49901 | 9.60 | 44 |
| 5-raw-sums | 0/20 | 1.000 | 0.05 | 0.025 .. 0.125 | 49901 | 8.97 | 47 |
| 5-simplified-sums | 0/20 | 1.000 | 0.0225 | 0 .. 0.085 | 49901 | 9.55 | 58 |
| control:from-koza1 | 0/20 | 1.000 | 0.015 | 0 .. 0.02 | 49901 | 4.03 | 48 |

