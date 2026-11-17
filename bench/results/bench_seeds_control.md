8 seeds per family, limit 30 s, 2 parallel jobs.  Cell: solved / median s / PAR-2 s (timeout = 2 x limit; median counts a timeout as the limit).

| family | `default` | `-phase-saving` | `-phase-saving,phase=0` | `-phase-saving,phase=1` |
|---|---|---|---|---|
| planted 3-SAT n=2000 r=4.0 | 6 / 21.46 / 225 | 8 / 2.54 / 21 | 8 / 3.73 / 56 | 7 / 5.68 / 95 |
| planted 3-SAT n=5000 r=3.8 | 2 / 30.00 / 377 | 8 / 0.95 / 12 | 5 / 27.51 / 273 | 6 / 13.08 / 196 |
| 3-colouring planted n=800 m=1840 | 7 / 7.14 / 115 | 5 / 14.92 / 220 | 5 / 14.99 / 221 | 5 / 4.41 / 191 |
| **total** | **15 / 717** | **21 / 254** | **18 / 550** | **18 / 483** |

Answer consistency: all configurations agree on every instance.
