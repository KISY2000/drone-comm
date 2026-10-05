# 补充硬件资料核实记录

> 当前方案更新：现行方案采用 F407 USART3 PB10/PB11、USART2 PA2/PA3 ↔ Zynq PS UART0 MIO14/MIO15，外部接点为 P2/P4/P5，默认 115200、通过后统一复测 460800。UART0 文本日志停用，P5 的 1–3/2–4 跳帽必须拆除。本文下方保留旧 USART6/UART1 EMIO 路线及当时状态作为历史证据；其中旧接线、跳帽、日志和 bitstream 要求不作为现行施工步骤，旧测试 PASS 不自动适用于新路线。当前接线见 [接线表](proposed_wiring.md)，迁移结果见 [本轮记录](progress_uart_mio_migration.md)。

核实日期：2026-10-05。本文是通信工程的硬件证据记录，不代表实板已经联调或通过验收。页码均为 PDF 文件的一基页码；原文件未修改。

本文件保留首批 Explorer/核心板证据。后补领航者资料盘已完成底板路线核实，见 [zynq_navigator_v2_evidence.md](zynq_navigator_v2_evidence.md)；当前建议接线见 [proposed_wiring.md](proposed_wiring.md)，用户已确认事实见 [user_clarifications_20261005.md](user_clarifications_20261005.md)。机载 E07 尚未接线，但对应建议 HAL 初始化和板级 helper 已实现。

最新补充：用户已明确确认 **7020**，官方型号为 `XC7Z020-2CLG400I`，无需再索取 7010/7020 型号。软件沿用 `xc7z020clg400-2`，按照官方 BANK13 默认 3.3V 推进 UART1 离线约束。V3.6/V3.9 底板资料的 J4.3/U5 RX、J4.4/T5 TX 路线一致；资料盘不能唯一证明实物底板修订号，丝印、针序和电压由上板操作者记录。官方型号/修订依据见 [官方资料核查](zynq_7020_vendor_lookup.md)，新增约束结果见 [7020推进记录](progress_7020_confirmation.md)。

## 资料与提取方法

| 文件 | 页数 | SHA-256 |
| --- | ---: | --- |
| `EXPLORER_V3.4.pdf` | 7 | `282206B31B7072204A5A105A289A41D5D4CA11A61664305AE6C48F56040BE9D6` |
| `ZYNQ_CORE_2V5_user.pdf` | 12 | `1C8CF8105E91D762D29AD604E82B629B3921FA474D5A0311ED6C04237DB107EA` |

原始资料保留在本地及受限协作空间；公开仓库只发布[来源与哈希](source/README.md)，不发布私人说明或厂商 PDF 副本。

使用本地 MarkItDown 自动路由提取。全部 19 页均存在原生文本层，无显示栅格图片，因此直接提取文本，未使用 OCR，也未调用云服务。

文本、逐页检查记录和 2 倍比例视觉复核图片保留在本地审计资料中；仓库保留页码、来源哈希和结构化核查结果。电路 PDF 的原生文本含 Altium 的 `PPII`/`NL` 对象标记，线网拓扑不能仅凭 Markdown 的阅读顺序判断；以下引脚与电压结论已对照原始页面渲染图核实。补充资料的结构、日期与图名以原图为准，文件名不证明实物的版本和器件装配选项。

## F407 Explorer V3.4：已确认事实与使用条件

| 项目 | 原图事实 | 证据页 | 对通信工程的影响 |
| --- | --- | --- | --- |
| MCU | `STM32F407ZGT6`，U14A/U14B | 2、3 | 可作为 F407 资料依据；地面和机载角色沿用最初分工文件，机载建议 CS/GDO 已实现，实物接线待核对。 |
| 主晶振 | Y2 为 `8MHz`，接 PH0/PH1（芯片 23/24 脚） | 2 | 对该原理图可采用 `HSE_VALUE=8000000`；不能据此推断 F103 的晶振。 |
| 低速晶振 | Y3 为 `32.768K`，接 PC14/PC15 | 2 | 本阶段时间基准可使用系统 tick，无需依赖 LSE。 |
| USART3 | PD8=`U3_TX`、PD9=`U3_RX`；芯片脚 77/78 | 3 | 计划中的外设应为 USART3。两脚同时连接 FSMC 总线、板载 SRAM 和外接 LCD。 |
| USART6 | PC6=`U6_TX`、PC7=`U6_RX`；芯片脚 96/97 | 2 | 计划功能正确，两脚同时连接摄像头接口；PC6 还接 ES8388 的 MCLK 输入。 |
| USART6 外接位置 | PC6 为 P9 的 29 脚；PC7 为 P9 的 28 脚 | 3 | 可在资料一致的实物 P9 上接线；插针方向、1 脚标记需实物确认。 |
| USART3 外接位置 | TFT_LCD 的 19 脚为 PD8、20 脚为 PD9 | 3 | 可从 LCD 接口引出；这不是资料中 P2 的串口接口。P2 USART3 是 PB10/PB11 的另一组引脚。 |
| SRAM 共享 | PD8=`FSMC_D13`、PD9=`FSMC_D14`；U15 CE 接 PG10=`FSMC_NE3`，有 R58 10k 上拉 | 3 | 用 PD8/PD9 通信时关闭 FSMC，将 PG10 明确置高使 SRAM 不选通；拔下 LCD 模块，避免外部器件驱动数据线。 |
| 音频与摄像头共享 | PC6 接 ES8388 MCLK 输入和摄像头 D0；PC7 接摄像头 D1 | 1、4 | 关闭 I2S/DCMI，拔下摄像头；ES8388 MCLK 是输入负载，仍需实板检查高波特率信号。仅关闭 MCU 的 DCMI 不保证插着的摄像头停止输出。 |

地面 F407 第一轮只初始化 USART3、USART6、对应 DMA、tick 和必要 GPIO。不要沿用板商综合例程自动初始化 LCD/FSMC、摄像头/DCMI 或音频/I2S。RX 与 TX 使用独立引脚，UART 为 3.3 V 逻辑电平，接线时双方 TX 接对方 RX并共地。资料中的 RS232/RS485 接口含收发器，不能直接当作 TTL UART 排针。

## F407 的 E07 SPI1 候选接法及冲突

用户已确认机载 E07 尚未接线。以下是对该原理图的候选评估；已选建议 PB3/PB4/PB5 SPI1、PG7 CSN、PG6 GDO0 并实现手动 HAL 初始化，完整针序见 [proposed_wiring.md](proposed_wiring.md)。该方案尚未成为实物接线确认记录，READY 默认为 0。

| SPI1 方案 | 原图连接 | 处理条件 |
| --- | --- | --- |
| PA5/PA6/PA7 | PA5 接 ADC 电位器链路；PA6 接摄像头 PCLK 输出；PA7 接 YT8512C 以太网 PHY 的 CRS_DV 输出 | 不推荐直接套用。PA5 存在模拟负载；PA6 需拔摄像头；PA7 可能与供电中的 PHY 主动输出冲突，仅关闭 MCU ETH 外设不足以证明安全。证据：第 1、2、4、5 页。 |
| PB3/PB4/PB5 | PB3=`SPI1_SCK`、PB4=`SPI1_MISO`、PB5=`SPI1_MOSI`，共享 U8 `25Q128` Flash 和板载无线接口 | 适合作为通信测试候选。上电初始化时先将 Flash CS=PB14 置高，整个 E07 测试期间保持高；不用 Flash 驱动。PB3/PB4 与 JTAG 共享，只保留 SWD，释放完整 JTAG 的 TDO/NJTRST 功能。证据：第 2、3、4、5 页。 |

资料第 5 页的 U16 是 `WIRELESS`/NRF 风格接口：1=GND、2=3.3V、3=PG6/CE、4=PG7/CSN、5=PB3/SCK、6=PB5/MOSI、7=PB4/MISO、8=PG8/IRQ（经过 R66 1k）。该接口不是 E07/CC1101 的标准管脚排列，必须按照 E07 模块实际管脚做转接。不能将 E07 按 NRF 针序直接插上。

当前建议 PG7（U16 pin4 / P9 pin31）作为 CSN，PG6（U16 pin3 / P9 pin32）作为输入 GDO0/EXTI6，GDO2 本轮不接；对应 GPIO/SPI/时钟和 helper 已实现。PG8 还与 RS485 收发器使能脚共享（第 3、4 页），本方案不使用 U16 pin8。具体跳线位置和 E07 模块侧针号仍需按实际接线记录，完成核对前 READY 保持关闭。

地面 F103 的 E07 SPI1 接线不由这份 F407 图决定。用户已确认 F103 为最小系统板、晶振 8MHz；芯片按最初已定的 STM32F103C8T6 配置，丝印待上板检查。具体板上焊接选项与 GDO 接脚仍按实际接线核对。原通信分工中的 F103 SPI2 勘误仍为 PB14=MISO、PB15=MOSI，第一轮保持预留。

## Zynq 核心板：已确认事实与边界

| 项目 | 原图事实 | 证据页 | 对当前计划的影响 |
| --- | --- | --- | --- |
| 器件范围 | 图中标记 `XC7Z020 / XC7Z010`、`Zynq7010/7020`，未给出实物完整丝印和速度等级 | 1–7 | 用户随后已确认7020，软件沿用现有目标 `xc7z020clg400-2`；上板记录完整丝印以核对封装/速度等级，不再将7010/7020作为待回答问题。 |
| PS 时钟 | X2=`33.333333Mhz`，经 R17/33R 接 PS_CLK/E7 | 4 | 适配现有 PS 配置时核对输入时钟，不将 PL 的 50MHz 误作 PS 晶振。 |
| PL 时钟 | X1=`50Mhz`，`PL_GCLK` 接 BANK34 的 U18 | 3 | 这是核心板已用的时钟输入网，不能分配给 UART EMIO。 |
| PS BANK500 | 电源为 +3.3V，包含 MIO0–15 | 4、7 | 原图的 UART_RXD/TXD 位于该电压域。 |
| PS BANK501 | 电源为 +1.8V，包含 MIO16–53 | 4、7 | 不能把这个 bank 的普通 MIO 直接按 3.3V UART 使用。 |
| PL BANK34 | 供电网为 VCCIO；U8 标注 `SPX3819M5-3-3` 输出 VCCIO | 7、10 | 原图表达 VCCIO 为 3.3V，但烧录/接线前需核对实板电压与装配，才锁定 LVCMOS33。 |
| PL BANK13/35 | 供电由零欧/NC 电阻在 +3.3V 与 VCCIO 间选择；第 7 页绘制默认 +3.3V 连接 | 7 | 本次BANK13按官方默认3.3V、LVCMOS33推进软件/离线约束；上板连接UART前测量实际电压并记录装配，不等待重复文字确认。 |
| 核心板串口 RX | `UART_RXD` 接 MIO14，芯片球位 C5，板间连接器 X3 的 89 脚 | 4、12 | 后补底板图已追溯至 UART1_RXD/P5 pin2，现有 PS 配置使用 UART0。 |
| 核心板串口 TX | `UART_TXD` 接 MIO15，芯片球位 C8，板间连接器 X3 的 87 脚 | 4、12 | 后补底板图已追溯至 UART1_TXD/P5 pin1；P5 1–3/2–4 选择 CH340C USB 日志，跳帽与日志仍待实板核对。 |
| PL 外引关系例 | BANK34 `B34_L4_P`=球位 V12 → X4 的 55 脚；`B34_L4_N`=W13 → X4 的 56 脚 | 3、12 | 只说明“芯片球位→核心板板间连接器”的映射方式；这两个脚不是已分配的 UART1。 |

UART1 经 EMIO 接地面 F407按原计划推进。后补资料盘底板第 5 页已闭合首选路线：RX=J4 pin3 → B13_L19_N → M1 J1_88 → 核心 X4 pin88 → U5；TX=J4 pin4 → B13_L19_P → M1 J1_90 → 核心 X4 pin90 → T5。两脚属 BANK13，7020 型号已由用户确认；V3.6/V3.9 资料路线一致。图示默认 3.3V用于软件/离线约束，实际电阻装配和电压列入上板检查。核心 X3/X4 为 0.8mm 板间连接器，不能直接当作外部杜邦线排针；外部接点采用底板 J4。完整主/旧底板比较及源哈希见 [zynq_navigator_v2_evidence.md](zynq_navigator_v2_evidence.md)。

现有 独立 YOLO 工程 part 为 `xc7z020clg400-2`、UART0 为 MIO14/15，与用户已确认的7020方向一致；完整丝印及底板修订号在上板记录中核对。独立 `tools/create_zynq_validation.tcl` 已验证/综合 UART1 EMIO 并导出 HDF，`tools/create_zynq_bsp.tcl` 已生成真实 SDK 2017.4 standalone BSP 和通信 ELF。生成 wrapper 的端口为 `COMM_UART1_rxd/COMM_UART1_txd`。旧注释模板继续保留；新增 `platforms/zynq/navigator_7020_uart1.xdc` 按U5 RX/T5 TX、LVCMOS33配置，独立检查脚本为 `tools/check_zynq_7020_pins.tcl`，已完成独立布局布线且最终DRC为0；过程告警和时序检查范围见最新进度与 `reports/zynq_7020_pincheck.json`。没有生成板用bitstream或执行实板验收。用户摄像头连接 FPGA，视频状态仍为软件模拟，本阶段不计作视频采集或 YOLO 上板完成。

## 上板操作者需记录与测量的事项

1. 角色按最初分工文件执行，无需再次确认两块 Explorer 的归属；上板时核对实际插着的 LCD、摄像头、NRF 无线模块或其他外设，并处理本文列出的共享引脚。
2. F103 按用户已确认的 C8T6/8MHz 生成配置；上板时核对完整丝印和 E07 的 CS/GDO 实际连接表。
3. 机载按 `proposed_wiring.md` 建议表接线，核对 PB14 / Flash CS、PG7 / E07 CSN、PG6 / GDO0、E07 模块侧针号及 SWD 配置；GDO2 本轮不接。
4. Zynq 7020型号已确认；上板时记录完整芯片/底板丝印，核对 J4 pin1 方向、测量 BANK13 为3.3V，并检查 P5 日志跳帽。V3.6/V3.9底板路线资料已补齐，无需再索取型号或核心板图；这些操作检查不阻塞按官方默认配置推进软件和离线约束。

用户已安装并授权使用官方 CubeMX，环境和实际生成记录另见 `toolchain.md`。上述实物检查决定上板接线和实际烧录配置；软件及离线约束按已确认7020与官方默认3.3V继续推进。最新状态见 `progress_7020_confirmation.md`，前轮完整验证见 `progress_20261005_followup.md`；PC 验证、SDK/HAL 编译、离线约束检查和实板验收分别记录。
