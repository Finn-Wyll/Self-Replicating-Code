# Results: custom

Budget per run: population 500 x 100 generations = 50000 program evaluations. 20 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table. Seeds start at 8000. Budget: 100 generation-equivalents in **case evaluations** (population x N x training cases); runs may use them for more generations on fewer cases. List benchmarks hold back 25% of the training cases to choose the final program.

### Algorithms

- **baseline**: stage-1 engine: no mechanisms (crossover + mutation, lexicase)
- **downsample-0.05**: config overlay: {"downsample":0.05}
- **downsample-0.1+const-tuning**: config overlay: {"constant_tuning":true,"downsample":0.1}
- **downsample-0.05+const-tuning**: config overlay: {"constant_tuning":true,"downsample":0.05}

## koza1: x^4 + x^3 + x^2 + x   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 7/20 | - | 0.99788 | 0.9943 .. 1 | 49901 | 998020 | 1.35 | 44 |
| downsample-0.05 | 14/20 | 0.056 | 1 | 0.9991 .. 1 | 290750 | 302380 | 2.52 | 32 |
| downsample-0.1+const-tuning | 9/20 | 0.748 | 0.99981 | 0.9982 .. 1 | 437850 | 892660 | 4.31 | 38 |
| downsample-0.05+const-tuning | 18/20 | 0.001 | 1 | 1 .. 1 | 146400 | 152110 | 0.84 | 22 |

## nguyen5: sin(x^2) cos(x) - 1   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.98857 | 0.9588 .. 0.9966 | 49901 | 998020 | 1.48 | 40 |
| downsample-0.05 | 0/20 | 1.000 | 0.99728 | 0.9946 .. 0.9993 | 961500 | 999960 | 9.37 | 40 |
| downsample-0.1+const-tuning | 2/20 | 0.487 | 0.99773 | 0.9852 .. 0.9993 | 490310 | 999480 | 5.34 | 36 |
| downsample-0.05+const-tuning | 3/20 | 0.231 | 0.99919 | 0.9979 .. 0.9997 | 962320 | 999670 | 8.72 | 39 |

## nguyen7: ln(x+1) + ln(x^2+1)   (x in [0,2])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 2/20 | - | 0.99932 | 0.9991 .. 0.9997 | 49901 | 998020 | 1.46 | 46 |
| downsample-0.05 | 3/20 | 1.000 | 0.99971 | 0.9996 .. 0.9999 | 961500 | 999960 | 9.23 | 47 |
| downsample-0.1+const-tuning | 1/20 | 1.000 | 0.9996 | 0.9993 .. 0.9998 | 490190 | 999280 | 4.92 | 50 |
| downsample-0.05+const-tuning | 5/20 | 0.407 | 0.9997 | 0.9995 .. 0.9999 | 962190 | 999530 | 7.77 | 46 |

## nguyen10: 2 sin(x) cos(y)   (x, y in [0,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 2/20 | - | 0.99719 | 0.9893 .. 0.999 | 49901 | 4990100 | 4.74 | 37 |
| downsample-0.05 | 8/20 | 0.065 | 0.99973 | 0.9976 .. 1 | 961500 | 4999800 | 15.65 | 46 |
| downsample-0.1+const-tuning | 6/20 | 0.235 | 0.99879 | 0.9971 .. 0.9999 | 490170 | 4996500 | 11.49 | 39 |
| downsample-0.05+const-tuning | 9/20 | 0.031 | 0.99979 | 0.9992 .. 1 | 962210 | 4997800 | 14.25 | 41 |

## keijzer6: sum_{i=1..x} 1/i   (train x=1..50, test x=1..120: extrapolation)

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 6/20 | - | 0.99947 | 0.9807 .. 0.9999 | 49901 | 2495050 | 3.30 | 42 |
| downsample-0.05 | 11/20 | 0.200 | 0.99994 | 0.9995 .. 1 | 271750 | 842425 | 4.37 | 37 |
| downsample-0.1+const-tuning | 13/20 | 0.056 | 0.99993 | 0.9987 .. 1 | 209130 | 1065775 | 3.70 | 35 |
| downsample-0.05+const-tuning | 16/20 | 0.004 | 0.99998 | 1 .. 1 | 92500 | 286400 | 1.37 | 28 |

## list-sum: sum of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 3/20 | - | 0.0375 | 0.02 .. 0.055 | 66368 | 4980925 | 5.98 | 38 |
| downsample-0.05 | 13/20 | 0.003 | 1 | 0.08 .. 1 | 874250 | 3671850 | 12.89 | 18 |
| downsample-0.1+const-tuning | 12/20 | 0.008 | 1 | 0.605 .. 1 | 111820 | 916110 | 1.95 | 22 |
| downsample-0.05+const-tuning | 13/20 | 0.003 | 1 | 0.065 .. 1 | 413160 | 1733590 | 4.76 | 22 |

## list-max: max of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 1/20 | - | 0.11 | 0.04 .. 0.285 | 66368 | 4980925 | 6.59 | 54 |
| downsample-0.05 | 4/20 | 0.342 | 0.5725 | 0.15 .. 0.99 | 1122000 | 4712400 | 19.65 | 43 |
| downsample-0.1+const-tuning | 5/20 | 0.182 | 0.5575 | 0.065 .. 0.985 | 609900 | 4996830 | 13.29 | 39 |
| downsample-0.05+const-tuning | 5/20 | 0.182 | 0.785 | 0.07 .. 0.995 | 1191820 | 4998470 | 16.32 | 30 |

