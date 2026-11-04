# Results: mechanisms

Budget per run: population 500 x 100 generations = 50000 program evaluations. 20 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table.

### Algorithms

- **baseline**: stage-1 engine: no mechanisms (crossover + mutation, lexicase)
- **+stress**: stress-induced mutagenesis in stagnating lineages
- **+hypermutation**: heritable, evolving mutation rates
- **+transfer**: horizontal transfer of code segments
- **+duplication**: gene duplication
- **+spatial**: population on a torus with local competition
- **+spatial+curriculum**: spatial + MEGA-plate difficulty bands
- **all**: all six mechanisms, crossover kept
- **bacterial**: all six mechanisms, no crossover (asexual + horizontal transfer, like real bacteria)
- **tree-gp**: reference: Koza-style tree GP

## koza1: x^4 + x^3 + x^2 + x   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 5/20 | - | 0.99875 | 0.9931 .. 0.9997 | 49901 | 1.50 | 44 |
| +stress | 9/20 | 0.320 | 0.99937 | 0.9976 .. 1 | 45909 | 1.66 | 35 |
| +hypermutation | 4/20 | 1.000 | 0.99712 | 0.9886 .. 0.9997 | 49901 | 1.46 | 38 |
| +transfer | 4/20 | 1.000 | 0.99581 | 0.9933 .. 0.999 | 49901 | 1.77 | 45 |
| +duplication | 9/20 | 0.320 | 0.99873 | 0.9903 .. 1 | 49901 | 1.49 | 40 |
| +spatial | 7/20 | 0.731 | 0.99761 | 0.9878 .. 1 | 50600 | 0.56 | 36 |
| +spatial+curriculum | 3/20 | 0.695 | 0.99279 | 0.9287 .. 0.9976 | 50600 | 0.64 | 34 |
| all | 5/20 | 1.000 | 0.99559 | 0.9918 .. 0.9998 | 50600 | 0.98 | 36 |
| bacterial | 4/20 | 1.000 | 0.99388 | 0.9735 .. 0.9994 | 50600 | 0.74 | 20 |
| tree-gp | 6/20 | 1.000 | 0.9997 | 0.9983 .. 0.9999 | 15470 | 0.07 | 50 |

## nguyen5: sin(x^2) cos(x) - 1   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.98433 | 0.9662 .. 0.9939 | 49901 | 1.84 | 40 |
| +stress | 1/20 | 1.000 | 0.98575 | 0.9655 .. 0.9953 | 49901 | 2.07 | 44 |
| +hypermutation | 0/20 | 1.000 | 0.96961 | 0.9567 .. 0.9914 | 49901 | 1.45 | 32 |
| +transfer | 1/20 | 1.000 | 0.98546 | 0.9284 .. 0.9954 | 49901 | 1.90 | 43 |
| +duplication | 1/20 | 1.000 | 0.98524 | 0.9544 .. 0.9975 | 49901 | 1.73 | 40 |
| +spatial | 0/20 | 1.000 | 0.98401 | 0.9674 .. 0.9961 | 50600 | 0.60 | 30 |
| +spatial+curriculum | 0/20 | 1.000 | 0.95682 | 0.86 .. 0.9805 | 50600 | 0.60 | 34 |
| all | 0/20 | 1.000 | 0.97716 | 0.9508 .. 0.9873 | 50600 | 0.92 | 28 |
| bacterial | 0/20 | 1.000 | 0.97598 | 0.9475 .. 0.9874 | 50600 | 0.66 | 26 |
| tree-gp | 0/20 | 1.000 | 0.99489 | 0.9569 .. 0.9985 | 49901 | 0.31 | 47 |

## nguyen7: ln(x+1) + ln(x^2+1)   (x in [0,2])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 2/20 | - | 0.99929 | 0.999 .. 0.9996 | 49901 | 1.71 | 40 |
| +stress | 0/20 | 0.487 | 0.99939 | 0.9987 .. 0.9997 | 49901 | 1.99 | 38 |
| +hypermutation | 1/20 | 1.000 | 0.99933 | 0.9986 .. 0.9996 | 49901 | 1.43 | 38 |
| +transfer | 1/20 | 1.000 | 0.99921 | 0.9985 .. 0.9994 | 49901 | 1.70 | 40 |
| +duplication | 0/20 | 0.487 | 0.99943 | 0.9965 .. 0.9997 | 49901 | 1.90 | 46 |
| +spatial | 1/20 | 1.000 | 0.99915 | 0.9988 .. 0.9996 | 50600 | 0.65 | 31 |
| +spatial+curriculum | 0/20 | 0.487 | 0.99888 | 0.9981 .. 0.999 | 50600 | 0.73 | 34 |
| all | 0/20 | 0.487 | 0.99881 | 0.9979 .. 0.9994 | 50600 | 1.14 | 30 |
| bacterial | 0/20 | 0.487 | 0.99823 | 0.9966 .. 0.9991 | 50600 | 0.71 | 32 |
| tree-gp | 4/20 | 0.661 | 0.99953 | 0.9994 .. 0.9999 | 49901 | 0.13 | 38 |

## nguyen10: 2 sin(x) cos(y)   (x, y in [0,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 5/20 | - | 0.99762 | 0.9905 .. 0.9999 | 49901 | 5.04 | 37 |
| +stress | 4/20 | 1.000 | 0.99723 | 0.9928 .. 0.9995 | 49901 | 5.66 | 34 |
| +hypermutation | 6/20 | 1.000 | 0.99833 | 0.9926 .. 0.9999 | 49901 | 4.20 | 34 |
| +transfer | 3/20 | 0.695 | 0.99727 | 0.9951 .. 0.999 | 49901 | 5.20 | 48 |
| +duplication | 3/20 | 0.695 | 0.99798 | 0.9881 .. 0.999 | 49901 | 5.53 | 36 |
| +spatial | 1/20 | 0.182 | 0.99041 | 0.9836 .. 0.9974 | 50600 | 2.17 | 46 |
| +spatial+curriculum | 0/20 | 0.047 | 0.97435 | 0.9411 .. 0.9916 | 50600 | 1.89 | 32 |
| all | 5/20 | 1.000 | 0.99066 | 0.9819 .. 0.9999 | 50600 | 2.62 | 26 |
| bacterial | 1/20 | 0.182 | 0.98326 | 0.9581 .. 0.989 | 50600 | 1.69 | 22 |
| tree-gp | 5/20 | 1.000 | 0.99905 | 0.9973 .. 0.9999 | 49901 | 1.73 | 78 |

## pagie1: 1/(1+x^-4) + 1/(1+y^-4)   (x, y in [-5,5])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.89208 | 0.87 .. 0.9157 | 49901 | 24.95 | 51 |
| +stress | 0/20 | 1.000 | 0.88329 | 0.8641 .. 0.9069 | 49901 | 25.53 | 52 |
| +hypermutation | 0/20 | 1.000 | 0.85624 | 0.7175 .. 0.9079 | 49901 | 25.05 | 50 |
| +transfer | 0/20 | 1.000 | 0.89033 | 0.8463 .. 0.9139 | 49901 | 24.54 | 48 |
| +duplication | 0/20 | 1.000 | 0.8865 | 0.8355 .. 0.9098 | 49901 | 23.95 | 55 |
| +spatial | 0/20 | 1.000 | 0.82319 | 0.808 .. 0.8734 | 50600 | 13.28 | 42 |
| +spatial+curriculum | 0/20 | 1.000 | 0.78379 | 0.7581 .. 0.7952 | 50600 | 11.19 | 30 |
| all | 0/20 | 1.000 | 0.79423 | 0.7584 .. 0.8269 | 50600 | 17.28 | 46 |
| bacterial | 0/20 | 1.000 | 0.76611 | 0.6408 .. 0.7904 | 50600 | 10.09 | 24 |
| tree-gp | 0/20 | 1.000 | 0.75045 | 0.6193 .. 0.8025 | 49901 | 12.75 | 90 |

## keijzer6: sum_{i=1..x} 1/i   (train x=1..50, test x=1..120: extrapolation)

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 6/20 | - | 0.99952 | 0.9976 .. 1 | 49901 | 3.53 | 40 |
| +stress | 3/20 | 0.451 | 0.99905 | 0.9967 .. 0.9998 | 49901 | 3.97 | 42 |
| +hypermutation | 6/20 | 1.000 | 0.99809 | 0.9916 .. 0.9999 | 49901 | 3.45 | 48 |
| +transfer | 6/20 | 1.000 | 0.9985 | 0.9964 .. 0.9999 | 49901 | 3.64 | 40 |
| +duplication | 4/20 | 0.716 | 0.99792 | 0.997 .. 0.9998 | 49901 | 4.04 | 45 |
| +spatial | 4/20 | 0.716 | 0.99916 | 0.9972 .. 0.9997 | 50600 | 1.46 | 34 |
| +spatial+curriculum | 2/20 | 0.235 | 0.99746 | 0.9959 .. 0.9992 | 50600 | 1.67 | 31 |
| all | 4/20 | 0.716 | 0.99942 | 0.999 .. 0.9996 | 50600 | 2.12 | 30 |
| bacterial | 4/20 | 0.716 | 0.99741 | 0.9944 .. 0.9996 | 50600 | 1.50 | 24 |
| tree-gp | 10/20 | 0.333 | 0.99987 | 0.9998 .. 1 | 10979 | 0.10 | 54 |

## list-sum: sum of a list (train length <= 10, test length <= 40)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 2/20 | - | 0.0875 | 0.06 .. 0.14 | 49901 | 3.11 | 49 |
| +stress | 2/20 | 1.000 | 0.08 | 0.045 .. 0.12 | 49901 | 3.18 | 44 |
| +hypermutation | 1/20 | 1.000 | 0.0625 | 0.025 .. 0.075 | 49901 | 3.00 | 46 |
| +transfer | 2/20 | 1.000 | 0.075 | 0.055 .. 0.14 | 49901 | 3.00 | 51 |
| +duplication | 2/20 | 1.000 | 0.065 | 0.035 .. 0.115 | 49901 | 3.35 | 44 |
| +spatial | 1/20 | 1.000 | 0.045 | 0.035 .. 0.08 | 50600 | 1.86 | 50 |
| +spatial+curriculum | 1/20 | 1.000 | 0.0525 | 0.04 .. 0.07 | 50600 | 1.57 | 36 |
| all | 0/20 | 0.487 | 0.0375 | 0.025 .. 0.06 | 50600 | 2.21 | 44 |
| bacterial | 0/20 | 0.487 | 0.045 | 0.035 .. 0.065 | 50600 | 1.53 | 28 |

## list-max: max of a list (train length <= 10, test length <= 40)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 4/20 | - | 0.4375 | 0.265 .. 0.97 | 49901 | 3.77 | 59 |
| +stress | 3/20 | 1.000 | 0.5025 | 0.36 .. 0.98 | 49901 | 3.80 | 44 |
| +hypermutation | 2/20 | 0.661 | 0.335 | 0.2 .. 0.495 | 49901 | 3.97 | 51 |
| +transfer | 4/20 | 1.000 | 0.38 | 0.26 .. 0.955 | 49901 | 4.41 | 57 |
| +duplication | 3/20 | 1.000 | 0.4975 | 0.375 .. 0.875 | 49901 | 3.88 | 48 |
| +spatial | 5/20 | 1.000 | 0.28 | 0.185 .. 0.53 | 50600 | 1.98 | 54 |
| +spatial+curriculum | 3/20 | 1.000 | 0.23 | 0.165 .. 0.49 | 50600 | 1.89 | 44 |
| all | 4/20 | 1.000 | 0.3 | 0.175 .. 0.49 | 50600 | 2.21 | 41 |
| bacterial | 0/20 | 0.106 | 0.19 | 0.125 .. 0.505 | 50600 | 1.62 | 28 |

## list-count_positive: count_positive of a list (train length <= 10, test length <= 40)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 0/20 | - | 0.0375 | 0.015 .. 0.06 | 49901 | 4.06 | 58 |
| +stress | 0/20 | 1.000 | 0.0275 | 0.01 .. 0.065 | 49901 | 3.79 | 61 |
| +hypermutation | 0/20 | 1.000 | 0.0375 | 0.01 .. 0.07 | 49901 | 3.75 | 56 |
| +transfer | 0/20 | 1.000 | 0.03 | 0.015 .. 0.06 | 49901 | 3.61 | 57 |
| +duplication | 0/20 | 1.000 | 0.0475 | 0.015 .. 0.09 | 49901 | 4.69 | 62 |
| +spatial | 0/20 | 1.000 | 0.025 | 0.015 .. 0.065 | 50600 | 1.86 | 46 |
| +spatial+curriculum | 0/20 | 1.000 | 0.03 | 0.01 .. 0.05 | 50600 | 2.06 | 52 |
| all | 0/20 | 1.000 | 0.0275 | 0.015 .. 0.06 | 50600 | 2.64 | 58 |
| bacterial | 0/20 | 1.000 | 0.025 | 0.02 .. 0.04 | 50600 | 1.75 | 39 |

