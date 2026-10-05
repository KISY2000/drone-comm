# 开发环境与真实编译记录

记录日期：2026-10-05。保留既有 YOLO、Vivado 和其他工程。本通信工程的验证输出都写入 `drone_comm/build`；没有修改已有 bitstream、模型或工程配置。

当前接口改为 F407 USART3 PB10/PB11、USART2 PA2/PA3，Zynq PS UART0 MIO14/MIO15。默认 115200，460800 作为三台有线节点统一复测配置；UART0 BSP 控制台停用、应用日志写有界 RAM。新路线的实际构建/测试结果见 [迁移记录](progress_uart_mio_migration.md)。下文工具安装版本和构建方式继续有效；旧 EMIO 结果只证明旧版源码，不自动证明新路线。7020 型号已确认，不再重复询问型号。

## 已安装、补齐及版本

| 项目 | 实际版本 / 来源 | 状态 |
| --- | --- | --- |
| Keil uVision | MDK Plus 5.40，安装目录内 `UV4/UV4.exe` | 沿用已有安装 |
| Keil 编译器 | Arm Compiler for Embedded 6.22，`ARM/ARMCLANG/bin/armclang.exe` | 真正编译官方 HAL 与通信适配，无许可证错误 |
| STM32F1 设备包 | Keil.STM32F1xx_DFP 2.4.1，官方 keil.com | 已下载并安装 |
| STM32F4 设备包 | Keil.STM32F4xx_DFP 3.1.1，官方 keil.com | 已下载并安装 |
| ARM CMSIS 设备依赖 | ARM.CMSIS 5.9.0，官方 keil.com | 已下载并安装 |
| STM32F1 HAL | STMicroelectronics/stm32f1xx_hal_driver v1.1.10 | 官方 GitHub tag，已本地展开 |
| STM32F4 HAL | STMicroelectronics/stm32f4xx_hal_driver v1.8.5 | 官方 GitHub tag，已本地展开 |
| STM32F1/F4 CMSIS device | ST 官方 v4.3.5 / v2.6.11 | 已本地展开 |
| ARM CMSIS Core | ARM-software/CMSIS_5 5.9.0 | 已本地展开 |
| Xilinx SDK | 2017.4，独立安装目录 | 沿用已有安装 |
| SDK ARM GCC | Linaro GCC 6.2.1 20161114 | 真正编译 Cortex-A9 适配与 STM32 对象 |
| PC 测试 GCC | Vivado 自带 MinGW GCC 6.2.0 | 本机可执行行为测试 |
| STM32CubeMX | 已安装6.18.1，EXE文件版本6.18.1-RC2，内置Java21 | 用户完成安装；官方 `-q` 脚本接口已实际生成两份地面工程 |
| CubeMX生成用固件源 | STM32CubeF1 v1.8.7 / STM32CubeF4 v1.28.3 | 从ST官方GitHub取得Drivers所需子集及锁定子模块；未安装无关中间件和示例 |

下载包、官方 URL 与 SHA-256 分别在 `deps/manifest.json`、`deps/packs/manifest.json`。设备包新版本展开到现有 Keil 的 `ARM/PACK/Keil/...` 与 `ARM/PACK/ARM/CMSIS/5.9.0`，没有覆盖其他版本。原始 `.pack` 保存在工程依赖目录。

CubeMX 正式产品页为 <https://www.st.com/en/development-tools/stm32cubemx.html>，可执行文件为安装目录内的 `STM32CubeMX.exe`。首轮官网下载曾返回HTTP401；此后用户已完成安装，该问题不再阻塞本工程。调用内置Java执行官方 `-q` 脚本，已生成真实 `.ioc` 和MDK项目，按生成C代码核对时钟、波特率、DMA和IRQ。没有把手写 `.ioc.template` 冒充实际生成结果。

## 可重复验证

在工程目录运行：

```powershell
python tools\run_checks.py --platforms
```

上面的完整平台命令要求已安装表中工具链，并将平台脚本中的工具路径对应到实际安装位置。只复现 PC 测试时，在原生 GCC 已加入 PATH 的终端执行 `python -X utf8 tools/run_checks.py --gcc gcc`；也可以通过 `--gcc` 指定其可执行文件。普通 Python 3 即可，不需要文档识别环境。原始构建日志保留当时环境信息；不应把机器安装目录当成仓库目录结构。

`tools/check_platforms.py` 单独运行真实依赖编译检查。F103、地面 F407、机载 F407 分别用 SDK ARM GCC 和 Keil ARMCLANG 编译共享核心及适配代码；地面目标还编译官方 HAL UART、DMA、SPI、GPIO、RCC、Cortex 和 F407 RNG 的真实实现。当前路线通过88项对象编译、1项Keil探针，以及两个误启BSP串口stdin/stdout的负向编译保护检查。结果与完整编译命令在 `build/platform_checks/results.json`；`run_checks.py --platforms` 同步留存到入包的 `reports/platform_compilation.json`。

快速 Zynq 对象检查使用真实 SDK 2017.4 的 `uartps_v3_5`、`scugic_v3_8`、`standalone_v6_5` 头文件，`xparameters.h` 是显式 compile-only fixture。独立的真实硬件导出/BSP/ELF 路径由 `tools/create_zynq_validation.tcl`、`tools/create_zynq_bsp.tcl` 运行，使用生成的 `xparameters.h`，不包含 fixture。BSP/应用采用 SDK hard-float ABI；应用 `-std=c99 -Wall -Wextra -Werror`，BSP 使用厂商选项。当前脚本应启用 UART0 MIO14/15、停 UART1 EMIO 和 BSP stdin/stdout；新路线实际结果见迁移记录及 `reports/zynq_sdk_validation.json`。输出目录必须与旧 EMIO 构建分开。

旧 UART1 EMIO 曾导出 HDF、链接真实 BSP/ELF，并通过独立 U5/T5 布局布线且最终 DRC=0；对应 XDC、`tools/check_zynq_7020_pins.tcl` 和 `reports/zynq_7020_pincheck.json` 仅保留为历史备用。当前 PS MIO UART 不需要这些约束或 PL bitstream；仍须匹配 PS 初始化、硬件导出与 BSP。没有烧录或实板验收记录，最终 FSBL/BOOT.bin 启动包尚未交付。

`platforms/stm32/tests/mock` 仅服务本机行为测试，验证桥接握手重放、真实诊断序列化和无线故障恢复；真实 HAL/SDK 编译的 include 路径不包含该目录。队列测试覆盖 HT/TC/IDLE 相同位置重采样、环形跨界、溢出及 32 位索引回卷。PC 模拟与对象编译不代表硬件收发、时钟、接线或无线距离已经通过。

## 工程模板与交接

`platforms/stm32/templates/ground_f103.uvprojx`、`ground_f407.uvprojx`、`air_f407.uvprojx` 是可以由 Keil 打开的静态库工程，使用同目录相对路径指向官方依赖；默认 `DC_BOARD_READY=0`。库工程不生成 HEX，不能直接烧录。旧路线三份工程已实际通过 `UV4.exe -b` 构建，0 errors / 0 warnings，生成三个 `.lib`；新路线结果见本轮报告。日志位于 `build/keil_project_logs`，库位于 `build/keil/<role>`。`tools/check_keil_projects.py` 可复验，`run_checks.py --platforms` 同步留存三份日志、项目/源码/库哈希到入包的 `reports/keil_project_build/`。独立对象检查与 uVision 工程构建是两份不同的验证证据。

另提供 `platforms/stm32/standalone` 三份完整通信工程，使用相同手动HAL后端，含主循环、SysTick、官方startup/system和严格scatter。F103限制64KiB Flash/20KiB RAM；F407使用1MiB Flash/128KiB普通SRAM。栈4096、堆0，统一为系统源文件声明HSE_VALUE=8000000U。`tools/check_stm32_standalone.py` 验证默认禁用的交付配置，再在build隔离项目以READY=1检查全部业务链接及内存；隔离输出明确标为LINK_ONLY_DO_NOT_FLASH，不生成HEX。源码与交付工程的READY始终为0。结果在 `reports/stm32_standalone.json`，并已纳入 `run_checks.py --platforms`。

三个 `.ioc.template` 继续作为早期参数备忘。实际CubeMX生成的地面F103/F407基底工程另位于 `platforms/stm32/cubemx`，包含真实IOC、启动文件、HAL依赖、MDK项目及复现说明；旧路线 F103 已完整链接且 0 错误 0 警告，F407 已完整链接且 0 错误 3 警告（官方 HAL `flash_ex.c` 的 unused Banks 参数）；新引脚和默认 115200 的生成/构建结果见本轮迁移报告。CubeMX默认Compiler5在本机不可用，因此工程显式选用已安装ARMCLANG6.22。生成基底只运行外设初始化，尚未接入通信主循环；三份通信库仍使用原先固定的HAL版本。不要混用两套HAL或重复的MSP/IRQ实现。

Zynq 验证源位于 `platforms/zynq`。应用按物理 PS UART0 基地址匹配配置表、使用生成 BSP 的物理 UART0 IRQ，不假定 device ID=0。`standalone_main.c` 提供 GIC 与主循环。交付 `DC_ZYNQ_BOARD_READY=0` 保持不变；板用版本启用前须核对 P5 跳帽已拆、MIO 电平、UART0 无文本日志及 PS 初始化/BSP/ELF 来自同一硬件配置。相关步骤见 `bring_up.md`。
