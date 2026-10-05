# CubeMX 地面板基底工程

这里保存 CubeMX 6.18.1 实际生成并经 Keil Arm Compiler 6.22 完整链接的两份基底工程；它们的 `main.c` 只初始化外设并进入空循环。通信软件、DMA 字节采集、IDLE 处理、E07 状态机和路由仍在 `platforms/stm32/common` 与共享库中，不能把本目录的空循环工程当成通信烧录包。

| 工程 | 本阶段时钟与外设 | 实际构建结果 |
|---|---|---|
| ground_f103 | STM32F103C8T6；用户已确认最小板 HSE 8 MHz，72 MHz；USART2 PA2/PA3；SPI1 PA5/PA6/PA7；PA4 CSN 高；PB0 双沿 EXTI；PB1 下拉输入；DMA1 Channel6 循环 RX | 0 错误、0 警告；完整链接 |
| ground_f407 | STM32F407ZGT6；HSE 8 MHz，168 MHz；USART3 PB10/PB11；USART2 PA2/PA3；DMA1 Stream1/Stream5 Channel4 循环 RX；RNG；PD3 PHY 复位低、PC1 MDC 低 | 0 错误、3 警告；完整链接 |

各地面 UART 默认 115200 bps、8N1，完成实板低速验证后再统一切换 460800；UART/DMA/EXTI 优先级为 5，SysTick 为 15；栈 0x1000、堆 0x200。F407 的 3 条警告来自官方 `stm32f4xx_hal_flash_ex.c` 中 `Banks` 未使用参数，原始源码和警告保留在证据中。F407 在 UART 初始化前将 PD3 PHY 复位保持低、PC1 MDC 保持低，未初始化 DCMI、FSMC、I2S、DAC 音频或以太网。这些措施不能单独证明 PHY MDIO 高阻，必须按接线表拆 P2/P4 跳帽和音频跳帽并检查 PA2/PA3 电气状态；摄像头接 FPGA 的既定分工继续保留。没有 AIR 工程，也没有实板验收结果。

直接打开 `ground_f103/MDK-ARM/ground_f103.uvprojx` 或对应 F407 工程即可构建，输出位置为项目根的 `build/cubemx_delivery`。交付目录仅保存源、头文件、启动文件、工程和 IOC，未包含 AXF/HEX 烧录输出。

CubeMX 原始 MDK 工程默认选择 Compiler 5，而本机安装的是 Arm Compiler 6.22。随附工程已明确选择 6.22 和 Keil F1 DFP 2.4.1 / F4 DFP 3.1.1；原始工程、Compiler 5 不可用日志、最终构建日志均在 `evidence`。`evidence/discovery.json` 记录安装版本、官方固件标签及锁定的子模块 commit。

重新生成使用官方命令行，不需要界面操作：

```powershell
powershell -ExecutionPolicy Bypass -File .\regenerate.ps1 -FetchOfficialFirmware
```

脚本从 STMicroelectronics 官方仓库获取 F1 v1.8.7 / F4 v1.28.3 及各自锁定的 HAL、CMSIS device 子模块；默认存放于项目 `build/cubemx_probe/firmware`。也可用 `-FirmwareRoot` 指向已有的同结构官方源码。脚本把真实 IOC 复制到新的输出目录，再指定固件、生成代码并选择本机 Compiler 6。默认输出为 `build/cubemx_regenerated`；可用 `-OutputRoot` 指定其他新目录，目录已存在时拒绝执行。重新生成不覆写交付源、IOC 或证据，日志在新目录的 `logs` 中。最小板晶振已确认，实际接线和实板收发仍需核对；重生成后需再次编译。

初始 IOC 来自 CubeMX，随后显式填入 DMA/NVIC/clock 字段，再由 CubeMX 重新加载、保存并生成。实际生成 C 文件已核对 PLL、UART 引脚与波特率、循环 DMA/channel、CSN 初始状态、EXTI、RNG 和中断优先级。此过程证明配置可生成和链接，实板收发和时钟精度仍需队友验证。

本轮新接线由真实 CubeMX CLI 重新加载 IOC 并生成，记录见 `evidence/mio14_generation.json` 和 `*_mio14_generation.log`。`*_cubemx_original.*` 与旧 `generate_*.log` 仅为首轮历史证据，不能作为当前引脚依据。`verify.py` 同时核对当前生成 C 的 AF7、DMA Stream/Channel、IRQ、115200 和 PHY GPIO 初始化顺序。

CubeMX 基底的波特率由各自 IOC 决定，当前没有链接共享配置头。若切换 460800，应同步修改 F103 USART2 及 F407 USART3/USART2 的 IOC，重新生成并构建；同时让完整通信工程和 Zynq 使用 `DC_UART_BAUD=460800u`。不能只修改链路一端。
