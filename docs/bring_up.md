# 队友实板联调步骤

本工程先交付可审查源码、PC 测试、HAL/SDK 适配及工程模板。静态库、对象编译和 PC 四节点仿真均不能代替上板验收。本文末尾的实板记录全部留为“待测”，完成测量后由操作者填写。

命令均从克隆后的仓库根目录执行。硬件证据见 [hardware_evidence.md](hardware_evidence.md)，建议接线见 [proposed_wiring.md](proposed_wiring.md)，Zynq 外接路线见 [zynq_navigator_v2_evidence.md](zynq_navigator_v2_evidence.md)，用户补充事实见 [user_clarifications_20261005.md](user_clarifications_20261005.md)，E07 参数见 [radio_profile.md](radio_profile.md)。各板角色沿用最初分工：地面 F407 管理会话与两路 UART，地面 F103 负责无线桥，Zynq 提供模拟视频状态，机载 F407 为通信测试端；摄像头连接 FPGA。

Zynq 为用户确认的 7020，目标 `xc7z020clg400-2`。本轮执行新路线：F407 USART3 PB10/PB11 接 F103，USART2 PA2/PA3 接 Zynq PS UART0 MIO14/MIO15，默认 115200 首测，460800 复测。旧 UART1 EMIO、U5/T5 XDC 与其布局布线记录仅作历史备用，不是本表的实物接线或验收结果。新路线的编译与回归证据见 [迁移记录](progress_uart_mio_migration.md)。

## 1. 先在 PC 上复现

在本机 PowerShell 执行：

```powershell
# 在克隆后的仓库根目录运行
python -X utf8 '.\tools\run_checks.py' --platforms
```

此命令只使用 Python 运行编译与测试，不进行文档识别。默认 PC 编译器是 Vivado 2017.4 自带 MinGW GCC；另一台电脑可以用 `--gcc <实际gcc.exe路径>` 指定自己的原生编译器。平台检查依赖 Keil ARMCLANG、SDK 2017.4 和工程 `deps` 中的 HAL/CMSIS；若只复现 PC 测试，可去掉 `--platforms`。

通过条件：进程退出码为 0，`reports/verification.json` 的 `status` 为 `PASS`，协议 C/Python 交叉验证、无线寄存器/FIFO 测试、四节点仿真、独立旧包回归和接收环形缓冲测试全部通过。检查报告仍应保留 `board_tested=false`。平台编译细节在 `build/platform_checks/results.json`，需要分别查看 F103、地面 F407、机载 F407 和 Zynq 的结果。

遇到失败先保存完整输出、实际编译器版本及源码版本。不要通过删除失败断言来完成交接。实板用程序只处理固定测试包和模拟遥测；没有电机输出，控制请求返回不支持。

## 2. 选择完整通信工程并核对烧录条件

uVision 打开下列模板之一：

| 角色 | 模板 | 角色编译宏 |
| --- | --- | --- |
| 地面 F103 | `platforms/stm32/standalone/ground_f103.uvprojx` | `USE_HAL_DRIVER,STM32F103xB,DC_STM32_F1,DC_ROLE_GROUND_F103` |
| 地面 F407 | `platforms/stm32/standalone/ground_f407.uvprojx` | `USE_HAL_DRIVER,STM32F407xx,DC_ROLE_GROUND_F407` |
| 机载通信测试 F407 | `platforms/stm32/standalone/air_f407.uvprojx` | `USE_HAL_DRIVER,STM32F407xx,DC_ROLE_AIR_F407` |

这些独立工程已有startup、system、main、SysTick和链接布局，默认 `DC_BOARD_READY=0`，输出用于软件验证的AXF而不生成HEX。每个目标只选一个角色宏。F103芯片沿用原方案C8T6，8MHz晶振已由用户确认；首次上板再核对芯片丝印，不要将64KB Flash配置改成未经核实的更大容量。早期 `platforms/stm32/templates/*.uvprojx` 仍是仅输出 `.lib` 的静态库工程，与这里的完整工程分别保留。

现有独立工程已经匹配官方startup、system和实际容量的scatter，并预留4096字节栈、0字节堆。所有源文件和HAL必须与同一芯片族、角色宏和include路径匹配；已有 `SysTick_Handler()` 调用一次 `HAL_IncTick()`，不能再增加第二套handler。检查 `reports/stm32_standalone.json` 的默认禁用/完整业务两种链接记录；build里的READY=1副本只用于链接检查，不作为烧录入口。

外设初始化采用以下两种集成方式之一：

- **使用现有通信专用初始化**：保留 `platforms/stm32/common/board_peripherals.c`，在 `main()` 先调用 `dc_board_peripherals_init()`，成功后调用 `dc_firmware_start()`，主循环调用 `dc_firmware_poll()`。该文件已有地面 F103/F407 的外设初始化，以及机载建议 SPI1、CS/GDO、Flash 隔离、时钟、MSP 与 IRQ；不要同时链接 CubeMX 生成的同名定义。机载 E07 尚未接线，按建议表核对后才启用 READY。
- **使用 CubeMX 生成工程**：按模板参数及实际生成记录使用真正的 `.ioc` 和 MDK-ARM 工程，排除 `board_peripherals.c`。完成 HAL、系统时钟、GPIO、DMA、UART 和该角色的 SPI/RNG 初始化，再调用 `dc_firmware_start()`；主循环调用 `dc_firmware_poll()`。`*.ioc.template` 是参数起点，不能直接改名后当作最终板级配置。

两种方式都加入 `platforms/stm32/common/main_hooks.c`，每个 HAL callback 只保留一个实现。原工程已有 `HAL_UARTEx_RxEventCallback`、`HAL_UART_TxCpltCallback`、`HAL_UART_ErrorCallback` 或 `HAL_GPIO_EXTI_Callback` 时，应合并逻辑。IRQ 交给对应 HAL handler，不能另加一套重复 IRQ；RX DMA 保持 circular、HT/TC 中断开启并使用 IDLE 事件。UART/DMA IRQ 使用相同配置优先级，避免同一接收端的 producer 嵌套；回调只搬数据或记录 GDO 事件，业务在主循环运行。

`dc_firmware_start()` 失败时保存故障并停止业务收发，不把失败忽略后继续跑。正常主循环应在约 1ms 内完成一轮。`DC_BOARD_READY` 默认是 0，核对下面的接线与共享功能、确认板级配置与实物一致后才在实际烧录工程中设为 1。HUB 启动会话种子使用 F407 硬件 RNG；RNG 失败或返回零时拒绝启动。

用户已安装官方 CubeMX 并授权调用；实际版本、批处理生成和工程验证结果见环境/进度记录。手动 HAL 初始化与 CubeMX 生成工程的集成分别核对，代码生成或静态库编译通过均不等于实板启动验收。

## 3. 接线与共享外设检查

三台有线节点全部使用 **115200 bps、8N1、无流控** 进行第一轮验收，成功后统一切换 **460800** 重测。使用 COBS + 0x00 分帧，不在业务串口夹入日志。全部接点按原理图编号并按实物 pin1 核对，断电交叉接线：

| 发送端 | 接收端 | UART |
| --- | --- | --- |
| F103 PA2 TX | Explorer P2.3 / PB11 RX | F103 USART2 → F407 USART3 |
| Explorer P2.4 / PB10 TX | F103 PA3 RX | F407 USART3 → F103 USART2 |
| Explorer P4.3 / PA2 TX | Navigator P5.2 / MIO14 RX | F407 USART2 → Zynq PS UART0 |
| Navigator P5.1 / MIO15 TX | Explorer P4.4 / PA3 RX | Zynq PS UART0 → F407 USART2 |
| 三块板 GND | 共地 | 各自供电，不并接 3.3V 输出 |

接线前逐项记录：

- **Navigator P5 拆 1–3、2–4 跳帽**。2–4 不得保留，否则 CH340C TX 和 F407 TX 会同时驱动 Zynq RX。接 PS 侧 P5.1/P5.2；核心板 X3 是细间距连接器，仅作网名追溯。
- **Explorer P2/P4 从 MCU 中排接线，拆通往 RS232/GPS/RS485 的选择跳帽**；仅接 3.3V TTL，不能接 MAX3232 后的 RS232 电平端。接线表的 P2.3/4、P4.3/4 是图面编号，方向以实物 pin1 为准。
- **Explorer 拆 P1 音频跳帽**，禁用 ETH、音频、DAC/I2S、DCMI/FSMC 的自动初始化。PA2 仍接 PHY MDIO；软件保持 PD3/PHY reset=低、PC1/MDC=低。PA3 仍可能经 R17 接音频 RC。未证明复位下 MDIO 一定高阻，必须测波形和误码；不要把“禁用软件外设”直接当“所有外部负载消失”。
- Zynq UART0 仅传协议。BSP stdin/stdout 停用，应用日志写有界 RAM 记录，通过 JTAG/调试器读取；所有诊断入口、启动程序和未来 FSBL 均需检查，禁止向 UART0 输出 printf/xil_printf/outbyte 文本。
- F103 SPI1：PA5 SCK、PA6 MISO、PA7 MOSI、PA4 CSN、PB0 GDO0；PB1/GDO2 预留不接。E07 使用 26MHz 晶振，不同于 F103 已确认的 8MHz HSE。
- 机载 E07 按最后信息尚未接线。建议 AIR SPI1 PB3/PB4/PB5、CSN PG7、GDO0 PG6/EXTI6，GDO2 不接。保持 PB14/Flash CS 高，使用 SWD，关闭 JTAG/SWV，拔掉 NRF 模块；NRF 风格 U16 必须按信号转接，不能直接插 E07，也不使用共享 RS485 使能的 PG8。

当前 MIO UART 不依赖 PL BANK13 或 U5/T5 约束；核对 MIO 的 3.3V 逻辑与实物供电。旧 PD8/PD9、PC6/PC7 及 J4 EMIO 方案不与本方案同时连接；摄像头仍在 FPGA 一侧。

## 4. 按链路逐级验证

每一级保存测量和日志，再进入下一级。以下均为待执行步骤。

1. **115200 有线单链路**：验证 F103 USART2、F407 USART3/USART2、Zynq PS UART0 的实际 TX/RX、交叉方向和电平。使用支持该波特率的 3.3V USB-UART、示波器或逻辑分析仪，保存 PA2/PA3 波形。用真实 C 编码固定 `dc_frame_t`，PC reference/codec 解码；测试短包、连续包及超过 DMA 半缓冲长度的输入，验证 HT/TC/IDLE、错误恢复和 UART0 无日志文本。E07 未连接时 F103 主循环仍会报告无线故障并每 500ms 有限重试，不能把此状态记作 E07 通过。
2. **460800 复测**：第一档通过后统一修改并重建 F103、地面 F407 和 Zynq 的波特率配置，记录三份应用的版本与实际配置；不能只改链路一端。重复固定帧、持续满速双路收发、DMA/IDLE、错误恢复和波形检查，尤其关注 MDIO 与音频 RC 负载。只有实测通过后才把 460800 作为上板工作速率；协议和无线期限不改。 共享宏是 `include/dc_link_config.h` 中的 `DC_UART_BAUD`，默认 `115200u`，复测时三台均重建为 `DC_UART_BAUD=460800`；CubeMX 基底需另同步 IOC 后重新生成，其字面值不会被该宏自动覆盖。
3. **E07 身份与配置**：接电源、天线和 SPI/GDO，读取 PARTNUM/VERSION，预期 PARTNUM=00、VERSION 既非 00 也非 FF，整组恒 00/FF 不能当读通。核对 `dc_radio_init()`、寄存器/PATABLE 读回，持续 `dc_radio_tick()` 至 `dc_radio_ready()`，保存 CSN/SPI/GDO0 波形。初始化返回成功不等于射频就绪。
4. **E07 双端固定测试**：完成 AIR 会话绑定后发送固定 TEST_REQUEST，核对 TEST_RESULT 载荷和请求引用；仅在 ready 且无在途事务时发送，忙时跳过新查询。验证 TX 20ms、TX 完成后回复 30ms、退避 5/10ms、最多两次重试和原始入队起固定 200ms 截止期。重复请求不重复执行业务，回复保留原事务引用与结果。
5. **Zynq 启动与软件匹配**：以新 UART0 MIO14/15 配置重新导出硬件，使用对应真实 BSP、PS 初始化和 ELF。`tools/create_zynq_validation.tcl`、`tools/create_zynq_bsp.tcl` 的结果见迁移记录；旧 EMIO 的 HDF/BSP/ELF 不能混用或冒充新验证。初始化 GIC 后启动通信并启中断，主循环调用 `dc_zynq_firmware_poll()`；检查 RAM 日志、UART0 base/IRQ 和无 UART1 业务依赖。MIO 通信本身不要求 PL bitstream，但 JTAG 或启动流程仍要正确初始化 PS。确认实物与配套产物后才设 `DC_ZYNQ_BOARD_READY=1`，FSBL/BOOT.bin 不在当前实板已验证范围。
6. **四节点闭环**：启动 HUB、F103、Zynq 和 AIR，确认源节点会话独立、F103 不改 AIR 源/session/seq；模拟遥测、固定测试回复和模拟视频状态到达 HUB。视频配置明确返回“视频硬件尚未接入”，保留模拟和未锁定标志。本步骤不算摄像头、YOLO 或飞控验收。
7. **断线、重启及旧包**：先断 Zynq，确认超时、视频状态失效且无线仍继续；再断无线、F103 UART 或重启节点，确认清队列、重握手。注入坏 CRC、超长帧、重复/旧序号、旧会话和旧 HELLO/ACK，旧包不更新业务状态，随后有效帧恢复。只有心跳时，旧遥测 1500ms 后失效，不能由旧 ACK 复活绑定。

Zynq 的 RAM 日志符号为 `dc_zynq_debug_log`：512 字节环形缓存，单次最多写 64 字节。`head` 是下一槽，`count` 为保留字节数，`overwritten` 为覆盖计数；暂停后从 `(head + 512 - count) % 512` 起读 `count` 字节，不按 C 字符串寻找 NUL。

通过调试器观察 `dc_node_t`、`dc_radio_stats_t` 与有界 RAM 日志，必要时使用独立仪器抓 UART；不为了读取日志重新插回 P5 跳帽。保存电压、跳帽状态、实际波特率、芯片身份、距离、波形、抓包、固件版本和重启前后会话，异常写清触发条件。

## 5. 空白实板结果表

程序/源码版本：________　操作者：________　日期：________　板版本/芯片丝印：________

| 验收项目 | 预期 | 实测值/证据文件 | 结果 |
| --- | --- | --- | --- |
| 供电、共地、TTL 电平、P2/P4/P5 方向 | 各自供电，不并输出；3.3V 逻辑和针序正确 | ________ | 待测 |
| P5 1–3/2–4、P2/P4 选择、P1 音频跳帽 | 相关跳帽已拆，主动驱动器隔离 | ________ | 待测 |
| PHY reset/MDC、PA2/PA3 波形 | PD3/PC1 保持低；两档 UART 无异常负载/争用 | ________ | 待测 |
| Zynq BSP/应用日志 | stdin/stdout 停用；日志只在 RAM，UART0 仅协议 | ________ | 待测 |
| F103 USART2 ↔ F407 USART3，115200 | 固定帧双向正确解码 | ________ | 待测 |
| F407 USART2 ↔ Zynq UART0 MIO，115200 | P4.3/4 ↔ P5.2/1 正确，持续收发成功 | ________ | 待测 |
| 三台节点统一 460800 复测 | 波形、双向帧、长时间收发及丢包满足联调要求 | ________ | 待测 |
| RX DMA/IDLE、连续包和坏帧 | 分片正确；溢出/故障后恢复有效帧 | ________ | 待测 |
| E07 双端身份与配置 | 身份有效、读回一致、进入 RX ready | ________ | 待测 |
| 固定 TEST_REQUEST/TEST_RESULT | 载荷/引用正确，重复业务仅执行一次 | ________ | 待测 |
| 20ms/30ms、5/10ms 与总截止期 | 最多两次重试，原始总截止期不后移 | ________ | 待测 |
| 模拟遥测和视频状态 | 两路到达，模拟标志保留 | ________ | 待测 |
| Zynq 断线 | 视频失效，无线仍运行 | ________ | 待测 |
| 重启、旧握手/会话注入 | 重新握手，旧包不恢复状态 | ________ | 待测 |
| 仅心跳时遥测过期 | 心跳不延长旧遥测新鲜度 | ________ | 待测 |

每项实际通过后才填“实板已通过”并保留证据；未执行继续“待测”。PC 测试和工具链构建分别记录为“PC 已验证”“已编译”。
