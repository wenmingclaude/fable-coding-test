8 seeds per family, limit 30 s, 2 parallel jobs.  Cell: solved / median s / PAR-2 s (timeout = 2 x limit; median counts a timeout as the limit).

| family | `default` | `-phase-saving` | `-minimize` | `-blocker` | `-stable` | `phase=0` |
|---|---|---|---|---|---|---|
| planted 3-SAT n=2000 r=4.0 | 6 / 21.58 / 226 | 8 / 2.44 / 21 | 5 / 17.11 / 223 | 7 / 17.88 / 144 | 0 / 30.00 / 480 | 3 / 30.00 / 322 |
| planted 3-SAT n=5000 r=3.8 | 2 / 30.00 / 377 | 8 / 0.94 / 13 | 4 / 29.00 / 321 | 3 / 30.00 / 338 | 0 / 30.00 / 480 | 5 / 24.49 / 249 |
| random 3-SAT n=250 r=4.26 | 8 / 0.61 / 8 | 8 / 0.33 / 6 | 8 / 0.62 / 9 | 8 / 0.74 / 8 | 8 / 0.94 / 7 | 8 / 0.65 / 8 |
| 3-colouring planted n=800 m=1840 | 7 / 7.60 / 115 | 5 / 14.40 / 218 | 3 / 30.00 / 335 | 6 / 20.46 / 194 | 4 / 29.50 / 344 | 7 / 7.31 / 115 |
| tseitin torus 5x5 | 8 / 0.42 / 3 | 8 / 0.36 / 3 | 8 / 0.40 / 3 | 8 / 0.56 / 4 | 8 / 0.43 / 3 | 8 / 0.44 / 4 |
| parity, shuffled, n=24 | 8 / 0.05 / 1 | 8 / 0.06 / 1 | 8 / 0.06 / 1 | 8 / 0.05 / 1 | 8 / 0.03 / 1 | 8 / 0.05 / 1 |
| **total** | **39 / 730** | **45 / 262** | **36 / 892** | **40 / 690** | **28 / 1316** | **39 / 699** |

Answer consistency: all configurations agree on every instance.
