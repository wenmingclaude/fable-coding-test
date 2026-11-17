A = `build/sat_solver`

B = `build/sat_solver_prefetch`

3 repetitions, median.

| instance | A: median s | B: median s | B / A | conflicts A | conflicts B |
|---|---|---|---|---|---|
| rand-250-b | 1.55 | 1.58 | 1.02 | 92787 | 92787 |
| php-10-9 | 2.57 | 2.50 | 0.97 | 66265 | 66265 |
| ordering-26 | 3.61 | 3.76 | 1.04 | 335286 | 335286 |
| planted-5000-r3.7 | 2.67 | 2.47 | 0.92 | 59289 | 59289 |
| planted-2000-r3.8-b | 1.36 | 1.38 | 1.01 | 52249 | 52249 |
| tseitin-5x5 | 0.38 | 0.41 | 1.09 | 60184 | 60184 |
| parity-same-2000 | 0.75 | 0.75 | 1.01 | 23902 | 23902 |
| col3-planted-600-1350 | 0.17 | 0.15 | 0.86 | 5802 | 5802 |

Geometric mean of B / A over 8 instances: 0.989
