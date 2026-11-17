A = `build/sat_solver`

B = `build/sat_solver_occorder`

1 repetitions, median.

| instance | A: median s | B: median s | B / A | conflicts A | conflicts B |
|---|---|---|---|---|---|
| planted-2000-r3.8-a | 1.02 | 0.01 | 0.01 | 33124 | 121 |
| planted-2000-r3.8-b | 1.75 | 1.84 | 1.05 | 52249 | 59238 |
| planted-2000-r4.0 | 22.39 | 58.15 | 2.60 | 477293 | 1021362 |
| planted-5000-r3.7 | 2.94 | 0.01 | 0.00 | 59289 | 6 |
| planted-20000-r3.5 | 0.39 | 0.05 | 0.12 | 2018 | 3 |
| rand-200-b | 0.02 | 0.06 | 2.36 | 2843 | 6096 |
| rand-250-a | 3.19 | 2.06 | 0.64 | 122499 | 84306 |
| rand-250-b | 2.06 | 2.30 | 1.12 | 92787 | 99271 |
| col3-planted-600-1350 | 0.21 | 0.05 | 0.23 | 5802 | 1434 |
| php-9-8 | 0.48 | 0.40 | 0.83 | 16515 | 16515 |
| ordering-20 | 0.32 | 0.38 | 1.17 | 43939 | 43939 |
| tseitin-5x5 | 0.46 | 0.57 | 1.23 | 60184 | 60184 |
| parity-shuffled-24 | 0.10 | 1.13 | 11.58 | 17086 | 130439 |
| core-50000 | 0.15 | 0.09 | 0.64 | 907 | 806 |

Geometric mean of B / A over 14 instances: 0.486
