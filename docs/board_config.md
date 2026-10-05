# 板级配置与启动条件

本文件描述通信测试软件的固定配置及待确认硬件条件。原图证据见 [hardware_evidence.md](hardware_evidence.md)，建议接线见 [proposed_wiring.md](proposed_wiring.md)，Zynq 底板路线见 [zynq_navigator_v2_evidence.md](zynq_navigator_v2_evidence.md)，用户补充事实见 [user_clarifications_20261005.md](user_clarifications_20261005.md)。默认 `DC_BOARD_READY=0`、`DC_ZYNQ_BOARD_READY=0`；PC 验证、真实依赖编译和实板验收分别记录，完整固件启动需要闭合下列硬件条件。

用户已确认 Zynq 为 7020，官方型号为 `XC7Z020-2CLG400I`，软件目标沿用 `xc7z020clg400-2`。当前确认的通信路线采用 PS UART0 MIO14/MIO15、Navigator P5.2/P5.1；此前 BANK13 U5/T5 的 UART1 EMIO 约束只保留为历史备用。新方案不依赖该 PL bank 或通信 bitstream。三台有线节点默认 115200/8N1，首测通过后统一切换 460800 复测。实际丝印、排针方向、跳帽、共地与电平仍由上板操作者核对；当前迁移结果见 [本轮记录](progress_uart_mio_migration.md)。


共享编译配置为 `include/dc_link_config.h` 的 `DC_UART_BAUD`，默认 `115200u`；460800 复测时三台有线节点都以 `DC_UART_BAUD=460800` 重建。CubeMX 初始化基底中的波特率是生成值；若选择迁入该基底，还需在 IOC 中同步修改并重新生成，不能假定编译宏会覆盖 CubeMX 的字面值。

## 地面 F103：STM32F103C8T6，用户确认 HSE 8MHz

| 功能 | 配置 |
| --- | --- |
| 系统时钟 | HSE 8MHz，PLL ×9，SYSCLK/HCLK 72MHz，APB1 36MHz，APB2 72MHz |
| USART2 | PA2 TX → Explorer P2.3/PB11 RX；PA3 RX ← P2.4/PB10 TX；默认 115200、8N1、无流控；460800 另行复测 |
| RX DMA | DMA1 Channel6，环形 256 字节；HT/TC 和 UART IDLE 均启用 |
| E07 SPI1 | PA5 SCK、PA6 MISO、PA7 MOSI，mode 0、MSB、软件 NSS，72MHz/32=2.25MHz |
| E07 控制 | PA4 CSN，启动前置高；PB0 GDO0 双边沿 EXTI0；PB1 GDO2 输入下拉，暂不启 EXTI |
| 中断 | USART2、DMA1 Channel6、EXTI0 的抢占优先级均为 5，SysTick 为 15 |

GDO2 的 CC1101 输出配置为高阻，因此软件将 PB1 保留为输入下拉，避免浮空中断；所有收发完成事件由 GDO0 和 driver 的状态/FIFO 检查处理。SPI由HAL初始化，自有收发函数的全部字节及TXE/RXNE/BSY等待共用2ms期限和4096次迭代预算；MISO-ready等待另有毫秒和迭代上限。错误时清除OVR/MODF并恢复保存的主机配置。SRES使用官方high/low/high/ready/reset序列，三段脉冲共享有限迭代预算，仅在主循环初始化或恢复时执行。具体预算及SysTick/中断前提见 `runtime_budget.md`。

首轮不初始化 SPI2。原说明中的 SPI2 勘误为 PB14=MISO、PB15=MOSI。上板前核对 MCU 丝印、8MHz 晶振以及实际 E07 模块 CS/GDO 接线，确认最小系统板没有连接会驱动这些引脚的外设。

## 地面 F407：Explorer 原理图 STM32F407ZGT6，HSE 8MHz

| 功能 | 配置 |
| --- | --- |
| 系统时钟 | HSE 8MHz；PLL M=8、N=336、P=2、Q=7；SYSCLK 168MHz，APB1 42MHz，APB2 84MHz |
| F103 链路 | USART3，PB10 TX/P2.4、PB11 RX/P2.3，AF7；默认 115200、8N1 |
| Zynq 链路 | USART2，PA2 TX/P4.3、PA3 RX/P4.4，AF7；默认 115200、8N1 |
| USART3 RX DMA | DMA1 Stream1 Channel4，256 字节环形，高优先级，byte/byte、memory increment、direct mode |
| USART2 RX DMA | DMA1 Stream5 Channel4，256 字节环形，其他参数同上 |
| RNG | PLLQ 输出 48MHz，HAL RNG 产生非零 HUB 启动会话种子；失败不开始协议 |
| 引脚复用隔离 | P2/P4 从 MCU 侧接线并断开串口选择跳帽；拆 P1 音频跳帽；PD3/PHY reset 低、PC1/MDC 低；不初始化 ETH/音频/DCMI/FSMC；当前 UART 不占用 SRAM 的 PD8/PD9 |

F407 DMA 缓冲必须放普通 SRAM1/SRAM2，不能放 CCMRAM；链接 RAM 区使用 `0x20000000` 起始普通 SRAM。当前 USART3 从 P2.4/P2.3 引出，USART2 从 P4.3/P4.4 引出，实际针序按 pin1 核对并隔离 RS232/GPS/RS485。USART3 IRQ 对应 DMA1 Stream1，USART2 IRQ 对应 DMA1 Stream5；两路均 AF7。旧 PD8/PD9、PC6/PC7 不再作为当前 UART 业务引脚。

PA2 仍接 PHY MDIO，复位低和 MDC 低不能替代实测高阻/波形检查；PA3 仍可能经 R17 接音频 RC。禁止启动厂商综合例程中的 ETH、DAC/I2S/音频初始化。P1 跳帽断开后先以 115200 测固定帧，再统一切换 460800 测边沿和误码；不预先要求改焊 R17。

两路 UART/DMA 中断都采用相同抢占优先级 5，生产者不会嵌套。HT、TC、IDLE 共用当前 NDTR 采样，重复采样不重复投递，跨尾部按环形顺序拷贝到 1024 字节字节队列；中断仅投递字节，主循环做 COBS/CRC/节点解析。ARM DMB 与编译器屏障保护单核中断/主循环的队列发布。

**中断延迟约束：**默认 115200、8N1 时半缓冲 128 字节约 11.11ms，1024 字节软件队列约容纳 88.9ms 满速数据。复测 460800、8N1 时半缓冲 128 字节约 2.78ms，必须在每个半缓冲期间至少服务一次 RX IRQ；整圈 DMA 未被服务时，仅 NDTR 无法判断绕过多少圈。禁止超过 2.78ms 的全局中断屏蔽或高优先级 ISR 占用。主循环目标间隔 ≤1ms，每路每次最多解析 128 字节，1024 字节队列约可容纳 22.2ms 数据。压力验收应连续向两路各送满波特率帧，测量最坏 IRQ 延迟并确认无丢帧/重复；故意超过该约束不能记为已保证恢复的数据完整性。

UART 硬件错误或字节队列满后，主循环中止并重启该路 RX DMA、清除旧字节，解析器丢弃直到下一 `0x00` 边界。TX 有界排队，通过中断发送，20ms 看门狗中止卡住的发送；两条链路独立维护，Zynq 故障不阻断 F103 链路。

## 机载 F407 通信测试

保留 `DC_ROLE_AIR_F407`，不初始化电机、DShot、IMU、PID。用户确认机载 E07 尚未接线；已实现基于 Explorer V3.4 的建议 AIR HAL 初始化及 `dc_air_radio_bus()`、`dc_air_gdo0_pin()`、`dc_air_gdo0_port()`。READY 默认关闭，原因是实物接线和共享外设条件尚未核对。具体转接针序见 [proposed_wiring.md](proposed_wiring.md)。

| 功能 | 已实现的建议配置 |
| --- | --- |
| 时钟 | HSE 8MHz，PLL M=8/N=336/P=2/Q=7，SYSCLK 168MHz，APB2 84MHz |
| SPI1 | PB3 SCK、PB4 MISO、PB5 MOSI，AF5，mode 0、8 位、MSB、软件 NSS，84MHz/32=2.625MHz |
| E07 控制 | PG7 CSN，初始高；PG6 GDO0，输入下拉及 EXTI6 双边沿，EXTI9_5 IRQ；GDO2 本轮不接 |
| 共享隔离 | PB14 / Flash CS 保持高，不启动 Flash；只用 SWD，不用 JTAG/SWV；拔掉 NRF 模块，不使用共享 RS485 使能的 PG8 |

上电先取消 Flash/E07 片选，再初始化 SPI；GDO ISR 仅记录事件。不要默认 PA7，因为板载 PHY 的 CRS_DV 可能主动驱动此脚。NRF 风格无线接口不能按 E07 模块针序直接插接。用户摄像头连接 FPGA，F407 不初始化 DCMI；实板按建议表接好并核对后才设置 board-ready。

## E07 故障恢复与诊断

空口会话由 HUB 的新挑战和 ACK 仲裁；F103 保留 AIR 消息源、会话、序号。旧 AIR 发现帧只转发给 HUB，不直接清掉健康绑定。桥保存 8 个退休 HUB 会话和单调挑战，拒绝已见旧 HUB/旧挑战以及过期 ACK。HUB 重新绑定 F103 时会主动重新握手 AIR，以恢复桥内丢失的 AIR 会话映射。

E07 进入 FAULT 后，主循环至多每 500ms 尝试一次 `dc_radio_init()`，清旧 RF 队列，成功计入 `rf_reinit_count`，失败计入 `rf_reinit_failures`。即使首次初始化失败，F103 UART/节点主循环仍能继续运行和报告故障。校准期间暂存 ACK 绑定，待 radio 允许切换后应用，不中断 SCAL；重新初始化后的空口绑定由 HUB 的新 probe/ACK 建立。所有初始化、SPI、FIFO 和重置发生在主循环，ISR 只记 GDO 事件。

F103 的 `RADIO_STATE` 载荷为 32 字节：byte0=0（真实 driver 诊断），byte1=状态，byte2=LQI，byte3=RSSI 有效；byte4 为 little-endian signed int16 RSSI×2 dBm，byte6 为 good RX age（uint16，饱和 65535）；byte8/12/16/20/24/28 分别为 uint32 RX good、坏包总数、重试、TX 完成、事务超时、恢复总数。恢复总数包含 driver FIFO/状态恢复及成功的应用级重初始化；driver 计数在重新初始化后重置，应用级重初始化成功/失败计数继续累计。无效 RSSI 的 age=65535。该状态不会把模拟机载遥测标成真实传感器读数。

## Zynq SDK 2017.4 与 PS UART0 MIO

通信使用物理 PS UART0，BSP 必须包含 `XPAR_PS7_UART_0_BASEADDR` 及对应物理 IRQ；代码按物理基地址查找 `XUartPs_ConfigTable`，不假定生成的设备 ID 就是 0。PS 配置启用 UART0 MIO14/MIO15、关闭业务 UART1 EMIO，不导出 `COMM_UART1_rxd/txd` 作为当前通信端口。默认 115200/8N1，与 F103/F407 同时变更；首测通过后统一构建 460800 版本。

RX FIFO threshold=16、receive timeout=8，单次 IRQ 最多读 64 字节到队列，main 最多解析 128 字节。TX 每次最多写 64 字节，FIFO 满立即退出，20ms 未完成则重置 TX FIFO并计数；不等待 UART 排空。最长 UART 帧 48 字节在 115200/8N1 下约 4.17ms，在 460800 下约 1.04ms，实际调度和超时仍需上板测量。

使用 SDK 真实 standalone BSP、初始化好的 GIC 和 ARM exception 向量。`dc_zynq_firmware_start(gic)` 成功后启中断，主循环调用 `dc_zynq_firmware_poll()`。应用日志进入有界 RAM 记录，由 JTAG/调试器查看；BSP stdin/stdout 停用，不往协议 UART 调用 printf/xil_printf/outbyte。将来更换启动程序或 FSBL 时也要审查其控制台路由，不把串口文本混入协议。

RAM 日志公开符号为 `dc_zynq_debug_log`，512 字节环形缓存，单次写入最多 64 字节，`head` 为下一写入位置，`count` 为保留字节数，`overwritten` 为覆盖计数。它不保证以 NUL 结尾；调试器暂停后按 `(head + 512 - count) % 512` 从最旧字节顺序读取。这里只用于诊断，不执行串口输出、动态分配或等待 UART。

Navigator 的 PS UART0 接点为 P5.2/MIO14 RX、P5.1/MIO15 TX。**P5 的 1–3、2–4 两只跳帽必须拆除**；板载 USB 串口 TX 不得与 F407 TX 同时连接到 MIO14。两板共地、不并接 3.3V 输出，使用 TTL 信号接点。核心 X3.89/X3.87 只用于核对网名，实际接线不使用细间距 X3。

`tools/create_zynq_validation.tcl` 与 `tools/create_zynq_bsp.tcl` 用于独立硬件导出和真实 BSP/ELF 构建；不修改原 YOLO 工程，不包含快速对象检查用的 fixture。新方案构建和回归结果见 [本轮记录](progress_uart_mio_migration.md)，旧 UART1 EMIO HDF/BSP/ELF 不能直接标记为新路线通过。复验选择新的输出目录，保证 PS 初始化和应用使用同一份硬件导出。PS 时钟原图为 33.333333MHz，PL 时钟为 50MHz，不可互换。

MIO 串口本身不需要 U5/T5 XDC 或 PL bitstream；实际 JTAG/启动仍需要正确的 PS 初始化和匹配 BSP。当前没有烧录或实板通过记录，没有生成最终 FSBL/BOOT.bin/SD/QSPI 启动包。`navigator_7020_uart1.xdc`、旧 wrapper 与 `zynq_7020_pincheck.json` 保留为历史 EMIO 备用证据，不能加入当前 MIO 通信构建并声称验证了新接线。
