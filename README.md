# cpu16-iss

用 C11 实现的 16 位微程序 CPU 模拟器，包含两套执行模型和指令边界差分验证。

- ISA 模型一次执行一条指令，直接计算寄存器、标志和内存变化。
- 微程序模型一次推进一个微周期，解释控制 ROM 的 29 位控制信号。
- 锁步验证在每条指令完成后比较两套模型的状态和累计周期数。

当前比较的是两套 C 模型，没有直接连接 RTL 仿真器。

## 构建和运行

程序需要 GCC 和 Make；完整测试额外使用 Python 3 标准库。

```bash
make
make test
make asan
./cpu16_sim -a rom/sum_100.asm
./cpu16_sim -c rom/blk_cu.coe -m rom/blk_mem.coe
```

默认输出：

```text
[PASS] Lockstep verified
ACC = 5050 (0x13BA)  MR = 0x0000
Instructions = 713  Cycles = 5494  CPI = 7.71
```

| 参数 | 功能 |
|---|---|
| `-a file.asm` | 汇编并加载程序 |
| `-m file.coe` | 加载主存，与 `-a` 二选一 |
| `-c file.coe` | 加载控制 ROM，默认使用内置微码 |
| `-o file.coe` | 导出运行前的内存镜像 |
| `--mode lockstep/micro/isa` | 执行模式，默认 lockstep |
| `--trace` | 每条指令打印一行，仍保留 lockstep 检查 |
| `--verbose` | 额外显示完整寄存器 |
| `--debug` | 使用微程序模型进入交互调试器 |
| `--json file`、`--csv file` | 导出最终状态和统计 |
| `--help` | 显示帮助 |

运行最多执行 100000 条指令，每条指令最多 32 个微周期。
非法指令、模型差异、文件错误和执行超限返回非零退出码。

## 最小示例

创建 `build/add.asm`：

```asm
LOAD 0x80
ADD  0x81
HALT
.word 0x80 5
.word 0x81 3
```

```bash
./cpu16_sim -a build/add.asm
./cpu16_sim -a build/add.asm --trace
./cpu16_sim -a build/add.asm --debug
```

结果为 ACC=8、3 条指令、22 个微周期。两条 `.word` 是数据声明，不计入执行指令数。

调试命令：`s` 指令单步、`m` 微周期单步、`c` 继续、`r` 查看寄存器、
`x 地址 [数量]` 查看内存、`b 地址` 设置断点、`q` 退出。
地址可使用十进制或带 `0x` 前缀的十六进制。
`c` 先执行到下一个指令边界再检查断点，每次最多推进 100000 条指令。

## 源码阅读顺序

| 文件 | 内容 |
|---|---|
| `include/cpu.h` | 状态结构和执行接口 |
| `include/isa.h` | 指令编码、29 个控制信号 |
| `src/cpu.c` | 微程序模型、ISA 模型、差分验证 |
| `src/isa.c` | 内置 ROM、操作码表、反汇编 |
| `src/coe_loader.c` | COE、轻量汇编、JSON/CSV |
| `src/debugger.c` | 单步、断点、内存查看 |
| `src/main.c` | 参数、加载、执行和输出 |

建议先读 `cpu16_step_isa()`，再读 `cpu16_step_micro()`，最后读
`cpu16_step_lockstep()`。两套模型独立实现运算逻辑。
微程序模型通过 `next_*` 暂存寄存器新值，在整个时钟边沿计算结束后提交。

## 处理器和指令

主存为 256×16 位，代码和数据共享；控制 ROM 为 128×32 位，低 29 位有效。
指令高 8 位是操作码，低 8 位是地址或跳转目标。PC 和 MAR 为 8 位。
CAR 指向微指令，与机器指令地址 PC 不同。

| 操作码 | 指令 | 行为 | 总微周期 |
|---|---|---|---:|
| 01 | STORE addr | MEM[addr] ← ACC | 7 |
| 02 | LOAD addr | ACC ← MEM[addr] | 7 |
| 03 | ADD addr | ACC ← ACC + MEM[addr] | 10 |
| 04 | SUB addr | ACC ← ACC - MEM[addr] | 10 |
| 05 | JMPGEZ addr | Sign=0 时跳转，否则 PC++ | 6 |
| 06 | JMP addr | PC ← addr | 5 |
| 07 | HALT | 停机，PC 不自增 | 5 |
| 08 | MPY addr | 有符号乘积写入 {MR,ACC} | 11 |
| 09 | AND addr | ACC ← ACC & MEM[addr] | 10 |
| 0A | OR addr | ACC ← ACC \| MEM[addr] | 10 |
| 0B | NOT addr | ACC ← ~MEM[addr] | 10 |
| 0C | SHL | 原微码实际为逻辑右移 | 7 |
| 0D | SHR | 原微码实际为逻辑左移 | 7 |
| 0E | SAR | 算术右移 | 7 |
| 0F | SAL | 原微码实际为逻辑右移 | 7 |

每条指令包含 4 个取指微周期。ISA 模型累加预设周期数，微程序模型逐周期计数。
CPI 是模拟 CPU 的微周期数/指令数，不是宿主机上模拟器的性能指标。

保留原微码的特殊行为：

- LOAD 不更新 ALU 标志；JMPGEZ 检查保留的 Sign。
- Sign 来自 16 位 ALU 输出；乘法 Sign 取低 16 位，Zero 检查完整乘积。
- SUB 的 Carry 是扩展减法的第 17 位；SAR 的 Carry 取原最高位。
- SHL/SHR/SAL 的名称与微码方向不一致，本次重构保持原映射。
- NOP 仅可识别和显示，不可执行；汇编器明确拒绝 NOP。
- HALT 固定识别 CAR=0x68，指令分派和 ISA 周期表对应当前 ROM。

## 输入格式与验证边界

COE 支持 radix=2/10/16、大小写不敏感的关键字和逗号分隔数字。
兼容原主存镜像省略末尾分号的情况。超过目标容量、位宽溢出、负数和非法数字报错。
解析使用固定大小 token 缓冲，不分配动态内存。

汇编支持数字地址、`#`/`;` 注释、`.word 地址 数值`；不支持标签、宏或链接。
需要地址的指令必须填写操作数；HALT 和移位可以省略操作数，也兼容显式填 0。
`.word` 数值范围是 -32768..65535。

差分比较 PC、ACC、MR、C/Z/V/S、累计周期、指令数、停机状态和全部主存。
不逐周期比较，也不比较所有内部寄存器。错误记录保存第一处差异，数值字段为 64 位。

测试包含原有 16 项用例，以及 3840 组指令输入、解析边界、复位、错误诊断、
三种执行模式、trace 差分、调试器、报告和超限处理。
测试通过不代表穷举正确，也不能证明与 RTL 周期完全一致。

`make clean` 只删除生成程序，保留 `build/add.asm` 等用户文件。
