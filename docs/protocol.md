# 通信协议 v1

本协议用于地面三核和机载通信测试端。真实 CONTROL_CMD 与 OPERATOR_CMD_REQUEST 均返回 UNSUPPORTED；模拟遥测、模拟视频状态带显式标志。硬件模块之间不跨模块修改状态变量。数据、接收统计和会话状态均在主循环中处理。

## 节点与封装

节点：`1=地面F407/HUB`、`2=地面F103`、`3=Zynq`、`4=机载F407`。无广播地址。共享 C 结构体仅用于内存，不直接发送内存布局。

| 消息体偏移 | 字段 | 长度 | 定义 |
| ---: | --- | ---: | --- |
| 0 | Version | 1 | 固定 1 |
| 1 | Type | 1 | 下表消息编号 |
| 2 | Source | 1 | 源节点 1–4 |
| 3 | Destination | 1 | 目的节点 1–4 |
| 4 | Session | 4 | 源节点的会话号，小端 |
| 8 | Sequence | 2 | 按源→目的递增的传输序号，小端 |
| 10 | Length | 2 | 载荷字节数，0–32，小端 |
| 12 | Payload | 0–32 | 类型对应数据 |
| 12+Length | CRC16 | 2 | 覆盖从 Version 到 Payload，小端 |

CRC-16/CCITT-FALSE：poly=`1021`，init=`FFFF`，refin/refout=false，xorout=0。标准检查：ASCII `123456789` → `29B1`。消息体总长 14–46 字节。

UART：`COBS(完整消息体) + 00`，最长 48 字节。当前接线首测默认 **115200/8N1**；地面 F103、地面 F407 和 Zynq 三台有线节点通过后统一切换 **460800/8N1** 复测，不支持链路中自动切速。波特率只改变串口物理传输速率，消息字节格式、无线速率和全部业务期限不变。IDLE、DMA HT/TC 只划分字节片段，不是业务帧边界。解析器超过长度上限后丢到下一个 `00`，CRC 错误不交付。噪声后首帧可能用于恢复定界，后续完整帧必须恢复。


共享编译配置为 `include/dc_link_config.h` 的 `DC_UART_BAUD`，默认 `115200u`；460800 复测时三台有线节点都以 `DC_UART_BAUD=460800` 重建。CubeMX 初始化基底中的波特率是生成值；若选择迁入该基底，还需在 IOC 中同步修改并重新生成，不能假定编译宏会覆盖 CubeMX 的字面值。

空口：CC1101 可变长度包为 `length | RF目的地址 | 消息体 | 硬件CRC`。RF 地址地面=1、机载=2，不是应用节点编号。RX 另追加 RSSI/LQI；COBS 不进入空口。长度与 FIFO 计算详见 radio_profile.md。

## 消息编号和载荷

| 编号 | 名称 | 方向 / 载荷 |
| --- | --- | --- |
| 01 | HELLO | 未绑定节点→HUB：boot_token:u32，session=0；HUB→节点：challenge:u32，header session=HUB会话；节点→HUB：boot_token:u32 + challenge:u32，session=0。 |
| 02 | HELLO_ACK | HUB→节点：boot_token:u32 + challenge:u32 + assigned_source_session:u32，共12B。 |
| 03 | HEARTBEAT | UART节点及HUB周期心跳，空载荷。AIR正常运行时由查询回复证明存活，避免额外主动RF心跳抢占半双工链路。 |
| 04 | RADIO_STATE | 地面F103→HUB，硬件适配提供下表32B诊断；PC纯节点模拟提供4B测试标志。 |
| 10 | TELEMETRY_QUERY | HUB→AIR，空载荷；应用只允许一个在途请求。 |
| 11 | TELEMETRY | AIR→HUB：request_seq:u16，flags:u8，reserved:u8，uptime_ms:u32，battery_mV:u16，roll_cdeg:i16，pitch_cdeg:i16，height_mm:u16，共16B。flags bit0=模拟。当前电池/姿态/高度均是测试常量。 |
| 12 | TEST_REQUEST | HUB→AIR，0–30B固定测试数据；上限留出回复的2B请求引用。 |
| 13 | TEST_RESULT | AIR→HUB：request_seq:u16 + 原测试数据。测试业务仅执行一次；重试可再次发送相同结果，回复传输seq使用新值。 |
| 20 | VIDEO_STATUS | Zynq→HUB：flags:u8，lock:u8，format:u8，reserved:u8，simulated_frame_count:u32，sync_errors:u32，共12B。flags bit0=模拟；当前lock=0/format=0，未接真实TVP5150。 |
| 21 | FPGA_STATUS | 保留状态消息类型；当前未产生温度或真实采集状态。 |
| 22 | VIDEO_CONFIG | HUB→Zynq，当前回复 ERROR/VIDEO_NOT_READY。 |
| 23 | SYSTEM_STATUS | HUB→Zynq：health_flags:u8，telemetry_simulated:u8，reserved:u16，telemetry_age_ms:u32，telemetry_count:u32，video_status_count:u32，共16B。health_flags bit0=AIR健康、bit1=遥测新鲜、bit2=F103健康、bit3=视频状态新鲜。无新鲜遥测时age=FFFFFFFF。 |
| 30 / 31 | CONTROL_CMD / OPERATOR_CMD_REQUEST | 测试端拒绝，绝不输出电机或转成飞控动作。 |
| 7F | ERROR | request_seq:u16 + request_type:u8 + error:u8；1=UNSUPPORTED、2=VIDEO_NOT_READY、3=BUSY保留。 |

RADIO_STATE（真实适配32B）：0 flags（bit0=模拟；真实诊断为0）、1驱动状态、2 LQI、3 RSSI有效标志、4 RSSI×2 dBm:i16、6最后有效包年龄ms:u16（饱和65535）、8有效RX:u32、12检测到的RX错误总数:u32、16重试:u32、20完成TX:u32、24事务超时:u32、28恢复:u32。CRC错误数只能代表本地观测，不宣称测得所有空口丢包。RSSI偏移采用TI 433MHz参考74dB，绝对校准仍待实板。

HUB保留 `latest_telemetry/latest_video_status/latest_radio_state`；Zynq保留 `latest_system_status`。`video_valid` 表示视频状态消息新鲜，**不表示视频已锁定**，实际锁定必须读 VIDEO_STATUS 的 lock。

## 会话与顺序

HUB上电从F407硬件RNG取得非零新会话，RNG失败时不进入通信就绪。PC测试使用明确的固定种子。HUB挑战在本次启动内单调递增，分配节点会话号不复用；F103启动token可以由UID/tick组合，绑定的新鲜度来自HUB挑战，不依赖Flash计数。

节点只有收到非零HUB会话和新挑战、并验证对应ACK后才就绪。重复ACK不重置序号或延长存活；超时后旧ACK不能复活节点。当前启动内记录最近8个退役HUB会话，并拒绝同HUB更旧挑战。这是有界测试协议的恢复机制，不是密码学鉴权或无限期抗重放保证；进入真实控制前必须另行设计授权与安全协议。

每轮挑战从首次发出开始有固定的1500ms绑定期限，500ms重发不刷新该期限。期限到达而握手仍未完成时，HUB换新挑战重试；即使旧响应先于本轮tick到达，也不能完成已过期的轮次。从节点重复收到相同挑战时不延长绑定等待，过期后继续拒绝旧挑战与旧ACK。这样既保留旧绑定拒绝，也能在初始响应连续丢失超过1500ms后自动恢复握手。

接受新绑定时将ACK的HUB seq登记为接收下界，拒绝此前排队的旧请求。业务帧以源会话+序号检查；16位序号按半区间比较，前向跳号接受并计丢失，重复/旧号不更新业务状态。地面F103透明转发HUB↔AIR，不重写源会话、seq或CRC。F103重新握手后HUB主动重建AIR绑定，恢复无线桥的expected_reply_session。

重试保留原请求、request_seq和截止期；AIR缓存测试结果，重发只增加回复的传输seq，原事务引用及数据不变。HUB只有匹配尚未过期的在途查询才更新遥测；HEARTBEAT不刷新遥测时间。成功事务、UART已收包和机载已响应是不同层级，真实执行飞控动作在本工程中不存在。

## 周期、拥塞与恢复

UART心跳500ms，节点/遥测状态超时1500ms。HUB每100ms仅在无在途事务时查询AIR，忙时跳过周期。事务固定200ms；RF TX看门狗20ms、回复等待30ms、最多2次重试、退避5/10ms。所有时差以本地uint32单调tick的无符号差计算，支持计数器回卷，不比较不同MCU的绝对时间。

帧队列容量8；提交失败返回false并计溢出，不覆盖未发送帧。RF队列中的查询入队起200ms过期；握手等无事务消息1500ms过期。UART接收字节溢出后丢弃不完整消息到下个定界符，不把旧缓存伪装成新消息。具体DMA中断延迟预算见 board_config.md。

SPI失败、FIFO异常、UART错误均通过有界恢复与统计报告；SPI/GDO与业务解析在主循环，ISR只记录事件/字节。无线和视频任务互相独立。当前所有编译和测试均属PC/适配验证，不能替代实板验收。
