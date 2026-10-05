# 2026-10-05 补充资料后的推进记录

> 当前方案更新：现行方案采用 F407 USART3 PB10/PB11、USART2 PA2/PA3 ↔ Zynq PS UART0 MIO14/MIO15，外部接点为 P2/P4/P5，默认 115200、通过后统一复测 460800。UART0 文本日志停用，P5 的 1–3/2–4 跳帽必须拆除。本文下方保留旧 USART6/UART1 EMIO 路线及当时状态作为历史证据；其中旧接线、跳帽、日志和 bitstream 要求不作为现行施工步骤，旧测试 PASS 不自动适用于新路线。当前接线见 [接线表](proposed_wiring.md)，迁移结果见 [本轮记录](progress_uart_mio_migration.md)。

> 最新补充：用户已确认 Zynq 为 **7020**，官方型号为 `XC7Z020-2CLG400I`，BANK13 默认3.3V。本页保留上一轮完整审查包的验证时点；后续型号/官方资料及新增 UART1 约束进度以 [7020推进记录](progress_7020_confirmation.md) 为准，官方依据见 [官方资料核查](zynq_7020_vendor_lookup.md)。下方上板待办已移除重复索取 7010/7020 型号的要求。

本轮继续推进通信软件，承接[首轮记录](progress_20261005.md)。用户已确认：机载E07尚未接线；F103是8MHz晶振最小系统板；使用领航者V2资料盘；摄像头接FPGA；CubeMX已经安装。详见[补充事实](user_clarifications_20261005.md)。无需再核实上述已回答事项。

## 本轮完成内容

| 项目 | 当前结果 | 验证边界 |
| --- | --- | --- |
| 机载E07建议接线 | PB3=SCK、PB4=MISO、PB5=MOSI、PG7=CSN、PG6=GDO0；实现GPIO、SPI、EXTI和总线挂钩 | 默认BOARD_READY=0，尚未接线 |
| 共享引脚处理 | AIR初始化先保持Flash CS/PB14高，使用SWD；不用共享RS485的PG8 | 实物须拔掉冲突外设，不能用软件替代接线检查 |
| 地面F103 CubeMX工程 | 真实IOC、72MHz时钟、USART2 460800、循环DMA、SPI1、GDO中断，已由Keil完成链接 | 生成的初始化基底，通信主循环尚未接入 |
| 地面F407 CubeMX工程 | 真实IOC、168MHz时钟、USART3/6各自循环DMA、RNG、PG10 SRAM CE，已完成链接 | 生成的初始化基底，通信主循环尚未接入 |
| 三份STM32完整通信工程 | 独立main、SysTick、官方startup/system、严格scatter已接入手写HAL通信后端，完成实际业务链接与内存检查 | 交付配置READY=0；READY=1仅用于隔离副本的链接检查，不烧录 |
| Zynq底板路线 | 查阅完整领航者V2资料盘；UART1 RX=J4.3/U5、TX=J4.4/T5 | 本轮完成时型号尚待确认，用户随后已确认7020；底板丝印/电压进入上板检查 |
| Zynq硬件导出 | 在原BD副本上开启UART1 EMIO，独立工程通过验证和综合，导出真实HDF | 该轮沿用原工程7020配置，未生成最终XDC或bitstream；新增约束另见最新记录 |
| Zynq完整软件链接 | 由真实HDF创建SDK2017.4 standalone BSP，编译并链接通信应用ELF | 不含compile fixture；BOARD_READY=0，未运行实板 |

地面串口与原计划一致：F103 USART2 ↔ 地面F407 USART3；地面F407 USART6 ↔ Zynq UART1 EMIO。Zynq UART0仍为日志接口。原说明SPI2的PB14/PB15和地面USART3两处勘误继续有效。

可施工接线详见[建议接线表](proposed_wiring.md)，Zynq封装球位的完整追溯见[底板证据](zynq_navigator_v2_evidence.md)。U16是NRF风格针序，E07需要按信号名转接，不能直接插入。

## 验证证据与复现

- `reports/verification.json`：PC协议、四节点闭环、无线模拟、HAL绑定、环形队列、SPI轮询、C/Python黄金帧，以及本轮最终对象编译和通信库构建结果。
- `reports/platform_compilation.json`：87项真实ARM对象编译及Keil探针；这是快速适配检查，其中Zynq参数仍有明确标记的compile-only fixture。
- `reports/keil_project_build/results.json`：三份通信静态库的实际uVision构建，零错误、零警告；库工程没有启动文件，不生成烧录镜像。
- `reports/stm32_standalone.json`：三角色分别验证默认禁用和完整业务链接两种配置。F103完整业务Flash 21120字节、RAM 9264字节，RAM包含4096字节栈，满足64KiB/20KiB链接范围；仍未测量实板栈峰值。F103为零错误零警告；两份F407通信工程各为零错误四警告（3条厂商HAL参数警告、1条MDK汇编预处理target提示），日志完整保留。
- `reports/zynq_sdk_validation.json`：另一路真实HDF/BSP/ELF验证，包含源文件、输出哈希与实际生成参数。该路径不使用fixture。ELF尺寸为text 45696、data 2756、bss 24784字节。
- `platforms/stm32/cubemx/README.md`：实际生成的两份CubeMX初始化基底和重建步骤。F103链接为零错误零警告；F407为零错误三警告，来自官方HAL的unused Banks参数，未隐藏这些警告。
- `reports/handoff_portability.json`：新目录解压后的PC测试、ARM对象检查、三份Keil通信库、三份独立通信程序的两种配置和两份CubeMX基底重建。该项不声称再次生成Vivado/SDK/CubeMX工程。

Vivado顶层 `synth_1/runme.log` 保留三条警告，零错误、零critical warning；IP独立综合日志另有厂商警告，随证据保留。SDK真实BSP与应用采用相同的hard-float ABI；通信应用启用C99、Wall、Wextra、Werror，BSP沿用厂商构建选项。既有YOLO工程未修改；复现时从保存的原BD副本新建工程。

```powershell
# 在克隆后的仓库根目录运行
python -X utf8 tools/run_checks.py --platforms
```

真实Zynq生成脚本为 `tools/create_zynq_validation.tcl` 和 `tools/create_zynq_bsp.tcl`。默认输出目录已经存在，脚本会拒绝覆盖；重跑时分别指定新的硬件输出目录和SDK工作区。已有验证产物在 `build/zynq_validation` 和 `build/zynq_sdk_2017_4`，交接包只带可复现的源码和证据，不分发未确认板级条件的烧录镜像。

## 后续软件推进与上板操作待办

| 时机 | 需要核实或执行的事项 | 为什么需要 |
| --- | --- | --- |
| 软件与离线约束 | 按已确认7020、`xc7z020clg400-2`、U5 RX/T5 TX及官方BANK13默认3.3V推进独立引脚检查 | 7020已由用户确认；V3.6/V3.9资料的J4.3/J4.4路线一致，资料盘无法唯一判定实物底板修订号 |
| 连接Zynq UART前 | 操作者记录芯片/底板丝印，测量BANK13为3.3V，核对J4针1方向；UART0日志按P5的1–3、2–4跳帽核对 | 官方默认3.3V用于离线配置；实物改焊可能改变供电，上板测量不作为等待用户再次文字确认的事项 |
| 接E07前 | 记录两块E07完整型号/频段、焊盘信号名与天线；核对F103完整芯片丝印 | 当前软件按CC1101/26MHz/433MHz与F103C8T6配置，模块针序以实物为准 |
| 接线施工 | 按接线表连接电源、GND、SPI、CSN、GDO0及交叉UART，移除共享引脚上的冲突外设 | 机载尚未接线，模拟验证不能替代实际接线 |
| 首次烧录前 | 用 `platforms/stm32/standalone` 对应角色完整工程核对最终芯片/接线，仅在确认后的工程中启用BOARD_READY；只保留一套HAL/MSP/IRQ | 主循环与链接集成已完成，最终烧录配置仍需与实物对应；CubeMX目录继续保留独立初始化基底 |
| 实板验收 | 先E07身份/固定包，再双向UART/模拟遥测，最后Zynq；测试拔掉Zynq时无线继续工作 | 填写[联调表](bring_up.md)，只将实测项目标为通过 |

7020 型号已经确认，无需再回答型号或等待底板版本的文字确认。软件按官方默认供电与已核对路线继续推进；实物丝印、针序、电压和接线由上板操作者按表记录。协议参数、CubeMX安装和摄像头接哪一侧也不需要重复回答。

## 当前交付边界

通信协议、无线状态机、模拟测试及适配层已经建立；机载端返回模拟遥测，Zynq上报模拟视频状态，真实配置请求仍明确返回视频硬件未接入。没有把本轮结果计作TVP5150采集、YOLO上板、PL视觉加速或实际飞行控制完成。后续“PL承担主要视觉计算”的方向保留。

所有实板验收项仍为待测。原首轮压缩包保留，新包另命名为 `drone_comm_review_20261005_followup.zip`，附SHA-256校验文件。
