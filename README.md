# 三核通信与 E07 无线软件基线

本仓库是无人机项目的**通信子系统**，实现地面 F103、地面 F407、Zynq 和机载 F407 的通信分工。机载端返回模拟遥测，Zynq 返回模拟视频状态且视频锁定为 0；真实控制请求返回不支持。YOLO、视频采集和飞控由后续阶段独立集成；当前仓库不代表完整作品已经实板验收。

第一次拿到工程，请先阅读[零基础操作手册](docs/beginner_manual.md)，或下载[Word 版手册](docs/无人机通信工程零基础操作手册_v1.0.docx)。手册按接收文件、软件准备、接线核对、Keil 下载、SDK JTAG 加载、参数调整和故障排查逐步说明；实板步骤仍需实际执行并记录结果。

## 当前接口方案

当前接线为 **F407 USART3 PB10/PB11 + USART2 PA2/PA3，Zynq PS UART0 MIO14/MIO15**。三台有线节点默认 **115200 bps、8N1**，首轮通过后一起切换到 460800 复测；无线参数、协议格式、会话和重试期限保持不变。

| 发送端 | 接收端 |
| --- | --- |
| F103 PA2 / USART2 TX | Explorer P2.3 / PB11 / USART3 RX |
| Explorer P2.4 / PB10 / USART3 TX | F103 PA3 / USART2 RX |
| Explorer P4.3 / PA2 / USART2 TX | Navigator P5.2 / MIO14 / PS UART0 RX |
| Navigator P5.1 / MIO15 / PS UART0 TX | Explorer P4.4 / PA3 / USART2 RX |

三块板共地，各自供电，不并接 3.3V 输出。拆掉 Navigator P5 的 1–3、2–4 跳帽；Explorer P2/P4 从 MCU 侧接线并断开 RS232/GPS/RS485 的选择跳帽，拆 P1 音频跳帽。地面 F407 停用以太网和音频，保持 PHY reset=PD3 低、MDC=PC1 低。PA2 的 MDIO 电气状态、PA3 经 R17 的音频 RC 负载及两档速率波形仍需实测。

UART0 专用于协议；Zynq 应用日志转为有界 RAM 记录，通过 JTAG/调试器查看，BSP stdin/stdout 停用。旧 PC6/PC7→UART1 EMIO、U5/T5 XDC 和布局布线报告保留为历史备用，当前 MIO 通信不依赖这些 PL 管脚或专用 bitstream。外部接线使用 P2/P4/P5，不能把细间距核心板 X3 当作手接接口。

## 已有软件基线与验证边界

- 可移植 C 协议库：COBS、CRC-16、增量解析、会话握手、序号检查、超时和有界队列。
- E07/CC1101 驱动：433MHz GFSK、寄存器读回、FIFO 收发、CRC 检查、固定截止期重传和故障恢复。
- STM32 HAL 适配及三份独立通信工程：有启动文件、主循环、SysTick 和严格链接布局；交付配置保持 `DC_BOARD_READY=0`。
- Zynq SDK 2017.4 裸机通信适配：真实 HDF/BSP/ELF 构建路径与快速编译 fixture 分开；交付配置保持 `DC_ZYNQ_BOARD_READY=0`。
- 地面 F103/F407 实际 CubeMX 生成工程是初始化基底，尚未合入通信主循环。完整通信程序在手动 HAL 独立工程中，两者不能混编重复的 MSP/IRQ。
- PC 四节点模拟、协议/握手回归、无线寄存器/FIFO 模拟、C/Python 黄金帧和平台适配测试均已建立。

最新修复及逐项复核见 [10 月 6 日全工程复查](docs/deep_review_20261006.md)，接口迁移背景见 [迁移记录](docs/progress_uart_mio_migration.md)；旧路线的通过记录不自动算作新路线通过。尚未接线、烧录或实板验收，机载 E07 按用户最后信息仍未接线。UART 物理波形、无线距离、实际重传时序、视频采集、YOLO 上板和飞控均未通过实板验收。

## 审查入口

| 内容 | 入口 |
| --- | --- |
| 全工程复查、缺陷修复和 YOLO 验证边界 | [10 月 6 日复查](docs/deep_review_20261006.md) |
| 当前 UART/MIO 迁移与验证范围 | [本轮进度](docs/progress_uart_mio_migration.md) |
| 当前接线和跳帽条件 | [接线表](docs/proposed_wiring.md) |
| 时钟、DMA、日志及板级限制 | [板级配置](docs/board_config.md) |
| 队友实板验收步骤 | [联调说明](docs/bring_up.md) |
| 节点、消息格式、会话 | [协议](docs/protocol.md) |
| 无线寄存器与超时 | [E07 配置](docs/radio_profile.md) |
| 工具版本、依赖与复现 | [工具链](docs/toolchain.md) |
| 内存、FIFO、轮询预算 | [运行预算](docs/runtime_budget.md) |
| 原理图事实与旧路线证据 | [硬件证据](docs/hardware_evidence.md) · [领航者底板](docs/zynq_navigator_v2_evidence.md) |
| 7020 厂商参数与修订 | [厂商资料](docs/zynq_7020_vendor_lookup.md) |
| 历史软件与旧 EMIO 验证 | [首轮](docs/progress_20261005.md) · [补充](docs/progress_20261005_followup.md) · [7020](docs/progress_7020_confirmation.md) |
| PC/平台统一回归 | [验证报告](reports/verification.json) |
| 三份完整 STM32 通信工程 | [独立工程](platforms/stm32/standalone/README.md) · [链接报告](reports/stm32_standalone.json) |
| 真实 Zynq HDF/BSP/ELF | [SDK 报告](reports/zynq_sdk_validation.json) |
| CubeMX 初始化基底 | [CubeMX 工程](platforms/stm32/cubemx/README.md) |
| 交付包解压重建 | [复现报告](reports/handoff_portability.json) |

共享逻辑位于 `include/`、`src/`，硬件适配位于 `platforms/`，测试位于 `tests/`，官方依赖位于 `deps/`。原始私人说明与厂商 PDF 保留在本地和受限协作空间，GitHub 仓库仅提供[资料来源与校验哈希](docs/source/README.md)。第三方代码许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 重新运行软件验证

```powershell
# 在克隆后的仓库根目录运行
python -X utf8 tools/run_checks.py --gcc gcc
```

先安装 Python 3，并将本机原生 GCC 加入 PATH；或将 `--gcc gcc` 改为实际编译器路径。既有记录使用 MinGW GCC 6.2。安装匹配版本的工具链后可追加 `--platforms` 检查 STM32 HAL、Keil ARMCLANG 和 Zynq SDK；各平台脚本的工具路径应按 [工具链说明](docs/toolchain.md) 核对。快速对象编译中的 `xparameters.h` 是显式 compile-only fixture；真实 HDF/BSP/ELF 路径由 `tools/create_zynq_validation.tcl`、`tools/create_zynq_bsp.tcl` 生成，不使用 fixture，重复生成须选择新的输出目录。独立 YOLO 工程的模型和卷积核保持原样，新增审核入口与 HLS 测试台的范围见本轮复查记录。

## GitHub 评审与作品提交

本次公开整理、检查范围与持续集成方式见 [GitHub 提交审查](docs/github_submission.md)。

评审先阅读[当前迁移记录](docs/progress_uart_mio_migration.md)、[协议](docs/protocol.md)、[接线表](docs/proposed_wiring.md)和[实板验收表](docs/bring_up.md)，再按上面的命令复现 PC 测试。报告中的 `board_tested=false` 表示尚未进行实板验收；真实工具链链接记录不等同于板上运行。

每次提交作品时记录仓库 URL、提交 SHA 和对应测试记录；后续实板结果应附操作者、固件版本、接线、波特率、抓包或波形。源码与历史报告共同用于审查，历史 ZIP 的清单只适用于当时交付包；GitHub 版本以其提交及当次验证结果为准。当前没有 FSBL/BOOT.bin、最终烧录镜像、真实视频/YOLO或飞控完成证明。
