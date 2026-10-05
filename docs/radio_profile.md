# E07 / CC1101 无线配置与调用约定

本配置用于三核通信联调和机载模拟遥测。完成状态为 **PC 寄存器/FIFO 模拟已验证**；频偏、天线、灵敏度、射频功率与空口收发尚待实板测量。

来源：[E07-400M10S 官方产品规格](https://www.cdebyte.com/products/E07-400M10S/1)、[TI CC1101 数据手册](https://www.ti.com/lit/ds/symlink/cc1101.pdf)。频率计算使用 E07 内部 26 MHz 晶振，不能替换成 STM32 的晶振频率。

## 空口与寄存器

| 项目 | 配置 | 对应寄存器 |
| --- | --- | --- |
| 载波 | 432.999817 MHz（名义 433 MHz），信道 0 | FREQ2/1/0 = `10 A7 62`；CHANNR=`00` |
| 调制 / 数据率 | GFSK，38.383484 kbps（名义 38.4 kbps） | MDMCFG2=`13`；MDMCFG4/3=`CA 83` |
| 接收带宽 | 101.5625 kHz | MDMCFG4 高四位 `C` |
| 频偏 | 20.629883 kHz | DEVIATN=`35` |
| 前导 / 同步 | 4 字节前导；同步字 `D3 91 D3 91`，30/32 位匹配 | MDMCFG1=`22`；SYNC1/0=`D3 91`；MDMCFG2 低三位 `3` |
| 包长 | 可变长度，长度值包含 1 字节地址与消息体 | PKTCTRL0=`45`；PKTLEN=`2F`（47 字节） |
| CRC / 白化 | CC1101 硬件 CRC、白化；软件消息体另含 CCITT-FALSE | PKTCTRL0=`45` |
| 地址 / 接收状态 | 精确地址匹配，禁止广播；附加 RSSI 和 LQI/CRC_OK | PKTCTRL1=`05` |
| 本端地址 | 地面 `01`，机载 `02` | ADDR=`01` 或 `02` |
| GDO0 | 同步字到达时高，包结束时低 | IOCFG0=`06`；其他 GDO 未使用 |
| TX / RX 包结束 | 返回 IDLE，再由主循环进入 RX | MCSM1=`00` |
| 校准 | 初始化显式 SCAL；从 IDLE 进入 TX/RX 自动校准 | MCSM0=`18` |
| 测试输出功率 | 433 MHz 数据手册 +10 dBm 表值 | PATABLE 第 0 项 `C0` |

完整可读回配置在 `src/dc_radio.c` 的 `profile` 表中；芯片身份检查要求 PARTNUM=`00`，VERSION 既非 `00` 也非 `FF`，并验证写入的配置与 PATABLE。版本允许 CC1101 硅片版本差异。若 E07 具体型号不是 E07-400M10S，应先核对型号、频段与晶振，不能直接使用本表。

数据率公式：`(256 + 0x83) × 2^10 × 26 MHz / 2^28`。带宽公式：`26 MHz / [8 × (4 + 0) × 2^3]`。频偏公式：`26 MHz × (8 + 5) × 2^3 / 2^17`。

发送 FIFO：`长度(1) | RF目的地址(1) | 协议原生消息体(14..46)`。长度字段最大 47，写入 FIFO 最大 48 字节；CC1101 自行生成空口 CRC。接收 FIFO：`长度(1) | 地址(1) | 消息体(14..46) | RSSI(1) | LQI/CRC_OK(1)`，最大 **50 字节 < 64 字节**。空口不发送 UART 的 COBS 分隔符。协议的源/目的节点字段、session、seq 不由地面无线桥重新编号。

## 状态机和主循环

1. `dc_radio_init()` 复位、检查芯片、写入并读回配置，开始校准。返回 `true` 只说明配置成功，需继续 `dc_radio_tick()` 等待 `dc_radio_ready()`。
2. 所有 SPI 操作、FIFO 解析、收发和恢复在主循环进行。GDO 中断只调用 `dc_radio_gdo_event(radio, level)` 记录高低电平事件；不在 ISR 内做 SPI。GDO 高表示已收到同步字，即使长度字节尚未进 FIFO，也会阻止 TX 中止该次接收。
3. 主循环建议每 1 ms 以内调用一次 `tick()`。每次只处理一个 RX 包，没有无界等待循环；同步字或长度字节首次被观察后，20 ms 内仍不完整则清空恢复。MARCSTATE、TXBYTES、RXBYTES 每次最多读取 4 次，要求相邻两次一致；不稳定时留到下次 tick，独立超时不会随之延后。
4. TX 完成需同时满足 TX FIFO 已空及 MARCSTATE 为 IDLE/RX；GDO 事件提供提示，遗漏事件仍可通过状态轮询收尾。无事务的 HELLO、ACK、心跳、机载回复也有 20 ms TX 看门狗，完成后恢复接收。
5. RX 硬件 CRC 错误、软件 CRC 错误、越界包长、非本端地址、溢出及 TX 下溢均拒绝业务交付，并按 IDLE→FIFO 清空→RX 恢复。SPI 失败或校准/RX 就绪超时进入 FAULT，平台可通过有界退避重新初始化。`recoveries` 只统计故障后的成功恢复；正常收包、TX 完成、初始化和会话取消不计入。超时边界采用 `now>=deadline`，校准、RX 就绪、同步字无后续数据分别计数，不混入 SPI 的 `io_errors`。

底层适配必须独立保证每次 SPI/CSN/MISO-ready 等待有上限；初始化 `SRES` 需满足 TI 规定的 CSN 时序。普通寄存器、状态寄存器、burst 访问和命令 strobe 必须使用不同的 CC1101 地址位；特别是 PARTNUM/VERSION/MARCSTATE/TXBYTES/RXBYTES 状态寄存器读要带 burst/status-read 位。GDO 使用对齐 32 位事件字，低位存电平、其余位存事件代次，只有 ISR 写入；主循环以快照屏蔽恢复前的旧事件，在 SRX 前记录快照，后来的新高电平仍可观察，不用主循环清 ISR 标记。

接收回调会交付所有 CRC 与地址合格帧，包括重传的重复请求。去重、会话检查及机载缓存应答由节点层处理。回调时接收机可能正处于 RX_START，回复应先入有界队列，待主循环 ready 后调用 `send_untracked()`。回调参数只在调用期间有效；需要保存时复制整个 `dc_frame_t`。

CRC 和协议校验均合格时保存 `last_rx_ms`、`last_rssi_x2_dbm`、`last_lqi` 并置 `rssi_valid`；`last_rssi_x2_dbm=(int8_t)RSSI_raw-148`（以半 dBm 保存，相当于 signed RSSI/2−74 dBm），`last_lqi=LQI_raw & 0x7F`，去掉 CRC_OK 位。坏帧不刷新这些值，平台 RADIO_STATE 可报告最后有效空口帧的时间和信号诊断。新初始化后 `rssi_valid=false`；状态报告需用时间戳显示数据年龄。

## 地面事务

- `dc_radio_request()` 只接受 `TELEMETRY_QUERY` 或 `TEST_REQUEST`，一个在途事务。忙时返回 `false`；平台应跳过新的 100 ms 周期查询，不累计无限查询队列。
- 初次调用固定 `now+200 ms` 总截止期；20 ms 等 TX 完成，完成后 30 ms 等回复。失败后依次退避 5 ms、10 ms，最多两次重试（三次总发送）。所有重试保留完整原请求、seq、session 与总截止期。
- 如果平台先排队，应在入队时记录截止期，出队前丢弃过期项；成功调用后可把公开的 `transaction_deadline` 收紧到原排队截止期，不能后移。
- HUB 与 AIR 使用各自的会话号。地面桥从发往 AIR 的 HELLO_ACK 的 payload+8 读取 AIR 会话，调用 `dc_radio_set_reply_session()` 绑定；开始新握手时用 0 清除绑定和旧事务。绑定为 0 时不能开始事务，HELLO/ACK 仍可通过无事务发送完成握手。
- 匹配回复需 type 对应（QUERY→TELEMETRY，TEST_REQUEST→TEST_RESULT），src/dst 与请求交换，session 等于绑定的 AIR 会话，并且 payload 前两字节以小端引用原 request.seq。回复自己的 seq 仍由机载节点独立产生；不能误要求 AIR 回复使用 HUB 的会话号。
- 退避期间到达的同一事务回复仍可完成事务；重复回复继续交付节点层，但不会再次触发事务完成回调。错误结果、HELLO/ACK 等不依赖事务配对，使用 `send_untracked()`。
- `dc_radio_cancel()` 清理旧请求和在途发送，不产生完成回调；节点重启或会话切换时，平台还需清理其无线队列。
- 事务完成回调报告 COMPLETE、TIMEOUT 或 IO_ERROR；匹配回复同时正常触发 receive 回调。业务路由使用 receive 回调，事务回调用于计数与诊断。

## PC 验证与实板验收

`tests/test_radio.c` 使用真实寄存器地址、SPI FIFO 消耗与 MARCSTATE 模型，覆盖：配置与 PATABLE 读回、身份失败、校准/RX 就绪超时、ISR 无 SPI、50 字节最大包、硬件/应用 CRC、禁止广播、无效长度、FIFO 溢出、分段接收和不完整接收超时、单事务匹配、重复交付、丢失 GDO 仍完成 TX、5/10 ms 重传、原事务不变、200 ms 截止期、无事务 TX 超时、TX 下溢、取消、32 位时间回卷、同步字先于 FIFO 时禁止 TX、旧 GDO 代次恢复和新中断可见、真实故障计数、瞬变寄存器读拒绝、不稳定状态无法推迟超时、截止期相等边界、正负 RSSI 符号、LQI 去除 CRC_OK 位及坏包不刷新诊断。

编译示例（从工程目录，GCC C99）：

```text
gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude src/dc_protocol.c src/dc_radio.c tests/test_radio.c -o test_radio.exe
test_radio.exe
```

实板需核对供电、E07 型号、SPI 引脚/GDO 电平、26 MHz 晶振及天线；读取身份与配置，示波器观察 GDO 与 TX 完成时长。先以近距离固定 TEST_REQUEST 验证数据完整性与重传，再验证周期遥测、拔掉机载/接收故障恢复及 Zynq 断开时无线仍工作。实测结果、距离和错误计数写入联调记录，未测项目不得标为“实板已通过”。
