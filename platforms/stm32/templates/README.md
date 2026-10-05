# 固件工程模板

`*.ioc.template` 是按照已知引脚和时钟写出的配置起点，不应直接改名后视为最终板级配置。用户已安装官方 CubeMX 并授权使用；实际生成验证结果另记于 `docs/toolchain.md` 和进度记录。新建对应 MCU 项目，按模板参数及 [板级配置](../../../docs/board_config.md) 核对，保存真正 `.ioc` 并生成 MDK-ARM 工程。用户补充事实见 [user_clarifications_20261005.md](../../../docs/user_clarifications_20261005.md)。

同目录 `*.uvprojx` 为 uVision 静态库工程：编译共享协议、节点、E07、适配代码和官方 HAL，输出 `.lib`，没有 startup、链接脚本、main 入口或烧录文件。与源码的对象编译检查使用相同依赖路径。可用 Keil 打开查看和编译；若选择 CubeMX 集成方式，使用其生成的启动文件、链接布局和 main，再添加这些源文件。

现在已有独立的 [完整通信工程](../standalone/README.md)，可直接审查启动、主循环和链接布局，无需从库工程重新手工集成；默认READY=0。实际CubeMX生成的初始化基底另见 [CubeMX工程](../cubemx/README.md)。三类工程不要混入同一套MSP/IRQ。

外设初始化有两个选择：使用 `common/board_peripherals.c` 的通信专用 HAL 初始化，或使用按模板生成的 CubeMX 初始化。两者不能同时链接 MSP/IRQ 定义。CubeMX 方式在 main 完成 HAL、时钟、GPIO、DMA、UART、SPI/RNG 初始化后调用 `dc_firmware_start()`；独立方式先调用 `dc_board_peripherals_init()`，成功后调用 `dc_firmware_start()`。循环调用 `dc_firmware_poll()`，目标周期不超过 1 ms；失败时停留故障状态，不开始通信。

把 `common/main_hooks.c` 加入工程，保留其中 UART/EXTI callbacks；IRQ handler 必须转给 HAL。若原工程有同名 callback/IRQ/SysTick，合并实现，不能复制重复符号。保持 RX DMA HT/TC 中断开启，ReceiveToIdle 的 callback 不使用过时的 Size，而统一读取当前 NDTR。SysTick 使用 HAL 时基时需执行 `HAL_IncTick()`。

地面 F103 宏：`USE_HAL_DRIVER,STM32F103xB,DC_STM32_F1,DC_ROLE_GROUND_F103`。地面 F407 宏：`USE_HAL_DRIVER,STM32F407xx,DC_ROLE_GROUND_F407`。机载测试 F407 宏：`USE_HAL_DRIVER,STM32F407xx,DC_ROLE_AIR_F407`。确认板级条件后才设置 `DC_BOARD_READY=1`。机载建议初始化及 `dc_air_radio_bus()`、`dc_air_gdo0_pin()`、`dc_air_gdo0_port()` 已实现：SPI1 PB3/PB4/PB5、CSN PG7、GDO0 PG6；READY 保持 0，直到按照 [proposed_wiring.md](../../../docs/proposed_wiring.md) 接好 E07 并核对共享外设。Zynq 外接路线另见 [zynq_navigator_v2_evidence.md](../../../docs/zynq_navigator_v2_evidence.md)，不由 STM32 模板替代。

当前地面 F407 使用 USART3 PB10/PB11 与 USART2 PA2/PA3（均 AF7）；RX DMA 分别为 DMA1 Stream1/Stream5 Channel4。所有地面 UART 初始 115200，实测后统一改 460800。完整通信工程通过共享 `dc_link_config.h` 配置速度；CubeMX 基底还须同步更新两块板的 IOC。地面新路线要求先保持 PD3 PHY 复位低和 PC1 MDC 低，不初始化 ETH/音频，并按接线文档移除 P2/P4 选择跳帽与音频跳帽。
