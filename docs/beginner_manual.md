# 无人机通信工程零基础操作手册

版本 1.0　编写日期 2026 年 10 月 6 日

适用对象：第一次使用 Keil、Vivado 和 Xilinx SDK 的项目成员。

本手册教你从接收源码包开始，完成 STM32 工程编译与 Flash 下载，以及 Zynq 7020 的首次 JTAG 加载和通信观察。程序用于通信测试：机载端返回模拟遥测，Zynq 返回模拟视频状态。当前没有可直接烧录的完整 YOLO 程序、摄像头程序或飞控程序。

本版依据通信代码提交 `601482189d42ceda024b9aca52912b8569b47c03` 编写。代码已经完成 PC 测试和真实工具链编译；本文中的实板操作步骤尚未在你的硬件上执行。完成接线、配置和实测后，才能把对应项目记为“实板已通过”。

**第一次操作的顺序：**准备软件和烧录器 → 保存原始源码包 → 核对板型与接线方案 → 外部信号线断开时逐块编译下载 → 断电后接第一段链路 → 加载 Zynq → 逐段联调。不要第一步就同时连接四块板。

仓库：https://github.com/KISY2000/drone-comm

## 01 先认清四块板和文件类型

这里有三个地面处理节点，以及一个机载通信测试节点。请给实物贴上下面的标签，尤其不要把两块 F407 的程序装反。

| 实物标签 | 主要任务 | 本轮使用的软件 |
| --- | --- | --- |
| 地面 F103 | 在地面 F407 和地面 E07 之间转发消息 | Keil uVision 5 |
| 地面 F407 或 HUB | 管理会话，连接 F103 和 Zynq，发起遥测查询 | Keil uVision 5 |
| Zynq 7020 | 与地面 F407 通信，返回模拟视频状态 | Vivado 2017.4 和 SDK 2017.4 |
| 机载 F407 或 AIR | 驱动另一只 E07，返回模拟遥测 | Keil uVision 5 |

**烧录**通常指把程序写进非易失存储器，断电后仍保留。**编译**是把 C 源码变成芯片能执行的程序；编译成功只说明软件能够生成。**调试**是用下载器暂停程序、查看变量和定位错误。

| 文件后缀 | 是什么 | 这次如何使用 |
| --- | --- | --- |
| `.uvprojx` | Keil 工程入口 | 双击或在 Keil 中打开 |
| `.c` 和 `.h` | C 程序与配置文件 | 修改后必须重新编译 |
| `.axf` | STM32 可执行文件，含调试信息 | 由当前 Keil 工程下载 |
| `.hex` | 带地址信息的烧录文件 | 可选生成，不能仅凭文件名判断角色 |
| `.lib` | 静态库 | 不能单独烧录 |
| `.hdf` | Vivado 2017.4 硬件描述 | 交给 SDK 生成对应 BSP |
| `.elf` | Zynq ARM 应用程序 | 本轮由 SDK 经 JTAG 加载到内存 |
| `.bit` | FPGA 可编程逻辑配置 | 当前 MIO 串口测试不需要生成它 |
| `BOOT.bin` | Zynq 启动镜像 | 当前尚未交付最终启动包 |

**两个重要区别：**STM32 本轮写入内部 Flash，正常情况下断电仍保留；Zynq 本轮经 JTAG 加载到内存，断电后要再次加载。把源码 ZIP 复制到开发板、U 盘或 SD 卡，不会自动完成编译或烧录。

## 02 准备工具和软件

你的电脑已经安装 Keil 5.40、Vivado 2017.4、SDK 2017.4 和 CubeMX。本机优先使用现有版本；下面的安装说明主要给队友的新电脑使用。安装完成不代表电脑已经识别下载器，后面还要检查连接。

| 必备物品 | 用途和要求 |
| --- | --- |
| Windows 电脑与数据线 | USB 线必须支持数据，只有充电功能的线不能下载 |
| ST-Link 下载器 | 给三块 STM32 下载和调试，可先用一个依次操作 |
| Zynq 兼容 JTAG 下载器 | 给 7020 下载和调试；普通 USB 转串口线不能替代 |
| 各板规定的电源 | 按厂家标注供电；板间连接信号和 GND，不并接电源输出 |
| 两只同型号 E07 和对应天线 | 一只地面，一只机载；核对是本配置适用的 CC1101 26 MHz 模块 |
| 杜邦线、万用表 | 核对接地、供电与通断；检查排针实际方向 |
| 逻辑分析仪或示波器 | 排查串口电气负载、SPI、GDO 时序；联调建议准备 |

**Keil 的作用和安装。**Keil uVision 是写代码、编译和下载 STM32 程序的窗口。新电脑从 Keil 官方页面获取 MDK，安装后打开 uVision；使用合法可用的许可证。当前基线是 MDK 5.40 和 Arm Compiler 6.22。进入 Pack Installer，检查以下设备包：Keil STM32F1xx DFP 2.4.1、STM32F4xx DFP 3.1.1、ARM CMSIS 5.9.0。安装同名不同版本时可能出现迁移提示，先记录，别直接覆盖基线。

**Vivado 和 SDK 的作用。**Vivado 配置 Zynq 的硬件环境；SDK 编译和调试其中 ARM 核的软件。新电脑在 AMD 官方历史版本中选择 2017.4，安装 Vivado、SDK、Zynq 7000 支持和下载线驱动。许可证提示按官方许可处理。这里使用 `.hdf` 时代的流程，不要把新版 Vitis 的 `.xsa` 步骤混进来。

**CubeMX 的作用。**它是 STM32 引脚与时钟的图形配置工具。本轮完整通信工程已经手动初始化外设，第一次烧录不需要打开 CubeMX 或点击 Generate Code。当前安装版本为 6.18.1；它生成的基底工程还没有通信主循环。

**查看手册。**Word 或 WPS 可打开本手册。Python 和 GCC 仅在你要复现 PC 测试时需要；不需要为烧录运行文档 OCR 环境。

官方入口：Keil https://www.keil.com/download/product/ ；STM32CubeMX https://www.st.com/en/development-tools/stm32cubemx.html ；AMD https://www.xilinx.com/support/download.html 。

## 03 收到文件包后的第一步

1. 在资源管理器里打开“查看”，显示“文件扩展名”。这样可以分清 `.uvprojx`、`.axf` 和 `.lib`，避免只看相似文件名。
2. 保留收到的原始 ZIP，另建一个操作目录。示例使用 `C:\drone_work\drone_comm`；路径尽量短，使用英文字母、数字和下划线。不要直接在压缩包窗口中双击工程。
3. 右键 ZIP →“全部解压缩”，把整个工程解压出来。如果出现 `drone-comm-main` 等外层文件夹，确认真正工程目录里面同时存在 `README.md`、`include`、`src`、`platforms`、`deps` 和 `tools`。
4. 再复制整份工程为实板操作副本，例如 `C:\drone_work\board_test`。以下实板改动都在这个副本进行。不要只复制一个 `.uvprojx`，它依赖其他目录中的相对路径。
5. 用记事本打开 `README.md`，确认当前路线写的是 F407 USART3 加 USART2、Zynq UART0 MIO14/15、默认 115200。看到 USART6、UART1 EMIO 或 U5/T5 的旧资料时，按“历史备用”处理。
6. 建立 `my_board_records` 文件夹，保存版本号、接线照片、编译日志和测试结果。每次测试记下是哪块板、哪个工程、哪个波特率。

从 GitHub 下载时，打开仓库 → Code → Download ZIP。这个按钮取得当时主分支最新版本。要严格复现本手册的软件基线，可下载下面这个固定提交的 ZIP：

https://github.com/KISY2000/drone-comm/archive/601482189d42ceda024b9aca52912b8569b47c03.zip

GitHub 源码包不含本机 `build` 输出、工具安装程序和私人 PDF。没有 `.axf`、`.elf` 或 HDF 是正常的，后面从源码生成。不要从旧聊天附件中找一个同名二进制文件代替当前编译。

**找到正确入口。**三份完整 STM32 程序都在 `platforms\stm32\standalone`。`platforms\stm32\templates` 中的是库工程；`platforms\stm32\cubemx` 中的是初始化基底；`build` 中带有 `link_only` 或 `enabled_link_only` 的文件只做链接验证。这三类都不是本手册的首次烧录入口。

**每次大改之前。**关闭 IDE，复制整个工作副本为新的日期目录。恢复时回到旧副本重新编译、下载；只恢复一个 `.c` 文件可能漏掉工程宏或 BSP 设置。

## 04 断电接线和串口路线

先关闭各板电源，拔掉供电 USB，再调整跳帽和杜邦线。TX 表示发送，RX 表示接收，串口必须 TX 接另一端 RX。GND 是共同参考地；本方案使用 3.3 V TTL 信号。

| 发送端 | 接收端 |
| --- | --- |
| F103 PA2 TX | 地面 Explorer P2.3，PB11 RX |
| 地面 Explorer P2.4，PB10 TX | F103 PA3 RX |
| 地面 Explorer P4.3，PA2 TX | Navigator P5.2，MIO14 RX |
| Navigator P5.1，MIO15 TX | 地面 Explorer P4.4，PA3 RX |
| 三块地面板 GND | 互相连接到共同 GND |

表中的数字是原理图针号，不表示照片中“从左往右数第几根”。必须先找到实物 pin 1 标记，再核对针号；不要按排针外形猜测。机载节点通过无线连接，远端独立供电，不需要拉一根长地线连接两地。

**Navigator P5。**拆除 1–3 和 2–4 跳帽，信号接 PS 一侧的 P5.1、P5.2。另一侧是板载 CH340 串口芯片路径。保留跳帽可能让两个发送端同时驱动一条线。调试时也不要为了“看日志”把跳帽插回。

**Explorer P2 和 P4。**从 MCU 中排接线，拆除通往 RS232、GPS、RS485 的相关选择跳帽。不要接 MAX3232 转换之后的 RS232 电平接口。

**Explorer P1。**拆除音频跳帽。地面程序禁用以太网和音频，并把 PD3 的 PHY reset、PC1 的 MDC 保持低。PA2 仍连接 MDIO，PA3 仍可能带音频 RC 负载，因此要测量波形；软件关闭外设并不证明外部负载已完全隔离。

**供电检查。**各板按厂家要求独立供电。板间不连接两个 3.3 V 或 5 V 电源输出。用万用表确认 GND、目标供电和无明显短路；无法确认针号或测出异常电压时，先停在此步。

**此时先核对方案，不急着连整机。**首次下载前，板上可能仍有旧程序。保持板间 UART 与 E07 信号线断开，先逐块下载第 06 至 09 节的测试程序。完成后全部断电，再只接 F103 与地面 F407 这一路；后续节点按第 13 节加入。不要让已上电的发送端连接到未上电的接收板。

默认参数统一为 115200 bps、8 个数据位、无奇偶校验、1 个停止位、无流控，也写作 115200 8N1。首轮通过后，再按第 15 节三台一起改为 460800。

## 05 两只 E07 应怎样接

E07 引脚要按照模块外壳型号及厂家引脚定义逐一找信号名。本表按信号对应，不给未经实物核对的 E07 排针朝向。先接电源、GND、天线，再核对 SPI 和 GDO；全部接线操作均在断电时进行。

| E07 信号 | 地面 F103 | 机载 Explorer F407 |
| --- | --- | --- |
| SCK | PA5 | PB3，U16.5 |
| MISO | PA6 | PB4，U16.7 |
| MOSI | PA7 | PB5，U16.6 |
| CSN | PA4 | PG7，U16.4 |
| GDO0 | PB0 | PG6，U16.3 |
| GDO2 | 预留不接 | 不接 |
| GND | 板上 GND | U16.1，GND |
| 电源 | 核对模块要求后接 3.3 V | 核对模块要求后接 U16.2，3.3 V |

SPI 与 UART 不同：SCK 接 SCK，MISO 接 MISO，MOSI 接 MOSI，不按 TX/RX 的方法交叉。CSN 是片选，GDO0 提供无线收发事件。少接 GDO0 可能造成超时或只能偶发收发。

**机载板的共享引脚。**拔掉原 NRF 模块，保持板载 Flash 的 PB14 片选为高。当前程序会配置 PB14。PB3、PB4 被无线 SPI 占用，因此调试只能选择 SWD，并关闭 SWV/Trace，不使用 JTAG。U16 是 NRF 风格接口，不能把 E07 当作针脚兼容模块直接插入；必须按表转接。U16.8 的 PG8 与 RS485 使能共用，本方案不用。

**频率配置。**当前两端使用同一份 433 MHz、GFSK、38.4 kbps 配置，模块晶振按 26 MHz。这个 26 MHz 是无线模块晶振，与 F103 已确认的 8 MHz 外部晶振不是一回事。不要在一端随意改频率或速率。

**未接 E07 时能做什么。**可以先验证 STM32 启动、下载器和有线链路；F103 会报告无线故障并重试初始化。此时遥测计数不增长属于预期，不能据此判定有线失败，也不能把无线记为通过。

初次无线试验可在同一桌面相隔约 1 至 2 米进行，两只模块使用适配天线，电源稳定。后续距离测试另行记录。不要把本轮模块联通结果当作飞行距离或飞控验收。

## 06 在 Keil 中打开正确工程

以下以地面 F103 为例。F407 操作相同，工程和器件必须换成对应项。开始时可以不连接硬件，先把软件编译通过。

1. 双击桌面的 Keil uVision 5。若无图标，本机程序在 `C:\Users\Li\AppData\Local\Keil_v5\UV4\UV4.exe`。
2. 点击 File → Open Project，进入实板副本的 `platforms\stm32\standalone` 文件夹，选择下表中的工程，点击 Open。
3. 左侧 Project 窗格出现源文件分组。如果没显示，使用 View 菜单打开 Project 窗格。双击源文件即可查看代码；不要新建空工程再逐个复制文件。
4. 点击 Project → Options for Target，或工具栏魔术棒图标。先检查 Device 页，再检查 Target 页的编译器。当前目标名称带 `standalone_disabled`，表示交付默认关闭硬件业务。
5. 在 Target 页确认使用 Arm Compiler 6.22。若提示找不到编译器，先到 Keil 的编译器配置中注册已安装的 6.22，或补齐官方安装；不要强行改成 Compiler 5 来绕过错误。

| 角色 | 打开的工程 | Device 应匹配 |
| --- | --- | --- |
| 地面 F103 | `ground_f103.uvprojx` | STM32F103C8，先核对实物丝印 |
| 地面 F407 | `ground_f407.uvprojx` | STM32F407ZG，先核对实物丝印 |
| 机载 F407 | `air_f407.uvprojx` | STM32F407ZG，先核对实物丝印 |

源目录位于工作副本，不是 `templates` 或 `cubemx`。每个工程只对应一个角色；两块 F407 即使芯片相同，程序的任务也不同。

**第一次先编译交付配置。**点击 Project → Rebuild all target files，等待下方 Build Output 窗口结束。需要看到 0 Error(s)。F407 基线存在厂商源文件和启动汇编相关的已知警告，具体以 `reports/stm32_standalone.json` 为准；遇到新的警告也要保存并核对。

编译失败时，双击第一条 error 跳到位置。常见原因是没有完整解压、设备包缺失或编译器版本不对。不要因为编译失败就删除报错代码、扩大内存容量或从网上随便替换 HAL 文件。

## 07 启用实板配置并生成自己的程序

本节必须在第 04、05 节相关硬件条件核对后，在工作副本里操作。交付工程中的 `DC_BOARD_READY=0` 是软件启动开关；如果直接下载这个默认版本，业务不会运行。它不是接线正确性的自动检测。

1. 打开 Options for Target → C/C++ 页。使用 Compiler 6 时，页名可能带 AC6。找到 Preprocessor Symbols 下的 Define 文本框。
2. 找到现有 `DC_BOARD_READY=0`，把这一项改为 `DC_BOARD_READY=1`。保留其余宏，不要同时留下 0 和 1 两个定义。
3. 确认还保留 `HSE_VALUE=8000000U`，以及当前角色对应的宏。首次不添加波特率覆盖，使用默认 115200。
4. 点击 Output 页，把 Name of Executable 改成易辨认的名字，例如 `ground_f103_board_115200`。两块 F407 分别用 `ground_f407_board_115200` 和 `air_f407_board_test`。
5. 在 Output 页点击 Select Folder for Objects，选择自己新建的对应输出文件夹，例如仓库根目录下 `build\board_test\ground_f103`。保留 Create Executable 与 Debug Information。首次用 AXF 下载，不必勾选 Create HEX File。
6. 点击 OK，保存工程，再执行 Rebuild all target files。只有出现 0 Error(s)，且输出目录中出现本次新时间戳的 `.axf`，才继续下载。

| 工程 | 必须保留的角色相关宏 |
| --- | --- |
| F103 | `USE_HAL_DRIVER STM32F103xB DC_STM32_F1 DC_ROLE_GROUND_F103` |
| 地面 F407 | `USE_HAL_DRIVER STM32F407xx DC_ROLE_GROUND_F407` |
| 机载 F407 | `USE_HAL_DRIVER STM32F407xx DC_ROLE_AIR_F407` |

**为什么不只改头文件。**`board_config.h` 中的默认值受到 `#ifndef` 保护；Keil 工程已经显式定义 `DC_BOARD_READY=0`。仅在头文件里把默认值改成 1，可能仍被工程设置覆盖。以 C/C++ 的 Define 和实际编译日志为准。

**内存配置先保持原样。**F103 使用 64 KiB Flash、20 KiB RAM；F407 使用 1 MiB Flash、128 KiB 普通 RAM。已有 scatter 文件、4096 字节栈和 0 字节堆。不要为了让链接通过而扩大到未经确认的容量。

**需要 HEX 的情况。**仅当队友使用独立烧录软件时，在 Output 中勾选 Create HEX File，再 Rebuild。HEX 与 AXF 必须来自同一次构建。HEX 自带地址；首次不建议转换成需要人工指定地址的 BIN。

## 08 连接 ST Link 和设置下载参数

请先只接一块 STM32 的电源和 ST-Link，保持板间 UART 及 E07 外部信号线断开。ST-Link 是下载器，USB 转串口模块不是 ST-Link。Explorer 若带有调试接口，先按其原理图确认针号，不照搬另一型号板的接口朝向。

| ST-Link 信号 | STM32 对应信号 |
| --- | --- |
| SWDIO | PA13 或板上标注 SWDIO 的引脚 |
| SWCLK | PA14 或板上标注 SWCLK 的引脚 |
| GND | 目标板 GND |
| NRST | 目标板复位脚，建议接上，便于复位连接 |
| VTref 或 VAPP | 按下载器说明接目标 3.3 V 参考电压，先核对它是不是电源输出 |

不同 ST-Link 的“3.3V”脚可能是输出或电压检测。目标板已独立供电时，不要把两个电源输出直接相连。先按该下载器说明书确定含义。

1. 断电接好线，给目标板按要求上电，再把 ST-Link 接电脑。打开 Windows 设备管理器，确认下载器没有黄色感叹号。若缺驱动，安装 ST 官方 ST-Link USB 驱动。
2. 在 Keil 打开正确工程，点击 Options for Target → Debug。选择 Use 后的 `ST-Link Debugger`，不要停留在 Use Simulator。
3. 点击旁边 Settings。在 Debug 页选择 Port 为 SW，或版本中名为 SWD 的选项。初次可把时钟设为约 1 MHz；连接不稳定时降到 100 kHz。能看到下载器序列号、目标电压和目标识别信息后再继续。
4. 机载板必须关闭 Trace/SWV，因为 PB3、PB4 已用于 E07 SPI。不要选 JTAG 来占用这些引脚。
5. 在 Flash Download 页核对算法，缺少时点 Add：F103 选择 `STM32F10x Med-density Flash`；F407 选择 `STM32F4xx 1MB Flash`。不选外部 Flash 算法。F103 算法支持范围可能显示 128 KiB，但本工程仍只使用 64 KiB，不要因此扩大链接容量。
6. 首次建议选择 Erase Sectors、Program、Verify，并按需要启用 Reset and Run。应用 Flash 起始地址为 `0x08000000`；链接容量 F103 为 `0x10000`，F407 为 `0x100000`。保留匹配算法自己的支持范围，不随意修改。不要为连接失败改 Option Bytes 或做全片擦除。
7. 回到 Utilities 页，选择同一个 ST-Link 目标驱动，或勾选 Use Target Driver for Flash Programming，确认下载与调试使用的是硬件驱动。

本次交付的工程没有预先保存 ST-Link 硬件下载配置，因此这些步骤不能省。电脑能看到 ST-Link，只说明下载器存在；还要确认它识别了目标芯片。

## 09 下载 STM32 并检查是否运行

**下载会替换这块板原有的应用程序。**如果板上有需要保留的队友程序，先索要其源码或原烧录文件。以下操作只针对明确分配给本工程的板，不改芯片读保护或其他 Option Bytes。

1. 再看一次工程名、板上角色标签、输出 AXF 的名字和时间戳。地面与机载 F407 不能互换。
2. 点击 Flash → Download，或工具栏的下载按钮，等待完成。在 Build Output 或下载窗口检查 Program 和 Verify 成功，且没有 Flash Download failed 等错误。仅出现“编译完成”不等于下载完成。
3. 按板上 RESET 键。如果没有自动运行，检查 Debug/Flash Download 中的 Reset and Run 设置。F103 正常从内部 Flash 启动时 BOOT0 应为低电平；不要把它长期放在系统串口下载模式。
4. 要检查状态，点击 Debug → Start/Stop Debug Session。若停在 `main`，点击 Run 或按 F5，让程序运行几秒，再点击 Stop 暂停。
5. 打开 View → Watch Windows → Watch 1，在空白行添加 `dc_startup_status` 和 `dc_firmware_started`。需要时展开变量值。确认后继续 Run，不要一直停在断点上做通信测试。

| 观察项 | 数值 | 含义及下一步 |
| --- | --- | --- |
| `dc_startup_status` | 0 | 尚未走完入口；先运行，或检查复位位置 |
| `dc_startup_status` | 1 | 外设初始化停止；默认 READY=0 也会到这里，核对宏和时钟 |
| `dc_startup_status` | 2 | 通信初始化失败；核对 UART/DMA，地面 F407 还需检查 RNG |
| `dc_startup_status` | 3 | 已进入通信轮询循环；还需继续检查链路 |
| `dc_firmware_started` | true 或 1 | 通信软件已启动；不代表 E07 已联通 |

**首次 E07 仍断开。**启动状态 3 可以成立，同时无线 FAULT 和初始化重试属于预期。这一轮只验软件启动，第 13 节接好模块后再验无线。程序没有约定某个 LED 表示成功，按变量和实际数据判断，电源灯亮不能证明程序运行。

**调试暂停会影响通信。**程序暂停超过 1500 ms，其他节点可能认为它离线。观察变量时短暂停下，截图后继续运行，允许它重新握手；不要把人为暂停造成的超时统计当作正常性能结果。

退出调试后，断电再上电，确认 STM32 仍能运行。依次对另外两块 STM32 重复第 06 至 09 节，每次核对角色。保留 Keil 工程、AXF 和编译日志到本次测试目录。

## 10 Zynq 首次运行需要哪些文件

Zynq 7020 内部既有 ARM 处理器，也有可编程逻辑。当前通信应用运行在 ARM 的 `ps7_cortexa9_0` 上；串口是 PS UART0，使用 MIO14 RX、MIO15 TX。这个接口无需 PL bitstream，但仍必须正确初始化 PS 时钟、DDR 和 MIO。

**本轮使用 JTAG 首次加载。**你需要匹配的硬件描述 HDF、由它生成的 BSP，以及编译后的应用 ELF。BSP 是板级支持包，包含本硬件对应的底层驱动；Workspace 是 SDK 保存各个项目的工作目录。三种产物必须来自同一套硬件配置。不能拿旧 UART1 EMIO 的 BSP 配新 UART0 应用，也不能把文件名相同当作版本相同。

**打开 PowerShell。**进入工作副本根目录，在资源管理器地址栏输入 `powershell` 后回车。看到终端提示符的目录是工作副本即可。下面代码逐块复制执行，不要复制说明文字。路径按本机现有安装填写；其他电脑需替换成真实安装路径。

先生成独立的硬件描述。本命令不打开或改动原 YOLO 工程。输出目录必须是尚不存在的新目录：

```powershell
& 'D:\Xilinx\Vivado\2017.4\bin\vivado.bat' `
  -mode batch -source '.\tools\create_zynq_validation.tcl' `
  -tclargs '.\build\manual_hw_01'
```

PowerShell 行尾的反引号表示下一行还是同一条命令，后面不要加空格。也可以把三行合成一行，删除两个反引号。

等待 Vivado 结束。需要确认无 ERROR，且生成 `build\manual_hw_01\dc_uart_validation.hdf`。脚本还保存 `validation.txt`，应标明 UART0 MIO14/15、UART1 disabled。当前器件目标为 `xc7z020clg400-2`；首次与实物核心板核对。

然后创建 SDK 工作区和匹配的 BSP：

```powershell
& 'D:\Xilinx\SDK\2017.4\bin\xsct.bat' `
  '.\tools\create_zynq_bsp.tcl' `
  '.\build\manual_hw_01\dc_uart_validation.hdf' `
  '.\build\manual_sdk_01' 115200
```

成功后应有 `build\manual_sdk_01\dc_zynq_comm\Debug\dc_zynq_comm.elf`。它仍是 READY=0 的基线应用，下一节创建明确的实板配置。另一个带 `link_only_ready1` 的应用只是链接检查用，不作为你的下载入口。

若提示 Refusing existing，请把新输出目录编号改为 02，并让后面的 HDF 路径一致。SDK 工作区和对应的 `manual_sdk_02_sources` 都必须尚不存在。脚本拒绝覆盖已有结果是正常保护，不要删除旧结果来掩盖失败。

## 11 在 SDK 中配置实板应用

1. 打开 Xilinx SDK 2017.4；本机入口为 `D:\Xilinx\SDK\2017.4\bin\xsdk.bat`。Workspace Launcher 出现时，选择刚生成的 `build\manual_sdk_01` 的绝对路径。若已打开其他工作区，使用 File → Switch Workspace → Other 切换。
2. 左侧 Project Explorer 应看到 `dc_hw`、`dc_bsp`、`dc_zynq_comm` 和一个 `link_only_ready1` 应用。首次只配置 `dc_zynq_comm`，不要选名字相似的 link-only 项目。
3. 右键 `dc_bsp` → Board Support Package Settings。在 standalone 参数中检查 `stdin=none`、`stdout=none`。如果更改，保存并重新生成 BSP，再编译。UART0 不能同时输出 `printf` 日志和通信数据。
4. 右键 `dc_zynq_comm` → Properties → C/C++ Build → Settings，找到 ARM v7 gcc compiler → Miscellaneous → Other flags。在原字符串末尾加一个空格，再追加 `-DDC_ZYNQ_BOARD_READY=1`。
5. 保留原来的 `-std=c99 -Wall -Wextra -Werror` 和 `-DDC_UART_BAUD=115200u`。确认没有相反的 READY=0，也不要在 Symbols 中重复添加 READY 或另一种波特率。
6. 点击 Apply、OK。Project → Clean，选择此应用；然后右键应用 → Build Project。看 Console 的完整构建结果和 Problems 页，要求没有编译或链接错误。
7. 在 `dc_zynq_comm\Debug` 中确认 ELF 更新时间。记录工作区、HDF 来源、READY=1、115200 和编译日志。这份 ELF 是你准备实板测试的应用，并未因此自动变成实板已验收版本。

菜单名称可能随 SDK 语言略有差异。关键是修改当前应用的 C 编译预处理定义，检查实际命令出现一次 `-DDC_ZYNQ_BOARD_READY=1`，并保留一致的波特率。

**修改哪个源码。**创建 BSP 脚本会复制源文件到 SDK 应用。以后在仓库里修改代码，旧工作区未必同步更新。最容易检查的方法是用新编号重新运行第 10 节，再重复本节配置；不要以为旧 ELF 自动包含刚改的源码。

**不要做的操作。**不要给当前应用加 `xil_printf` 到 UART0，不要为看到串口文字把 BSP stdout 改回 UART0，不要在当前 MIO 路线加载旧 U5/T5 XDC。日志已经放入 RAM，第 12 节会介绍查看方法。

## 12 通过 JTAG 加载 Zynq 和观察运行

本节是首次内存加载，不是往板载 Flash 写最终镜像。先断电，将领航者板上 BOOT 拨码的 1、2 都拨到 ON，再上电，进入 JTAG 启动模式。依据为厂家《领航者ZYNQ开发板规格书V1.2》第 3.3 节。按实物 ON 丝印判断方向，不照搬照片中的上下方向。

1. 将兼容的 JTAG 下载器连接板上 JTAG 接口，再连接电脑。板子单独按厂家要求供电。P5 的协议隔离跳帽保持第 04 节状态，CH340 串口连接不能替代 JTAG。
2. 在 SDK 选中 `dc_zynq_comm`，打开 Run → Debug Configurations。左侧选 `Xilinx C/C++ application (System Debugger)`，新建配置；不选 GDB 或 QEMU。Debug Type 选 `Standalone Application Debug`。
3. 在 Target Setup 页，Connection 选 Local，Hardware Platform 选 `dc_hw`，Initialization File 选当前工作区的 `dc_hw/ps7_init.tcl`。勾选 `Reset entire system`、`Run ps7_init`、`Run ps7_post_config`；不要勾选 `Program FPGA`。这保证先复位并初始化 PS 和 DDR，再加载应用。不要选旧工程的初始化脚本。
4. 在 Applications 页选择处理器 `ps7_cortexa9_0`，勾选 `Download Application`，Project Name 选 `dc_zynq_comm`，Application 指向本次 `dc_zynq_comm/Debug/dc_zynq_comm.elf`，勾选 `Stop at program entry`。不要选 link-only 应用。
5. 点击 Debug，成功时进入 Debug 视图，程序停在入口或 main。点击 Resume 或按 F8，让它运行几秒，再 Suspend 查看变量；使用完继续 Resume。停在入口只证明加载完成，还没证明应用通信正常。
6. 在 `main_hooks.c` 中查看 `started`、`node` 和 `port`，在 Expressions 或 Variables 中展开。找不到 static 变量时，可双击该文件函数代码左侧边栏设置断点，Resume 后暂停查看。看完取消断点再 Resume，避免循环每轮都停下。

正常启动后，`started` 应为 true。与地面 F407 握手完成后，`node.ready` 应为 true，`node.session` 非零。如果程序从 main 返回，重点检查 PS 初始化、READY 宏、UART0 BSP 和中断配置；不能以“ELF 下载完成”代替运行检查。

**RAM 日志。**在 Expressions 中添加 `dc_zynq_debug_log`。它包含 512 字节环形缓冲区，`count` 是有效字节数，`head` 是下一写入位置，`overwritten` 是覆盖计数。首次启动可在缓冲内容中找到 `drone_comm UART0 MIO14/15 ready`。它不是始终以零结尾的字符串；环回后从 `(head + 512 - count) % 512` 开始读取 count 字节。

**停止和重来。**点击 Terminate 结束本次调试；若断电，重新执行目标复位、PS 初始化、ELF 下载和 Resume。当前没有断电自动恢复这个 JTAG 应用的机制。

## 13 怎样判断四节点通信成功

程序没有配套图形上位机，也不会在串口助手中打印一行行中文遥测。它发送的是 COBS 加零字节定界的二进制协议。串口助手显示乱码或不显示文字，都不能单独判断失败；不要向业务串口发送随意文本来“试一下”。

**主要从地面 F407 观察。**在 Keil 打开 `platforms\stm32\common\main_hooks.c`，在 `dc_firmware_poll` 的可执行行设置断点，Run 后暂停，展开文件内的 `app`。在此作用域中 Watch 才容易找到该 static 变量。看完取消断点再 Run，避免循环每轮都停下。若被优化隐藏，可在副本中降低 C 优化后 Rebuild，仍须检查内存和编译结果。

| 地面 F407 观察项 | 正常时应看到 |
| --- | --- |
| `app.node.ready` | true，HUB 自身已启动 |
| `app.node.peers[2].ready` | true，地面 F103 握手成功 |
| `app.node.peers[3].ready` | true，Zynq 握手成功 |
| `app.node.peers[4].ready` | true，机载 F407 经过无线握手成功 |
| `app.node.telemetry_rx` | 在四节点正常运行时持续增长 |
| `app.node.telemetry_valid` | true，当前有新鲜的模拟遥测 |
| `app.node.video_rx` | Zynq 连通后增长，约每 500 ms 上报 |
| `app.node.video_valid` | true，收到新鲜状态，但不表示真实视频已锁定 |

默认 HUB 每 100 ms 尝试一次遥测查询；在途事务未完成时会跳过新查询。因此不用先点击某个“开始采集”按钮，也不能简单要求计数每秒严格增加 10。

**建议按四步验收。**第一步只连接 F103 与地面 F407，检查 peer 2；第二步添加 Zynq，检查 peer 3 和 video_rx；第三步连接两端 E07 并启动机载板，检查 peer 4 和 telemetry_rx；第四步连续运行至少 60 秒，记录开始、结束计数和异常计数。这个 60 秒只是首次冒烟检查，不代表长期可靠性认证。

F103 或机载 F407 上可查看 `app.radio.state` 和 `app.radio.stats`。正常接收空闲期常处于 `DC_RADIO_RX`，发送期间会切换状态；`rx_valid`、`tx_ok`、地面端的 `transaction_ok` 应有进展。同时看 `app.rf_reinit_count` 与 `app.rf_reinit_failures`：反复初始化会清掉 radio 内部统计，不能仅靠 `io_errors`、`recoveries` 判断。持续失败时检查 SPI、电源、天线与 GDO0。

**模拟标志不能忽略。**HUB 的 `latest_telemetry.payload[2]` 的模拟标志为 1；`latest_video_status.payload[0]` 为模拟，`payload[1]` 为 0，表示实际视频未锁定。计数增长只证明对应的通信路径工作。

## 14 断线测试和进阶测试的边界

**先记录，再制造断线。**正常运行时记下地面 F407 的 telemetry_rx、video_rx 和会话值。不要在测试同时让调试器长期暂停程序；观察用短暂停或经确认可用的实时读取。

1. 保持 F103、地面 F407、机载 F407 运行，只暂停 Zynq 应用超过 2 秒，模拟它不再发消息。地面 F407 应将对应状态判为超时，`video_valid` 变 false；无线遥测仍应继续增长。
2. 恢复 Zynq 运行，等待重新握手，观察视频状态恢复。此项证明软件路径的隔离；真正拔线测试应断电操作，单独记录，不能把调试暂停记成电气热拔插通过。
3. 在其余节点保持运行时，按机载板 RESET。观察该节点重新握手、旧遥测过期后重新获得新数据，不靠重启整个系统恢复。
4. 断电检查后制造无线断开，再上电测试，记录重试、超时和恢复计数。不要通过拔天线来模拟无线断开；可暂停或关闭远端节点。

**固定测试请求。**协议已实现 TEST_REQUEST 和 TEST_RESULT，PC 回归中已验证，但当前固件没有面向零基础用户的按键菜单或“发送测试包”按钮。默认上板自动跑的是遥测查询。不能把 telemetry_rx 增长等同于固定测试包下行验收。

如果需要固定测试包验收，由负责固件的同学在独立测试副本中，通过 `dc_node_request()` 从 HUB 主循环发起请求，检查 TEST_RESULT 的事务引用和回显载荷，再恢复普通版本。不要在中断中发送，不要在调试 Watch 里随意调用有副作用的函数。该测试接口的使用约束见 `include/dc_node.h` 和 `docs/bring_up.md`。

**时序测试。**TX 20 ms、发送结束后回复 30 ms、退避 5/10 ms、最多两次重试和总截止期 200 ms 是软件目标值。需要逻辑分析仪记录实际 SPI/GDO/UART 时间，PC 测试通过不能代替实测。当前 CC1101 完整复位与重配包含同步操作，累计耗时和对 UART 的影响尤其需要实测；不要对外宣称硬实时期限已验收。

**只用心跳测试遥测过期。**需要专门的故障注入方式阻止新遥测但保留心跳，不能简单暂停全部节点来代替。坏 CRC、旧会话、DMA 溢出等也是进阶故障测试；零基础首次操作先完成正常路径，并把这些项目保留为待测。

## 15 怎样调整参数和恢复旧版本

每次只改变一个明确目标，保存旧版本，记录修改位置，然后重新编译所有受影响节点。不要一边改接线、一边改波特率、同时又换无线参数，否则失败后难以定位。

**把有线波特率从 115200 改为 460800。**先确认 115200 已实测通过、PA2/PA3 波形正常，再执行下面步骤。

1. 复制当前通过的整个工程为新副本，保留 115200 的 AXF、ELF 和测试记录。
2. 在 F103 与地面 F407 的 Keil Options for Target → C/C++ → Define 中各增加一次 `DC_UART_BAUD=460800`。保留 READY=1 和原角色宏，Rebuild，输出名改成包含 460800。
3. Zynq 使用新的 SDK 工作区重新运行 `create_zynq_bsp.tcl`，最后一个参数从 115200 改成 460800；同一份 MIO HDF 可继续使用。按第 11 节启用 READY=1 并编译。也可在已明确核对的工作区替换原 `-DDC_UART_BAUD=115200u`，但不能重复保留两种值。
4. F103、地面 F407、Zynq 三者都更新完后再测试。AIR 当前没有这条有线 UART，无线速率不会因为这个宏变成 460800。
5. 重复第 13 节和波形检查。失败时，三台一起恢复保存的 115200 构建，不要只回退其中一台。

| 想改的内容 | 当前来源 | 初学者建议 |
| --- | --- | --- |
| 有线波特率 | `include/dc_link_config.h` 和编译宏 | 按上面统一更改 |
| 板级引脚 | 下文列出的各平台板级文件 | 先改接线表，再由固件同学改 GPIO、复用、DMA 和 IRQ |
| F103 或 F407 晶振 | HSE 宏、时钟初始化、实物晶振 | 已按 8 MHz 建立；换板需重新核对完整时钟树 |
| 查询、心跳、节点超时 | `include/dc_node.h` | 初次保留 100、500、1500 ms |
| 无线期限与重试 | `include/dc_radio.h` 和驱动 | 初次不改，修改须重新验证时序与重复请求 |
| 无线频率、速率、功率 | `src/dc_radio.c` 中的 profile 和功率配置 | 两端必须匹配；先核对模块、寄存器计算及测试要求 |
| 模拟遥测值 | `src/dc_node.c` | 修改数值不等于接入真实传感器 |

STM32 板级文件是 `platforms/stm32/common/board_config.h` 和同目录的 `board_peripherals.c`；Zynq 板级文件是 `platforms/zynq/board_config.h`。这些同名文件不能相互替代。

**CubeMX 什么时候用。**以后换引脚时，可在 CubeMX 中 File → Open Project 打开真正 `.ioc`，检查 Pinout & Configuration、Clock Configuration、Project Manager，再生成到独立副本。当前通信程序使用手动 HAL 初始化，不能直接覆盖 main、MSP、IRQ；需要固件同学合并，避免出现两套同名函数。

## 16 常见问题按这个顺序排查

**Keil 找不到头文件或源码。**确认整个包已经解压，工程在完整目录结构内。不要把 `.uvprojx` 单独搬到桌面。再检查设备包和 `deps` 是否齐全。

**提示找不到编译器或许可证。**核对 Arm Compiler 6.22 的真实安装位置及合法可用授权，保存完整错误。不要通过改成 ARM Compiler 5 或删除编译选项碰运气。

**No ST-Link detected。**先检查 USB 数据线、USB 口和驱动，再检查设备管理器；此错误发生在电脑到下载器之间，还没轮到排查业务程序。

**检测到 ST-Link 但 No target connected。**检查目标板供电、共地、SWDIO/SWCLK、目标电压参考和 NRST；降低 SWD 时钟。必要时在 ST-Link 设置中尝试 Connect under Reset，并确认 NRST 已连接。不要启用读保护或执行解除保护来试错。

**Flash Download failed。**先看是否选对 Device、ST-Link 驱动和 Flash 算法，再检查当前 AXF 是否刚编译成功、容量是否匹配。保存首条下载错误，不能只截“失败”弹窗。

**下载成功但 dc_startup_status 为 1。**先核对实际编译命令的 READY=1，然后排查 HSE 和时钟初始化。为 2 时检查通信初始化，HUB 还要查 RNG；为 3 但不联通时检查线和另一端程序。

**SDK 下载 ELF 后 DDR 写入失败或程序跑飞。**确认是本次 HDF/BSP，先复位并执行配套 ps7_init 和 ps7_post_config，再下载。不要直接更改链接地址来绕过 DDR 初始化问题。

**SDK 主程序运行后退出。**默认 READY=0 会导致启动失败返回；还要查 GIC、UART0、IRQ 和 BSP stdin/stdout 设置。Console 没有文字是本方案正常现象，去看 RAM 日志。

**串口有乱码。**这是二进制协议，先检查解码方式而不是改成别的波特率。确认两端均为 115200 或均为 460800，8N1，共地，TX/RX 交叉，且没有混入 printf 输出。

**E07 一直失败。**核对供电、SPI、CSN、GDO0、天线和双端配置，并查看 `app.rf_reinit_failures` 是否增长。读到整组 00 或 FF 不能证明 SPI 成功；PARTNUM 预期为 00，VERSION 应既非 00 也非 FF。初始化成功后仍需进入接收就绪并完成双端收发。

**刚看完变量就出现超时。**先恢复所有调试器 Run/Resume，等待重新握手，再观察。暂停中的程序不收发数据，这不是正常运行条件。

## 17 SD 启动和 YOLO 到哪一步

**Zynq 的 SD 卡自动启动尚未交付。**当前没有最终 FSBL、BOOT.bin 或实板验证过的启动包。第一轮使用第 10 至 12 节的 JTAG 路径；不要把 `.elf` 改名为 `BOOT.bin`，也不要拿旧工程镜像替代。

以后做 SD 启动时，工程负责人需要先准备匹配硬件的 FSBL，检查其全部日志路径不会向协议 UART0 发送文本，使用 SDK 的 Create Boot Image 按 FSBL、必要时的 bitstream、应用 ELF 的顺序生成镜像，再按厂家要求准备 SD 卡和启动拨码。完成断电重启、协议纯净性和异常恢复实测后，才发布可直接照做的 SD 烧录包。当前不应让初学者凭本段独立拼装最终启动镜像。

**YOLO 是另一条工作线。**本地 `yolo_prep` 和 `yolo_accel` 保存模型准备、参数处理和卷积原型。已有 PC 逐层比较，以及单个卷积核的 HLS C 仿真、综合和 RTL 协同仿真。它们尚未形成完整 YOLO 网络的 FPGA 加速系统。

| 项目 | 当前状态 | 你现在能做什么 |
| --- | --- | --- |
| 通信源码和工具链 | PC 已验证，已编译 | 按本手册做首次实板联调 |
| E07 与串口硬件通信 | 尚未实板验收 | 接线、测波形、记录联调结果 |
| Zynq 模拟视频状态 | 软件已实现 | 观察状态计数，保留模拟标志 |
| 完整 YOLO 上板 | 尚未完成 | 不在通信包中寻找 YOLO 烧录文件 |
| 摄像头实时图像 | 尚未接入当前通信应用 | 后续确定采集、缓存与算法接口 |
| 飞控与电机输出 | 不属于本轮通信测试 | 不把本程序用于实际飞行 |

原有 `D:\FPGA\zynq_yolo` 工程继续保留。不要为了通信串口测试，直接覆盖它的工程、约束或 bitstream。未来仍以 PL 承担主要视觉计算为目标；本手册不会把“模拟视频状态成功”计作“YOLO 已上板”。

## 18 每次测试应留下什么记录

把下面项目复制到本次 `my_board_records` 中。结果只用“未执行”“失败”“通过”三种明确状态；软件测试、编译通过和实板通过分开记录。

| 记录项 | 本次填写 |
| --- | --- |
| 操作者和日期 | ________________________ |
| 源码提交或原始 ZIP 名称 | ________________________ |
| 板角色、板版本、芯片丝印 | ________________________ |
| 跳帽状态与接线照片文件名 | ________________________ |
| 电源电压、信号电平和共同 GND | ________________________ |
| 软件与编译器版本 | ________________________ |
| READY 宏、波特率、无线配置 | ________________________ |
| AXF 或 ELF 路径及构建时间 | ________________________ |
| Zynq HDF、BSP、PS 初始化来源 | ________________________ |
| 下载校验与启动状态 | ________________________ |
| 60 秒前后遥测和视频计数 | ________________________ |
| 超时、恢复、错误计数变化 | ________________________ |
| 断线、重启测试与波形文件 | ________________________ |
| 最终结论与尚未完成项目 | ________________________ |

**向队友反馈故障时。**同时提供板角色、源码版本、完整第一条错误、接线照片和复现步骤。描述例如“地面 F407，115200，READY=1，启动状态 3，peer 2 一直 false”，比“烧不进去”更容易定位。

**第一次应完成的最小结果。**三块 STM32 均能明确下载并进入状态 3；Zynq 能经匹配的 PS 初始化加载 ELF；地面 F407 能看到 F103 和 Zynq 握手；E07 双端接好后模拟遥测增长；Zynq 停止时无线仍继续。每一项完成后留下证据，未完成的保留待测。

进一步资料均在工程 `docs` 目录：接线查 `proposed_wiring.md`，详细验收查 `bring_up.md`，软件版本查 `toolchain.md`，协议查 `protocol.md`，最新复查查 `deep_review_20261006.md`。新手首次操作以本手册顺序为主，遇到实物与资料不一致时记录差异，再更正对应配置。
