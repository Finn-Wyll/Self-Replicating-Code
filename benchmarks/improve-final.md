# Results: custom

Budget per run: population 500 x 100 generations = 50000 program evaluations. 20 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table. Seeds start at 9000. Budget: 100 generation-equivalents in **case evaluations** (population x N x training cases); runs may use them for more generations on fewer cases. List benchmarks hold back 25% of the training cases to choose the final program.

### Algorithms

- **baseline**: stage-1 engine: no mechanisms (crossover + mutation, lexicase)
- **tree-gp**: reference: Koza-style tree GP
- **downsample-0.1**: config overlay: {"downsample":0.1}
- **downsample-0.05+const-tuning**: config overlay: {"constant_tuning":true,"downsample":0.05}

## koza1: x^4 + x^3 + x^2 + x   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 6/20 | - | 0.99911 | 0.9959 .. 1 | 49901 | 998020 | 1.58 | 40 |
| tree-gp | 6/20 | 1.000 | 0.99908 | 0.997 .. 0.9999 | 17716 | 354310 | 0.09 | 52 |
| downsample-0.1 | 11/20 | 0.200 | 1 | 0.9973 .. 1 | 78500 | 160140 | 0.83 | 30 |
| downsample-0.05+const-tuning | 14/20 | 0.026 | 1 | 0.9996 .. 1 | 160890 | 167200 | 1.25 | 31 |

## nguyen5: sin(x^2) cos(x) - 1   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.97979 | 0.9584 .. 0.9964 | 49901 | 998020 | 1.67 | 40 |
| tree-gp | 1/20 | 1.000 | 0.9804 | 0.8859 .. 0.9966 | 49901 | 998020 | 0.43 | 66 |
| downsample-0.1 | 2/20 | 0.487 | 0.99761 | 0.9945 .. 0.9989 | 490000 | 999600 | 6.19 | 40 |
| downsample-0.05+const-tuning | 2/20 | 0.487 | 0.99681 | 0.9929 .. 0.9985 | 962270 | 999690 | 10.46 | 36 |

## nguyen7: ln(x+1) + ln(x^2+1)   (x in [0,2])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 1/20 | - | 0.9993 | 0.9991 .. 0.9996 | 49901 | 998020 | 1.72 | 40 |
| tree-gp | 6/20 | 0.091 | 0.99978 | 0.9995 .. 0.9999 | 17216 | 344330 | 0.05 | 42 |
| downsample-0.1 | 5/20 | 0.182 | 0.99953 | 0.9988 .. 0.9999 | 490000 | 999600 | 5.78 | 34 |
| downsample-0.05+const-tuning | 6/20 | 0.091 | 0.99977 | 0.9993 .. 0.9999 | 962420 | 999800 | 10.55 | 36 |

## nguyen10: 2 sin(x) cos(y)   (x, y in [0,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 2/20 | - | 0.9969 | 0.9905 .. 0.9993 | 49901 | 4990100 | 5.94 | 43 |
| tree-gp | 1/20 | 1.000 | 0.99718 | 0.9957 .. 0.9986 | 49901 | 4990100 | 1.71 | 65 |
| downsample-0.1 | 12/20 | 0.002 | 0.99992 | 0.9988 .. 1 | 409750 | 4179450 | 11.07 | 45 |
| downsample-0.05+const-tuning | 9/20 | 0.031 | 0.9996 | 0.9986 .. 1 | 962370 | 4997700 | 17.55 | 32 |

## pagie1: 1/(1+x^-4) + 1/(1+y^-4)   (x, y in [-5,5])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.89846 | 0.8717 .. 0.9165 | 49901 | 33733076 | 25.19 | 50 |
| tree-gp | 0/20 | 1.000 | 0.69441 | 0.5281 .. 0.7739 | 49901 | 33733076 | 12.74 | 80 |
| downsample-0.1 | 0/20 | 1.000 | 0.92002 | 0.8461 .. 0.939 | 487000 | 33774424 | 36.84 | 51 |
| downsample-0.05+const-tuning | 0/20 | 1.000 | 0.92794 | 0.9176 .. 0.949 | 957270 | 33792036 | 44.89 | 40 |

## keijzer6: sum_{i=1..x} 1/i   (train x=1..50, test x=1..120: extrapolation)

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 6/20 | - | 0.99892 | 0.9721 .. 0.9999 | 49901 | 2495050 | 3.02 | 50 |
| tree-gp | 8/20 | 0.741 | 0.99983 | 0.9996 .. 0.9999 | 10480 | 524000 | 0.08 | 44 |
| downsample-0.1 | 13/20 | 0.056 | 0.99996 | 0.9997 .. 1 | 158750 | 809625 | 2.53 | 36 |
| downsample-0.05+const-tuning | 13/20 | 0.056 | 0.99994 | 0.9998 .. 1 | 205460 | 636155 | 2.69 | 38 |

## list-sum: sum of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 4/20 | - | 0.055 | 0.02 .. 0.085 | 66368 | 4980925 | 4.91 | 36 |
| downsample-0.1 | 12/20 | 0.022 | 1 | 0.085 .. 1 | 351750 | 2884350 | 6.02 | 22 |
| downsample-0.05+const-tuning | 11/20 | 0.048 | 1 | 0.04 .. 1 | 827460 | 3471790 | 8.71 | 20 |

## list-max: max of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 3/20 | - | 0.1425 | 0.03 .. 0.96 | 66368 | 4980925 | 4.53 | 38 |
| downsample-0.1 | 6/20 | 0.451 | 0.9775 | 0.135 .. 1 | 597000 | 4895400 | 10.76 | 49 |
| downsample-0.05+const-tuning | 4/20 | 1.000 | 0.99 | 0.3 .. 0.995 | 1191880 | 4998440 | 16.17 | 36 |

## list-count_positive: count_positive of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.0075 | 0 .. 0.015 | 66368 | 4980925 | 6.14 | 51 |
| downsample-0.1 | 0/20 | 1.000 | 0 | 0 .. 0.01 | 609500 | 4997900 | 11.10 | 38 |
| downsample-0.05+const-tuning | 0/20 | 1.000 | 0.015 | 0 .. 0.105 | 1191600 | 4999240 | 14.87 | 26 |

