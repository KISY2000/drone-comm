# 独立 STM32 通信工程

三份 `*.uvprojx` 将共享协议、手动 HAL 适配和 `common/board_peripherals.c` 链接为完整通信应用。采用 `deps/` 中 STM32F1 HAL 1.1.10、STM32F4 HAL 1.8.5、CMSIS Device F1 4.3.5 / F4 2.6.11、CMSIS Core 5.9.0；不要加入 CubeMX 的 main、MSP、IRQ 或外设初始化文件。原静态库模板和 CubeMX 基底继续独立保留。

`main.c` 依次调用板级初始化、通信启动和轮询，`SysTick_Handler` 调用 `HAL_IncTick()`。启动失败时进入等待中断的停止循环，调试器可查看 `dc_startup_status`。启动文件复制自官方 CMSIS ARM 模板，只调整栈为 4096 字节、堆为 0，并统一文本换行；来源、哈希和修改记录见 `startup_provenance.json`，许可文件随附。系统初始化文件直接引用原始 `deps/` 文件。

所有交付模板显式设置 `DC_BOARD_READY=0`。项目编译参数统一设置 `HSE_VALUE=8000000U`，包括默认使用 25 MHz 的 F4 系统时钟源文件。地面 F103 的 8 MHz 晶振已由用户确认；F407 依据 Explorer 原理图的 8 MHz 配置准备软件，芯片丝印及实际接线待确认。不要把成功链接理解为可直接烧录。

F103 链接范围严格限制为 64 KiB Flash、20 KiB RAM；F407 按 STM32F407ZG 工程限制为 1 MiB Flash、128 KiB 普通 SRAM，不使用 CCM、外部 SRAM 或 SDRAM。4096 字节栈计入 RAM，微型 C 运行库不使用动态堆。栈大小只是静态预留，最坏运行栈深和中断嵌套须实板测试。模板不生成 HEX。

重新生成模板：

```powershell
python platforms/stm32/standalone/create_projects.py
```

真实 Keil 完整编译与链接检查：

```powershell
python tools/check_stm32_standalone.py
```

脚本先重建默认禁用项目，再在 `build/stm32_standalone/enabled_link_only/projects/` 生成独立重定位项目，仅在这个编译检查副本设置 `DC_BOARD_READY=1`。这样可以检查完整业务路径的链接符号和内存，避免默认禁用分支经优化后掩盖问题。启用副本和 AXF 仅留在 `build/`，不得烧录，不生成 HEX，不修改 `common/board_config.h` 或交付模板。日志、链接 map 和哈希报告保存到 `reports/stm32_standalone/` 与 `reports/stm32_standalone.json`；报告必须区分 `default_disabled` 与 `enabled_link_only`，并保留 `board_tested=false`。

本地 ARM Compiler 6.22 完整链接结果：F103 两种模式均为 0 错误、0 警告；F407 每个项目为 0 错误、4 警告，其中 3 条来自旧版官方 HAL `flash_ex.c` 未使用的 `Banks` 参数，1 条来自 MDK 启动汇编预处理使用的 target 参数提示。这些原始警告保留在报告和日志中，未修改厂商代码或关闭诊断。

本轮地面路由为 USART3 PB10/PB11（Explorer P2）连接 F103，USART2 PA2/PA3（Explorer P4）连接 Zynq P5。代码在 UART 初始化前将 PD3 PHY 复位和 PC1 MDC 保持低，不初始化以太网或音频；物理拆跳帽、电平与共享支路负载仍须实测。完整应用包含新的 UART/DMA IRQ 和业务句柄绑定。

波特率统一来自 `include/dc_link_config.h`，默认 `115200`；硬件低速验收后可在各通信端一致定义 `DC_UART_BAUD=460800u`。执行 `python tools/check_stm32_standalone.py --baud-460800` 会在六项默认验证外增加两个地面角色的 460800 完整业务链接检查，输出至独立 `enabled_link_only_460800` 目录。机载仅使用 E07，不重复有线波特率构建。两种速度的编译通过均不代表实板波形或丢包验收。
