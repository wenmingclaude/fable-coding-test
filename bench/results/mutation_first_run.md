| mutant | category | injected bug | result | detected as | by test group |
|---|---|---|---|---|---|
| `neg-asserting` | wrong negation | learnt clause gets the UIP literal with the wrong sign | killed | other failure | G. input format and command line |
| `neg-enqueue` | wrong negation | assigning p does not make ¬p false | killed | invalid model | G. input format and command line |
| `neg-parse` | wrong negation | negative input literals are read as positive | killed | invalid model | G. input format and command line |
| `neg-tautology` | wrong negation | x ∨ ¬x is 'simplified' by dropping ¬x instead of the clause | killed | wrong answer | A. differential fuzz vs brute force (n <= 18) |
| `neg-final` | wrong negation | failed-assumption core is returned without negating back | killed | invalid core | F. incremental solving under assumptions |
| `prop-lost-watch` | missed propagation | a moved watch is not registered with the new literal | killed | other failure | G. input format and command line |
| `prop-qhead` | missed propagation | after backtracking the propagation queue is not rewound | killed | crash | G. input format and command line |
| `prop-skip-unit-long` | missed propagation | a unit long clause does not force its last literal | killed | other failure | F. incremental solving under assumptions |
| `prop-skip-unit-binary` | missed propagation | a unit binary clause does not force its other literal | killed | hang / timeout | H. size limits (200k variables / 1M clauses / 5M literals) |
| `prop-learnt-watch` | missed propagation | learnt clause does not watch its highest-level literal | killed | crash | G. input format and command line |
| `prop-blocker` | missed propagation | clause skipped when its blocker is merely assigned | killed | invalid model | G. input format and command line |
| `empty-input` | empty clause | an empty clause in the input is ignored | killed | wrong answer | G. input format and command line |
| `empty-unit-conflict` | empty clause | contradictory unit clauses in the input are ignored | killed | wrong answer | G. input format and command line |
| `empty-proof` | empty clause | the final empty clause is not written to the proof | SURVIVED |  |  |
| `undo-value` | state restoration | backtracking leaves the negated literal assigned | killed | crash | G. input format and command line |
| `undo-heap` | state restoration | unassigned variables are not returned to the decision heap | killed | other failure | G. input format and command line |
| `undo-level` | state restoration | backtracking keeps one decision level too many | killed | other failure | G. input format and command line |
| `undo-query` | state restoration | assignments of one incremental query leak into the next | killed | invalid model | F. incremental solving under assumptions |
| `undo-seen` | state restoration | analysis marks of minimised-away literals are not cleared (the bug that was really made during development) | killed | crash | G. input format and command line |
| `confl-binary` | ignored conflict | a falsified binary clause is not reported | killed | wrong answer | G. input format and command line |
| `confl-long` | ignored conflict | a falsified long clause is not reported | killed | crash | G. input format and command line |
| `confl-assumption` | ignored conflict | an assumption that is already false is silently accepted | killed | wrong answer | F. incremental solving under assumptions |
| `learn-uip` | wrong learning | analysis stops one resolution step early | killed | other failure | G. input format and command line |
| `learn-backjump` | wrong learning | always backjumps to level 0 while keeping the clause as reason | killed | other failure | G. input format and command line |
| `learn-minimize` | wrong learning | minimisation treats unexplored literals as redundant | killed | other failure | G. input format and command line |
| `learn-level0` | wrong learning | literals of lower levels are dropped from the learnt clause | killed | crash | G. input format and command line |
| `learn-core` | wrong learning | failed-assumption analysis keeps at most one further assumption | killed | invalid core | F. incremental solving under assumptions |
| `db-locked` | clause database | reduction may delete a clause that is the reason of an assignment | killed | crash | F. incremental solving under assumptions |
| `db-satisfied` | clause database | simplification deletes clauses that merely contain a false literal | killed | invalid model | A. differential fuzz vs brute force (n <= 18) |
| `db-strengthen` | clause database | simplification drops an unassigned literal | killed | wrong answer | A. differential fuzz vs brute force (n <= 18) |
| `db-reloc` | clause database | garbage collection does not update reason references | killed | crash | F. incremental solving under assumptions |
| `proof-learnt` | proof logging | learnt clauses are not written to the proof | killed | invalid proof | G. input format and command line |
| `proof-order` | proof logging | a strengthened clause is logged after its parent is deleted | killed | invalid proof | A. differential fuzz vs brute force (n <= 18) |
| `proof-units` | proof logging | level-0 implied literals are not logged as unit lemmas | SURVIVED |  |  |
| `proof-core` | proof logging | the clause justifying a failed-assumption core is not logged | SURVIVED |  |  |
| `out-model` | output | the model is printed with every variable true | killed | invalid model | G. input format and command line |
| `out-status` | output | resource limit is reported as UNSATISFIABLE | killed | other failure | G. input format and command line |
| `chk-always` | checker accepts illegal step | the RUP test always succeeds | killed | hand-written case | E. proof checker under attack |
| `chk-no-undo` | checker accepts illegal step | temporary assumptions of a RUP test are not undone | killed | other failure | G. input format and command line |
| `chk-unit-is-conflict` | checker accepts illegal step | a unit clause is mistaken for a falsified one | killed | hand-written case | E. proof checker under attack |
| `chk-assigned` | checker accepts illegal step | a lemma is accepted if any of its literals is assigned | killed | hand-written case | E. proof checker under attack |
| `chk-unit-refutes` | checker accepts illegal step | adding a unit clause counts as deriving the empty clause | killed | hand-written case | E. proof checker under attack |
| `chk-no-empty` | checker accepts illegal step | a proof that never derives the empty clause is accepted | killed | other failure | G. input format and command line |
| `chk-deleted` | checker accepts illegal step | deleted clauses keep taking part in propagation | killed | hang / timeout | A. differential fuzz vs brute force (n <= 18) |
| `chk-sign` | checker accepts illegal step | the checker reads negative literals as positive | killed | invalid proof | G. input format and command line |
| `chk-watch` | checker accepts illegal step | the checker forgets a moved watch (misses conflicts: too strict) | killed | invalid proof | A. differential fuzz vs brute force (n <= 18) |
| `mdl-last-clause` | model checker | the last clause is not checked | killed | hand-written case | E. proof checker under attack |
| `mdl-missing` | model checker | variables without a value are tolerated | SURVIVED |  |  |
| `mdl-sign` | model checker | negative literals are evaluated like positive ones | killed | invalid model | G. input format and command line |
| `mdl-status` | model checker | the status line is not required to say SATISFIABLE | killed | hand-written case | E. proof checker under attack |

50 mutants: 46 killed, 4 survived.
