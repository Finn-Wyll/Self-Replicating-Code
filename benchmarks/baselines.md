# Results: baselines

Budget per run: population 500 x 100 generations = 50000 program evaluations. 20 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table.

### Algorithms

- **adaptogen**: linear programs, full core instruction set (incl. if/loop), lexicase
- **adaptogen-classic**: as above, restricted to the tree-GP function set
- **adaptogen-tournament**: full instruction set, tournament selection (size 4)
- **tree-gp**: Koza-style tree GP, {+,-,*,/,sin,cos,exp,log}, tournament 7
- **hill-climber**: (1+1) mutation-only search on linear programs, restarts
- **random-search**: random linear programs, keep the best
- **polyfit**: polynomial least squares, degree by hold-out (not evolutionary)

## koza1: x^4 + x^3 + x^2 + x   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| adaptogen | 5/20 | - | 0.99875 | 0.9931 .. 0.9997 | 49901 | 1.50 | 44 |
| adaptogen-classic | 4/20 | 1.000 | 0.99901 | 0.9979 .. 0.9994 | 49901 | 1.89 | 17 |
| adaptogen-tournament | 6/20 | 1.000 | 0.99662 | 0.9648 .. 1 | 49901 | 0.76 | 51 |
| tree-gp | 6/20 | 1.000 | 0.9997 | 0.9983 .. 0.9999 | 15470 | 0.06 | 50 |
| hill-climber | 2/20 | 0.407 | 0.99561 | 0.9881 .. 0.9987 | 50000 | 0.90 | 18 |
| random-search | 0/20 | 0.047 | 0.91023 | 0.9055 .. 0.9138 | 50000 | 0.16 | 6 |
| polyfit | 20/20 | 0.000 | 1 | 1 .. 1 | 1 | 0.00 | 5 |

## nguyen5: sin(x^2) cos(x) - 1   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| adaptogen | 0/20 | - | 0.98433 | 0.9662 .. 0.9939 | 49901 | 1.68 | 40 |
| adaptogen-classic | 0/20 | 1.000 | 0.97722 | 0.9621 .. 0.9953 | 49901 | 1.76 | 14 |
| adaptogen-tournament | 0/20 | 1.000 | 0.98148 | 0.9534 .. 0.9973 | 49901 | 0.71 | 47 |
| tree-gp | 0/20 | 1.000 | 0.99489 | 0.9569 .. 0.9985 | 49901 | 0.27 | 47 |
| hill-climber | 0/20 | 1.000 | 0.98672 | 0.964 .. 0.9942 | 50000 | 0.78 | 14 |
| random-search | 0/20 | 1.000 | 0.7655 | 0.7375 .. 0.8143 | 50000 | 0.16 | 8 |
| polyfit | 20/20 | 0.000 | 1 | 1 .. 1 | 1 | 0.00 | 12 |

## nguyen7: ln(x+1) + ln(x^2+1)   (x in [0,2])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| adaptogen | 2/20 | - | 0.99929 | 0.999 .. 0.9996 | 49901 | 1.67 | 40 |
| adaptogen-classic | 2/20 | 1.000 | 0.9995 | 0.9978 .. 0.9997 | 49901 | 1.74 | 19 |
| adaptogen-tournament | 0/20 | 0.487 | 0.99893 | 0.9978 .. 0.9994 | 49901 | 0.69 | 38 |
| tree-gp | 4/20 | 0.661 | 0.99953 | 0.9994 .. 0.9999 | 49901 | 0.12 | 38 |
| hill-climber | 2/20 | 1.000 | 0.99873 | 0.9983 .. 0.9993 | 50000 | 0.88 | 16 |
| random-search | 0/20 | 0.487 | 0.99543 | 0.9896 .. 0.9975 | 50000 | 0.16 | 8 |
| polyfit | 20/20 | 0.000 | 1 | 1 .. 1 | 1 | 0.00 | 12 |

## nguyen10: 2 sin(x) cos(y)   (x, y in [0,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| adaptogen | 5/20 | - | 0.99762 | 0.9905 .. 0.9999 | 49901 | 4.67 | 37 |
| adaptogen-classic | 6/20 | 1.000 | 0.99867 | 0.9955 .. 0.9999 | 49901 | 5.96 | 20 |
| adaptogen-tournament | 2/20 | 0.407 | 0.99082 | 0.9652 .. 0.9945 | 49901 | 2.28 | 41 |
| tree-gp | 5/20 | 1.000 | 0.99905 | 0.9973 .. 0.9999 | 49901 | 1.63 | 78 |
| hill-climber | 3/20 | 0.695 | 0.98919 | 0.9808 .. 0.9906 | 50000 | 2.75 | 14 |
| random-search | 0/20 | 0.047 | 0.86927 | 0.858 .. 0.8834 | 50000 | 0.57 | 10 |
| polyfit | 20/20 | 0.000 | 1 | 1 .. 1 | 1 | 0.00 | 45 |

## pagie1: 1/(1+x^-4) + 1/(1+y^-4)   (x, y in [-5,5])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| adaptogen | 0/20 | - | 0.89208 | 0.87 .. 0.9157 | 49901 | 21.26 | 51 |
| adaptogen-classic | 0/20 | 1.000 | 0.70083 | 0.6526 .. 0.7797 | 49901 | 23.10 | 24 |
| adaptogen-tournament | 0/20 | 1.000 | 0.82034 | 0.7948 .. 0.8687 | 49901 | 16.79 | 61 |
| tree-gp | 0/20 | 1.000 | 0.75045 | 0.6193 .. 0.8025 | 49901 | 12.60 | 90 |
| hill-climber | 0/20 | 1.000 | 0.80476 | 0.768 .. 0.8439 | 50000 | 19.56 | 22 |
| random-search | 0/20 | 1.000 | 0.31933 | 0.1908 .. 0.4032 | 50000 | 3.74 | 14 |
| polyfit | 0/20 | 1.000 | 0.98309 | 0.9827 .. 0.9839 | 1 | 0.01 | 66 |

## keijzer6: sum_{i=1..x} 1/i   (train x=1..50, test x=1..120: extrapolation)

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| adaptogen | 6/20 | - | 0.99952 | 0.9976 .. 1 | 49901 | 3.39 | 40 |
| adaptogen-classic | 15/20 | 0.010 | 0.99996 | 0.9999 .. 1 | 20460 | 1.31 | 16 |
| adaptogen-tournament | 10/20 | 0.333 | 0.99989 | 0.9993 .. 1 | 49652 | 1.85 | 40 |
| tree-gp | 10/20 | 0.333 | 0.99987 | 0.9998 .. 1 | 10979 | 0.10 | 54 |
| hill-climber | 9/20 | 0.514 | 0.99977 | 0.9994 .. 0.9999 | 50000 | 2.46 | 12 |
| random-search | 0/20 | 0.020 | 0.98606 | 0.9861 .. 0.9885 | 50000 | 0.53 | 12 |
| polyfit | 0/20 | 0.020 | -3.896e+13 | -3.896e+13 .. -3.896e+13 | 1 | 0.00 | 13 |

