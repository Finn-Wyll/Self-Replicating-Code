# Results: custom

Budget per run: population 500 x 500 generations = 250000 program evaluations. 20 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table. Seeds start at 5000. List benchmarks hold back 25% of the training cases to choose the final program.

### Algorithms

- **baseline**: stage-1 engine: no mechanisms (crossover + mutation, lexicase)
- **stress-pop-p15**: config overlay: {"mechanisms":{"stress":{"enabled":true,"patience":15,"trigger":"population"}}}
- **hyper-start2**: config overlay: {"mechanisms":{"hypermutation":{"enabled":true,"initial_rate":2.0}}}
- **transfer-0.05**: config overlay: {"mechanisms":{"transfer":{"enabled":true,"rate":0.05}}}
- **dup-seg12**: config overlay: {"mechanisms":{"duplication":{"enabled":true,"max_segment":12}}}
- **spatial-r1**: config overlay: {"mechanisms":{"spatial":true}}
- **curriculum-r2-b2**: config overlay: {"mechanisms":{"curriculum":{"bands":2,"enabled":true},"spatial":{"enabled":true,"radius":2}}}
- **combined**: config overlay: {"mechanisms":{"hypermutation":{"enabled":true,"initial_rate":2.0},"stress":{"enabled":true,"patience":15,"trigger":"population"},"transfer":{"enabled":true,"rate":0.05}}}

## koza1: x^4 + x^3 + x^2 + x   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 7/20 | - | 0.99955 | 0.9985 .. 1 | 101048 | 3.48 | 45 |
| stress-pop-p15 | 9/20 | 0.748 | 0.9998 | 0.9984 .. 1 | 68614 | 3.14 | 44 |
| hyper-start2 | 8/20 | 1.000 | 0.99969 | 0.9992 .. 1 | 97556 | 3.22 | 42 |
| transfer-0.05 | 10/20 | 0.523 | 0.99992 | 0.9989 .. 1 | 91068 | 3.52 | 43 |
| dup-seg12 | 11/20 | 0.341 | 0.99991 | 0.9987 .. 1 | 16468 | 0.43 | 39 |
| spatial-r1 | 9/20 | 0.748 | 0.99946 | 0.9978 .. 1 | 61985 | 0.78 | 32 |
| curriculum-r2-b2 | 11/20 | 0.341 | 1 | 0.9976 .. 1 | 46805 | 0.60 | 36 |
| combined | 9/20 | 0.748 | 0.99963 | 0.9995 .. 1 | 50899 | 1.74 | 36 |

## nguyen10: 2 sin(x) cos(y)   (x, y in [0,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 4/20 | - | 0.99876 | 0.9973 .. 0.9996 | 249501 | 31.38 | 42 |
| stress-pop-p15 | 5/20 | 1.000 | 0.99844 | 0.9949 .. 0.9998 | 249501 | 33.22 | 44 |
| hyper-start2 | 4/20 | 1.000 | 0.99908 | 0.9966 .. 0.9997 | 249501 | 25.44 | 40 |
| transfer-0.05 | 7/20 | 0.480 | 0.99915 | 0.9976 .. 1 | 249501 | 29.99 | 36 |
| dup-seg12 | 5/20 | 1.000 | 0.99931 | 0.9991 .. 0.9998 | 249501 | 32.74 | 38 |
| spatial-r1 | 4/20 | 1.000 | 0.99782 | 0.9946 .. 0.9995 | 253000 | 14.97 | 45 |
| curriculum-r2-b2 | 4/20 | 1.000 | 0.99627 | 0.9916 .. 0.9995 | 253000 | 14.67 | 41 |
| combined | 6/20 | 0.716 | 0.99939 | 0.9982 .. 0.9999 | 249501 | 26.60 | 40 |

## keijzer6: sum_{i=1..x} 1/i   (train x=1..50, test x=1..120: extrapolation)

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 6/20 | - | 0.99921 | 0.9683 .. 0.9999 | 249501 | 20.95 | 56 |
| stress-pop-p15 | 8/20 | 0.741 | 0.99982 | 0.9987 .. 1 | 155689 | 26.33 | 48 |
| hyper-start2 | 9/20 | 0.514 | 0.99988 | 0.9981 .. 0.9999 | 143962 | 10.56 | 36 |
| transfer-0.05 | 9/20 | 0.514 | 0.99989 | 0.9992 .. 1 | 99552 | 9.86 | 51 |
| dup-seg12 | 6/20 | 1.000 | 0.99917 | 0.9983 .. 1 | 196108 | 19.50 | 47 |
| spatial-r1 | 12/20 | 0.111 | 0.99994 | 0.9998 .. 1 | 79948 | 3.96 | 36 |
| curriculum-r2-b2 | 8/20 | 0.741 | 0.99972 | 0.9986 .. 0.9999 | 200629 | 10.40 | 44 |
| combined | 6/20 | 1.000 | 0.99974 | 0.9983 .. 0.9999 | 85580 | 13.90 | 45 |

## list-sum: sum of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 12/20 | - | 1 | 0.025 .. 1 | 96058 | 9.97 | 34 |
| stress-pop-p15 | 11/20 | 1.000 | 1 | 0.055 .. 1 | 225300 | 20.59 | 34 |
| hyper-start2 | 7/20 | 0.205 | 0.0575 | 0.03 .. 1 | 249501 | 18.70 | 36 |
| transfer-0.05 | 7/20 | 0.205 | 0.0575 | 0.04 .. 1 | 249501 | 20.54 | 38 |
| dup-seg12 | 8/20 | 0.343 | 0.0925 | 0.04 .. 1 | 249501 | 19.59 | 44 |
| spatial-r1 | 3/20 | 0.008 | 0.0425 | 0.03 .. 0.06 | 253000 | 17.86 | 38 |
| curriculum-r2-b2 | 1/20 | 0.000 | 0.0375 | 0.03 .. 0.05 | 253000 | 18.37 | 30 |
| combined | 6/20 | 0.111 | 0.095 | 0.035 .. 1 | 249501 | 20.31 | 38 |

## list-max: max of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 4/20 | - | 0.26 | 0.11 .. 0.985 | 249501 | 27.24 | 36 |
| stress-pop-p15 | 7/20 | 0.480 | 0.23 | 0.075 .. 1 | 249501 | 28.90 | 44 |
| hyper-start2 | 3/20 | 1.000 | 0.4875 | 0.06 .. 0.795 | 249501 | 29.16 | 54 |
| transfer-0.05 | 7/20 | 0.480 | 0.44 | 0.165 .. 1 | 249501 | 27.09 | 54 |
| dup-seg12 | 2/20 | 0.661 | 0.5475 | 0.16 .. 0.985 | 249501 | 24.30 | 45 |
| spatial-r1 | 7/20 | 0.480 | 0.525 | 0.125 .. 1 | 253000 | 15.41 | 46 |
| curriculum-r2-b2 | 5/20 | 1.000 | 0.1275 | 0.045 .. 0.715 | 253000 | 18.45 | 34 |
| combined | 4/20 | 1.000 | 0.2225 | 0.055 .. 0.995 | 249501 | 25.50 | 54 |

