| formula | queries (SAT / UNSAT) | incremental: s | conflicts | fresh runs: s | conflicts | time ratio |
|---|---|---|---|---|---|---|
| planted 3-SAT n=5000 r=3.7 | 40 (40 / 0) | 2.46 | 52190 | 158.26 | 3353895 | 64.4x |
| random 3-SAT n=200 r=4.2 | 40 (0 / 40) | 1.26 | 64398 | 1.05 | 153630 | 0.8x |
| 3-colouring planted n=600 m=1300 | 40 (40 / 0) | 0.08 | 3492 | 0.30 | 10552 | 3.7x |
| pigeonhole 9 into 9 (sat) | 40 (23 / 17) | 0.02 | 2352 | 0.08 | 2262 | 4.4x |
| planted 5-SAT 200k vars, 1M clauses | 20 (20 / 0) | 1.69 | 0 | 14.34 | 0 | 8.5x |

All answers agree between the two methods.
