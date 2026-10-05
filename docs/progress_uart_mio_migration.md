# UART 接线迁移：USART3 / USART2 / PS UART0 MIO

本轮依据团队确认的接线变更，将新接点和板载隔离条件一起纳入工程。原始资料以及独立维护的 YOLO、卷积原型工程保持原样。现阶段没有实物接线、烧录或上板验收记录。

## 采用的接法与默认值

| 链路 | 当前接口与外部接点 | 旧路线的处理 |
| --- | --- | --- |
| F103 → HUB | PA2 USART2 TX → Explorer P2.3 / PB11 USART3 RX | 原 F407 PD9 不再用于此链路 |
| HUB → F103 | Explorer P2.4 / PB10 USART3 TX → F103 PA3 USART2 RX | 原 F407 PD8 不再用于此链路 |
| HUB → Zynq | Explorer P4.3 / PA2 USART2 TX → Navigator P5.2 / MIO14 PS UART0 RX | 原 PC6 / UART1 EMIO 仅作历史备用 |
| Zynq → HUB | Navigator P5.1 / MIO15 PS UART0 TX → Explorer P4.4 / PA3 USART2 RX | 原 PC7 / UART1 EMIO 仅作历史备用 |

三块板共地、各自供电，不并接 3.3V 输出。F103、地面 F407、Zynq 默认 **115200 bps、8N1** 首测；通过后统一切到 **460800** 复测，不能只改一端。无线仍为原 433MHz/GFSK/约38.4kbps，协议字节格式、CRC、会话、序号、心跳、重试与截止期不变。最长 UART 帧在 115200 下约 4.17ms，保留 20ms UART TX 看门狗，实际调度余量待实测。

## 必须随接线执行的条件

- Navigator P5 拆 **1–3、2–4** 两只跳帽，避免 CH340C TX 与 F407 TX 同时驱动 MIO14。P5.1/P5.2 是外接位置，核心 X3.87/X3.89 只用于追溯网络。
- Explorer P2/P4 从 MCU 中排接线，拆 RS232/GPS/RS485 选择跳帽，只接 TTL 端。P1 音频跳帽断开，不初始化音频/DAC/I2S。
- 地面 F407 停用 ETH，保持 PD3/PHY reset 低、PC1/MDC 低。PA2 的 MDIO 负载和 PA3 经 R17 的音频 RC 支路并未凭软件消失；复位下 MDIO 高阻未实测证明，两档波形/误码仍是必测项。
- Zynq PS UART0 只用于协议，BSP stdin/stdout 停用，应用日志转有界 RAM 记录并通过 JTAG/调试器观察。启动与未来 FSBL 控制台同样不能向该 UART 混入文字。
- 保持 `DC_BOARD_READY=0` 和 `DC_ZYNQ_BOARD_READY=0` 作为交付默认。隔离副本的启用配置用于编译链接检查，不代表已经烧录。

## 修改范围与验证登记

| 范围 | 需要核对的实际改动 | 本轮验证状态 |
| --- | --- | --- |
| STM32 手动 HAL / IRQ | USART3 PB10/PB11 AF7；USART2 PA2/PA3 AF7；DMA1 Stream1/5 Channel4；PHY reset/MDC 隔离 | 8项完整链接通过，map确认新IRQ来自实际板级实现，无旧huart6 |
| 共享速率与测试 | 三台有线节点 115200 默认、460800 可选复测；业务期限与无线 profile 不变 | PC协议/无线/四节点/适配/内存日志全部通过；两档业务链接均通过 |
| CubeMX | 地面两份 `.ioc` 与实际初始化代码一致；HUB 改 USART2、PB10/PB11 与隔离 GPIO | CubeMX 6.18.1真实重新生成、2份Keil链接及生成C路由检查通过 |
| Zynq 软件 | UART0 物理 base/IRQ；RAM 日志；BSP stdin/stdout 停用 | SDK链接通过；PC日志截断/回卷通过；误启BSP串口stdin/stdout的编译拒绝检查通过 |
| 真实 PS 导出 / SDK | 当前 MIO 配置真实 HDF、BSP、ELF；不混用旧 EMIO 导出 | 新Vivado综合/HDF及SDK 2档速率×READY0/隔离READY1共4项链接通过 |
| 文档 | README、协议速率、接线、板级、运行预算、联调和工具链同步；历史证据加范围说明 | 已更新并核对源码与接线一致 |
| 交付包 | `drone_comm_review_20261005_mio_uart.zip`，保留旧包 | 候选包新目录解压重建、源码一致性与旧证据拒绝检查通过；最终包补齐此记录及报告 |
| 实板 | 接线、电平、跳帽、波形、UART、E07、四节点闭环 | 全部待测 |

本轮总回归见 [verification.json](../reports/verification.json)：88项真实ARM对象检查、Keil探针、3份通信静态库、8项STM32完整链接、2份CubeMX基底构建以及PC测试通过。C/Python仍覆盖3组黄金帧、132组随机帧和20组CRC破坏用例；新内存日志测试验证单次64字节限额、截断、连续覆盖和多次回卷。另有两个负向编译检查，确认误启串口stdin或stdout时被明确拒绝。

[真实SDK证据](../reports/zynq_sdk_validation.json)记录UART0基址0xE0000000、IRQ59、HDF中MIO14输入/MIO15输出且配置3.3V、UART1关闭。新输出分别在 `build/zynq_uart0_validation`、`build/zynq_uart0_sdk_115200` 和 `build/zynq_uart0_sdk_460800`。两档均检查隔离READY1应用的完整通信符号和真实启动调用；交付默认仍READY0。

告警保留：F103独立程序0错误0警告；F407独立程序各0错误4警告；CubeMX F407基底0错误3条厂商HAL警告。Vivado顶层3警告、0 critical/0错误；SDK厂商空stdio函数告警及退出时channel-close服务日志原样保留，应用确定性无UART的inbyte/outbyte覆盖了这些空函数。上述结果不表述为所有日志零告警。

旧 `zynq_7020_pincheck.json` 的 UART1/U5/T5 布局布线结果保留为历史备用，不适用于当前 PS MIO 线路；旧SDK证据另存于 `reports/history/uart1_emio`。打包校验要求当前UART0路线、两档速率及stdout隔离证据与源码哈希一致。

[交接复现记录](../reports/handoff_portability.json)针对 `build/drone_comm_mio_uart_candidate.zip` 在全新目录解压执行全部PC/平台/Keil/CubeMX基底构建，结果PASS；源码哈希与主工程一致，故意改动副本源码后打包器也正确拒绝旧验证记录。最终ZIP仅补入该复现报告和文档结果，受测源码不变，打包时逐项校验源码/证据哈希以及压缩包CRC和文件内容。该复现不重复运行Vivado/SDK/CubeMX生成，硬件导出与SDK链接由独立真实工具链报告证明；全部实板项目仍未验收。

三份 `platforms/stm32/standalone` 是已有完整通信程序；`platforms/stm32/cubemx` 是初始化基底，尚未接入通信主循环。两者区别继续保留，不把 CubeMX 生成成功描述为通信业务已经迁入该工程。

当前 UART0 MIO 通信无需 U5/T5 XDC 或专用 PL bitstream；运行仍要求真实 PS 初始化与对应 BSP/ELF。FSBL/BOOT.bin、SD/QSPI 启动包未作为本轮完成项。原 UART1 EMIO 资料和报告没有删除，可供将来改回备用方案时独立复验。

## 操作者仍需完成的实板项目

1. 断电按 [接线表](proposed_wiring.md) 接线；核对实物 pin1、电源、共地、所有选择跳帽和 E07 转接，不把不同板 3.3V 输出并接。
2. 记录 PHY reset/MDC 状态、PA2/PA3 和 MIO14/MIO15 电平，检查串口线上没有来自板载串口或日志的第二个发送源。
3. 核对板用固件的启动/时钟/PS 配置后启用 board-ready；先在三台有线节点统一 115200 测收发，再统一 460800 测波形、连续帧、DMA/IDLE 和错误恢复。
4. E07 身份、寄存器、SPI/GDO、固定包、模拟遥测、重试与恢复逐项实测；保持已有射频参数与期限。
5. 按 [联调表](bring_up.md) 完成四节点、断开 Zynq 无线继续运行、重启重连、旧包拒绝和长期稳定性测试，保存固件版本、抓包、波形和计数。

真实视频采集、TVP5150/DMA、YOLO 上板与 PL 视觉计算、IMU/PID/DShot/飞行控制没有因本次串口迁移而完成。所有硬件验收行继续保留“待测”。
