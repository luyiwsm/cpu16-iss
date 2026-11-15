# cpu16-iss

用 C11 编写的 16 位微程序 CPU 模拟器，支持程序执行、单步调试和差分验证。

项目提供两套执行模型：ISA 模型直接执行机器指令，微程序模型按控制信号逐周期运行。默认同时运行两套模型，在每条指令完成后比较寄存器、标志位、内存和累计周期，发现差异时报告出错位置。

支持 15 条指令，可加载 Vivado `.coe` 镜像，也可运行项目自带的简易汇编程序。

## 快速开始

在 Linux 或 WSL 中，需要 GCC 和 Make；运行完整测试还需要 Python 3。

```bash
make
./cpu16_sim -a rom/sum_100.asm
```

示例计算 1 加到 100，输出：

```text
[PASS] Lockstep verified
ACC = 5050 (0x13BA)  MR = 0x0000
Instructions = 713  Cycles = 5494  CPI = 7.71
```

## 常用命令

```bash
# 加载控制 ROM 和主存镜像
./cpu16_sim -c rom/blk_cu.coe -m rom/blk_mem.coe

# 查看每条指令的执行结果
./cpu16_sim -a rom/sum_100.asm --trace

# 进入交互调试器
./cpu16_sim -a rom/sum_100.asm --debug

# 导出运行结果
./cpu16_sim -a rom/sum_100.asm --json results/sum100.json

# 运行测试和内存检查
make test
make asan
```

调试器支持：`s` 指令单步、`m` 微周期单步、`r` 查看寄存器、`x 地址 [数量]` 查看内存、`b 地址` 设置断点、`c` 继续、`q` 退出。

使用 `--mode isa` 或 `--mode micro` 可单独运行一种模型，`--verbose` 显示完整寄存器，`--help` 查看全部选项。

## 项目结构

```text
src/       执行模型、差分验证、文件解析和调试器
include/   CPU 状态、指令编码和接口定义
rom/       控制 ROM、测试镜像和汇编示例
tests/     指令、解析器和命令行测试
```

核心实现位于 `src/cpu.c`，内置微码和指令表位于 `src/isa.c`。

## 实现说明

- 主存为 256 × 16 位，控制 ROM 为 128 × 32 位，其中 29 位用于控制信号。
- 保留随项目提供的微码行为：`SHR` 实际左移，`SHL`、`SAL` 实际右移；`JMPGEZ` 检查 ALU 的 Sign 标志，`LOAD` 不更新该标志。
- 差分验证目前在两套 C 模型之间进行，尚未接入 RTL 仿真。
