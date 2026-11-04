# Benchmark results

Budget per run: population 500 x 100 generations = 50000 program evaluations (random search and hill climbing get the same number of evaluations). 20 independent runs per cell; each run samples its own training data. "Solved" = RMSE on the **unseen test set** below 1% of the target's standard deviation (R^2 > 0.9999). Runs stop early when the training set is solved.

### Algorithms

- **adaptogen**: linear programs, full core instruction set (incl. if/loop), lexicase
- **adaptogen-classic**: as above, restricted to the tree-GP function set
- **adaptogen-tournament**: full instruction set, tournament selection (size 4)
- **tree-gp**: Koza-style tree GP, {+,-,*,/,sin,cos,exp,log}, tournament 7
- **hill-climber**: (1+1) mutation-only search on linear programs, restarts
- **random-search**: random linear programs, keep the best
- **polyfit**: polynomial least squares, degree by hold-out (not evolutionary)

## koza1: `x^4 + x^3 + x^2 + x   (x in [-1,1])`

| algorithm | solved (test) | median test R^2 | R^2 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|
| adaptogen | 4/20 | 0.99881 | 0.9971 .. 0.9995 | 49901 | 1.51 | 44 |
| adaptogen-classic | 4/20 | 0.99851 | 0.9977 .. 0.9995 | 49901 | 1.82 | 18 |
| adaptogen-tournament | 4/20 | 0.99728 | 0.9946 .. 0.9999 | 49901 | 0.87 | 50 |
| tree-gp | 6/20 | 0.99970 | 0.9983 .. 0.9999 | 15470 | 0.06 | 50 |
| hill-climber | 2/20 | 0.99561 | 0.9881 .. 0.9987 | 50000 | 1.04 | 18 |
| random-search | 0/20 | 0.91023 | 0.9055 .. 0.9138 | 50000 | 0.18 | 6 |
| polyfit | 20/20 | 1.00000 | 1.0000 .. 1.0000 | 1 | 0.00 | 5 |

## nguyen5: `sin(x^2) cos(x) - 1   (x in [-1,1])`

| algorithm | solved (test) | median test R^2 | R^2 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|
| adaptogen | 0/20 | 0.99396 | 0.9835 .. 0.9973 | 49901 | 1.75 | 36 |
| adaptogen-classic | 0/20 | 0.96736 | 0.9322 .. 0.9934 | 49901 | 1.80 | 15 |
| adaptogen-tournament | 1/20 | 0.98551 | 0.9709 .. 0.9966 | 49901 | 0.91 | 54 |
| tree-gp | 0/20 | 0.99489 | 0.9569 .. 0.9985 | 49901 | 0.39 | 47 |
| hill-climber | 0/20 | 0.98672 | 0.9640 .. 0.9942 | 50000 | 0.95 | 14 |
| random-search | 0/20 | 0.76550 | 0.7375 .. 0.8143 | 50000 | 0.22 | 8 |
| polyfit | 20/20 | 1.00000 | 1.0000 .. 1.0000 | 1 | 0.00 | 12 |

## nguyen7: `ln(x+1) + ln(x^2+1)   (x in [0,2])`

| algorithm | solved (test) | median test R^2 | R^2 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|
| adaptogen | 2/20 | 0.99945 | 0.9990 .. 0.9998 | 49901 | 1.59 | 38 |
| adaptogen-classic | 2/20 | 0.99942 | 0.9988 .. 0.9998 | 49901 | 1.82 | 20 |
| adaptogen-tournament | 0/20 | 0.99878 | 0.9965 .. 0.9994 | 49901 | 0.84 | 43 |
| tree-gp | 4/20 | 0.99953 | 0.9994 .. 0.9999 | 49901 | 0.12 | 38 |
| hill-climber | 2/20 | 0.99873 | 0.9983 .. 0.9993 | 50000 | 0.98 | 16 |
| random-search | 0/20 | 0.99543 | 0.9896 .. 0.9975 | 50000 | 0.19 | 8 |
| polyfit | 20/20 | 1.00000 | 1.0000 .. 1.0000 | 1 | 0.00 | 12 |

## nguyen10: `2 sin(x) cos(y)   (x, y in [0,1])`

| algorithm | solved (test) | median test R^2 | R^2 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|
| adaptogen | 6/20 | 0.99574 | 0.9903 .. 1.0000 | 49901 | 4.07 | 36 |
| adaptogen-classic | 3/20 | 0.99786 | 0.9938 .. 0.9991 | 49901 | 6.31 | 24 |
| adaptogen-tournament | 0/20 | 0.98918 | 0.9641 .. 0.9947 | 49901 | 2.42 | 60 |
| tree-gp | 5/20 | 0.99905 | 0.9973 .. 0.9999 | 49901 | 1.66 | 78 |
| hill-climber | 3/20 | 0.98919 | 0.9808 .. 0.9906 | 50000 | 2.88 | 14 |
| random-search | 0/20 | 0.86927 | 0.8580 .. 0.8834 | 50000 | 0.58 | 10 |
| polyfit | 20/20 | 1.00000 | 1.0000 .. 1.0000 | 1 | 0.00 | 45 |

## pagie1: `1/(1+x^-4) + 1/(1+y^-4)   (x, y in [-5,5])`

| algorithm | solved (test) | median test R^2 | R^2 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|
| adaptogen | 0/20 | 0.87091 | 0.8395 .. 0.8937 | 49901 | 26.86 | 55 |
| adaptogen-classic | 0/20 | 0.75454 | 0.5516 .. 0.8105 | 49901 | 27.34 | 24 |
| adaptogen-tournament | 0/20 | 0.81641 | 0.7729 .. 0.8446 | 49901 | 22.55 | 60 |
| tree-gp | 0/20 | 0.75045 | 0.6193 .. 0.8025 | 49901 | 14.04 | 90 |
| hill-climber | 0/20 | 0.80476 | 0.7680 .. 0.8439 | 50000 | 22.08 | 22 |
| random-search | 0/20 | 0.31933 | 0.1908 .. 0.4032 | 50000 | 4.19 | 14 |
| polyfit | 0/20 | 0.98309 | 0.9827 .. 0.9839 | 1 | 0.01 | 66 |

## keijzer6: `sum_{i=1..x} 1/i   (train x=1..50, test x=1..120: extrapolation)`

| algorithm | solved (test) | median test R^2 | R^2 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|
| adaptogen | 5/20 | 0.99902 | 0.9929 .. 0.9999 | 49901 | 3.95 | 50 |
| adaptogen-classic | 17/20 | 0.99997 | 0.9999 .. 1.0000 | 17466 | 1.00 | 13 |
| adaptogen-tournament | 4/20 | 0.99928 | 0.9978 .. 0.9998 | 49901 | 2.31 | 48 |
| tree-gp | 10/20 | 0.99987 | 0.9998 .. 1.0000 | 10979 | 0.13 | 54 |
| hill-climber | 9/20 | 0.99977 | 0.9994 .. 0.9999 | 50000 | 2.82 | 12 |
| random-search | 0/20 | 0.98606 | 0.9861 .. 0.9885 | 50000 | 0.59 | 12 |
| polyfit | 0/20 | -38960299819031.85938 | -38960299819031.8594 .. -38960299819031.8594 | 1 | 0.00 | 13 |

