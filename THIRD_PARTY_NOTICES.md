# 第三方来源与许可

本文件索引本仓库实际使用的第三方组件及随附许可。原版权声明和许可证保留在对应目录；以下摘要不替代原文，也不将一种许可证扩展到整个仓库。项目原创通信代码与文档目前未另行指定开源许可证，提交到 GitHub 不代表授予额外使用许可。

## 固定版本的编译依赖

| 组件 | 版本与官方来源 | 随附许可 |
| --- | --- | --- |
| STM32F1 HAL | [ST v1.1.10](https://github.com/STMicroelectronics/stm32f1xx-hal-driver/tree/v1.1.10) | [BSD-3-Clause](deps/stm32f1xx-hal-driver-1.1.10/LICENSE.md) |
| STM32F4 HAL | [ST v1.8.5](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/tree/v1.8.5) | [BSD-3-Clause](deps/stm32f4xx-hal-driver-1.8.5/LICENSE.md) |
| STM32F1 CMSIS Device | [ST v4.3.5](https://github.com/STMicroelectronics/cmsis-device-f1/tree/v4.3.5) | [Apache-2.0](deps/cmsis-device-f1-4.3.5/License.md) |
| STM32F4 CMSIS Device | [ST v2.6.11](https://github.com/STMicroelectronics/cmsis-device-f4/tree/v2.6.11) | [Apache-2.0](deps/cmsis-device-f4-2.6.11/LICENSE.md) |
| CMSIS Core | [ARM CMSIS_5 5.9.0](https://github.com/ARM-software/CMSIS_5/tree/5.9.0)，仓库仅保留所需 Core 头文件及来源说明 | [Apache-2.0](deps/CMSIS_5-5.9.0/LICENSE.txt) |

下载 URL、锁定版本和原下载包 SHA-256 见 [deps/manifest.json](deps/manifest.json)。设备包版本和下载哈希见 [deps/packs/manifest.json](deps/packs/manifest.json)；不随仓库分发 Keil、CubeMX、Vivado 或 SDK 安装器和许可证密钥。

## STM32 工程中的副本

`platforms/stm32/standalone` 的启动汇编来自官方 CMSIS Device，调整了栈、堆和换行；来源与修改见 [startup_provenance.json](platforms/stm32/standalone/startup_provenance.json)。随附 [F1 许可](platforms/stm32/standalone/LICENSE_STM32F1.md)与 [F4 许可](platforms/stm32/standalone/LICENSE_STM32F4.md)均为 Apache-2.0。系统初始化代码引用 `deps/` 中的原组件。

CubeMX 初始化基底使用官方 [STM32CubeF1 v1.8.7](https://github.com/STMicroelectronics/STM32CubeF1/tree/v1.8.7) 和 [STM32CubeF4 v1.28.3](https://github.com/STMicroelectronics/STM32CubeF4/tree/v1.28.3) 所需 Drivers 子集；锁定提交与子模块提交见 [生成来源记录](platforms/stm32/cubemx/evidence/discovery.json)。该基底中的 HAL 为 BSD-3-Clause，CMSIS/Core/Device 为 Apache-2.0；各源码文件声明及 [F1 组件许可表](platforms/stm32/cubemx/ground_f103/LICENSE.md)、[F4 组件许可表](platforms/stm32/cubemx/ground_f407/LICENSE.md)适用于对应文件。许可表还列出上游包中未分发的组件，不表示这些中间件或 BSP 已包含在本仓库。

## Xilinx 构建证据

Zynq 适配依赖另行安装的 SDK 2017.4。仓库提供本项目的适配与生成脚本、硬件配置和构建证据，不分发整个 SDK。报告中部分 BSP 配置文件由 HSI 自动生成，保留其 Xilinx 原始许可头；该许可包含仅用于 Xilinx 器件或与其通过总线/互连交互的应用限制，不能概括成无限制 MIT 许可。具体原文例如 [生成 UART 配置](reports/zynq_sdk_validation/115200/bsp_xuartps_g.c)。

## 图纸与内部说明

原始私人分工说明、厂商 PDF、聊天附件和识别缓存不随 GitHub 仓库发布。其来源、哈希和获取方式见 [资料来源说明](docs/source/README.md)。本项目整理的引脚结论和软件测试记录不改变原厂文档的权利归属，也不代替实板验收。
