# Recorded benchmark and test outputs

Raw outputs of the runs quoted in docs/RESULTS.md.  Regenerate with the
commands listed there (`make bench`, `make test-full`, `make mutation`, ...).

| file | produced by |
|---|---|
| `bench_main.md`, `bench_main_runs.csv` | `bench/run_bench.py --limit 20 --configs dpll,cdcl --jobs 1 --proof` |
| `bench_ablation.md`, `bench_ablation_runs.csv` | `bench/run_bench.py --limit 20 --jobs 2` with one feature switched off per configuration |
| `bench_seeds.md`, `bench_seeds_runs.log` | `bench/seeds_bench.py --seeds 8 --limit 30 --jobs 2` |
| `bench_seeds_control.md`, `..._runs.log` | the phase-saving control experiment (E4) |
| `bench_incremental.md` | `bench/incremental_bench.py` |
| `exp_prefetch.md` | experiment E1, `bench/compare.py` |
| `exp_occorder.md` | experiment E2, `bench/compare.py` |
| `mutation_first_run.*` | first mutation run (46/50 killed), before the test gaps were closed |
| `mutation_final.md`, `mutation_final.log` | final mutation run |
| `test_full.log`, `test_sanitize.log` | `make test-full`, `make sanitize` |

Note: the `peak_mb` column of the two CSV files was recorded with getrusage,
which on Linux includes the memory of the parent process at fork time (see
docs/RESULTS.md, section 2); it is an upper bound, not the solver's own peak.
