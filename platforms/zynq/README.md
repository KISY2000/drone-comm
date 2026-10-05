# Zynq7020 UART0 通信适配

当前路线为 PS UART0：MIO14 RX 从领航者 P5.2 接地面 F407 PA2 TX（探索者 P4.3）；MIO15 TX 从 P5.1 接 F407 PA3 RX（P4.4）。X3.89/87 仅用于核对核心板网络，不能当作手接接口。各板共地，不互接独立稳压器的 3.3 V 输出。

接线前必须拆下 P5 的 1–3、2–4 两只 USB 串口跳帽，尤其 2–4 保留会造成两个 TX 驱动同一 RX。初验采用 115200、8N1，确认波形和丢包后全部节点同时改为 460800；共享 `dc_link_config.h` 的 `DC_UART_BAUD` 控制两档编译配置。

`board_config.h` 按物理 UART0 基地址寻找 BSP 实例，不依赖 device ID 编号，并拒绝启用 `STDIN_BASEADDRESS` 或 `STDOUT_BASEADDRESS` 的 BSP。SDK 生成脚本设置 stdin=none、stdout=none；驱动不会输出文本。`dc_zynq_log.c` 仅写 512 字节环形 RAM，每次最多 64 字符，主循环单写者，调试器可查看 `dc_zynq_debug_log` 的 bytes/head/count/overwritten。应用的无 UART outbyte 丢弃其他 printf 输出，inbyte 固定返回 0；这样也覆盖了 SDK 2017.4 无设备 inbyte 缺少返回值的问题。厂商生成的两条空函数编译告警仍在报告中披露。未来的 FSBL/启动程序也必须禁用 UART0 文本日志；本轮没有生成 FSBL/BOOT.bin。

`tools/create_zynq_validation.tcl` 在独立目录复制已审计 BD，仅启用 UART0 MIO14/15、关闭 UART1；原始 YOLO 工程不变。该通信链路不使用 PL EMIO，所以不需要 UART 包脚 XDC；旧 `navigator_7020_uart1.xdc` 和 UART1 模板仅作历史备用。新综合/HDF 验证不声称等同旧 EMIO 布局布线、DRC 或实板信号验证。

复现命令（工程根目录运行，输出目录必须未存在）：

```powershell
& 'D:/Xilinx/Vivado/2017.4/bin/vivado.bat' -mode batch -source tools/create_zynq_validation.tcl
& 'D:/Xilinx/SDK/2017.4/bin/xsct.bat' tools/create_zynq_bsp.tcl build/zynq_uart0_validation/dc_uart_validation.hdf build/zynq_uart0_sdk_115200 115200
& 'D:/Xilinx/SDK/2017.4/bin/xsct.bat' tools/create_zynq_bsp.tcl build/zynq_uart0_validation/dc_uart_validation.hdf build/zynq_uart0_sdk_460800 460800
& 'D:/LocalOCR/.venv/Scripts/python.exe' tools/collect_zynq_validation.py
```

上板仍需要 PS 初始化（来自当前 HDF 的 JTAG 启动脚本或未来启动程序）。`DC_ZYNQ_BOARD_READY=0` 保持不变；BSP/ELF 链接通过不代表烧录或实板通信通过。板级检查、115200 实测、460800 波形/丢包和视频硬件仍未验收。无 `.bit`、FSBL 或 BOOT.bin。

每档波特率都额外生成 `dc_zynq_comm_link_only_ready1`，仅在隔离的验证应用编译参数中令 BOARD_READY=1，以核对完整启动路径、UART 初始化和协议业务符号，避免默认关闭业务导致空程序链接假通过。它不是交付烧录镜像，也未被执行；交付源码默认仍为 0。
