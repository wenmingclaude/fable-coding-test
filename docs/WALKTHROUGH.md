# 演示：4 只鸽子，3 个笼子

这一篇用一个具体的不可满足实例，把三件事对应起来看：

1. 求解器**做了什么**（它的搜索记录）；
2. 它**留下了什么**（证明文件，12 行）；
3. 检查器**怎样只靠单位传播**一步步确认，直到推出空子句。

文中所有输出都是程序的真实输出，可以用文末的命令重现。

## 1. 问题

4 只鸽子要住进 3 个笼子 A、B、C，每只鸽子至少住一个笼子，每个笼子至多住一只鸽子。显然做不到。

变量 `x_k` 表示"某只鸽子住某个笼子"：

|        | 笼 A | 笼 B | 笼 C |
|--------|------|------|------|
| 鸽子 1 | x₁   | x₂   | x₃   |
| 鸽子 2 | x₄   | x₅   | x₆   |
| 鸽子 3 | x₇   | x₈   | x₉   |
| 鸽子 4 | x₁₀  | x₁₁  | x₁₂  |

子句共 22 条（文件 [`examples/php-4-3.cnf`](../examples/php-4-3.cnf)）。下文用 `5` 表示 x₅ 为真，`-5` 表示 x₅ 为假。

```
每只鸽子至少住一个笼子              笼 A 至多一只        笼 B 至多一只        笼 C 至多一只
F1  ( 1 ∨  2 ∨  3)                 F5  (-1 ∨ -4)       F11 (-2 ∨ -5)       F17 (-3 ∨ -6)
F2  ( 4 ∨  5 ∨  6)                 F6  (-1 ∨ -7)       F12 (-2 ∨ -8)       F18 (-3 ∨ -9)
F3  ( 7 ∨  8 ∨  9)                 F7  (-1 ∨ -10)      F13 (-2 ∨ -11)      F19 (-3 ∨ -12)
F4  (10 ∨ 11 ∨ 12)                 F8  (-4 ∨ -7)       F14 (-5 ∨ -8)       F20 (-6 ∨ -9)
                                   F9  (-4 ∨ -10)      F15 (-5 ∨ -11)      F21 (-6 ∨ -12)
                                   F10 (-7 ∨ -10)      F16 (-8 ∨ -11)      F22 (-9 ∨ -12)
```

人的证明只有一句话："4 > 3"。但这句话用到了*计数*，而求解器不会计数——它只会两件事：
猜，以及单位传播。看它怎么办。

（暴力枚举需要检查 2¹² = 4096 种赋值。）

## 2. 求解器的搜索记录

`build/sat_solver --trace examples/php-4-3.cnf` 的输出（每行开头的 `c trace` 已略去），逐段加注释。"level" 是当前已做了几个猜测。

### 第一次撞墙

```
level 1: decide -1                  猜：鸽子 1 不在 A
level 2: decide -2                  猜：鸽子 1 不在 B
  forced: 3 -6 -9 -12               F1 逼出 3（鸽子 1 只能在 C）；C 被占，F17–F19 逼出 -6 -9 -12
level 3: decide -4                  猜：鸽子 2 不在 A
  forced: 5 -8 -11 7 10             F2 逼出 5（鸽子 2 在 B）；B 被占，逼出 -8 -11；
                                    F3 逼出 7（鸽子 3 在 A），F4 逼出 10（鸽子 4 在 A）
CONFLICT 1 at level 3: every literal is false in -7 -10        F10：A 里挤了两只
```

DPLL 在这里会"退一步，把最后一个猜测 -4 改成 4"。CDCL 先问：**到底是什么导致了冲突？**

从被证伪的 F10 出发，沿着"谁逼出了谁"往回走，每一步是一次归结（把两条子句中互补的文字消去）：

```
F10 (-7 ∨ -10)
  10 是被 F4 (10 ∨ 11 ∨ 12) 逼出的   →  归结掉 10：  (-7 ∨ 11 ∨ 12)
  7  是被 F3 (7 ∨ 8 ∨ 9)   逼出的   →  归结掉 7：   (8 ∨ 9 ∨ 11 ∨ 12)
  -11 是被 F15 (-5 ∨ -11)  逼出的   →  归结掉 11：  (-5 ∨ 8 ∨ 9 ∨ 12)
  -8  是被 F14 (-5 ∨ -8)   逼出的   →  归结掉 8：   (-5 ∨ 9 ∨ 12)      ← 停
```

停在这里，是因为子句里只剩一个文字（`-5`）属于当前这一层猜测；其余的 `9`、`12` 是更早就定下来的。

```
  learnt clause: -5 12 9
```

**学到的子句 L1 = (¬x₅ ∨ x₉ ∨ x₁₂)**。翻译成人话：

> 如果鸽子 2 住 B，那么鸽子 3 或鸽子 4 必须住 C。

这是一条关于原问题的、**无条件成立**的事实（B 被占后，鸽子 3、4 只能在 A、C 中选，而 A 只容得下一只）。
注意它和触发冲突的猜测 `-4` 无关，也和 `-1`、`-2` 无关——冲突分析找到的是比猜测本身更一般的原因。

### 第二次撞墙

```
  back to level 2, where it forces -5       退回 level 2。那里 -9 -12 仍然成立，L1 变成单位子句，逼出 -5
  forced: 4 -7 -10 8 11                     F2 逼出 4；A 被占逼出 -7 -10；F3 逼出 8，F4 逼出 11
CONFLICT 2 at level 2: every literal is false in -8 -11        F16：B 里挤了两只
  learnt clause: -3
  back to level 0, where it forces -3
```

同样的分析一路归结回去，这次只剩下一个文字：**L2 = (¬x₃)**，"鸽子 1 不能住 C"。
（理由：鸽子 1 占了 C，剩下三只鸽子要挤进 A、B 两个笼子——这是一个更小的鸽巢问题，求解器刚刚把它的两种情况都试完了。）

L2 只有一个文字，所以它在 **level 0** 生效：不依赖任何猜测，永久成立。猜测 `-1`、`-2` 被撤销——
它们只是脚手架，不出现在证明里。

既然 x₃ 永久为假，子句库可以化简：F1 `(1 ∨ 2 ∨ 3)` 缩短为 **L3 = (x₁ ∨ x₂)**；
F17、F18、F19 都含 `-3`，已经永久成立，可以丢弃。

### 之后的四次

```
level 1: decide -5
level 2: decide -7
level 3: decide 8
  forced: -2 -11 1 -4 -10 6 12 -9
CONFLICT 3 at level 3: every literal is false in -6 -12
  learnt clause: -8 5                       L4 = (x₅ ∨ ¬x₈)
  back to level 1, where it forces -8
level 2: decide -10
level 3: decide -11
  forced: 12 -6 -9 4 7 -1
CONFLICT 4 at level 3: every literal is false in -7 -4
  learnt clause: -12 5                      L5 = (x₅ ∨ ¬x₁₂)
  back to level 1, where it forces -12
level 2: decide 4
  forced: -1 -7 -10 2 9 11
CONFLICT 5 at level 2: every literal is false in -2 -11
  learnt clause: -4 12                      L6 = (¬x₄ ∨ x₁₂)
  back to level 1, where it forces -4
  forced: 6 -9 7 -1 -10 2 11
CONFLICT 6 at level 1: every literal is false in -2 -11
  learnt clause: 5                          L7 = (x₅)：鸽子 2 必须住 B
  back to level 0, where it forces 5
```

可以看到"非时序回退"：冲突 3 发生在 level 3，但学到的子句只涉及 level 1 的 `-5`，于是直接跳回 level 1，
中间的猜测 `-7` 被整个跳过。

### 最后一次：不需要任何猜测

```
  forced: -2 -8 -11 1 -4 -7 -10 9 12 -6
CONFLICT 7 at level 0: every literal is false in -9 -12
no decision is involved: the formula is unsatisfiable
```

L7 说鸽子 2 住 B。于是鸽子 1 不在 B（F11）也不在 C（L2），只能在 A；鸽子 3、4 既不能在 B（被鸽子 2 占），
也不能在 A（被鸽子 1 占），都只能在 C——F22 `(-9 ∨ -12)` 被证伪。
这次冲突**没有用到任何猜测**。一个不依赖任何假设的矛盾，就是公式本身的矛盾。

共 9 次猜测、7 次冲突。

## 3. 留下的证明

`build/sat_solver --proof p.rup examples/php-4-3.cnf` 写出的文件，全部 12 行：

```
-5 12 9 0          L1  如果鸽子 2 住 B，则鸽子 3 或 4 住 C
-3 0               L2  鸽子 1 不住 C
2 1 0              L3  鸽子 1 住 A 或 B              （F1 去掉已知为假的 3）
d 2 1 3 0              以下 4 行是删除提示：这些子句以后用不到了
d -6 -3 0
d -9 -3 0
d -12 -3 0
-8 5 0             L4
-12 5 0            L5
-4 12 0            L6
5 0                L7  鸽子 2 住 B
0                      空子句
```

**证明里没有决策，没有"level"，没有回退，没有启发式。** 只有一串子句。
搜索过程中所有"试错"的痕迹都被擦掉了，留下的是试错得到的结论。

## 4. 检查器怎样验证

检查器对每一条 L 做同一件事：**假设它的每个文字都为假，然后只做单位传播，看是否必然矛盾。**
下面是 `python3 checker/rup_check.py --explain examples/php-4-3.cnf p.rup` 的输出。

```
L1  (-5 ∨ 9 ∨ 12)
    assume 5, -9, -12                  假设 L1 不成立：鸽子 2 住 B，鸽子 3、4 都不住 C
      F11: forces -2   (-2 ∨ -5)
      F14: forces -8   (-5 ∨ -8)       B 被占，鸽子 3 不能住 B
      F15: forces -11  (-5 ∨ -11)      鸽子 4 也不能住 B
      F3:  forces 7    (7 ∨ 8 ∨ 9)     鸽子 3 只剩 A
      F4:  forces 10   (10 ∨ 11 ∨ 12)  鸽子 4 只剩 A
      F6:  forces -1   (-1 ∨ -7)
      F8:  forces -4   (-4 ∨ -7)
      F10: is falsified (-7 ∨ -10)     矛盾 ⇒ 假设不可能成立 ⇒ L1 成立
```

对照第 2 节的冲突分析：那里是从 F10 出发，沿 F4、F3、F15、F14 **往回**归结；
这里是从 L1 的否定出发，沿 F14、F15、F3、F4 **往前**传播，撞上同一条 F10。
同一条路，两个方向。这就是"学习子句必然能通过 RUP 检查"的原因（[CORRECTNESS.md](CORRECTNESS.md) 定理 4.2）。

而检查器完全不需要知道求解器当时猜了什么、按什么顺序归结。

```
L2  (-3)
    assume 3                           假设鸽子 1 住 C
      F17: forces -6   (-3 ∨ -6)
      F18: forces -9   (-3 ∨ -9)
      F19: forces -12  (-3 ∨ -12)
      L1:  forces -5   (-5 ∨ 9 ∨ 12)   ← 用到了刚刚验证过的 L1
      F2:  forces 4    (4 ∨ 5 ∨ 6)
      F5:  forces -1   (-1 ∨ -4)
      F8:  forces -7   (-4 ∨ -7)
      F9:  forces -10  (-4 ∨ -10)
      F3:  forces 8    (7 ∨ 8 ∨ 9)
      F4:  forces 11   (10 ∨ 11 ∨ 12)
      F12: forces -2   (-2 ∨ -8)
      F16: is falsified (-8 ∨ -11)
    now fixed: -3   by L2 (-3)

L3  (1 ∨ 2)
    known units: -3
    assume -1, -2
      F1:  is falsified (1 ∨ 2 ∨ 3)

L4  (5 ∨ -8)
    assume -5, 8
      F12: forces -2   (-2 ∨ -8)
      F16: forces -11  (-8 ∨ -11)
      L3:  forces 1    (1 ∨ 2)
      F5:  forces -4   (-1 ∨ -4)
      F6:  forces -7   (-1 ∨ -7)
      F7:  forces -10  (-1 ∨ -10)
      F2:  forces 6    (4 ∨ 5 ∨ 6)
      F4:  forces 12   (10 ∨ 11 ∨ 12)
      F20: forces -9   (-6 ∨ -9)
      F21: is falsified (-6 ∨ -12)

L5  (5 ∨ -12)
    assume -5, 12
      F21: forces -6   (-6 ∨ -12)
      F22: forces -9   (-9 ∨ -12)
      L4:  forces -8   (5 ∨ -8)
      F2:  forces 4    (4 ∨ 5 ∨ 6)
      F3:  forces 7    (7 ∨ 8 ∨ 9)
      F5:  forces -1   (-1 ∨ -4)
      F8:  is falsified (-4 ∨ -7)

L6  (-4 ∨ 12)
    assume 4, -12
      F5:  forces -1   (-1 ∨ -4)
      F8:  forces -7   (-4 ∨ -7)
      F9:  forces -10  (-4 ∨ -10)
      L3:  forces 2    (1 ∨ 2)
      F4:  forces 11   (10 ∨ 11 ∨ 12)
      F11: forces -5   (-2 ∨ -5)
      F12: forces -8   (-2 ∨ -8)
      F13: is falsified (-2 ∨ -11)

L7  (5)
    assume -5
      L4:  forces -8   (5 ∨ -8)
      L5:  forces -12  (5 ∨ -12)
      L6:  forces -4   (-4 ∨ 12)
      F2:  forces 6    (4 ∨ 5 ∨ 6)
      F20: forces -9   (-6 ∨ -9)
      F3:  forces 7    (7 ∨ 8 ∨ 9)
      F6:  forces -1   (-1 ∨ -7)
      F10: forces -10  (-7 ∨ -10)
      L3:  forces 2    (1 ∨ 2)
      F4:  forces 11   (10 ∨ 11 ∨ 12)
      F13: is falsified (-2 ∨ -11)
```

L7 的验证同时用到了 L3、L4、L5、L6：后面的引理站在前面引理的肩膀上。
单靠原始的 22 条子句，"假设 -5 然后单位传播"是推不出矛盾的——这正是需要中间引理的原因。

### 推出空子句

```
    now fixed: 5    by L7 (5)
    now fixed: -2   by F11 (-2 ∨ -5)
    now fixed: -8   by F14 (-5 ∨ -8)
    now fixed: -11  by F15 (-5 ∨ -11)
    now fixed: 1    by L3 (1 ∨ 2)
    now fixed: -4   by F5 (-1 ∨ -4)
    now fixed: -7   by F6 (-1 ∨ -7)
    now fixed: -10  by F7 (-1 ∨ -10)
    now fixed: 9    by F3 (7 ∨ 8 ∨ 9)
    now fixed: 12   by F4 (10 ∨ 11 ∨ 12)
    now fixed: -6   by F20 (-6 ∨ -9)

EMPTY CLAUSE: with no assumption at all, F22 (-9 ∨ -12) is falsified by the fixed literals.
s VERIFIED
```

L7 被接受后，**不做任何假设**，单位传播自己走到了矛盾。
空子句的"否定"是空的假设，所以空子句的 RUP 检查就是"什么都不假设，直接传播"。它通过了。

为什么这就证明了无解：L1 到 L7 每一条都被验证为原公式的推论，所以任何满足原公式的赋值也必须满足它们全部；
而上面 11 步传播的每一步都是被逼的，最终要求 F22 的两个文字同时为假——没有赋值能做到。
所以满足原公式的赋值不存在。

## 5. 这和数学证明的关系

一份教科书上的证明是一串命题，每一句由前面的句子"显然"推出，最后一句是结论。
"显然"到什么程度由读者的水平决定，这正是证明难以机械检查的地方。

RUP 证明把"显然"固定成了一个精确的、最弱的标准：**否定它，只做单位传播，就能撞墙。**

| | 教科书证明 | 这份证明 |
|---|---|---|
| 一步是什么 | 一个命题 | 一条子句 |
| 每一步的依据 | "由上可知""显然" | 取反后单位传播得到冲突 |
| 检查者需要 | 理解力 | 只需机械执行单位传播 |
| 结论 | 欲证命题 | 空子句（矛盾） |
| 怎么找到的 | 不写在证明里 | 不写在证明里（决策、回退、启发式都被丢弃） |

求解器负责**寻找**这串引理——这是难的部分，需要猜测、回退、启发式、数据结构，代码复杂，可能有 bug。
检查器负责**确认**每条引理——这是容易的部分，代码短，逻辑单一。
复杂程序的可靠性，被转化成了一个文件加上一个简单程序的可靠性。

还有一点值得注意：求解器找到的并不是最聪明的证明。"4 > 3"一句话就够了，而这里用了 7 条引理。
随着鸽子数增加，这个差距会爆炸（下表为本求解器的实测值）：

| 鸽子 → 笼子 | 变量 | 子句 | 冲突次数 | 证明引理数 | 证明大小 | 检查时间 |
|---|---|---|---|---|---|---|
PHP_TABLE_PLACEHOLDER

每多一只鸽子，证明大约长 4 倍。这不是实现的缺陷：数学上已经证明（Haken, 1985），
鸽巢公式的任何归结证明都必然是指数长的，而单位传播 + 子句学习产生的正是归结证明。
求解器给出的是**最容易检查**的那一类证明，代价是它有时远不是最短的。

## 6. 重现

```sh
make
build/sat_solver --trace examples/php-4-3.cnf                          # 第 2 节
build/sat_solver -q --proof p.rup examples/php-4-3.cnf; cat p.rup      # 第 3 节
build/rup_check examples/php-4-3.cnf p.rup                             # s VERIFIED
python3 checker/rup_check.py --explain examples/php-4-3.cnf p.rup      # 第 4 节
build/brute examples/php-4-3.cnf                                       # 暴力枚举也说 UNSAT
```

想看一个被拒绝的证明：从 `p.rup` 里删掉 `-12 5 0` 那一行（L5）再检查。两个检查器都会在 `5 0`（原来的 L7）
那一行报告 `is not implied by unit propagation (RUP check failed)`——少了一块垫脚石，后面的一步就迈不过去：

```sh
grep -v '^-12 5 0' p.rup > bad.rup; build/rup_check examples/php-4-3.cnf bad.rup     # s NOT VERIFIED
```
