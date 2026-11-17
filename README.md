# 一个必须自证清白的 SAT 求解器

给定一个 CNF 公式，判断它是否可满足——并且**不要求任何人相信求解器**：

* 回答 SAT 时附上一组赋值，由独立程序逐子句代入检查；
* 回答 UNSAT 时附上一份证明，由独立程序逐步检查。检查器只懂两件事：单位传播，以及一条规则——
  "把子句 C 的文字全部取反后做单位传播，若推出矛盾，则 C 可以加入"。

求解器核心约 1200 行 C++20（`solver.hpp` + `solver.cpp`），是完整的 CDCL（冲突驱动子句学习）实现；
证明检查器约 360 行（另加 170 行输入解析），与求解器不共享任何代码。

```
              ┌──────────────┐  s SATISFIABLE + v ...   ┌───────────────┐
 formula.cnf ─┤  sat_solver  ├─────────────────────────▶│  model_check  │─▶ s MODEL VERIFIED
      │       │  (CDCL 搜索) │  s UNSATISFIABLE         ├───────────────┤
      │       └──────────────┘  + proof.rup ───────────▶│   rup_check   │─▶ s VERIFIED
      └────────────────────────────────────────────────▶│  (只懂单位传播) │
                                                        └───────────────┘
```

## 快速开始

```sh
make                                   # 编译到 build/

build/sat_solver examples/tiny_sat.cnf                 # s SATISFIABLE / v 1 2 3 0
build/sat_solver --proof p.rup examples/php-4-3.cnf    # s UNSATISFIABLE，证明写入 p.rup
build/rup_check examples/php-4-3.cnf p.rup             # s VERIFIED
python3 checker/rup_check.py --explain examples/php-4-3.cnf p.rup   # 打印每一步推导

build/sat_solver examples/tiny_sat.cnf > out.txt; build/model_check examples/tiny_sat.cnf out.txt

make test          # 快速测试套件（约 3 分钟）
make test-full     # 完整测试套件
make sanitize      # 同一套件，AddressSanitizer + UBSan 构建
make mutation      # 变异测试：植入 50 个 bug，看测试能否发现
make bench         # 基线 DPLL 与最终求解器对比 + 逐项消融
```

依赖：g++（C++20）、make、Python 3（仅标准库）。不使用任何现成的 SAT 库。

## 命令行

```
sat_solver [选项] formula.cnf
  --proof FILE            写出 UNSAT 证明（RUP 子句添加 + 'd' 删除提示，即 DRUP 格式）
  --proof-no-deletions    只写添加行
  --queries FILE          增量模式：每个查询是一组以 0 结尾的假设文字，全部在同一个求解器实例上回答
  --time-limit SEC  --conflict-limit N  --mem-limit MB     到达上限时输出 s UNKNOWN
  --trace                 把每次决策、传播、冲突、学到的子句打印出来（教学用）
  --no-minimize --no-phase-saving --no-reduce --no-blocker --no-inline-binary
  --no-vsids --no-simplify --no-stable --no-target-phase --restart=ema|luby|none --phase=occ|0|1
                          逐项关闭某个特性（用于消融实验）
退出码：10 = SAT，20 = UNSAT，0 = UNKNOWN 或增量模式，1 = 输入/用法错误
```

增量模式示例（`examples/queries.txt` 每行一个查询）：

```sh
build/sat_solver -q --queries examples/queries.txt --proof session.rup examples/php-4-4.cnf
# q 1                                   （无假设）
# s SATISFIABLE
# v -1 -2 -3 4 ...
# q 2                                   （假设 1 5 16：鸽子 1、2 都住 1 号笼，鸽子 4 住 4 号笼）
# s UNSATISFIABLE UNDER ASSUMPTIONS
# a 5 1 0                               ← 足以导致矛盾的假设子集；16 与矛盾无关，没有出现
build/rup_check --assume "5 1" examples/php-4-4.cnf session.rup    # 独立验证：F ∧ x5 ∧ x1 确实无解
build/rup_check examples/php-4-4.cnf session.rup                   # NOT VERIFIED：F 本身并没有被反驳
```

"在假设下无解"与"公式本身无解"是两个不同的结论，输出、证明和检查方式都分开（见 PROOF_FORMAT.md §3.5）。

## 文档

| 文档 | 内容 |
|---|---|
| [docs/WALKTHROUGH.md](docs/WALKTHROUGH.md) | **从这里开始。** 用"4 只鸽子 3 个笼子"完整演示：求解器怎样搜索，学到了什么，这些东西怎样一步步推出空子句 |
| [docs/DESIGN.md](docs/DESIGN.md) | 算法设计：每个部件是为了解决什么问题而出现的 |
| [docs/PROOF_FORMAT.md](docs/PROOF_FORMAT.md) | 输入、输出与证明格式的完整规范；检查规则 |
| [docs/CORRECTNESS.md](docs/CORRECTNESS.md) | 不变量、可靠性定理及其论证、复杂度；哪些是定理，哪些只是测试支持的经验 |
| [docs/RESULTS.md](docs/RESULTS.md) | 全部实测数据：测试、变异测试、基准、消融、失败的优化实验、已知限制 |
| [tests/README.md](tests/README.md) | 测试体系说明 |

## 目录

```
src/         CDCL 求解器（solver.hpp / solver.cpp / dimacs.hpp / main.cpp）
checker/     独立验证：rup_check.cpp（证明）、model_check.cpp（赋值）、rup_check.py（参考实现）
baseline/    基线求解器：DPLL，无学习
tools/       brute.cpp（暴力枚举）、gen.py（实例生成器）、cnf.py（Python 侧读写）
tests/       测试组 A–H（run_all.py）、变异测试（mutation.py）
bench/       基准测试（run_bench.py）及记录下来的原始结果（bench/results/）
examples/    小例子
docs/        文档
```

## 结果一览

（详见 [docs/RESULTS.md](docs/RESULTS.md)，全部为实测，原始输出在 `bench/results/`）

* 完整测试套件：40 000 个小公式与暴力枚举逐一比对、约 1 万个变形变体、4 万多次增量查询；每一个 SAT 答案都经代入检查，
  每一个 UNSAT 答案都附有证明并通过独立检查，没有一个证书被拒绝。最大的一份证明 1.28 GB，检查通过。
* 变异测试：植入 50 个 bug（求解器 37、证明检查器 9、赋值检查器 4），全部被测试发现。
* ASan + UBSan 构建上的测试套件全部通过。
* 与无学习的 DPLL 基线相比：47 个基准实例解出 45 个对 36 个；在鸽巢问题上反而慢 3–5 倍（第 6.2 节解释了原因）。
* 不是所有优化都有效：软件预取、按出现次数排序决策都没有可测收益；相位保存在本基准集上整体有害；
  有一族实例在规模翻倍时从 2 秒跳到超过 300 秒。这些都如实记录在 RESULTS.md 中。

## 支持规模

目标：20 万变量、100 万子句、500 万文字、1 GiB 内存。在这个规模上的实测时间与内存见
[docs/RESULTS.md](docs/RESULTS.md)。SAT 是 NP 完全问题：规模内的实例并不都能在合理时间内解出，
哪些能、哪些不能，RESULTS.md 的"已知限制"一节按实例族给出了数据。
