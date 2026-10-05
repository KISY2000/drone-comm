# 原始资料来源与校验

GitHub 仓库不发布原始私人分工说明、厂商 PDF、聊天附件或 OCR 中间文件。原件保持不变，仍保存在本地与受限协作空间。这里提供资料名称、公开获取入口与原件 SHA-256，便于审查者独立取得资料后确认版本。

厂商入口是获取资料的起点，不保证目前下载版本与本项目核查过的旧版本相同；版本不同应重新核对针序、器件和电压。不要把网盘目录名称或官方默认装配直接当作实物已经验收。

| 原件 | 来源与获取方式 | SHA-256 |
| --- | --- | --- |
| 无人机系统_三核通信与软件分工说明.pdf | 项目内部需求资料，无公开下载地址；技术接口已整理到[协议](../protocol.md)和[板级配置](../board_config.md) | `94467E6B2FF8F9D881937D5BBE80F887FD4639B1536BABFFF1217BB173A5707B` |
| EXPLORER_V3.4.pdf | [正点原子官网](https://www.alientek.com/)，查找探索者 STM32F4 开发板对应资料盘及 V3.4 原理图 | `282206B31B7072204A5A105A289A41D5D4CA11A61664305AE6C48F56040BE9D6` |
| ZYNQ_CORE_2V5_user.pdf | [正点原子官网](https://www.alientek.com/)，查找领航者 V2 ZYNQ 资料盘中的 7010/7020 核心板 2V5 原理图 | `1C8CF8105E91D762D29AD604E82B629B3921FA474D5A0311ED6C04237DB107EA` |

领航者规格书 V1.2、FPGA 开发指南 V3.4、底板和核心改版说明、V3.9 结构装配图的版本、页码及原件哈希见[7020 厂商资料核对](../zynq_7020_vendor_lookup.md)。主目录与旧目录底板原理图的核查范围见[领航者底板证据](../zynq_navigator_v2_evidence.md)。这些文件同样从厂商资料入口获取，没有随 GitHub 仓库复制。

无线配置的公开依据为 [E07-400M10S 官方规格](https://www.cdebyte.com/products/E07-400M10S/1)及 [TI CC1101 数据手册](https://www.ti.com/lit/ds/symlink/cc1101.pdf)。实现所用寄存器和期限见[E07 配置](../radio_profile.md)。

[source_manifest.json](source_manifest.json)保留首批三份原件的文件名、字节数和哈希；`repository_included=false` 表示该原件不在 GitHub。历史软件审查 ZIP 曾包含原件，其清单和校验值只适用于该 ZIP，不能当作当前 GitHub 文件清单。
