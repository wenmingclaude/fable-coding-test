# 时间旅行文本编辑器

一个保留全部历史的文本编辑器：每次编辑都基于任意一个旧版本生成新版本，所有版本永远可查。

| 操作 | 含义 | 时间（期望） |
|---|---|---|
| `INSERT v p s` / `DELETE v l r` / `REVERSE v l r` / `MOVE v l r p` | 由版本 v 生成新版本 | O(log n)（INSERT 另加 \|s\|） |
| `EQUAL v1 l1 r1 v2 l2 r2` / `PAL v l r` | 子串相等 / 回文 | O(log n) |
| `LCP v1 p1 v2 p2` | 两个后缀的最长公共前缀 | O(log n + log² 答案) |

- 每次编辑新增 0.7–2 KB 内存（23–64 个 32 字节节点），只与 log(文本长度) 有关，与版本数量无关；
- |S₀| = q = 200000 的整题输入：0.6–1.7 秒（2.3 GHz 共享虚拟机），峰值内存最坏 414 MiB（限制 768 MB）；
- C++20，只依赖标准库；核心三个头文件约 780 行（含注释与自检代码）。

> **关于正确性的诚实说明。** 文本内容和所有编辑是精确的。三种查询基于字符串哈希，是**概率正确**
> 的：回答 NO 一定对；回答 YES（或 LCP 偏大）有极小概率出错，整次满规模运行出错的概率不超过
> 2×10⁻⁶（`make dual` 的双基模式下不超过 4×10⁻¹⁹）。运行时间是**期望** O(log n)，不是最坏情况
> 保证。推导和前提见 [docs/DESIGN.md §8](docs/DESIGN.md#8-随机化两处性质完全不同)。

## 使用

```sh
make                                 # 编译全部程序到 build/
build/tte < examples/sample.in       # 标准输入读题，标准输出写答案
build/tte --seed 42 --stats < in     # 固定随机种子；向 stderr 输出节点数与内存
build/explain                        # 画出几个小版本的树，标出哪些节点是共享的

make test        # 单元测试 + 样例 + 短的白盒/黑盒随机测试（约 10 秒）
make fuzz        # 长时间随机测试（约 1 分钟），make fuzz SEED=7 换一批输入
make sanitize    # 在 AddressSanitizer + UBSan 下重跑，节点池每次扩容都搬家
make dual        # 用两个独立哈希基重编译并重跑测试（节点 48 字节）
make collide     # 把哈希模数换成 8191，观察真实发生的碰撞只造成"允许方向"的错误
make mutation    # 变异测试：植入 44 个错误，检查测试能否全部发现（约 5 分钟）
make stress      # 满规模（20 万 × 20 万）对照字符串模型（约 30 秒）
make bench       # 满规模的时间与内存
```

输入不合法（越界、未知操作、非 a–z 字符）时报告行号并以状态 2 退出，不会给出错误答案。

也可以当库用（只有头文件）：

```cpp
#include "src/editor.hpp"

tte::Editor ed("abacaba");
int v1 = ed.reverse(0, 0, 4);          // 新版本号 1
bool same = ed.equal(0, 0, 3, v1, 1, 4);
unsigned k = ed.lcp(0, 0, v1, 1);      // 3
std::string old = ed.text(0);          // 仍然是 "abacaba"
```

## 核心思路

1. **一个版本 = 一棵树的根。** 树的中序遍历是文本。编辑不修改旧节点，只为"从根到修改位置"的那条
   路径新建节点，其余子树直接共用。
2. **反转 = 翻转引用里的一个比特。** 引用是"节点编号 + 方向位"；方向位为 1 表示把这个节点镜像着
   读。反转一个区间不碰任何节点，所以不可能影响别的版本——没有惰性标记，没有下推。
3. **树形只取决于内容。** 用随机优先级平衡（treap）。不论经过多少次编辑和分叉，每个版本的树都
   和"直接用这段文本新建"的树服从同一分布，所以代价不随历史增长。
4. **每个节点带着 (正向哈希, 反向哈希)。** 连接两段文本时哈希可以 O(1) 合成，镜像时两个哈希
   互换。于是 EQUAL 是比较哈希；PAL 是"这棵树和它的镜像做 EQUAL"；LCP 是在两棵树上用两个游标做
   搜索。

完整说明：

- [docs/DESIGN.md](docs/DESIGN.md) —— 数据结构、不变量、正确性论证、随机化与哈希碰撞分析、
  复杂度、内存控制、抽象层面的讨论、备选方案。
- [docs/TESTING.md](docs/TESTING.md) —— 每一类测试做了什么、实测结果、性能与内存数据、
  开发过程中发现的问题及修复。

## 关于本分支

本分支（`claude/nifty-keller-23x1n4`）是一个独立的、没有父提交的分支，只包含这一道题；第一道题
的代码在 `claude/dynamic-constraint-solver-kul4vl` 分支上，两者没有共同历史，互不影响。

## 目录

```
src/editor.hpp     编辑器本体：持久化 treap、四种编辑、三种查询
src/hash.hpp       模 2^61−1 的算术；摘要 (长度, 哈希, 反向哈希) 及其连接/反转
src/pool.hpp       节点池（扩容时不出现两份数据）
src/main.cpp       命令行程序：解析输入、校验、输出

verify/naive.cpp   独立的朴素验证器：每个版本一个 std::string，逐字符实现每个操作

tests/unit.cpp     单元测试（代数定律、样例、边界、分支历史、反复翻转、MOVE 下标穷举……）
tests/fuzz.cpp     白盒随机测试：每次编辑后检查不变量，并逐字节确认旧节点未被改动
tests/diff.sh      黑盒差分：随机输入分别喂给 tte 和 naive，输出必须逐字节相同
tests/gen.cpp      随机输入生成器（9 种工作负载）
tests/stress.cpp   满规模测试，对照一个只保留"活跃版本"的字符串模型
tests/mutation.py  变异测试

bench/bench.cpp    进程内基准：各操作的单次耗时与内存
bench/run.sh       整个程序在满规模输入上的时间与峰值内存
bench/measure.cpp  用 wait4 测量子进程的时间与峰值内存

examples/explain.cpp   把小例子的树画出来
examples/sample.in/out 题面样例
```
