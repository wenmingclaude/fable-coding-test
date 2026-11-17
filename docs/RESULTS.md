# 实测结果

本文所有数字都来自实际运行，原始输出保存在 `bench/results/`。凡是没有运行过的东西，本文明确写"没有运行"。

## 0. 环境与复现

| 项目 | 值 |
|---|---|
| CPU | Intel Xeon @ 2.10 GHz，4 个 vCPU（云端容器，可能与其他负载共享物理机） |
| 内存 | 15 GiB |
| 编译器 | g++ 13.3.0，`-std=c++20 -O2` |
| Python | 3.13（仅标准库） |

```sh
make && make test            # 快速测试套件
make test-full               # 完整测试套件
make sanitize                # ASan + UBSan + libstdc++ 断言构建上的快速套件
make mutation                # 变异测试
make bench                   # 第 6 节的全部基准（约 2 小时）
```

计时是挂钟时间，同一台机器上重复运行有 ±10% 左右的波动（见 E1 的对照数据），
单个实例上的差别小于这个幅度时不能下结论。

---

## 1. 摘要

| 问题 | 结论 | 依据 |
|---|---|---|
| SAT 答案是否都有效 | 完整套件中分开计数的 43 118 个 SAT 答案（含 16 364 个假设下的），以及变形、消融测试中未分开计数的答案，全部通过代入检查 | 每个 SAT 答案都由 `model_check`（C++）和 `tools/cnf.py`（Python）各代入一次 |
| UNSAT 答案是否都有证明 | 完整套件中每一个 UNSAT 答案都附有证明并通过检查（分开计数的 13 672 个无假设反驳与 16 954 个假设下的冲突核，另有变形、消融测试中的），没有一个被拒绝；最大的一份 1.28 GB（序原理 n=50） | 每个 UNSAT 答案的证明都由 `rup_check` 检查；抽样再由 Python 参考检查器（含严格删除语义）和"忽略删除行"模式检查 |
| 与暴力枚举是否一致 | 40 000 个 ≤ 18 变量的公式全部一致（另含 5 714 次消融配置运行） | 测试组 A |
| 测试能否发现植入的 bug | 50 个植入的 bug 全部被发现（第一轮 46/50，存活的 4 个暴露了测试缺口，补上后 50/50） | 第 5 节 |
| 内存错误 / 未定义行为 | ASan + UBSan + libstdc++ 断言构建上，快速套件 8 组全部通过 | 第 3.3 节 |
| 比基线 DPLL 快多少 | 47 个基准实例中解出 45 个 vs 36 个；PAR-2 总分 100 s vs 474 s。在需要"学习"的实例族上快 1–3 个数量级；在鸽巢问题上**慢** 3–5 倍 | 第 6.2 节 |
| 证书的代价 | 写证明几乎不增加求解时间（差异在噪声内）；检查时间约为求解时间的 0.5–2 倍；证明文件最大 84 MB（鸽巢 11→10） | 第 6.3 节 |
| 规模上限 | 20 万变量 / 100 万子句 / 500 万文字的实例：读入并解出用时 < 1 秒，峰值内存 87 MB（预算 1 GiB）；但同样规模内存在解不出的实例（见 6.7、第 7 节） | 第 6.7 节 |

---

## 2. 开发过程中真实出现的 bug

下面是开发中实际犯下、并被测试体系抓住的错误。它们说明各道防线确实在起作用，而不是摆设。

| # | 错误 | 后果 | 被谁发现 |
|---|---|---|---|
| 1 | 冲突分析中，学习子句最小化**原地压缩**子句后，按压缩后的子句清除 `seen` 标记，被删掉的文字的标记残留到下一次分析 | 学到不是推论的子句 → **错误的 UNSAT 答案**（第一轮 3 000 个随机小公式共报告 239 个失败，包括错误答案、被拒绝的证明，以及 bug 3 造成的工具误报；当时没有分别计数） | 同时被两道独立防线抓住：与暴力枚举的差分测试（"solver says UNSAT, instance is SAT"），以及证明检查器（"lemma 12 is not implied by unit propagation"） |
| 2 | 按决策层编号索引的数组 `lbdStamp_` 按变量数分配；但带假设求解时每个假设占一层（可以是空层），层数可以超过变量数 | 堆内存越界写 | 增量测试中进程以 `munmap_chunk(): invalid pointer` 崩溃；在 sanitizer 构建上重跑，libstdc++ 断言直接定位到越界下标 |
| 3 | Python 侧 DIMACS 读取器把头部 `p cnf` 中的 `cnf` 当作注释开头 | 测试工具误报 | 测试组 A 的"test error"（测试代码自身的异常也计为失败，不会被静默吞掉） |
| 4 | 峰值内存用 `getrusage` 的 `ru_maxrss` 测量。Linux 会把 fork 时**父进程**的内存计入子进程的这个值 | 测量错误：从 800 MB 的 Python 进程启动时，一次 13 MB 的运行被报告为 813 MB；规模测试表中所有实例都显示同一个数字 | 读结果表时发现"所有实例的内存都恰好是 153 MB"不可能；单独实验证实后改为读取 `/proc/self/status` 的 `VmHWM` |

第 1 个 bug 是这个项目的核心论点的一次实际演示：**求解器有 bug，但没有一个错误答案能通过检查。**
如果没有证明检查器，这类错误只能靠"恰好有暴力对照"才能发现；20 万变量的实例没有暴力对照可用，但检查器照样能用。
这个 bug 被保留为变异测试中的 `undo-seen`。

另有几处是**测试本身**写错了，也如实列出：
* 两个手写证明用例的预期结论写错（构造的公式本身已能被单位传播反驳，用例没有测到想测的东西），已改写；
* 经典族测试中 4 个实例被错标为"容易"档（阈值附近的隐藏解实例实际很难），超时后改为"困难"档，另补了真正容易的同族实例；
* 变异测试的第一次对照运行失败：在"超时上限 20 秒"模式下，经典族困难档中一份很大的证明，其检查时间超过了上限。检查器的超时上限因此单独放宽为求解器的 8 倍。

---

## 3. 测试结果

### 3.1 完整套件（`make test-full`）

| 组 | 内容 | 结果 | 用时 |
|---|---|---|---|
| A 差分（暴力枚举） | 40 000 个 0–18 变量的不规则公式；另以 15 种消融配置重跑其中 5 714 个 | 通过：40 000 个答案与暴力枚举一致（26 549 SAT / 13 451 UNSAT），基线 DPLL 也全部一致；1 363 份证明另经 Python 参考检查器（含严格删除语义）和"忽略删除行"模式复查 | 175 s |
| B 构造已知 | 180 个隐藏解实例（20–20 000 变量），180 个混入矛盾部件的实例，12 个隐藏鸽巢核 | 通过；1 个隐藏解 5-SAT 实例（n=1000）300 秒超时，其余全部给出正确且经验证的答案 | 305 s |
| C 经典族 | 46 个实例，分容易 / 困难两档 | 通过：容易档全部解出；困难档 3 个超时（见下表） | 300 s |
| D 变形 | 1 518 个基础公式 × 7 种变换 = 10 626 个变体 | 通过：每个变体的结论与原公式相同，且各自的证书都通过检查 | 24 s |
| E 检查器对抗 | 26 个手写证明、20 个手写赋值、500 份合法证明及其 6 000 个随机破坏版本、4 000 个"差一点正确"的证明、4 962 个伪造证明 | 通过：C++ 与 Python 检查器在 31 924 次判定中全部一致；没有一个针对可满足公式的证明被接受 | 79 s |
| F 增量 | 1 512 个会话、41 368 次查询 | 通过：16 364 个 SAT 答案、16 954 个冲突核、8 050 个"公式本身无解"答案全部独立验证 | 43 s |
| G 输入格式 | 21 个合法边界输入、15 个非法输入、资源上限 | 通过：三个独立读取器对合法输入结论一致，对非法输入全部报错 | 1 s |
| H 规模上限 | 10 个实例 | **1 个失败**（30 000 输入的奇偶矛盾 900 秒超时，见 6.7 节）；其余 9 个通过。把该行换成 4 000 输入后单独重跑本组：10 个全部通过 | 453 s |

C 组困难档的超时：隐藏解 3-SAT n=5000 r=5.0、隐藏解 3-SAT n=20000 r=6.0、隐藏解 5-SAT n=1000 r=18（各 120 秒）。
困难档中解出的较大实例：

| 实例 | 冲突 | 求解 s | 证明 | 检查 s |
|---|---|---|---|---|
| 鸽巢 10→9 | 66 265 | 3.1 | 11.8 MB | 5.0 |
| 随机 3-SAT n=250（UNSAT） | 276 366 | 9.0 | 31.1 MB | 11.7 |
| 乱序奇偶矛盾 n=40 | 1 448 056 | 22.3 | 133 MB | 51.3 |
| 序原理 n=30 | 1 591 226 | 29.4 | 412 MB | 37.4 |
| 序原理 n=50 | 3 065 943 | 71.0 | **1.28 GB** | 90.6 |

### 3.2 快速套件（`make test`）

同样 8 组，实例数约为完整套件的 1/10，规模组的实例缩小 10 倍。最终代码上的结果：**8 组全部通过**（175 秒，`bench/results/test_quick.log`）。

### 3.3 Sanitizer（`make sanitize`）

快速套件的全部 8 组，跑在 `-fsanitize=address,undefined -fno-sanitize-recover=all -D_GLIBCXX_ASSERTIONS` 构建上
（求解器、两个检查器、暴力枚举、基线全部如此构建）。**8 组全部通过**（227 秒）：没有任何地址错误、越界、使用已释放内存、未定义行为或 libstdc++ 断言失败的报告。这只说明在**被执行到的路径**上没有检测到这些问题，不是对所有输入的保证。

开发过程中 sanitizer 抓到过一次真实错误（第 2 节 bug 2）。

---

## 4. 证书检查汇总

完整测试套件中，求解器给出的每一个 SAT / UNSAT 答案都经过了独立检查，**没有一个证书被拒绝**：

| 来源 | SAT：赋值经 `model_check` 与 Python 代入检查 | UNSAT：证明经 `rup_check` 检查 |
|---|---|---|
| A 差分（默认配置） | 26 549 | 13 451 |
| A 差分（15 种消融配置） | 5 714 次运行，SAT 与 UNSAT 合计 | |
| B 构造已知 | 179 | 192 |
| C 经典族 | 21 | 25 |
| D 变形 | 10 626 个变体，SAT 与 UNSAT 合计 | |
| G 输入格式 | 21 个，合计 | |
| H 规模上限 | 5 | 4 |
| F 增量 | 16 364（赋值同时满足所有假设） | 8 050 个"公式本身无解"由会话证明直接反驳；16 954 个"在假设下无解"由 `rup_check --assume 冲突核` 检查 |

带假设的 UNSAT 答案要过两关：冲突核在**不使用增量求解器**的情况下被独立判定为不可满足（小公式用暴力枚举，
大公式用一次全新的、带证明检查的求解）；会话证明在 `--assume 冲突核` 下推出空子句。冲突核平均包含查询中 48% 的假设文字
（29 830 / 62 379），也就是说，它通常确实比全部假设小。

暴力枚举的对照范围：变量数 ≤ 18。更大的实例上，答案的正确性**只**由证书保证（加上构造已知的结论，若有）。
这正是本项目的设计意图：20 万变量的公式无法穷举，但它的证明照样可以检查。

---

## 5. 变异测试

`tests/mutation.py`：50 个人为植入的 bug，每个是源代码中的一处文本替换（求解器 37 个、证明检查器 9 个、赋值检查器 4 个）。
对每个变异体：复制源码 → 打补丁 → 编译 → 用 `--fail-fast` 跑快速测试套件，单次求解超时上限 30 秒。
运行前先做对照：未打补丁的源码以同样方式编译，必须通过全部测试（对照运行用时 178 秒，通过）。

### 5.1 第一轮：46 / 50

第一轮有 4 个变异体存活。逐个分析后发现，它们都对应**文档承诺了、但没有测试强制检查**的行为：

| 存活的变异体 | 为什么没被发现 | 补上的测试 |
|---|---|---|
| `empty-proof`：证明末尾不写空子句 | 检查器只要求"某一时刻单位传播得到冲突"，而最后一条单位引理加入后传播就已经冲突，显式的空子句是多余的 | 要求证明文件的最后一行是 `0`（PROOF_FORMAT.md 承诺了这一点） |
| `proof-units`：第 0 层推出的文字不写成单位引理 | 本项目检查器的删除语义是宽松的：删除子句后，已推出的文字仍然保留（这是可靠的，见 PROOF_FORMAT.md §3.4）。所以即使求解器没写单位引理，检查器也不会察觉 | 在 Python 参考检查器中增加"严格删除语义"模式（每次删除后从头重新推导），所有抽样证明必须在该模式下也通过；另加一个能确定性地触发该情形的手写公式 |
| `proof-core`：冲突核对应的子句不写进证明 | 冲突核的正确性是用 `rup_check --assume` 检查的，而只要会话证明中有足够的学习子句，`--assume` 下传播就会冲突，不需要那条子句 | 要求子句 `(¬c₁ ∨ … ∨ ¬c_k)` 确实作为一条引理出现在会话证明中 |
| `mdl-missing`：赋值检查器容忍没给值的变量 | 手写的赋值用例里，所有变量都出现在某个子句中，漏掉任何一个都会让某个子句无法满足 | 增加一个不出现在任何子句中的变量，以及"漏掉它"的用例 |

其中 `proof-units` 揭示了一个值得写明的设计问题：本项目的证明在**本项目的**检查器下有效并不依赖单位引理，
但在严格解释删除的检查器（如某些第三方工具）下会依赖。补上的测试保证证明在两种解释下都有效。

第一轮的完整结果保存在 `bench/results/mutation_first_run.*`。

### 5.2 最终结果：50 / 50

补上测试后，在最终代码上重跑全部 50 个变异体：**全部被发现**。

| mutant | category | injected bug | result | detected as | by test group | first failure reported |
|---|---|---|---|---|---|---|
| `neg-asserting` | wrong negation | learnt clause gets the UIP literal with the wrong sign | killed | wrong output | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=20) |
| `neg-enqueue` | wrong negation | assigning p does not make ¬p false | killed | invalid model | G. input format and command line | INVALID MODEL (model_check) on f0000019.cnf (options none): c clause 2 is falsified |
| `neg-parse` | wrong negation | negative input literals are read as positive | killed | invalid model | G. input format and command line | INVALID MODEL (model_check) on f0000019.cnf (options none): c clause 2 is falsified |
| `neg-tautology` | wrong negation | x ∨ ¬x is 'simplified' by dropping ¬x instead of the clause | killed | wrong answer | A. differential fuzz vs brute force | WRONG ANSWER: solver says UNSAT, instance is SAT by construction: f0000098.cnf (options none) |
| `neg-final` | wrong negation | failed-assumption core is returned without negating back | killed | invalid core | F. incremental solving under assumptions | core [5, -1] is not a subset of the assumptions: f0016150.cnf, assumptions [1, 1, -5] |
| `prop-lost-watch` | missed propagation | a moved watch is not registered with the new literal | killed | wrong output | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=10) |
| `prop-qhead` | missed propagation | after backtracking the propagation queue is not rewound | killed | crash | G. input format and command line | solver printed 0 status lines on f0000025.cnf (options none); rc=-6; output tail: |
| `prop-skip-unit-long` | missed propagation | a unit long clause does not force its last literal | killed | invalid proof | A. differential fuzz vs brute force | INVALID PROOF on f0000971.cnf (options none): the proof does not end with the empty clause |
| `prop-skip-unit-binary` | missed propagation | a unit binary clause does not force its other literal | killed | invalid proof | G. input format and command line | INVALID PROOF on f0000025.cnf (options none): the proof does not end with the empty clause |
| `prop-learnt-watch` | missed propagation | learnt clause does not watch its highest-level literal | killed | crash | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=-11) |
| `prop-blocker` | missed propagation | clause skipped when its blocker is merely assigned | killed | wrong output | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=10) |
| `empty-input` | empty clause | an empty clause in the input is ignored | killed | wrong answer | G. input format and command line | WRONG ANSWER: solver says SAT, instance is UNSAT by construction: f0000007.cnf (options none) |
| `empty-unit-conflict` | empty clause | contradictory unit clauses in the input are ignored | killed | wrong answer | G. input format and command line | WRONG ANSWER: solver says SAT, instance is UNSAT by construction: f0000040.cnf (options none) |
| `empty-proof` | empty clause | the final empty clause is not written to the proof | killed | invalid proof | G. input format and command line | INVALID PROOF on f0000007.cnf (options none): the proof does not end with the empty clause |
| `undo-value` | state restoration | backtracking leaves the negated literal assigned | killed | crash | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=-11) |
| `undo-heap` | state restoration | unassigned variables are not returned to the decision heap | killed | wrong output | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=10) |
| `undo-level` | state restoration | backtracking keeps one decision level too many | killed | wrong output | G. input format and command line | case 'several clauses on one line': got UNKNOWN |
| `undo-query` | state restoration | assignments of one incremental query leak into the next | killed | invalid model | F. incremental solving under assumptions | INVALID MODEL under assumptions (assumption 1 false): f0016145.cnf, assumptions [1, 1] |
| `undo-seen` | state restoration | analysis marks of minimised-away literals are not cleared (the bug that was really made during development) | killed | crash | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=-11) |
| `confl-binary` | ignored conflict | a falsified binary clause is not reported | killed | wrong answer | G. input format and command line | WRONG ANSWER: solver says SAT, instance is UNSAT by construction: f0000025.cnf (options none) |
| `confl-long` | ignored conflict | a falsified long clause is not reported | killed | crash | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=-6) |
| `confl-assumption` | ignored conflict | an assumption that is already false is silently accepted | killed | wrong answer | F. incremental solving under assumptions | WRONG ANSWER (SAT, should be UNSAT): f0016145.cnf, assumptions [4, -4] |
| `learn-uip` | wrong learning | analysis stops one resolution step early | killed | wrong output | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=20) |
| `learn-backjump` | wrong learning | always backjumps to level 0 while keeping the clause as reason | killed | wrong output | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=20) |
| `learn-minimize` | wrong learning | minimisation treats unexplored literals as redundant | killed | wrong output | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=20) |
| `learn-level0` | wrong learning | literals of lower levels are dropped from the learnt clause | killed | crash | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=-11) |
| `learn-core` | wrong learning | failed-assumption analysis keeps at most one further assumption | killed | invalid core | F. incremental solving under assumptions | INVALID CORE: formula ∧ [3, 11] is satisfiable: f0016159.cnf, assumptions [8, 11, 3, 6, -4, -9, 8] |
| `db-locked` | clause database | reduction may delete a clause that is the reason of an assignment | killed | crash | F. incremental solving under assumptions | incremental session failed (rc=-6) on f0016895.cnf f0016898.queries:  -276 277 278 |
| `db-satisfied` | clause database | simplification deletes clauses that merely contain a false literal | killed | wrong answer | G. input format and command line | WRONG ANSWER: solver says SAT, instance is UNSAT by construction: f0000061.cnf (options none) |
| `db-strengthen` | clause database | simplification drops an unassigned literal | killed | invalid proof | G. input format and command line | INVALID PROOF on f0000061.cnf (options none): the proof does not end with the empty clause |
| `db-reloc` | clause database | garbage collection does not update reason references | killed | crash | G. input format and command line | time limit did not produce s UNKNOWN (rc=-11) |
| `proof-learnt` | proof logging | learnt clauses are not written to the proof | killed | invalid proof | G. input format and command line | INVALID PROOF on f0000025.cnf (options none): c formula: 2 variables, 4 clauses |
| `proof-order` | proof logging | a strengthened clause is logged after its parent is deleted | killed | invalid proof | G. input format and command line | INVALID PROOF on f0000061.cnf (options none): c formula: 4 variables, 6 clauses |
| `proof-units` | proof logging | level-0 implied literals are not logged as unit lemmas | killed | invalid proof | G. input format and command line | INVALID PROOF on f0000061.cnf (options none): rejected under strict deletion semantics (a top-level literal wa… |
| `proof-core` | proof logging | the clause justifying a failed-assumption core is not logged | killed | invalid proof | F. incremental solving under assumptions | INVALID PROOF: the clause justifying core [-4, 4] was not logged: f0016145.cnf, assumptions [4, -4] |
| `out-model` | output | the model is printed with every variable true | killed | invalid model | G. input format and command line | INVALID MODEL (model_check) on f0000019.cnf (options none): c clause 2 is falsified |
| `out-status` | output | resource limit is reported as UNSATISFIABLE | killed | wrong output | G. input format and command line | conflict limit did not produce s UNKNOWN (rc=20) |
| `chk-always` | checker accepts illegal step | the RUP test always succeeds | killed | hand-written case | E. proof checker under attack | hand-written case 'empty clause claimed too early' (ignore_deletions=False): expected False, C++ True, referen… |
| `chk-no-undo` | checker accepts illegal step | temporary assumptions of a RUP test are not undone | killed | other failure | G. input format and command line | proof stream of a satisfiable run was accepted as a refutation |
| `chk-unit-is-conflict` | checker accepts illegal step | a unit clause is mistaken for a falsified one | killed | hand-written case | E. proof checker under attack | hand-written case 'lemma that is not implied by the formula' (ignore_deletions=False): expected False, C++ Tru… |
| `chk-assigned` | checker accepts illegal step | a lemma is accepted if any of its literals is assigned | killed | hand-written case | E. proof checker under attack | hand-written case "lemma equal to the negation of the formula's only model" (ignore_deletions=False): expected… |
| `chk-unit-refutes` | checker accepts illegal step | adding a unit clause counts as deriving the empty clause | killed | other failure | G. input format and command line | proof stream of a satisfiable run was accepted as a refutation |
| `chk-no-empty` | checker accepts illegal step | a proof that never derives the empty clause is accepted | killed | other failure | G. input format and command line | proof stream of a satisfiable run was accepted as a refutation |
| `chk-deleted` | checker accepts illegal step | deleted clauses keep taking part in propagation | killed | invalid proof | G. input format and command line | INVALID PROOF on f0000061.cnf (options none): c formula: 4 variables, 6 clauses |
| `chk-sign` | checker accepts illegal step | the checker reads negative literals as positive | killed | invalid proof | G. input format and command line | INVALID PROOF on f0000025.cnf (options none): c formula: 2 variables, 4 clauses |
| `chk-watch` | checker accepts illegal step | the checker forgets a moved watch (misses conflicts: too strict) | killed | invalid proof | A. differential fuzz vs brute force | INVALID PROOF on f0000178.cnf (options none): c formula: 16 variables, 62 clauses |
| `mdl-last-clause` | model checker | the last clause is not checked | killed | other failure | E. proof checker under attack | model_check case 'last clause falsified': expected False, got True |
| `mdl-missing` | model checker | variables without a value are tolerated | killed | other failure | E. proof checker under attack | model_check case 'unused variable without a value': expected False, got True |
| `mdl-sign` | model checker | negative literals are evaluated like positive ones | killed | invalid model | G. input format and command line | INVALID MODEL (model_check) on f0000016.cnf (options none): c clause 1 is falsified |
| `mdl-status` | model checker | the status line is not required to say SATISFIABLE | killed | other failure | E. proof checker under attack | model_check case 'status is UNSATISFIABLE': expected False, got True |

50 mutants: 50 killed, 0 survived.

几点说明：

* 由于使用 `--fail-fast`，"由哪个测试组发现"只是**第一个**发现它的组（G 组最先运行，所以名列最多），不代表其他组发现不了。
  例如 `neg-asserting` 在 G 组是因为它在 50 次冲突内就（凭错误的推理）给出了答案，而测试要求此时输出 UNKNOWN；
  同一个变异体也会让学到的子句无法通过 RUP 检查。没有为每个变异体单独记录"所有能发现它的组"（那需要约 2.5 小时）。
* 证明检查器的 9 个变异体中，8 个让它接受非法步骤，1 个（`chk-watch`）让它过严。8 个"错误接受"全部被发现，
  主要靠第 E 组的手写证明和"差一点正确"的证明——在其他组里，一个错误接受一切的检查器看起来与正确的检查器毫无区别，
  因为求解器交来的证明本来就是正确的。这正是 E 组存在的理由。
* 变异体是由写测试的同一个人设计的，难免有盲区；变异测试得分是**经验证据**，不是正确性证明。

---

## 6. 性能

### 6.1 时间花在哪里

`valgrind --tool=callgrind`，阈值附近随机 3-SAT（200 变量、852 子句，UNSAT，30 023 次冲突）：

| 函数 | 指令占比 |
|---|---|
| `propagate`（含内联的 vector 操作） | **80.1%** |
| `analyze` + `litRedundant`（冲突分析与最小化） | 8.0% |
| `cancelUntil`（回退） | 1.9% |
| `heapUp` / `bumpVar`（VSIDS） | 2.2% |

单位传播压倒性地占据了运行时间，所以优化应当集中在它上面。传播内部的工作（求解器自带计数器，同一实例）：

| 监视表项访问 | 3 535 万次 |
|---|---|
| 其中被阻塞文字直接跳过（子句已满足，不读子句内存） | 59.7% |
| 需要读子句内存 | 40.3% |

也就是说，**一多半的"访问一条子句"其实是无效扫描**：那条子句早已被满足。阻塞文字把这部分访问从
"读监视表 + 读子句（一次很可能的缓存缺失）"降为"只读监视表"。

缓存模拟（`valgrind --tool=cachegrind`，同一实例）：

| 配置 | 冲突次数 | L1 数据缓存缺失 | 每次冲突的 L1 缺失 |
|---|---|---|---|
| 默认 | 30 023 | 2 864 万 | 954 |
| `--no-blocker` | 25 622 | 3 607 万 | 1 408 |

去掉阻塞文字后每次冲突的 L1 缺失多出约 48%。（注意：去掉阻塞文字会改变监视表的顺序，从而改变搜索路径，
所以两次运行的冲突次数不同，只能按每次冲突归一化比较。这个实例只有 200 个变量，整个工作集都在 L2 里，
因此末级缓存缺失可以忽略；在大实例上缓存缺失的代价更高。）

二元子句内联在二元子句多的实例上作用明显：一个 18 万变量、60 万子句的 3-着色实例中，83.3% 的监视表访问是二元子句，
内联后它们完全不读子句内存。但对挂钟时间的影响只有 0.230 s 对 0.258 s（约 11%），因为这个实例本身几乎不需要搜索（140 次冲突）。

### 6.2 基线 DPLL 与最终求解器

基线 `baseline/dpll.cpp`：同样的双监视文字传播，但没有学习、没有非时序回退、没有重启，变量顺序固定（出现次数多者优先）。
47 个实例、每次运行 20 秒上限、顺序执行（一次只跑一个进程）。完整的逐实例表格见 `bench/results/bench_main.md`。

| 配置 | 解出 | PAR-2 总分 (s) | 鸽巢 | 随机 3-SAT（阈值） | 隐藏解 | 图着色 | 奇偶 | 序原理 | 传播链 | 重复 / 隐藏核 | 大规模 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 基线 DPLL | 36/47 | 473.8 | 4/4, 1.0 | 10/10, 16.0 | 3/7, 160.0 | 7/7, 1.4 | 6/8, 94.3 | 0/3, 120.0 | 3/3, 0.2 | 1/3, 80.1 | 2/2, 0.8 |
| 最终 CDCL | 45/47 | 100.4 | 4/4, 3.0 | 10/10, 5.1 | 5/7, 85.2 | 7/7, 0.2 | 8/8, 1.2 | 3/3, 4.2 | 3/3, 0.3 | 3/3, 0.2 | 2/2, 1.1 |

单元格：解出数/实例数，PAR-2 秒数（超时按 2 × 时限计）。所有配置给出答案的实例上，SAT/UNSAT 结论彼此一致，且与构造已知的结论一致。

挑出几行有代表性的：

| 实例 | 答案 | DPLL | CDCL | 说明 |
|---|---|---|---|---|
| 奇偶矛盾，同序，60 个输入 | UNSAT | >20 s | 0.00 s（523 次冲突） | DPLL 的搜索树是指数大小；CDCL 学到的子句相当于沿链条传递的中间奇偶值 |
| 序原理，14 个元素 | UNSAT | >20 s | 0.01 s | 已知有多项式长度的归结证明，但任何树状搜索都是指数的——这正是"学习"的作用 |
| Tseitin 5×5 环面 | UNSAT | 10.3 s（6 711 万次决策） | 0.41 s | |
| 隐藏鸽巢核（3 000 变量的可满足部分 + 6→5 鸽巢） | UNSAT | >20 s | 0.01 s | DPLL 在无关的可满足部分里反复回溯；CDCL 的冲突分析只把相关变量的活跃度调高 |
| 鸽巢 10→9 | UNSAT | **0.90 s** | **2.59 s** | 见下 |
| 隐藏解 3-SAT，2 000 变量，r=3.8 (a) | SAT | **0.01 s** | **0.83 s** | 见下 |
| 20 万变量 / 100 万子句 隐藏解 5-SAT | SAT | 0.51 s | 0.71 s | 两者都几乎不需要搜索；时间主要是读文件 |

**CDCL 并非处处更快**，三种情况下基线更快：

1. **鸽巢问题**：任何归结证明都是指数长的（Haken 1985），学习无法缩短它。CDCL 只经历了 66 265 次冲突（DPLL 做了 491 万次决策），
   但每次冲突要做分析、维护子句库，单位时间做的事更多，总体反而慢 3 倍。学习只能消除"重复的"工作，而鸽巢问题的难处不在重复。
2. **DPLL 的固定顺序恰好合适的实例**：DPLL 只用 1 320 次决策、646 次冲突就找到解，CDCL 用了 33 124 次冲突。
   DPLL 的变量顺序是"出现次数多者优先、取出现次数多的符号"，在隐藏解实例上这恰好朝着答案走（原因见 6.6 节 E4）。
   这是运气成分很大的比较——同族另一个种子 (b) 上 DPLL 超时而 CDCL 用 1.28 s。
3. **纯读文件的大实例**：慢 30–40%。可能的原因是 CDCL 要多建一些数据结构（活跃度堆、子句元数据、初始相位统计），没有单独测量。

### 6.3 证书的代价

26 个 UNSAT 实例，默认配置。完整表格见 `bench/results/bench_main.md`。

| 实例 | 求解 s | 带证明求解 s | 证明大小 | 检查 s | 检查 / 求解 |
|---|---|---|---|---|---|
| 鸽巢 10→9 | 2.59 | 2.56 | 11.7 MB | 4.35 | 1.7× |
| 随机 3-SAT 250 变量 (b) | 1.71 | 1.56 | 10.1 MB | 2.23 | 1.3× |
| 序原理 26 | 3.85 | 3.77 | 70.5 MB | 4.98 | 1.3× |
| Tseitin 5×5 | 0.41 | 0.42 | 4.2 MB | 0.63 | 1.6× |
| 奇偶矛盾 2000 | 0.69 | 0.83 | 0.8 MB | 0.03 | 0.04× |
| 20 万变量单位传播链 | 0.15 | 0.10 | < 1 KB | 0.12 | 0.8× |

* 写证明不改变搜索路径，只是追加文本；带证明与不带证明的时间差在测量噪声以内。
* 检查时间与求解时间同一量级（26 个实例中 0.04×–2.0×）。这说明本检查器并不"便宜"——它要把每条引理的单位传播重做一遍。
  它便宜的地方在于**代码**：362 行（加 168 行输入解析），只有一种推理规则。
* 证明可以很大：鸽巢 11→10 的证明 84 MB，检查 58.6 s（见 WALKTHROUGH.md 的增长表）。

### 6.4 逐项消融（每次关掉一个特性）

47 个实例，每个配置只关掉一项，20 秒上限，2 个进程并行。完整数据见 `bench/results/bench_ablation.md`。

| 配置 | 解出 | PAR-2 (s) |
|---|---|---|
| 默认 | 46/47 | 80.2 |
| `--no-minimize` | 46/47 | 80.3 |
| `--no-phase-saving` | 46/47 | **50.2** |
| `--no-reduce` | 45/47 | 103.6 |
| `--no-blocker` | 46/47 | 82.0 |
| `--no-inline-binary` | 46/47 | 79.0 |
| `--no-vsids`（固定变量顺序） | **35/47** | **516.0** |
| `--no-simplify` | 45/47 | 100.1 |
| `--no-stable` | **41/47** | **253.9** |
| `--no-target-phase` | 42/47 | 235.4 |
| `--restart=luby` | 45/47 | 97.8 |
| `--restart=none` | **41/47** | **248.9** |
| `--phase=0` | 45/47 | 120.7 |
| `--phase=1` | 44/47 | 149.1 |

（同一个默认配置在 6.2 节顺序执行时解出 45/47、在这里并行执行时解出 46/47：差别来自一个求解时间约 19–20 秒、
恰好落在时限边缘的实例。这本身就是下面要说的问题。）

**这张表里只有几条结论是可靠的**：VSIDS 和重启不可或缺——关掉 VSIDS 多出 11 个超时，关掉重启多出 5 个，
都分布在多个实例族上；关掉稳定模式或目标相位分别多出 5 个和 4 个超时，但都集中在隐藏解族。

**其余各行的差别不可信。** 逐实例拆开看，`--no-phase-saving`、`--no-simplify`、`--restart=luby` 与默认之间的 PAR-2 差距，
各有 20 秒左右来自同一个实例 `planted-2000-r4.0`——它在默认配置下约 19 秒解出，稍有扰动就越过 20 秒的时限，
PAR-2 立刻多算 20 秒。一个实例决定了整张表的排名。这促成了下一节的多种子实验。

### 6.5 多随机种子的消融

每个实例族用 8 个不同的随机种子生成 8 个实例，30 秒上限。单元格：解出数 / 中位时间 / PAR-2 秒数。
一个配置必须在多数种子上占优，而不是靠一个幸运的实例。原始输出见 `bench/results/bench_seeds*.{md,log}`。

| 实例族 | 默认 | `--no-phase-saving` | `--no-minimize` | `--no-blocker` | `--no-stable` | `--phase=0` |
|---|---|---|---|---|---|---|
| 隐藏解 3-SAT n=2000 r=4.0 | 6 / 21.6 / 226 | **8 / 2.4 / 21** | 5 / 17.1 / 223 | 7 / 17.9 / 144 | 0 / 30 / 480 | 3 / 30 / 322 |
| 隐藏解 3-SAT n=5000 r=3.8 | 2 / 30 / 377 | **8 / 0.9 / 13** | 4 / 29.0 / 321 | 3 / 30 / 338 | 0 / 30 / 480 | 5 / 24.5 / 249 |
| 随机 3-SAT n=250 r=4.26 | 8 / 0.61 / 8 | 8 / 0.33 / 6 | 8 / 0.62 / 9 | 8 / 0.74 / 8 | 8 / 0.94 / 7 | 8 / 0.65 / 8 |
| 3-着色（隐藏着色）n=800 m=1840 | **7 / 7.6 / 115** | 5 / 14.4 / 218 | 3 / 30 / 335 | 6 / 20.5 / 194 | 4 / 29.5 / 344 | 7 / 7.3 / 115 |
| Tseitin 5×5 环面 | 8 / 0.42 / 3 | 8 / 0.36 / 3 | 8 / 0.40 / 3 | 8 / 0.56 / 4 | 8 / 0.43 / 3 | 8 / 0.44 / 4 |
| 奇偶矛盾，乱序，n=24 | 8 / 0.05 / 1 | 8 / 0.06 / 1 | 8 / 0.06 / 1 | 8 / 0.05 / 1 | 8 / 0.03 / 1 | 8 / 0.05 / 1 |
| **合计** | **39 / 730** | **45 / 262** | 36 / 892 | 40 / 690 | 28 / 1316 | 39 / 699 |

所有配置在所有实例上的答案一致。

读法：

* **稳定模式**（`--no-stable`）：再次确认必要，关掉后两个隐藏解族 16 个实例一个也解不出，着色族也从 7/8 降到 4/8。
* **学习子句最小化**（`--no-minimize`）：总体有益（36 对 39 解出），在着色族上最明显（3/8 对 7/8）。
  它在单实例消融里看起来"没有作用"（80.3 对 80.2），那是被一个边缘实例掩盖了。
* **阻塞文字**（`--no-blocker`）：解出数 40 对 39、PAR-2 690 对 730——**没有可测的整体收益**。
  它确实减少了内存访问（6.1 节：每次冲突的 L1 缺失少 1/3），但在这些实例上，改变监视表顺序对搜索路径的影响
  比省下的访存更大，两者混在一起，看不出净效果。
* **初始相位按出现次数取值**（默认）对比固定取 0（`--phase=0`）：39/730 对 39/699，**没有可测的差别**。
  这项改动是根据单实例消融（它让 `core-50000` 从超时变成 0.12 秒）加入默认配置的，多种子数据并不支持它，
  但也不反对。它被保留，但我不认为它被证明有效（见实验 E3）。
* **相位保存**（`--no-phase-saving`）：见实验 E4，这是最出乎意料的结果。

### 6.6 失败或未采用的优化实验

每个实验都保留了可复现的开关和原始数据。

#### E1 监视表软件预取 —— 无效，未采用

想法：一个文字被赋值时，它的监视表稍后会被扫描；在 `enqueue` 中提前 `__builtin_prefetch` 这个监视表的首地址，
把内存延迟藏在别的工作后面。

编译开关 `-DSAT_PREFETCH`（`make build/sat_solver_prefetch`），8 个实例各运行 3 次取中位数。
搜索路径完全相同（冲突次数逐个一致），因此差别只来自访存：

| 实例 | 默认 s | 预取 s | 比值 |
|---|---|---|---|
| rand-250-b | 1.55 | 1.58 | 1.02 |
| php-10-9 | 2.57 | 2.50 | 0.97 |
| ordering-26 | 3.61 | 3.76 | 1.04 |
| planted-5000-r3.7 | 2.67 | 2.47 | 0.92 |
| planted-2000-r3.8-b | 1.36 | 1.38 | 1.01 |
| tseitin-5x5 | 0.38 | 0.41 | 1.09 |
| parity-same-2000 | 0.75 | 0.75 | 1.01 |
| col3-planted-600-1350 | 0.17 | 0.15 | 0.86 |

几何平均比值 0.989，逐实例在 0.86–1.09 之间，正负都有，在测量噪声以内。结论：**没有可测的收益**，不启用。
可能的原因（未验证）：这些实例的工作集大多装得进 L2 缓存；并且从赋值到扫描之间的间隔太短，预取来不及生效。

#### E2 按出现次数决定初始决策顺序 —— 结果两极分化，未采用

编译开关 `-DSAT_OCC_ORDER`（`make build/sat_solver_occorder`）：开始时按变量出现次数设置初始活跃度，让出现次数多的变量先被决策。

单次运行（`bench/results/exp_occorder.md`，14 个实例）的几何平均比值是 **0.486**，看起来快了一倍。逐实例看：

| 实例 | 默认 s | E2 s | 比值 |
|---|---|---|---|
| planted-5000-r3.7 | 2.94 | 0.01 | 0.00 |
| planted-2000-r3.8-a | 1.02 | 0.01 | 0.01 |
| planted-20000-r3.5 | 0.39 | 0.05 | 0.12 |
| col3-planted-600-1350 | 0.21 | 0.05 | 0.23 |
| rand-250-a | 3.19 | 2.06 | 0.64 |
| core-50000 | 0.15 | 0.09 | 0.64 |
| php-9-8、ordering-20、tseitin-5x5 | | | 0.83–1.23（冲突次数完全相同） |
| planted-2000-r3.8-b | 1.75 | 1.84 | 1.05 |
| rand-250-b | 2.06 | 2.30 | 1.12 |
| rand-200-b | 0.02 | 0.06 | 2.36 |
| planted-2000-r4.0 | 22.4 | 58.2 | 2.60 |
| parity-shuffled-24 | 0.10 | 1.13 | **11.6** |

几何平均被四个隐藏解实例（其中一个是隐藏着色）主导——在那里出现次数统计泄露了答案（见 E4）。多种子实验（`bench/results/bench_seeds_occorder.md`）
给出的是另一幅图景：**37 个解出 / PAR-2 772，默认配置是 39 / 730**。隐藏解 n=2000 族 4/8 对 6/8，n=5000 族 4/8 对 2/8，
着色族 5/8 对 7/8。结论：没有可测的整体收益，未采用。这也是"几何平均数会骗人"的一个实例。

#### E3 按出现次数决定初始取值 —— 采用，但证据薄弱

想法：变量第一次被决策时，取能直接满足更多子句的那个值。

单实例消融（当时的默认是固定取 0）中，PAR-2 从 118.9 降到 78.1，主要来自两个实例：`core-50000`（超时 → 0.12 s）
和 `planted-20000-r3.5`（4.5 s → 0.37 s）；同时 `rand-250-a` 从 0.26 s 变慢到 2.85 s。据此把它设为默认。

事后的多种子实验（6.5 节）显示它与固定取 0 **没有可测的差别**（39/730 对 39/699），而且在两个隐藏解族上方向相反
（n=2000：6/8 对 3/8；n=5000：2/8 对 5/8）。如实记录：这是一个依据不足的决定。它没有被撤回，因为数据同样不能说明它有害；
`--phase=0` 可以恢复原来的行为。

#### E4 相位保存 —— 在本基准集上整体有害，原因只解释了一部分，默认未改

多种子实验中，关掉相位保存在两个隐藏解族上解出 16/16，默认只有 8/16；在着色族上却是 5/8 对 7/8。

**假设**：隐藏解实例的生成方式会泄露答案。生成器只保留被隐藏赋值满足的子句，于是每个变量"出现次数更多的那个符号"
倾向于与隐藏赋值一致。实测：n=2000 和 n=5000 两族中，多数符号与隐藏赋值一致的变量占 64.2% 和 63.4%（另有约 10% 平票）。
关掉相位保存后，每次决策都使用初始相位，也就是"多数符号"——等于一直朝隐藏解的方向猜。

**对照实验**：如果假设完全成立，关掉相位保存的同时把初始相位固定为 0 或 1（不再携带泄露的信息），优势应当消失。

| 实例族（8 个种子） | 默认 | 关相位保存（初始相位 = 多数符号） | 关相位保存，初始相位 = 0 | 关相位保存，初始相位 = 1 |
|---|---|---|---|---|
| 隐藏解 3-SAT n=2000 r=4.0 | 6 / 21.5 / 225 | 8 / 2.5 / 21 | 8 / 3.7 / 56 | 7 / 5.7 / 95 |
| 隐藏解 3-SAT n=5000 r=3.8 | 2 / 30 / 377 | 8 / 0.95 / 12 | 5 / 27.5 / 273 | 6 / 13.1 / 196 |
| 3-着色 n=800 m=1840 | 7 / 7.1 / 115 | 5 / 14.9 / 220 | 5 / 15.0 / 221 | 5 / 4.4 / 191 |

结果**部分推翻**了假设：
* 泄露确实贡献了一部分优势：n=5000 族中，多数符号版本 8/8，固定相位版本只有 5/8、6/8。
* 但即使用不携带信息的固定相位，关掉相位保存在两个隐藏解族上仍然比默认好（n=2000：8/8、7/8 对 6/8；n=5000：5/8、6/8 对 2/8）。
  所以有另一个因素，我没有找到并验证它。一个未经检验的猜测：相位保存与稳定模式的"目标相位"叠加后，
  求解器会长时间停留在某个"几乎满足"的局部区域；每次都从固定相位出发则提供了更多样化的起点。
* 在着色族上，三种关掉相位保存的变体都比默认差。

**决定**：默认仍开启相位保存。理由：效果的方向随实例族而反转；占优的两个族出自同一个生成器（隐藏解随机 3-SAT），
在 6 个族中权重过大；我没有在真实世界（工业）实例上测试过，而那是相位保存最初被提出时针对的场景。
这个决定基于判断而不是本项目的数据——**就本项目的基准集而言，数据支持关掉它**。需要时可用 `--no-phase-saving`。

#### 教训

单实例、单次运行的消融表会被时限边缘的实例主导。本项目最初根据这样一张表做出了 E3 的决定，后来的多种子数据没有证实它。
多种子数据又暴露了一个由生成器带来的偏差（E4）：在某个实例族上快，与"对 SAT 问题快"是两回事。

### 6.7 规模上限

`make test-full` 的 H 组（最终代码，`bench/results/test_scale_full.log`）。内存是求解器 / 检查器进程自己报告的峰值常驻内存（`VmHWM`）。

| 实例 | 变量 | 子句 | 结果 | 求解 s | 求解器内存 | 证明 | 检查 s | 检查器内存 |
|---|---|---|---|---|---|---|---|---|
| 隐藏解 5-SAT（500 万文字） | 200 000 | 1 000 000 | SAT | 0.86 | 87 MB | - | - | - |
| 隐藏解 3-SAT | 200 000 | 600 000 | SAT | 0.48 | 60 MB | - | - | - |
| 隐藏解 5-SAT + 隐藏鸽巢核 | 200 000 | 999 981 | UNSAT | 0.82 | 87 MB | 0.01 MB | 0.63 | 103 MB |
| 每条子句重复 14 次 | 20 000 | 993 817 | SAT | 0.32 | 59 MB | - | - | - |
| 单位传播链 | 200 000 | 200 001 | UNSAT | 0.13 | 40 MB | < 0.01 MB | 0.13 | 35 MB |
| 蕴含梯子 | 200 000 | 200 002 | UNSAT | 0.06 | 40 MB | < 0.01 MB | 0.06 | 34 MB |
| 奇环 2-着色 | 199 999 | 399 998 | UNSAT | 0.12 | 42 MB | < 0.01 MB | 0.10 | 44 MB |
| 偶环 2-着色 | 200 000 | 400 000 | SAT | 0.11 | 42 MB | - | - | - |
| 奇偶矛盾，4 000 输入 | 11 996 | 31 988 | UNSAT | 1.95 | 11 MB | 2.5 MB | 0.09 | 11 MB |
| 鸽巢 200→200（可满足；有意超出 100 万子句的目标） | 40 000 | 3 980 200 | SAT | 0.42 | 186 MB | - | - | - |

时间包括读入文件。这些实例的共同点是**结构上容易**：一旦读进来，几乎不需要搜索。它们检验的是数据结构在规模上限下的
时间与内存开销（读入 500 万个文字约 0.5 秒、87 MB），**不是**"这个规模的任意实例都能解"。

**奇偶矛盾的悬崖。** 完整套件第一次运行时，这一行是 30 000 个输入的同一族实例，900 秒超时，测试失败。随后测量：

| 输入数 | 默认配置 |
|---|---|
| 2 000 | 0.88 s（23 902 次冲突） |
| 4 000 | 1.85 s（56 603 次冲突） |
| 8 000 | **> 300 s** |

从 4 000 到 8 000 不是平滑增长，而是断崖。在 8 000 输入的实例上做消融（每个配置 120 秒上限；得到答案的都附带证明并通过了检查）：

| 配置 | 结果 |
|---|---|
| 默认、`--restart=luby`、`--no-phase-saving`、`--phase=0`、`--no-reduce` | 全部超时 |
| `--no-minimize` | 3.2 s，251 229 次冲突，证明 67 MB |
| `--no-target-phase` | 3.0 s，194 289 次冲突，证明 21 MB |
| `--no-stable` | **0.15 s**，74 288 次冲突，证明 2.3 MB |

默认配置在这个实例上每秒仍能完成上万次冲突（4 万次冲突用时 1.9 秒），但证明没有进展。罪魁是稳定模式中的**目标相位**：
它反复把搜索引回"迄今最好的部分赋值"，而在一条长奇偶链上，这个赋值恰好让矛盾出现在链的最远端，每次冲突学到的都是长而无用的子句。
关掉目标相位（或整个稳定模式）即可解决。

但不能就此关掉它：多种子实验中，关掉稳定模式会让两个隐藏解族的 16 个实例一个也解不出（6.5 节）。
这是本项目反复遇到的现象——**同一个启发式在一类实例上不可或缺，在另一类实例上是瓶颈**。我没有找到对两者都好的规则，
因此保留默认配置，把规模测试中这一行换成实测可解的 4 000 输入（快速套件中使用的 3 000 输入保持不变），并在此如实记录。
（这一族实例本身是"容易"的：真正的求解器会用异或推理或等价替换在多项式时间内解决它们，本项目没有实现，见 DESIGN.md 第 7 节。）

### 6.8 增量求解

`bench/incremental_bench.py`：同一个公式上随机生成若干组假设（每组 3–6 个文字），比较
(a) 一个求解器进程依次回答全部查询（`--queries`），与 (b) 每个查询单独启动一次求解器、把假设作为单位子句加入公式。
两种方法对每个查询的 SAT/UNSAT 结论逐一比对，全部一致。

| 公式 | 查询数（SAT / UNSAT） | 增量：秒 | 冲突 | 逐个重新求解：秒 | 冲突 | 时间比 |
|---|---|---|---|---|---|---|
| 隐藏解 3-SAT n=5000 r=3.7 | 40 (40 / 0) | 2.46 | 52 190 | 158.26 | 3 353 895 | **64×** |
| 随机 3-SAT n=200 r=4.2 | 40 (0 / 40) | 1.26 | 64 398 | 1.05 | 153 630 | **0.8×（增量更慢）** |
| 3-着色 n=600 m=1300 | 40 (40 / 0) | 0.08 | 3 492 | 0.30 | 10 552 | 3.7× |
| 鸽巢 9→9（可满足） | 40 (23 / 17) | 0.02 | 2 352 | 0.08 | 2 262 | 4.4× |
| 20 万变量 / 100 万子句 隐藏解 5-SAT | 20 (20 / 0) | 1.69 | 0 | 14.34 | 0 | 8.5× |

* 最后一行没有任何冲突：收益全部来自不必重复读入和建立 100 万条子句。
* 第一行的收益来自复用：第一个查询付出了"学会这个公式"的代价，之后的查询几乎立即回答。
* **随机 3-SAT 一行增量反而慢 20%。** 增量方式的总冲突数少得多（6.4 万对 15.4 万，说明学到的子句确实被复用了），
  但每次冲突的代价高约 3 倍（约 5 万次冲突/秒对约 15 万次冲突/秒，后者还包含了 40 次进程启动）。
  一个合理但**未经验证**的解释：作为单位子句时，假设在第 0 层就让大量子句被化简删除；作为假设时它们只是决策，
  子句库没有被化简，学到的子句也会带上假设文字而变长。

---

## 7. 已知限制与未完成之处

**性能**
* 同一族、同一配置下，规模稍增就可能从秒级跳到解不出：同序奇偶矛盾 4 000 输入 1.9 秒，8 000 输入超过 300 秒（6.7 节，原因已定位但未修复）。
* 以下实例在给定时限内**解不出**（都是实测）：隐藏解 5-SAT n=1000、每变量 14 条子句（20 秒，所有配置都超时）；
  阈值附近的隐藏解 3-SAT n=2000 r=4.0（30 秒内 8 个种子解出 6 个）、n=5000 r=3.8（8 个解出 2 个）；
  乱序奇偶矛盾 n=40（经典族测试，120 秒）；隐藏解 3-SAT n=5000 r=5.0 和 n=20000 r=6.0（120 秒）。
* 鸽巢问题的时间与证明长度都随规模指数增长（11→10：37.7 万次冲突，证明 84 MB，检查 58.6 秒）。这是归结证明系统的下界，
  不是实现缺陷，但也意味着本求解器对这一类问题无能为力。
* 序原理 n=50 用时约 73 秒（虽然它有多项式长度的归结证明）。
* 所有基准实例都来自本仓库自己的生成器（随机与组合构造）。**没有在 SAT 竞赛的工业实例上测试过**——本次环境没有下载这些实例。
  第 6.6 节说明了生成器本身可以造成偏差，因此本文的性能结论只适用于这些实例族。
* 只有一个线程；没有预处理（变量消元、子句包含消除）、没有异或约束的高斯消元、没有局部搜索。
  这些都能在特定实例族上带来数量级的提升，但会让证明格式（需要 DRAT）和检查器变复杂（DESIGN.md 第 7 节）。

**内存**
* 学习子句的保留量没有硬上限。`--mem-limit`（默认 1024 MB）只约束子句存储区：超过一半时更频繁地删减，超过上限时放弃并输出
  `s UNKNOWN`（测试组 G 验证了这一行为）。监视表等其他结构不计入这个预算，实际峰值内存见 6.7 节。
* 证明文件没有大小限制，可以达到数十 MB 以上（序原理 26：70.5 MB）。检查器在内存中保存所有未删除的引理。

**正确性论证的边界**（详见 CORRECTNESS.md）
* 证明检查器（362 行 C++，加 168 行输入解析）没有形式化验证。它的可信度来自：极简的规则、一个独立编写的朴素 Python 版本在数千个证明上的一致性、
  对抗性测试与变异测试。
* 本仓库的测试框架支持把证明交给第三方检查器 drat-trim 复核（若 `build/third_party/drat-trim` 存在），
  **但本次没有运行**：在这个环境里下载并编译外部代码的操作没有执行。所有报告的证明都只经过本项目自己的两个检查器。
* 冲突核（假设子集）是充分的，但不保证最小。
* 带重启时的终止性只有一个存在性论证（CORRECTNESS.md 4.6），不给出任何有意义的上界；实际依靠资源上限。

**工程**
* 时间上限每 256 次冲突 / 4096 次决策检查一次；一次很长的单位传播或输入解析不会被中断。
* 变量数上限 2^30，子句存储区上限 2^31 个 32 位字（约 8 GB），超出时报错并输出 `s UNKNOWN`。
* 增量接口只支持"固定公式 + 每次不同的假设"，不支持在查询之间加入新子句。
