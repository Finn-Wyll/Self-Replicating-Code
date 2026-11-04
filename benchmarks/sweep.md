# Results: custom

Budget per run: population 500 x 100 generations = 50000 program evaluations. 20 independent runs per cell; each run samples its own data. "Solved" is judged on the **unseen test set** (formulas: RMSE < 1% of the target's spread; lists: every test list answered exactly). Runs stop early when the training set is solved. p = two-sided Fisher exact test of the solved count against the first algorithm in the table. Seeds start at 3000. List benchmarks hold back 25% of the training cases to choose the final program.

### Algorithms

- **baseline**: stage-1 engine: no mechanisms (crossover + mutation, lexicase)
- **stress-lineage-p3**: config overlay: {"mechanisms":{"stress":{"enabled":true}}}
- **stress-lineage-p10**: config overlay: {"mechanisms":{"stress":{"enabled":true,"patience":10}}}
- **stress-lineage-p10-mild**: config overlay: {"mechanisms":{"stress":{"enabled":true,"growth":0.2,"max_multiplier":3,"patience":10}}}
- **stress-pop-p5**: config overlay: {"mechanisms":{"stress":{"enabled":true,"patience":5,"trigger":"population"}}}
- **stress-pop-p15**: config overlay: {"mechanisms":{"stress":{"enabled":true,"patience":15,"trigger":"population"}}}
- **hyper-default**: config overlay: {"mechanisms":{"hypermutation":true}}
- **hyper-slow**: config overlay: {"mechanisms":{"hypermutation":{"change_sigma":0.3,"enabled":true,"p_change":0.05}}}
- **hyper-start2**: config overlay: {"mechanisms":{"hypermutation":{"enabled":true,"initial_rate":2.0}}}
- **transfer-0.05**: config overlay: {"mechanisms":{"transfer":{"enabled":true,"rate":0.05}}}
- **transfer-0.2**: config overlay: {"mechanisms":{"transfer":true}}
- **transfer-0.5**: config overlay: {"mechanisms":{"transfer":{"enabled":true,"rate":0.5}}}
- **transfer-seg2**: config overlay: {"mechanisms":{"transfer":{"enabled":true,"max_segment":2}}}
- **transfer-seg12**: config overlay: {"mechanisms":{"transfer":{"enabled":true,"max_segment":12}}}
- **dup-0.05**: config overlay: {"mechanisms":{"duplication":{"enabled":true,"rate":0.05}}}
- **dup-0.1**: config overlay: {"mechanisms":{"duplication":true}}
- **dup-0.3**: config overlay: {"mechanisms":{"duplication":{"enabled":true,"rate":0.3}}}
- **dup-seg12**: config overlay: {"mechanisms":{"duplication":{"enabled":true,"max_segment":12}}}
- **spatial-r1**: config overlay: {"mechanisms":{"spatial":true}}
- **spatial-r2**: config overlay: {"mechanisms":{"spatial":{"enabled":true,"radius":2}}}
- **spatial-r3**: config overlay: {"mechanisms":{"spatial":{"enabled":true,"radius":3}}}
- **curriculum-r2-b2**: config overlay: {"mechanisms":{"curriculum":{"bands":2,"enabled":true},"spatial":{"enabled":true,"radius":2}}}
- **curriculum-r2-b4**: config overlay: {"mechanisms":{"curriculum":{"bands":4,"enabled":true},"spatial":{"enabled":true,"radius":2}}}

## koza1: x^4 + x^3 + x^2 + x   (x in [-1,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 6/20 | - | 0.99853 | 0.9943 .. 1 | 49901 | 1.43 | 36 |
| stress-lineage-p3 | 7/20 | 1.000 | 0.99906 | 0.994 .. 1 | 49901 | 1.81 | 36 |
| stress-lineage-p10 | 9/20 | 0.514 | 0.99963 | 0.9965 .. 1 | 39921 | 1.69 | 42 |
| stress-lineage-p10-mild | 8/20 | 0.741 | 0.99848 | 0.9973 .. 1 | 49901 | 1.53 | 53 |
| stress-pop-p5 | 10/20 | 0.333 | 0.99965 | 0.996 .. 1 | 40420 | 1.54 | 50 |
| stress-pop-p15 | 10/20 | 0.333 | 0.99965 | 0.9973 .. 1 | 39422 | 1.48 | 31 |
| hyper-default | 5/20 | 1.000 | 0.99792 | 0.9955 .. 0.9992 | 49901 | 1.54 | 37 |
| hyper-slow | 4/20 | 0.716 | 0.99768 | 0.9769 .. 0.9994 | 49901 | 1.52 | 36 |
| hyper-start2 | 9/20 | 0.514 | 0.99986 | 0.9962 .. 1 | 49901 | 1.64 | 34 |
| transfer-0.05 | 8/20 | 0.741 | 0.99808 | 0.9915 .. 1 | 49901 | 1.60 | 41 |
| transfer-0.2 | 7/20 | 1.000 | 0.99705 | 0.9831 .. 1 | 49901 | 1.59 | 34 |
| transfer-0.5 | 5/20 | 1.000 | 0.99523 | 0.9807 .. 0.9998 | 49901 | 1.67 | 36 |
| transfer-seg2 | 7/20 | 1.000 | 0.99918 | 0.9971 .. 1 | 49901 | 1.41 | 40 |
| transfer-seg12 | 4/20 | 0.716 | 0.99818 | 0.9964 .. 0.9997 | 49901 | 1.70 | 39 |
| dup-0.05 | 1/20 | 0.091 | 0.99476 | 0.9859 .. 0.9983 | 49901 | 1.74 | 42 |
| dup-0.1 | 4/20 | 0.716 | 0.99715 | 0.9935 .. 0.9998 | 49901 | 1.56 | 40 |
| dup-0.3 | 8/20 | 0.741 | 0.99941 | 0.998 .. 1 | 49901 | 1.47 | 32 |
| dup-seg12 | 3/20 | 0.451 | 0.9973 | 0.9929 .. 0.9997 | 49901 | 1.59 | 43 |
| spatial-r1 | 6/20 | 1.000 | 0.99932 | 0.997 .. 1 | 50600 | 0.55 | 42 |
| spatial-r2 | 5/20 | 1.000 | 0.99877 | 0.9933 .. 0.9997 | 50600 | 0.64 | 34 |
| spatial-r3 | 5/20 | 1.000 | 0.99848 | 0.9952 .. 0.9997 | 50600 | 0.69 | 42 |
| curriculum-r2-b2 | 4/20 | 0.716 | 0.99621 | 0.9916 .. 0.9986 | 50600 | 0.61 | 28 |
| curriculum-r2-b4 | 4/20 | 0.716 | 0.99684 | 0.9925 .. 0.9979 | 50600 | 0.73 | 26 |

## nguyen10: 2 sin(x) cos(y)   (x, y in [0,1])

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 4/20 | - | 0.99458 | 0.9916 .. 0.9986 | 49901 | 4.79 | 38 |
| stress-lineage-p3 | 5/20 | 1.000 | 0.99524 | 0.991 .. 0.998 | 49901 | 5.04 | 37 |
| stress-lineage-p10 | 3/20 | 1.000 | 0.99599 | 0.993 .. 0.9985 | 49901 | 4.81 | 33 |
| stress-lineage-p10-mild | 2/20 | 0.661 | 0.99679 | 0.9926 .. 0.9981 | 49901 | 5.14 | 39 |
| stress-pop-p5 | 3/20 | 1.000 | 0.9938 | 0.9877 .. 0.9986 | 49901 | 5.07 | 40 |
| stress-pop-p15 | 3/20 | 1.000 | 0.99588 | 0.9904 .. 0.998 | 49901 | 4.70 | 34 |
| hyper-default | 2/20 | 0.661 | 0.99447 | 0.9864 .. 0.9979 | 49901 | 4.42 | 34 |
| hyper-slow | 3/20 | 1.000 | 0.99768 | 0.9963 .. 0.9997 | 49901 | 4.36 | 40 |
| hyper-start2 | 4/20 | 1.000 | 0.99606 | 0.9915 .. 0.9994 | 49901 | 4.14 | 39 |
| transfer-0.05 | 7/20 | 0.480 | 0.99834 | 0.9924 .. 0.9999 | 49901 | 4.09 | 36 |
| transfer-0.2 | 2/20 | 0.661 | 0.99409 | 0.9881 .. 0.9966 | 49901 | 4.70 | 43 |
| transfer-0.5 | 4/20 | 1.000 | 0.99529 | 0.9895 .. 0.9994 | 49901 | 4.99 | 44 |
| transfer-seg2 | 1/20 | 0.342 | 0.9966 | 0.9938 .. 0.9983 | 49901 | 5.37 | 38 |
| transfer-seg12 | 4/20 | 1.000 | 0.99667 | 0.9954 .. 0.9997 | 49901 | 5.37 | 40 |
| dup-0.05 | 3/20 | 1.000 | 0.9951 | 0.9912 .. 0.9978 | 49901 | 4.56 | 36 |
| dup-0.1 | 0/20 | 0.106 | 0.99548 | 0.9909 .. 0.997 | 49901 | 4.70 | 45 |
| dup-0.3 | 1/20 | 0.342 | 0.99373 | 0.9884 .. 0.9972 | 49901 | 4.62 | 30 |
| dup-seg12 | 4/20 | 1.000 | 0.99705 | 0.9925 .. 0.9995 | 49901 | 5.09 | 39 |
| spatial-r1 | 1/20 | 0.342 | 0.98269 | 0.979 .. 0.9947 | 50600 | 2.19 | 48 |
| spatial-r2 | 2/20 | 0.661 | 0.99404 | 0.9867 .. 0.9975 | 50600 | 2.23 | 32 |
| spatial-r3 | 4/20 | 1.000 | 0.99564 | 0.9911 .. 0.999 | 50600 | 2.37 | 30 |
| curriculum-r2-b2 | 0/20 | 0.106 | 0.98819 | 0.9806 .. 0.9959 | 50600 | 2.19 | 40 |
| curriculum-r2-b4 | 2/20 | 0.661 | 0.98793 | 0.9694 .. 0.9936 | 50600 | 2.19 | 33 |

## keijzer6: sum_{i=1..x} 1/i   (train x=1..50, test x=1..120: extrapolation)

| algorithm | solved (test) | p vs first | median test R^2 | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 4/20 | - | 0.99964 | 0.9963 .. 0.9999 | 49901 | 3.73 | 40 |
| stress-lineage-p3 | 6/20 | 0.716 | 0.99928 | 0.9975 .. 0.9999 | 49901 | 4.52 | 38 |
| stress-lineage-p10 | 7/20 | 0.480 | 0.99956 | 0.9836 .. 0.9999 | 49901 | 3.40 | 34 |
| stress-lineage-p10-mild | 8/20 | 0.301 | 0.99971 | 0.9954 .. 1 | 49901 | 3.68 | 34 |
| stress-pop-p5 | 9/20 | 0.176 | 0.99976 | 0.9973 .. 1 | 49901 | 3.92 | 34 |
| stress-pop-p15 | 8/20 | 0.301 | 0.9996 | 0.9966 .. 1 | 49901 | 3.64 | 40 |
| hyper-default | 6/20 | 0.716 | 0.9993 | 0.9983 .. 0.9999 | 49901 | 3.58 | 37 |
| hyper-slow | 1/20 | 0.342 | 0.99781 | 0.9916 .. 0.9997 | 49901 | 4.38 | 46 |
| hyper-start2 | 6/20 | 0.716 | 0.99959 | 0.9985 .. 1 | 49901 | 4.02 | 46 |
| transfer-0.05 | 6/20 | 0.716 | 0.99821 | 0.9823 .. 0.9999 | 49901 | 3.54 | 32 |
| transfer-0.2 | 9/20 | 0.176 | 0.99972 | 0.9972 .. 1 | 47406 | 3.78 | 32 |
| transfer-0.5 | 6/20 | 0.716 | 0.99878 | 0.9959 .. 0.9999 | 49901 | 4.07 | 41 |
| transfer-seg2 | 6/20 | 0.716 | 0.99949 | 0.9732 .. 0.9999 | 49901 | 3.58 | 34 |
| transfer-seg12 | 4/20 | 1.000 | 0.999 | 0.996 .. 0.9998 | 49901 | 4.55 | 38 |
| dup-0.05 | 7/20 | 0.480 | 0.99954 | 0.9978 .. 0.9999 | 49901 | 3.57 | 32 |
| dup-0.1 | 8/20 | 0.301 | 0.99936 | 0.9949 .. 0.9999 | 49901 | 3.35 | 36 |
| dup-0.3 | 2/20 | 0.661 | 0.99679 | 0.7585 .. 0.9997 | 49901 | 4.11 | 32 |
| dup-seg12 | 8/20 | 0.301 | 0.99981 | 0.9982 .. 1 | 49901 | 3.70 | 34 |
| spatial-r1 | 6/20 | 0.716 | 0.99944 | 0.9985 .. 1 | 50600 | 1.36 | 41 |
| spatial-r2 | 4/20 | 1.000 | 0.99912 | 0.9949 .. 0.9998 | 50600 | 2.39 | 48 |
| spatial-r3 | 3/20 | 1.000 | 0.99873 | 0.9967 .. 0.9997 | 50600 | 1.88 | 41 |
| curriculum-r2-b2 | 3/20 | 1.000 | 0.99753 | 0.9448 .. 0.9994 | 50600 | 2.43 | 40 |
| curriculum-r2-b4 | 2/20 | 0.661 | 0.99694 | 0.9897 .. 0.9994 | 50600 | 1.36 | 30 |

## list-sum: sum of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 7/20 | - | 0.07 | 0.015 .. 1 | 49901 | 3.79 | 24 |
| stress-lineage-p3 | 3/20 | 0.273 | 0.05 | 0.035 .. 0.085 | 49901 | 4.07 | 30 |
| stress-lineage-p10 | 2/20 | 0.127 | 0.0475 | 0.02 .. 0.065 | 49901 | 4.50 | 37 |
| stress-lineage-p10-mild | 1/20 | 0.044 | 0.045 | 0.025 .. 0.065 | 49901 | 4.15 | 44 |
| stress-pop-p5 | 2/20 | 0.127 | 0.035 | 0.02 .. 0.09 | 49901 | 3.98 | 44 |
| stress-pop-p15 | 4/20 | 0.480 | 0.045 | 0.03 .. 0.095 | 49901 | 3.77 | 42 |
| hyper-default | 3/20 | 0.273 | 0.0375 | 0.025 .. 0.055 | 49901 | 4.11 | 35 |
| hyper-slow | 4/20 | 0.480 | 0.035 | 0.03 .. 0.11 | 49901 | 3.36 | 46 |
| hyper-start2 | 6/20 | 1.000 | 0.0525 | 0.03 .. 1 | 49901 | 4.01 | 33 |
| transfer-0.05 | 6/20 | 1.000 | 0.0725 | 0.04 .. 1 | 49901 | 3.73 | 26 |
| transfer-0.2 | 3/20 | 0.273 | 0.04 | 0.02 .. 0.05 | 49901 | 4.17 | 28 |
| transfer-0.5 | 3/20 | 0.273 | 0.0575 | 0.025 .. 0.08 | 49901 | 4.62 | 30 |
| transfer-seg2 | 2/20 | 0.127 | 0.035 | 0.02 .. 0.075 | 49901 | 4.14 | 38 |
| transfer-seg12 | 4/20 | 0.480 | 0.0275 | 0.01 .. 0.055 | 49901 | 3.90 | 39 |
| dup-0.05 | 5/20 | 0.731 | 0.035 | 0 .. 0.59 | 49901 | 3.76 | 30 |
| dup-0.1 | 6/20 | 1.000 | 0.06 | 0.01 .. 1 | 49901 | 3.64 | 41 |
| dup-0.3 | 4/20 | 0.480 | 0.0525 | 0.03 .. 0.13 | 49901 | 4.18 | 40 |
| dup-seg12 | 4/20 | 0.480 | 0.0525 | 0.03 .. 0.465 | 49901 | 3.45 | 36 |
| spatial-r1 | 1/20 | 0.044 | 0.055 | 0.045 .. 0.075 | 50600 | 2.14 | 25 |
| spatial-r2 | 1/20 | 0.044 | 0.04 | 0.03 .. 0.055 | 50600 | 2.57 | 41 |
| spatial-r3 | 2/20 | 0.127 | 0.06 | 0.035 .. 0.08 | 50600 | 2.71 | 36 |
| curriculum-r2-b2 | 2/20 | 0.127 | 0.0375 | 0.03 .. 0.06 | 50600 | 2.74 | 24 |
| curriculum-r2-b4 | 1/20 | 0.044 | 0.045 | 0.035 .. 0.055 | 50600 | 2.14 | 31 |

## list-max: max of a list (train length <= 20, test length <= 50)

| algorithm | solved (test) | p vs first | median test accuracy | 25%..75% | median evaluations | median time (s) | median size |
|---|---|---|---|---|---|---|---|
| baseline | 3/20 | - | 0.455 | 0.055 .. 0.89 | 49901 | 4.56 | 44 |
| stress-lineage-p3 | 3/20 | 1.000 | 0.135 | 0.065 .. 0.28 | 49901 | 5.62 | 40 |
| stress-lineage-p10 | 1/20 | 0.605 | 0.0675 | 0.03 .. 0.18 | 49901 | 4.88 | 46 |
| stress-lineage-p10-mild | 1/20 | 0.605 | 0.1125 | 0.05 .. 0.42 | 49901 | 4.96 | 49 |
| stress-pop-p5 | 2/20 | 1.000 | 0.0625 | 0.04 .. 0.12 | 49901 | 4.43 | 60 |
| stress-pop-p15 | 2/20 | 1.000 | 0.0675 | 0.055 .. 0.25 | 49901 | 4.47 | 68 |
| hyper-default | 1/20 | 0.605 | 0.0775 | 0.04 .. 0.32 | 49901 | 4.14 | 40 |
| hyper-slow | 4/20 | 1.000 | 0.1125 | 0.04 .. 0.41 | 49901 | 5.13 | 48 |
| hyper-start2 | 4/20 | 1.000 | 0.145 | 0.06 .. 0.975 | 49901 | 3.73 | 40 |
| transfer-0.05 | 1/20 | 0.605 | 0.24 | 0.055 .. 0.985 | 49901 | 4.75 | 52 |
| transfer-0.2 | 2/20 | 1.000 | 0.2625 | 0.04 .. 0.975 | 49901 | 4.65 | 47 |
| transfer-0.5 | 1/20 | 0.605 | 0.07 | 0.04 .. 0.125 | 49901 | 4.97 | 57 |
| transfer-seg2 | 2/20 | 1.000 | 0.3475 | 0.08 .. 0.915 | 49901 | 4.33 | 50 |
| transfer-seg12 | 2/20 | 1.000 | 0.115 | 0.035 .. 0.415 | 49901 | 4.90 | 38 |
| dup-0.05 | 4/20 | 1.000 | 0.325 | 0.125 .. 0.985 | 49901 | 4.45 | 28 |
| dup-0.1 | 2/20 | 1.000 | 0.1025 | 0.045 .. 0.205 | 49901 | 4.84 | 43 |
| dup-0.3 | 2/20 | 1.000 | 0.245 | 0.095 .. 0.495 | 49901 | 4.70 | 43 |
| dup-seg12 | 1/20 | 0.605 | 0.0975 | 0.015 .. 0.245 | 49901 | 5.53 | 48 |
| spatial-r1 | 3/20 | 1.000 | 0.1225 | 0.06 .. 0.235 | 50600 | 2.57 | 46 |
| spatial-r2 | 1/20 | 0.605 | 0.08 | 0.055 .. 0.185 | 50600 | 2.20 | 45 |
| spatial-r3 | 1/20 | 0.605 | 0.13 | 0.08 .. 0.33 | 50600 | 2.60 | 46 |
| curriculum-r2-b2 | 4/20 | 1.000 | 0.265 | 0.06 .. 0.565 | 50600 | 2.37 | 46 |
| curriculum-r2-b4 | 3/20 | 1.000 | 0.2325 | 0.075 .. 0.525 | 50600 | 2.22 | 50 |

