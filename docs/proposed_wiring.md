# 通信测试建议接线表

日期：2026-10-05。用户已明确：F103 是最小系统板；机载 E07 尚未接线；摄像头连接 FPGA，不连接 F407。本文给出可实施的建议接法，**不是实物接线确认记录**。按表完成接线、板型和电平核对前，`DC_BOARD_READY` 保持默认 `0`。本轮机载 F407 仅作 E07 通信测试，不初始化 IMU、PID、电调或实际飞控。

## 机载 F407 Explorer V3.4 → E07

依据 `EXPLORER_V3.4.pdf` 第 2–5 页的原生文字与原图复核，建议复用 U16 的 NRF 风格无线插座。该插座与 E07/CC1101 模块的管脚排列不同，**通过跳线或转接板按信号名连接，不能直接按 NRF 针序插入 E07**。E07 模块侧的焊盘编号以实际型号和丝印为准；本表只固定已核实的 Explorer 接点。

| E07 信号 | 机载 F407 信号 | U16 无线插座编号 | P9 备用引出编号 | 软件设置 |
| --- | --- | ---: | ---: | --- |
| GND | GND | 1 | 44 | 共地 |
| VCC | VCC3.3 | 2 | 不从 GPIO 取电 | 3.3 V 电源 |
| GDO0 | PG6，原 `NRF_CE` | 3 | 32 | 改为输入；EXTI6 双边沿，IRQ 为 EXTI9_5 |
| CSN | PG7，原 `NRF_CS` | 4 | 31 | 普通输出，上电先置高；软件片选 |
| SCK | PB3 / SPI1_SCK | 5 | 7 | AF5_SPI1 |
| MOSI / SI | PB5 / SPI1_MOSI | 6 | 5 | AF5_SPI1，F407 → E07 |
| MISO / SO | PB4 / SPI1_MISO | 7 | 6 | AF5_SPI1，E07 → F407；同时用于 SO-ready 检查 |
| GDO2 | 本阶段不接 | 不接 8 | 不接 PG8 | CC1101 IOCFG2 高阻，预留 |

表中的 U16、P9 编号都是原理图编号，接线前按实物 1 脚标记及连接方向核对；不要把二维符号排列当作实物正反面方向。E07 与板共地，模块使用 3.3 V 电源和逻辑电平，电源线尽量短，模块附近保留合适的去耦；两端使用相同频段的模块和天线。

PG6/PG7 的可用性依据：第 3 页 MCU 的 PG6（芯片 91 脚）网名为 `NRF_CE`、PG7（92 脚）为 `NRF_CS`，另被动引到 P9；第 5 页接到 U16 的 3、4 脚。在所给图中未见其它板载主动驱动器共用 PG6/PG7。芯片标注的 FSMC_INT2/INT3、PG7 USART6_CK 是可复用功能，不是另一个板载器件。需要拔掉原 NRF 模块，并保证这些接点没有同时连接其它外部设备。

接线和初始化必须处理以下共享关系：

- PB3/PB4/PB5 同时接板载 U8 `25Q128` Flash。先把 **PB14 / Flash CS 置高并持续保持高**，不启动 Flash 驱动；E07 和 Flash 不能同时被选中。该动作已进入建议 AIR HAL 初始化。
- PB3/PB4 与 JTAG 的 TDO/NJTRST 共享。调试器选择 **SWD，使用 PA13/PA14**，不用 JTAG 或 SWV/SWO；AIR 初始化将 PB3/4/5 设置为 AF5_SPI1，并关闭 TRACE_IOEN。
- **不使用 U16 第 8 脚 / PG8**。它经 R66 1 kΩ 接 NRF_IRQ，且第 3、4 页显示 PG8 同时控制 RS485 收发器 RE/DE；它不是独立的 GDO 输入。
- 用户的摄像头在 FPGA 一侧；F407 不初始化 DCMI、摄像头、LCD/FSMC、ETH、I2S、RS485 或 NRF 驱动，也不把这些外设留在板商综合例程的自动初始化中。若 F407 上仍插着独立 NRF/摄像头等外设，应先按本文冲突表移除。

已提供可编译的建议 AIR 手动 HAL 配置：8 MHz HSE、168 MHz 系统时钟、SPI1 APB2=84 MHz / 32，约 2.625 MHz，mode 0、8 位、MSB、软件 NSS、SPI CRC 和 TI 模式关闭；PG7 片选初始高，PG6 输入下拉及双边沿 EXTI6。`dc_air_radio_bus()`、`dc_air_gdo0_pin()`、`dc_air_gdo0_port()` 与 EXTI9_5 入口已实现。只选择手动 `board_peripherals.c` 或最终 CubeMX 生成的外设/MSP/IRQ 实现之一，避免重复定义；编译通过仍不等于已完成接线或实板无线验收。

## 地面 F103 最小系统板 → E07

F103 最小系统板是用户提供的已知事实。以下沿用既定通信接点，尚不证明杜邦线或焊接已完成。该角色按 STM32F103C8T6、8 MHz 主晶振生成软件，实物仍需核对板上标注。

| E07 信号 | F103 接点 | 当前用途 |
| --- | --- | --- |
| GND / VCC | GND / 3.3 V | 共地、电源 |
| SCK | PA5 / SPI1_SCK | AF 输出 |
| MISO / SO | PA6 / SPI1_MISO | 输入及 SO-ready |
| MOSI / SI | PA7 / SPI1_MOSI | AF 输出 |
| CSN | PA4 | 软件片选，初始高 |
| GDO0 | PB0 / EXTI0 | 双边沿中断，只记录事件 |
| GDO2 | PB1 预留 | 当前不接；输入下拉，无 EXTI1 |

F103 SPI2 本轮不使用；最初说明文件应勘误为 **PB14=MISO、PB15=MOSI**，不要根据旧标签反接。

## 地面 F407 的两路 UART：当前批准路线

两路默认 **115200 bps、8N1、无流控、3.3 V TTL**；三台有线节点首轮收发通过后一起切换到 **460800** 测波形、丢包及持续收发。帧格式仍是 COBS + 0x00。F103 ↔ 地面 F407 使用 USART3 PB10/PB11，地面 F407 ↔ Zynq 使用 USART2 PA2/PA3 与 PS UART0 MIO14/MIO15。

| 发送端 | 接收端 | 实际外部接点 |
| --- | --- | --- |
| F103 USART2 TX / PA2 | F407 USART3 RX / PB11 | Explorer P2.3 |
| F407 USART3 TX / PB10 | F103 USART2 RX / PA3 | Explorer P2.4 |
| F407 USART2 TX / PA2 | Zynq PS UART0 RX / MIO14 | Explorer P4.3 → Navigator P5.2 |
| Zynq PS UART0 TX / MIO15 | F407 USART2 RX / PA3 | Navigator P5.1 → Explorer P4.4 |
| 三块板 GND | 共地 | 选择实物确认的 GND；不并接各板 3.3 V 输出 |

P2/P4/P5 的编号以原理图为准，上板前按实物 pin1 和丝印核对方向。核心板 X3.89 对应 MIO14 RX、X3.87 对应 MIO15 TX，但 X3 是细间距板间连接器；它仅用于追溯网络，实际跳线接 Navigator **P5**。

以下隔离条件属于本接法的一部分，不能省略：

1. **Navigator P5：拆掉 1–3、2–4 两只跳帽。** 尤其 2–4 会把板载 CH340C 的 TX 接到 MIO14；保留它将使 USB 串口 TX 与 F407 TX 同时驱动 Zynq RX。接到 P5.1/P5.2 的 PS 侧，不接 CH340 或 RS232 电平端。
2. **Explorer P2/P4：从 MCU 中排接信号，拆掉通往 RS232/GPS/RS485 的选择跳帽。** 不把 MAX3232 后的 RS232 电平口当 TTL 口使用；所有外接 GPS/RS485/串口模块按共享关系断开。
3. **Explorer PA2 的 PHY MDIO：停用 ETH 初始化，PD3/PHY reset 保持低、PC1/MDC 保持低。** 这限制 PHY 工作和管理访问，但目前没有证明复位下 MDIO 在实物上一定高阻；115200 和 460800 下均检查 PA2 的电平、边沿及争用迹象。
4. **Explorer PA3 的音频支路：拆 P1 音频跳帽，停用音频/DAC/I2S 初始化。** PA3 仍可能通过 R17 连接 RC 支路。先测量波形与误码；没有实测依据时不先改焊电阻。
5. **Zynq UART0 专用于协议。** 应用日志转到有界 RAM 记录，由 JTAG/调试器观察；BSP stdin/stdout 停用。不得在这条线调用 printf/xil_printf/outbyte 输出文本。启动程序、诊断入口和未来 FSBL 若使用此 UART，也要关闭文本控制台或另设路由。

当前无需 U5/T5 的 UART1 EMIO 通信约束，也无需为这条 PS MIO 串口单独配置 PL bitstream；仍必须使用与 PS 配置匹配的硬件导出、BSP 和 PS 初始化。旧 USART6 PC6/PC7、J4.3/U5 与 J4.4/T5 方案及其 XDC/报告保留为历史备用，不与当前路线同时接线。摄像头仍接 FPGA，视频链路不经过 F407。

## 接线后启用与首轮验收

1. 断电完成 UART 与 E07 转接，记录 P2/P4/P5/P1 跳帽、实物 pin1、模块型号、3.3 V 逻辑和共地。各板电源输出不并接。机载未接线状态仅在实际完成后更新。
2. 使用一套板级初始化和最终 startup/SysTick/链接配置；核对 PHY reset、MDC、音频隔离及无共享外设自动初始化。核对 PS UART0 BSP 参数和 RAM 日志后，才在实际烧录工程中启用对应 board-ready。
3. 三台有线节点先以 115200 验证 UART 固定帧、DMA/IDLE、波形与错误恢复，再一起切到 460800 复测并记录。软件编译通过不作为两档实际波特率通过。
4. 读取 CC1101 身份和配置，观察 GDO0/TX/RX，完成固定测试和模拟遥测，再做四节点闭环。无线参数、20/30ms 超时、5/10ms 退避、两次重试和 200ms 总截止期不因 UART 迁移而改变。
5. 保存 CRC、重传、掉线恢复、RSSI/LQI、UART 溢出、重初始化及断开 Zynq 后无线继续运行的记录。只把实际测过的项目填“实板已通过”；视频采集、YOLO 和飞控保持单独验收。
