# 领航者 V2 Zynq 资料盘通信引脚证据

> 当前方案更新：现行方案采用 F407 USART3 PB10/PB11、USART2 PA2/PA3 ↔ Zynq PS UART0 MIO14/MIO15，外部接点为 P2/P4/P5，默认 115200、通过后统一复测 460800。UART0 文本日志停用，P5 的 1–3/2–4 跳帽必须拆除。本文下方保留旧 USART6/UART1 EMIO 路线及当时状态作为历史证据；其中旧接线、跳帽、日志和 bitstream 要求不作为现行施工步骤，旧测试 PASS 不自动适用于新路线。当前接线见 [接线表](proposed_wiring.md)，迁移结果见 [本轮记录](progress_uart_mio_migration.md)。

> 后续更新：用户已确认实物为 **7020**。官方手册另已查明标准型号为 **XC7Z020-2CLG400I**、BANK13默认3.3V，见 [7020厂商资料核对](zynq_7020_vendor_lookup.md) 和 [最新推进记录](progress_7020_confirmation.md)。下文保留首次路线审查时的证据与状态；其中“型号未确认”“未运行实现”属于当时状态，不再作为继续软件工作的前提。

核实日期：2026-10-05。资料已支持一组可审查的 UART1 EMIO 候选接线：Zynq RX 为 **J4 第 3 脚 / U5**，Zynq TX 为 **J4 第 4 脚 / T5**。这两根线属于 BANK13，仅 7020 器件具有该 bank。当前已有 Vivado 工程配置为 `xc7z020clg400-2`；工程目标与实物确认分别记录，本文不把资料盘版本或工程 part 当作实物型号已经确认。

本次只读取原资料和 独立 YOLO 工程，新增本文件及 `reports/zynq_navigator_v2_evidence.json`。没有修改原 YOLO 工程，没有锁定生产用 XDC，没有生成或验收可供实板烧录的 bitstream。

## 原件、版本与读取方法

资料来源为厂商“正点原子领航者(V2)ZYNQ开发板资料盘(A盘)”，公开获取入口见[来源说明](source/README.md)。主目录为 `3_开发板原理图和硬件相关文件`；旧目录为 `9_领航者ZYNQ底板V3.7版本（音频芯片为WM8960）\3_开发板原理图`。

| 原件 | 位置 | SHA-256 |
| --- | --- | --- |
| 领航者ZYNQ底板原理图_V3.9.pdf | 主目录，7 页 | `EC8B68EEB07E0743E6C60528444F6AEB457DC60E897DDF9EB86D5A56C1AB6A69` |
| 领航者ZYNQ IO引脚分配总表.xlsx | 主目录 | `12EA3D18B8CD7259D5299ECF7A95856B40C0AA8AFB0D8100A8DFBEC8229D3EA6` |
| ZYNQ7010_7020核心板原理图_2V5.pdf | 主目录，12 页 | `1C8CF8105E91D762D29AD604E82B629B3921FA474D5A0311ED6C04237DB107EA` |
| NAVIGATOR_ZYNQ_IO.xdc | 主目录 | `80438A533021B8BFF2F904E454384E179DB1E20AA0384093659D703949B20452` |
| 领航者ZYNQ底板原理图_V3.6.pdf | 旧目录，7 页 | `4CD216D5F2E9C1AF0FB954884E384C2E6FE488F71588461F5F2D245099C8C63F` |

使用本地 MarkItDown 默认自动路由。底板 PDF 为原生文本，直接提取，未使用 OCR 或云服务。主目录核心图与用户此前提供的 `ZYNQ_CORE_2V5_user.pdf`、旧目录核心图的 SHA-256 相同，因此复用已完成的核心板文本提取；新增视觉核对 BANK13 第 6 页。审计 JSON 保留逐页原生文本/栅格图片计数和全部源文件哈希。

电路 PDF 的隐藏 Altium 对象文本存在逐字字符和 `PI`/`NL` 标记。连线及针序以原始页面渲染图为准，不按 Markdown 阅读顺序猜测。底板第 5、6 页和核心第 3、4、6、7、10、12 页已视觉交叉核实；提取和渲染输出保留在本地审计资料中，未随公开仓库发布。页码均为 PDF 文件的一基页码。

旧目录标签写 V3.7，但底板文件名写 V3.6。两个底板文件第 5 页的 J4 第 3/4 脚路线，以及第 6 页 PS UART 的 P5 选择线路一致。这个比较只覆盖候选通信线路；不证明所有底板版本、器件装配或实物连接完全一致。

## UART1 EMIO 首选候选接线

| 计划功能 | 外部排针 | 底板网名 | 底板核心接口 | 核心连接器 | FPGA PACKAGE_PIN | bank | 接 F407 |
| --- | --- | --- | --- | --- | --- | ---: | --- |
| Zynq UART1 RX | J4 pin3 | B13_L19_N | M1 J1_88 | X4 pin88 | U5 | 13 | USART6 PC6 TX |
| Zynq UART1 TX | J4 pin4 | B13_L19_P | M1 J1_90 | X4 pin90 | T5 | 13 | USART6 PC7 RX |
| 共地 | J4 pin37 或 pin39 | GND | GND | GND | — | — | F407 GND |

证据链：底板第 5 页 `EXT_IO` 的 J4 pin3/pin4；同页 `CORE_IO` 的 M1 J1_88/J1_90；底板 J1 对应核心 X4；核心第 12 页 X4 pin88/pin90；核心第 6 页 BANK13 的 U5=`IO_L19N_T3_VREF_13`、T5=`IO_L19P_T3_13`。P/N 是原始差分对名称，本方案将两脚分别作为单端 UART RX/TX 使用。

底板候选线路未见接至其他板载有源外设，主目录厂商 XDC 也没有使用 U5/T5。不要误接排针上的电源脚：J4 pin38 为 +3.3V，pin40 为 +5V。本阶段双方各自供电，信号为 TX/RX 交叉与共地；不需要把两块板的电源输出并接。

核心第 7 页 BANK13 电源为 `VCCO_B13`，图示默认由 +3.3V 经 0R 电阻连接，另有从 `VCCIO` 接入的 NC 装配选项。`LVCMOS33` 因而是**确认实物供电为 3.3V 后**采用的约束值。J4 标注 `BANK13 (ZYNQ7020 Only)`；7010 实物不能使用本候选对。

现有工程未导出 UART1 EMIO，接线方向是本阶段提出的设计分配。最终工程需启用 PS UART1，经 EMIO 导出 TX/RX 并使其成为顶层端口；实际端口名以生成 wrapper 为准，再将相应端口约束到 T5/U5。本文不给出可直接粘贴烧录的 XDC，以免在实物型号/电压尚未核对时误锁定。

## BANK34 备用评估

J3 的 BANK34 扩展线网共享 LCD/触摸屏，J4 中 BANK34 线网共享摄像头。已核对排针中没有与首选 BANK13 方案同等的“无外设共享”BANK34 候选对。

若实物为 7010，可审查以下备用方案；目前未选用，也未写入工程配置：

| 备用功能 | 外部排针 | 线网及共享接口 | 核心连接器 | PACKAGE_PIN | bank |
| --- | --- | --- | --- | --- | ---: |
| UART1 TX | J3 pin31 | B34_L7_P / LCD_R5 | X4 pin37 | Y16 | 34 |
| UART1 RX | J3 pin32 | B34_L7_N / LCD_R4 | X4 pin39 | Y17 | 34 |

证据：底板第 5 页 J3、M1 J1_37/J1_39，核心第 3、12 页。使用条件为拔下 LCD 模块，不启动这些 LCD 输出，并为通信保留这两根显示接口线。BANK34 由 `VCCIO` 供电，核心第 10 页 U8 为 `SPX3819M5-3-3`，图表达 3.3V；仍应核对实物电压与装配。该备用可能影响后续显示功能，所以首选保留 BANK13 的独立排针线。

## UART0 USB 日志路线

现有 Vivado 工程的 **PS UART0 / MIO14–15** 与底板的 PS USB 串口线路吻合。底板线路名 `UART1_RXD/TXD` 是板商网络命名，不能据此把软件外设改成 PS UART1。

| 路径 | 证据 |
| --- | --- |
| PS UART0 RX：MIO14 / C5 → 核心 X3 pin89 → 底板 M1 J2_89 → UART1_RXD → P5 pin2 | 核心第 4、12 页；底板第 5、6 页；IO 表 `PS IO引脚列表` 第 18 行 |
| PS UART0 TX：MIO15 / C8 → 核心 X3 pin87 → 底板 M1 J2_87 → UART1_TXD → P5 pin1 | 核心第 4、12 页；底板第 5、6 页；IO 表 `PS IO引脚列表` 第 19 行 |
| P5 pin3=CH340C_RXD；pin4=CH340C_TXD；pin5=UART3_TX；pin6=UART3_RX | 底板第 6 页 |
| P5 短接 **1–3、2–4** 将 PS 串口连接到 CH340C；U11/U12 为 74LVC1G125GW 缓冲；U14 为 CH340C，USB 口为 USB_UART Type-C | 底板第 6 页 |
| PS BANK500（MIO0–15）为 +3.3V；PS BANK501 为 +1.8V | 核心第 7 页 |

P5 短接 3–5、4–6 时选板上 PL UART3 线网。不能在一列同时短接两组而将 PS/PL 两个发送端短接。上板时需按 pin1 标记核对实际跳帽，UART0 日志是否到 PC 还需实测。

IO 表 `PL IO引脚列表` 第 19/20 行列出 RS232/RS485 的 K14/M15，第 22/23 行列出 ATK MODULE 的 T19/J15；厂商 XDC 同时包含多个可选 UART 例程映射。它们不是本计划 UART1 EMIO 的唯一指定引脚，也不能把 RS232 电平端口当作可直连 F407 的 3.3V UART。厂商全表不适合整体加入当前小型通信工程，必须只提取实际使用的约束。

## 现有 FPGA 工程只读核对

| 项目 | 已读取的状态 |
| --- | --- |
| 工程 | 独立维护的 `zynq_yolo.xpr` |
| XPR SHA-256 | `DE379A543E856A00EA3A7722D506A6E2CD3F64E84D3D7DE1809E16161377C825` |
| part | `xc7z020clg400-2` |
| BD | `zynq_yolo.srcs\sources_1\bd\system\system.bd`，Vivado 2017.4，文件中 `isValidated=true` |
| BD SHA-256 | `9E0384C55978D05DF21C0FCB88553E4369FD4DC63FB59C77622379431DB0D6D0` |
| UART0 | `PCW_UART0_PERIPHERAL_ENABLE=1`，`PCW_UART0_UART0_IO=MIO 14 .. 15`，`PCW_EN_EMIO_UART0=0`；MIO14/15 IOTYPE 为 LVCMOS 3.3V |
| UART1 | 未见显式 UART1 配置或导出的 EMIO TX/RX 顶层端口 |
| 顶层外部接口 | DDR、FIXED_IO；未见外部 PL 标量端口 |
| 约束 | XPR `constrs_1` 文件集没有 File 条目；工程内未发现 `.xdc` |

首选 U5/T5 在已读工程中没有现有 PL 使用冲突。这个检查说明现有设计可以作为 PS 配置基础，不证明实物是 `-2` 速度级，也不证明验证标记等同于本次已运行实现/实板验收。未重新启动 Vivado、未改原项目，现有 YOLO 模型和 PL 视觉计算方向保持原任务边界。

## 最小实物确认与后续使用

在生成最终板用约束和烧录文件前，核对以下信息即可推进，不必重新确认软件协议或四节点角色：

1. 核心芯片完整丝印是否与现有 `xc7z020clg400-2` 目标一致，至少确认 7020/CLG400，才使用 BANK13 候选对。
2. 底板丝印版本和 J4 pin1 方向是否与资料一致；J4 第 3/4 脚没有接其他外部模块。
3. BANK13 的实际 VCCO 为 3.3V，电阻装配符合默认图或已测量确认；排针 pin38 的 +3.3V 电源不单独证明 BANK13 供电。
4. P5 的跳帽是否为 1–3、2–4，确认 UART0 USB 日志能够到 PC。

通信软件/PC 验证和 SDK 逻辑编译可继续进行。本文结论为“资料与候选路径已核实”；硬件接线、电压、UART0 日志、460800 bps 波形和 UART1 上板通信均为待测，不能计作视频采集或 YOLO 上板完成。
