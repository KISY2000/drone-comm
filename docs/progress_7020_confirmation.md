# 7020 已确认后的接口落实

> 当前方案更新：现行方案采用 F407 USART3 PB10/PB11、USART2 PA2/PA3 ↔ Zynq PS UART0 MIO14/MIO15，外部接点为 P2/P4/P5，默认 115200、通过后统一复测 460800。UART0 文本日志停用，P5 的 1–3/2–4 跳帽必须拆除。本文下方保留旧 USART6/UART1 EMIO 路线及当时状态作为历史证据；其中旧接线、跳帽、日志和 bitstream 要求不作为现行施工步骤，旧测试 PASS 不自动适用于新路线。当前接线见 [接线表](proposed_wiring.md)，迁移结果见 [本轮记录](progress_uart_mio_migration.md)。

硬件负责人已确认采用 7020。封装、速度等级、默认电压和版本差异通过厂商资料核对；实物丝印与测量结果另行登记。

## 查明的官方参数

| 项目 | 当前采用值 | 依据 |
| --- | --- | --- |
| 器件 | XC7Z020-2CLG400I | FPGA开发指南V3.4，PDF第32–33页；规格书V1.2，第14页 |
| Vivado part | xc7z020clg400-2 | 与官方标准型号及现有工程一致 |
| UART1 I/O bank | BANK13，默认3.3V，LVCMOS33 | 规格书第13/21页；核心原理图2V5第7页 |
| 默认电阻装配 | R223/R221为0R接+3.3V；R220/R222为NC，预留VCCIO来源 | 核心原理图2V5第7页 |
| 核心V2.5改版 | 优化电源及启动顺序，连接器引脚与V2.4兼容 | 官方核心板改版说明 |
| 底板V3.8/V3.9改版 | V3.8更换音频芯片及Type-C电阻封装；V3.9调整连接器/电源丝印 | 官方底板改版说明 |

资料盘同时含多个底板版本，不能仅凭文件夹名称认定实物丝印版本。此前审查的旧版和V3.9原理图中，本轮J4.3/J4.4路线一致；这些版本差异不再阻塞当前UART接口的软件及离线实现。

来源、页码、文件哈希和图面复核见 [厂商资料核对](zynq_7020_vendor_lookup.md)。结构化选择保存在 `platforms/zynq/board_profile_7020.json`，其中分别记录用户确认、厂商默认值与实测状态。

## 当前接线分配

| 地面F407 | Zynq外部接点 | Zynq信号与封装脚 |
| --- | --- | --- |
| PC6 / USART6_TX | J4第3脚 | UART1_RX，U5 |
| PC7 / USART6_RX | J4第4脚 | UART1_TX，T5 |
| GND | J4第37或39脚 | GND |

两块板各自供电，只接交叉TX/RX和共地。J4第38脚是+3.3V、第40脚是+5V，不是本表的信号或地。UART0日志继续使用PS MIO14/15；P5短接1–3、2–4连接USB串口。

## 工程与离线检查

已新增实际约束 `platforms/zynq/navigator_7020_uart1.xdc`：RX=U5、TX=T5，两脚使用LVCMOS33。原来全注释的 `uart1_pins.xdc.template` 保留为早期备忘，新的7020配置以实际XDC及板级JSON为准。

`tools/check_zynq_7020_pins.tcl` 在独立目录复制先前综合检查点，并合入PS7和reset的IP检查点，随后载入XDC，执行优化、布局、布线以及I/O、DRC和时序报告。没有打开或改写原 独立 YOLO 工程 工程。检查证明U5/T5均为BANK13有效通用I/O，UART方向与约束一致，并完成布局布线，DRC违规数为0。

报告在 `reports/zynq_7020_pincheck.json` 及 `reports/zynq_7020_pincheck_evidence/`。该离线运行的时钟列表为空，时序报告WNS/TNS为NA；UART是异步外部链路，没有人为编造I/O延时或添加全局false-path来消除提示。因此该结果验证管脚、I/O标准与实现DRC，不代表460800bps外部线路或实板通信时序已经验收。

检查点加载日志还保留4条critical warning：读取顶层时有2个未合入的IP黑盒，读取两个IP检查点时工具建议使用XCI。合入后黑盒数为0，最终布线错误和DRC违规均为0；没有将这些过程告警表述为“全程零警告”。

重新执行时，指定新的输出目录，脚本会拒绝覆盖已存在目录：

```powershell
vivado -mode batch -nojournal -nolog `
  -source tools/check_zynq_7020_pins.tcl -tclargs build/zynq_7020_pincheck_new
```

检查依赖原独立综合输出 `build/zynq_validation`；解压包到其他位置后应先按 `tools/create_zynq_validation.tcl` 重建该目录。所有源码、约束及证据均随新的 `drone_comm_review_20261005_7020.zip` 审查包交付，既有两份审查ZIP保留。

本轮最终软件验证及新目录解压重建均通过：PC测试、87项ARM对象检查、三份通信库、三角色默认/完整业务两种链接配置、两份CubeMX基底均已复核。源码变化后拒绝旧验证记录的打包检查也通过。结果分别在 `reports/verification.json` 和 `reports/handoff_portability.json`；解压重建不重复执行Vivado，而以单独的7020实现报告及源码/证据哈希记录此次离线实现。

## 上板时实际要做的事

目前不需要再回答7010/7020、标准封装、速度级或默认电压。上板人员按 [联调步骤](bring_up.md) 核对排针方向、完成接线、检查BANK13实际供电和P5跳帽，再启用对应BOARD_READY配置。这些属于接线和测量操作，不能由资料阅读代替。

当前仍保持 `DC_BOARD_READY=0`、`DC_ZYNQ_BOARD_READY=0`，未生成本轮bitstream、未烧录或执行实板验收。视频和遥测仍为通信测试用模拟数据；已有YOLO工程保持原样。
