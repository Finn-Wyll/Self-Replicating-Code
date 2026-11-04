# Results: custom

Budget per run: population 500 x 100 generations = 50000 program evaluations. 20 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table. Seeds start at 8000. Budget: 100 generation-equivalents in **case evaluations** (population x N x training cases); runs may use them for more generations on fewer cases. List benchmarks hold back 25% of the training cases to choose the final program.

### Algorithms

- **baseline**: stage-1 engine: no mechanisms (crossover + mutation, lexicase)
- **downsample-0.5**: config overlay: {"downsample":0.5}
- **downsample-0.25**: config overlay: {"downsample":0.25}
- **downsample-0.1**: config overlay: {"downsample":0.1}
- **const-tuning**: config overlay: {"constant_tuning":true}
- **const-tuning-top5**: config overlay: {"constant_tuning":{"enabled":true,"top":5}}
- **cache**: config overlay: {"cache_duplicates":true}
- **downsample-0.25+const-tuning**: config overlay: {"constant_tuning":true,"downsample":0.25}

## koza1: x^4 + x^3 + x^2 + x   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 7/20 | - | 0.99788 | 0.9943 .. 1 | 49901 | 998020 | 1.36 | 44 |
| downsample-0.5 | 4/20 | 0.480 | 0.99838 | 0.9842 .. 0.9995 | 99500 | 998980 | 2.15 | 44 |
| downsample-0.25 | 8/20 | 1.000 | 0.99932 | 0.9973 .. 1 | 198000 | 997920 | 2.77 | 41 |
| downsample-0.1 | 12/20 | 0.205 | 1 | 0.9997 .. 1 | 219250 | 447270 | 2.45 | 42 |
| const-tuning | 5/20 | 0.731 | 0.99781 | 0.9963 .. 0.9997 | 49675 | 993500 | 1.36 | 40 |
| const-tuning-top5 | 7/20 | 1.000 | 0.9967 | 0.9826 .. 1 | 49548 | 990960 | 1.24 | 44 |
| cache | 8/20 | 1.000 | 0.99791 | 0.9943 .. 1 | 49034 | 980690 | 1.44 | 44 |
| downsample-0.25+const-tuning | 9/20 | 0.748 | 0.99893 | 0.9924 .. 1 | 198000 | 997620 | 2.81 | 34 |

## nguyen5: sin(x^2) cos(x) - 1   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.98857 | 0.9588 .. 0.9966 | 49901 | 998020 | 1.50 | 40 |
| downsample-0.5 | 0/20 | 1.000 | 0.98632 | 0.9524 .. 0.9914 | 99500 | 998980 | 2.16 | 46 |
| downsample-0.25 | 2/20 | 0.487 | 0.99615 | 0.9717 .. 0.9991 | 198000 | 997920 | 3.03 | 42 |
| downsample-0.1 | 2/20 | 0.487 | 0.99779 | 0.974 .. 0.9991 | 490000 | 999600 | 5.80 | 36 |
| const-tuning | 0/20 | 1.000 | 0.98703 | 0.9621 .. 0.9983 | 49755 | 995100 | 1.47 | 37 |
| const-tuning-top5 | 1/20 | 1.000 | 0.98777 | 0.9588 .. 0.9913 | 49678 | 993560 | 1.38 | 36 |
| cache | 0/20 | 1.000 | 0.98865 | 0.9588 .. 0.9973 | 49714 | 994290 | 1.59 | 40 |
| downsample-0.25+const-tuning | 0/20 | 1.000 | 0.99834 | 0.9723 .. 0.9993 | 198120 | 998220 | 2.90 | 40 |

## nguyen7: ln(x+1) + ln(x^2+1)   (x in [0,2])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 2/20 | - | 0.99932 | 0.9991 .. 0.9997 | 49901 | 998020 | 1.48 | 46 |
| downsample-0.5 | 0/20 | 0.487 | 0.99932 | 0.9988 .. 0.9995 | 99500 | 998980 | 2.31 | 46 |
| downsample-0.25 | 3/20 | 1.000 | 0.99952 | 0.9984 .. 0.9999 | 198000 | 997920 | 2.92 | 38 |
| downsample-0.1 | 1/20 | 1.000 | 0.99966 | 0.9996 .. 0.9998 | 490000 | 999600 | 5.40 | 42 |
| const-tuning | 1/20 | 1.000 | 0.99955 | 0.9993 .. 0.9997 | 49745 | 994900 | 1.52 | 40 |
| const-tuning-top5 | 0/20 | 0.487 | 0.99956 | 0.9993 .. 0.9997 | 49907 | 998140 | 1.29 | 38 |
| cache | 2/20 | 1.000 | 0.99941 | 0.9991 .. 0.9998 | 49676 | 993520 | 1.62 | 46 |
| downsample-0.25+const-tuning | 5/20 | 0.407 | 0.99972 | 0.9994 .. 0.9999 | 198010 | 997670 | 2.67 | 42 |

## nguyen10: 2 sin(x) cos(y)   (x, y in [0,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 2/20 | - | 0.99719 | 0.9893 .. 0.999 | 49901 | 4990100 | 4.69 | 37 |
| downsample-0.5 | 3/20 | 1.000 | 0.99898 | 0.9956 .. 0.9995 | 99500 | 4994900 | 5.72 | 42 |
| downsample-0.25 | 4/20 | 0.661 | 0.99803 | 0.9959 .. 0.9999 | 198000 | 4989600 | 7.52 | 40 |
| downsample-0.1 | 9/20 | 0.031 | 0.99904 | 0.9969 .. 1 | 490000 | 4998000 | 11.96 | 36 |
| const-tuning | 2/20 | 1.000 | 0.99717 | 0.9899 .. 0.9991 | 49745 | 4974500 | 4.41 | 33 |
| const-tuning-top5 | 2/20 | 1.000 | 0.99351 | 0.9877 .. 0.9985 | 49700 | 4969950 | 4.25 | 40 |
| cache | 2/20 | 1.000 | 0.99716 | 0.9906 .. 0.9989 | 49678 | 4967800 | 4.94 | 37 |
| downsample-0.25+const-tuning | 8/20 | 0.065 | 0.9997 | 0.9975 .. 1 | 198080 | 4990100 | 7.25 | 36 |

## keijzer6: sum_{i=1..x} 1/i   (train x=1..50, test x=1..120: extrapolation)

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 6/20 | - | 0.99947 | 0.9807 .. 0.9999 | 49901 | 2495050 | 3.71 | 42 |
| downsample-0.5 | 5/20 | 1.000 | 0.99945 | 0.9941 .. 0.9999 | 99500 | 2497450 | 5.09 | 39 |
| downsample-0.25 | 7/20 | 1.000 | 0.99976 | 0.9986 .. 1 | 190500 | 2495550 | 6.32 | 47 |
| downsample-0.1 | 12/20 | 0.111 | 0.99993 | 0.9993 .. 1 | 335250 | 1709775 | 6.93 | 30 |
| const-tuning | 4/20 | 0.716 | 0.98972 | 0.898 .. 0.9998 | 49745 | 2487250 | 3.31 | 42 |
| const-tuning-top5 | 4/20 | 0.716 | 0.99911 | 0.9974 .. 0.9998 | 49702 | 2485125 | 3.21 | 46 |
| cache | 6/20 | 1.000 | 0.99948 | 0.9807 .. 0.9999 | 49686 | 2484300 | 3.35 | 43 |
| downsample-0.25+const-tuning | 10/20 | 0.333 | 0.9999 | 0.9978 .. 1 | 120580 | 1579140 | 3.84 | 32 |

## list-sum: sum of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 3/20 | - | 0.0375 | 0.02 .. 0.055 | 66368 | 4980925 | 5.31 | 38 |
| downsample-0.5 | 6/20 | 0.451 | 0.075 | 0.035 .. 1 | 130500 | 4985100 | 6.15 | 35 |
| downsample-0.25 | 9/20 | 0.082 | 0.6525 | 0.075 .. 1 | 260000 | 4992000 | 7.78 | 40 |
| downsample-0.1 | 14/20 | 0.001 | 1 | 0.99 .. 1 | 346250 | 2839250 | 6.88 | 22 |
| const-tuning | 3/20 | 1.000 | 0.035 | 0.02 .. 0.04 | 66222 | 4969900 | 6.01 | 28 |
| const-tuning-top5 | 2/20 | 1.000 | 0.035 | 0.01 .. 0.055 | 66320 | 4976750 | 5.05 | 48 |
| cache | 3/20 | 1.000 | 0.0375 | 0.02 .. 0.06 | 66375 | 4981612 | 6.12 | 40 |
| downsample-0.25+const-tuning | 13/20 | 0.003 | 1 | 0.07 .. 1 | 197830 | 3797670 | 6.26 | 29 |

## list-max: max of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median case evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|---|
| baseline | 1/20 | - | 0.11 | 0.04 .. 0.285 | 66368 | 4980925 | 6.23 | 54 |
| downsample-0.5 | 1/20 | 1.000 | 0.35 | 0.11 .. 0.835 | 130500 | 4985100 | 7.36 | 54 |
| downsample-0.25 | 6/20 | 0.091 | 0.1075 | 0.035 .. 1 | 260000 | 4992000 | 8.50 | 36 |
| downsample-0.1 | 7/20 | 0.044 | 0.77 | 0.14 .. 1 | 609500 | 4997900 | 10.56 | 34 |
| const-tuning | 4/20 | 0.342 | 0.1175 | 0.045 .. 0.31 | 66433 | 4985675 | 5.34 | 50 |
| const-tuning-top5 | 2/20 | 1.000 | 0.0975 | 0.065 .. 0.53 | 66450 | 4986525 | 5.34 | 48 |
| cache | 1/20 | 1.000 | 0.12 | 0.04 .. 0.335 | 66334 | 4978550 | 6.68 | 50 |
| downsample-0.25+const-tuning | 2/20 | 1.000 | 0.5975 | 0.07 .. 0.98 | 260420 | 4998080 | 8.72 | 46 |

