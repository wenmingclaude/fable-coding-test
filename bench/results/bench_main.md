# Benchmark summary

Time limit 20 s per run, 1 parallel jobs, PAR-2 scoring (timeout = 2 x limit).

## Solved instances and PAR-2 time per configuration

| configuration | solved | PAR-2 total (s) | pigeonhole | random 3-SAT (threshold) | hidden solution | graph colouring | parity | ordering principle | propagation chains | duplicates / hidden core | large |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `dpll` | 36/47 | 473.8 | 4/4, 1.0 | 10/10, 16.0 | 3/7, 160.0 | 7/7, 1.4 | 6/8, 94.3 | 0/3, 120.0 | 3/3, 0.2 | 1/3, 80.1 | 2/2, 0.8 |
| `cdcl` | 45/47 | 100.4 | 4/4, 3.0 | 10/10, 5.1 | 5/7, 85.2 | 7/7, 0.2 | 8/8, 1.2 | 3/3, 4.2 | 3/3, 0.3 | 3/3, 0.2 | 2/2, 1.1 |

Family cells: solved/total, PAR-2 seconds.

## Baseline DPLL vs. final solver, per instance

| instance | vars | clauses | answer | DPLL s | DPLL decisions | CDCL s | CDCL conflicts | speed-up |
|---|---|---|---|---|---|---|---|---|
| php-7-6 | 42 | 133 | UNSAT | 0.00 | 3245 | 0.00 | 683 | 1.0x |
| php-8-7 | 56 | 204 | UNSAT | 0.01 | 32780 | 0.04 | 4329 | 0.2x |
| php-9-8 | 72 | 297 | UNSAT | 0.07 | 378343 | 0.37 | 16515 | 0.2x |
| php-10-9 | 90 | 415 | UNSAT | 0.90 | 4912514 | 2.59 | 66265 | 0.3x |
| rand-150-a | 150 | 639 | UNSAT | 0.05 | 34518 | 0.03 | 4697 | 1.7x |
| rand-150-b | 150 | 639 | UNSAT | 0.04 | 28429 | 0.03 | 3942 | 1.6x |
| rand-150-c | 150 | 639 | SAT | 0.01 | 5813 | 0.03 | 3488 | 0.4x |
| rand-150-d | 150 | 639 | SAT | 0.03 | 17286 | 0.01 | 1898 | 2.3x |
| rand-200-a | 200 | 852 | SAT | 0.45 | 268267 | 0.08 | 8601 | 5.6x |
| rand-200-b | 200 | 852 | SAT | 0.17 | 100108 | 0.02 | 2843 | 8.3x |
| rand-200-c | 200 | 852 | UNSAT | 0.39 | 241733 | 0.23 | 21222 | 1.7x |
| rand-200-d | 200 | 852 | SAT | 0.05 | 30171 | 0.11 | 11297 | 0.5x |
| rand-250-a | 250 | 1065 | SAT | 3.42 | 2045836 | 2.81 | 122499 | 1.2x |
| rand-250-b | 250 | 1065 | UNSAT | 11.40 | 6729767 | 1.71 | 92787 | 6.7x |
| planted-2000-r3.8-a | 2000 | 7600 | SAT | 0.01 | 1320 | 0.83 | 33124 | 0.0x |
| planted-2000-r3.8-b | 2000 | 7600 | SAT | >20 | - | 1.28 | 52249 | >16x |
| planted-2000-r4.0 | 2000 | 8000 | timeout | >20 | - | >20 | - | - |
| planted-5000-r3.7 | 5000 | 18500 | SAT | >20 | - | 2.67 | 59289 | >7x |
| planted-20000-r3.5 | 20000 | 70000 | SAT | 0.03 | 6840 | 0.46 | 2018 | 0.1x |
| planted-400-r4.25 | 400 | 1700 | SAT | 0.00 | 335 | 0.00 | 6 | 1.0x |
| planted5-1000-r14 | 1000 | 14000 | timeout | >20 | - | >20 | - | - |
| col3-planted-600-1200 | 1800 | 6000 | SAT | 0.00 | 230 | 0.01 | 59 | 1.0x |
| col3-planted-600-1350 | 1800 | 6450 | SAT | 1.18 | 162765 | 0.16 | 5802 | 7.3x |
| col4-planted-300-1100 | 1200 | 6500 | SAT | 0.00 | 146 | 0.01 | 128 | 1.0x |
| col3-clique-200 | 600 | 1718 | UNSAT | 0.01 | 6155 | 0.00 | 6 | 1.0x |
| col5-clique-150 | 750 | 4225 | UNSAT | 0.07 | 191999 | 0.00 | 143 | 13.4x |
| col3-random-250-590 | 750 | 2770 | UNSAT | 0.01 | 803 | 0.01 | 567 | 1.5x |
| col3-random-300-700 | 900 | 3300 | UNSAT | 0.11 | 24365 | 0.02 | 1620 | 4.9x |
| parity-same-60 | 176 | 468 | UNSAT | >20 | - | 0.00 | 523 | >2000x |
| parity-same-2000 | 5996 | 15988 | UNSAT | >20 | - | 0.69 | 23902 | >29x |
| parity-shuffled-20 | 56 | 148 | UNSAT | 0.20 | 524287 | 0.01 | 3193 | 25.5x |
| parity-shuffled-24 | 68 | 180 | UNSAT | 3.83 | 8388607 | 0.06 | 17086 | 63.9x |
| tseitin-4x4 | 32 | 128 | UNSAT | 0.01 | 131071 | 0.01 | 3167 | 1.9x |
| tseitin-5x5 | 50 | 200 | UNSAT | 10.29 | 67108863 | 0.41 | 60184 | 25.3x |
| xor-planted-40 | 40 | 160 | SAT | 0.00 | 10 | 0.00 | 17 | 1.0x |
| xor-planted-70 | 70 | 280 | SAT | 0.00 | 63 | 0.00 | 74 | 1.0x |
| ordering-14 | 182 | 2380 | UNSAT | >20 | - | 0.01 | 2247 | >1429x |
| ordering-20 | 380 | 7240 | UNSAT | >20 | - | 0.29 | 43939 | >69x |
| ordering-26 | 650 | 16276 | UNSAT | >20 | - | 3.85 | 335286 | >5x |
| chain-200k-unsat | 200000 | 200001 | UNSAT | 0.09 | 0 | 0.15 | 1 | 0.6x |
| ladder-100k-unsat | 200000 | 200002 | UNSAT | 0.06 | 1 | 0.06 | 2 | 1.1x |
| cycle-199999 | 199999 | 399998 | UNSAT | 0.06 | 1 | 0.10 | 2 | 0.6x |
| dup-2000-x50 | 2000 | 351433 | SAT | 0.05 | 717 | 0.07 | 18 | 0.7x |
| core-3000 | 3030 | 10581 | UNSAT | >20 | - | 0.01 | 180 | >2000x |
| core-50000 | 50042 | 175133 | UNSAT | >20 | - | 0.15 | 907 | >135x |
| planted5-200k-1M | 200000 | 1000000 | SAT | 0.51 | 165207 | 0.71 | 0 | 0.7x |
| planted3-200k-600k | 200000 | 600000 | SAT | 0.29 | 96357 | 0.37 | 2 | 0.8x |

## Cost of certificates (default configuration, UNSAT instances)

| instance | solve s | solve s with proof | proof MB | check s | check / solve |
|---|---|---|---|---|---|
| php-7-6 | 0.00 | 0.01 | 0.05 | 0.005 | 0.5x |
| php-8-7 | 0.04 | 0.04 | 0.45 | 0.075 | 1.8x |
| php-9-8 | 0.37 | 0.49 | 2.63 | 0.602 | 1.6x |
| php-10-9 | 2.59 | 2.56 | 11.75 | 4.353 | 1.7x |
| rand-150-a | 0.03 | 0.03 | 0.28 | 0.037 | 1.2x |
| rand-150-b | 0.03 | 0.02 | 0.22 | 0.026 | 1.0x |
| rand-200-c | 0.23 | 0.24 | 1.72 | 0.297 | 1.3x |
| rand-250-b | 1.71 | 1.56 | 10.08 | 2.226 | 1.3x |
| col3-clique-200 | 0.00 | 0.00 | 0.00 | 0.002 | 0.2x |
| col5-clique-150 | 0.00 | 0.00 | 0.00 | 0.004 | 0.4x |
| col3-random-250-590 | 0.01 | 0.01 | 0.06 | 0.007 | 0.7x |
| col3-random-300-700 | 0.02 | 0.02 | 0.19 | 0.023 | 1.0x |
| parity-same-60 | 0.00 | 0.00 | 0.01 | 0.002 | 0.2x |
| parity-same-2000 | 0.69 | 0.83 | 0.83 | 0.025 | 0.0x |
| parity-shuffled-20 | 0.01 | 0.01 | 0.13 | 0.011 | 1.1x |
| parity-shuffled-24 | 0.06 | 0.06 | 0.87 | 0.117 | 2.0x |
| tseitin-4x4 | 0.01 | 0.01 | 0.15 | 0.009 | 0.9x |
| tseitin-5x5 | 0.41 | 0.42 | 4.15 | 0.634 | 1.6x |
| ordering-14 | 0.01 | 0.01 | 0.19 | 0.016 | 1.1x |
| ordering-20 | 0.29 | 0.31 | 6.17 | 0.421 | 1.5x |
| ordering-26 | 3.85 | 3.77 | 70.50 | 4.977 | 1.3x |
| chain-200k-unsat | 0.15 | 0.10 | 0.00 | 0.122 | 0.8x |
| ladder-100k-unsat | 0.06 | 0.06 | 0.00 | 0.049 | 0.9x |
| cycle-199999 | 0.10 | 0.08 | 0.00 | 0.082 | 0.8x |
| core-3000 | 0.01 | 0.01 | 0.02 | 0.006 | 0.6x |
| core-50000 | 0.15 | 0.15 | 0.19 | 0.077 | 0.5x |

## Consistency

No disagreement between configurations, no crash, every checked proof accepted.
