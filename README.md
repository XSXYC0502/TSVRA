# TSVRA - TSV路由算法仿真器

## 项目简介

TSVRA (TSV Routing Algorithm) 是一个3D芯片TSV（Through-Silicon Via）路由模拟系统。该系统模拟了多层芯片结构中的TSV传输、失效处理和冗余路由机制。

## 主要特性

- **多层TSV网格模型**：支持可配置的多层TSV网格（默认2层）
- **冗余TSV机制**：每个4×4区域的四个顶角为冗余TSV
- **三种失效模式**：
  - 模式a：仅初始化时失效
  - 模式b：仅运行时动态失效
  - 模式c：混合失效（初始化 + 运行时）
- **A*路由算法**：支持可扩展的启发函数接口
- **热点区域模拟**：基于权重的请求生成，模拟真实的访问模式
- **详细统计输出**：命令行统计 + CSV导出

## 系统要求

- C++20兼容编译器（GCC 10+, Clang 11+, 或 Visual Studio 2022 / MSVC）
- CMake 3.20+

## 编译

```bash
# 配置
cmake -S . -B build

# 编译（Linux / macOS / Windows 通用）
cmake --build build --config Release

# 可执行文件将生成在 bin/ 目录下
```

Windows + MSVC:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

If you prefer not to use presets, the equivalent configure command is:

```powershell
cmake -S . -B build/windows-msvc -G "Visual Studio 17 2022" -A x64
```

If CMake reports `No CMAKE_CXX_COMPILER could be found` at `CMakeLists.txt:3`, that means MSVC was not discoverable from the current shell. Using the preset above avoids relying on an already-initialized compiler environment.

## 使用方法

### 基本用法

```bash
./bin/tsvra
```

Windows PowerShell / CMD:

```powershell
.\bin\tsvra.exe
```

### 使用配置文件

```bash
./bin/tsvra --config config.ini
```

Windows PowerShell / CMD:

```powershell
.\bin\tsvra.exe --config config.ini
```

### 命令行参数

```bash
./bin/tsvra [选项]

选项:
  --config <file>           从文件加载配置
  --layers <n>              层数 (默认: 2)
  --grid-factor <n>         网格因子 (默认: 4, 网格大小 = 4*n)
  --failure-mode <a|b|c>    失效模式 (默认: a)
  --failure-rate <rate>     失效率 (默认: 1e-6)
  --vertical-delay <n>      垂直延迟 (默认: 1)
  --horizontal-delay <n>    水平延迟 (默认: 1000)
  --output <prefix>         输出文件前缀 (默认: tsvra_output)
  --seed <n>                随机数种子 (默认: 0=随机)
  -h, --help                显示帮助信息
```

### 示例

```bash
# 使用16x16网格（grid-factor=4）
./bin/tsvra --grid-factor 4

# 运行时失效模式
./bin/tsvra --failure-mode b --failure-rate 1e-5

# 自定义输出文件
./bin/tsvra --output experiment1

# 使用固定随机种子（可重现结果）
./bin/tsvra --seed 12345
```

## 配置参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `num_layers` | 芯片层数 | 2 |
| `grid_factor` | 网格因子（网格大小 = 4 × grid_factor） | 4 (16×16) |
| `failure_mode` | 失效模式（a/b/c） | a |
| `failure_rate` | TSV失效概率 | 1e-6 |
| `vertical_delay` | 垂直传输延迟 | 1 |
| `horizontal_delay` | 水平传输延迟 | 1000 |
| `simulation_cycles` | 仿真周期数 | 100000 |
| `output_prefix` | 输出文件前缀 | tsvra_output |
| `random_seed` | 随机数种子（0=随机） | 0 |

## 输出文件

运行后会生成两个CSV文件：

1. **`{prefix}_summary.csv`**：统计摘要
   - 总请求数、完成数、失败数
   - 成功率
   - 平均/最大/最小延迟
   - TSV失效统计

2. **`{prefix}_requests.csv`**：详细请求记录
   - 每个请求的起点、终点坐标
   - 生成时间、完成时间、延迟
   - 状态（完成/失败）

## 项目结构

```
TSVRA/
├── CMakeLists.txt          # CMake构建配置
├── README.md               # 本文件
├── config.ini              # 配置文件示例
├── include/                # 头文件目录
│   ├── config.hpp          # 配置管理
│   ├── tsv.hpp             # TSV单元
│   ├── grid.hpp            # TSV网格
│   ├── router.hpp          # A*路由算法
│   ├── request.hpp         # 请求定义
│   ├── request_generator.hpp # 请求生成器
│   ├── simulator.hpp       # 仿真引擎
│   └── statistics.hpp      # 统计输出
└── src/                    # 源文件目录
    ├── config.cpp
    ├── tsv.cpp
    ├── grid.cpp
    ├── router.cpp
    ├── request_generator.cpp
    ├── simulator.cpp
    ├── statistics.cpp
    └── main.cpp            # 主程序入口
```

## 核心算法

### TSV网格布局

- 网格大小：N × N × L（N = 4 × grid_factor，L = num_layers）
- 冗余TSV位置：每个4×4区域的四个顶角
- 坐标系统：(x, y, z)，其中z表示层

### A*路由算法

- **启发函数**：曼哈顿距离（可扩展）
- **移动规则**：
  - 水平移动（同层）：延迟 = horizontal_delay
  - 垂直移动（层间TSV）：延迟 = vertical_delay
- **路由策略**：
  - 避开失效TSV
  - 考虑TSV占用时间
  - 优先选择最短路径

### 失效模型

- **模式a（初始失效）**：仿真开始时随机失效部分TSV
- **模式b（运行时失效）**：仿真过程中动态产生失效
- **模式c（混合失效）**：结合a和b

### 请求生成

- **热点区域**：4×4 TSV块为基本单元，每个区域有随机热力值
- **生成策略**：基于热力值的加权随机采样
- **请求频率**：平均每100个周期生成1个请求（可调整）

## 编译警告

项目使用严格的编译选项以确保代码质量：

- `-Wall -Wextra -Wpedantic`：启用所有警告
- `-Werror`：将警告视为错误
- 其他详细警告选项（见CMakeLists.txt）

## 扩展性

### 自定义启发函数

```cpp
// 在Router中设置自定义启发函数
router.set_heuristic([](const Point& a, const Point& b) {
    // 自定义距离计算
    return custom_distance(a, b);
});
```

### 调整请求生成率

修改`request_generator.cpp`中的`request_probability`：

```cpp
double request_probability = 0.01; // 1%概率/周期
```

## 许可证

[添加您的许可证信息]

## 作者

[添加作者信息]

## 致谢

本项目用于3D芯片TSV路由算法的研究和性能评估。
