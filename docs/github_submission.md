# GitHub 提交审查与复现

审查日期：2026-10-06。本仓库提交无人机通信子系统的软件基线，包含 F103、地面/机载 F407、Zynq 适配及 PC 测试。本轮从头复核的缺陷与分层结果见[全工程复查](deep_review_20261006.md)。完整视觉、飞控和最终实板作品另阶段验收。

## 公开文件范围

- 保留协议源码、测试、固件适配、Keil/CubeMX/SDK 工程和脚本、版本锁定的依赖、许可证及验证证据。
- 原始说明 PDF、板卡 PDF 和聊天截图不进入公开仓库。来源、哈希及公开官方入口见 [资料来源](source/README.md)。
- 私人微信路径、聊天原话和本机文档链接已从项目说明中移除。原始编译日志中的构建路径用于说明生成环境，队友复现时不应照搬路径。
- 构建产物、安装包、缓存和本地备份由根 `.gitignore` 排除。根 `.gitattributes` 保持文件原始字节，避免跨平台换行转换使证据哈希失效。
- 第三方许可见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。公开可读不等同于已为原创代码授予某一种开源许可证。

## 已执行的检查

1. 文档中的当前线路一致：F407 USART3 PB10/PB11、USART2 PA2/PA3，Zynq PS UART0 MIO14/15；旧 EMIO 文件有历史范围说明。
2. 在 C 测试中加入并执行标准 CRC 向量 `123456789 → 0x29B1` 的直接断言；同时保留 C/Python 黄金帧和随机交叉验证。
3. 重新运行 `tools/run_checks.py --platforms`，PC 用例、88 项 ARM 对象检查、Keil 工程、STM32 完整链接及 CubeMX 初始化基底重建通过。结果见 [verification.json](../reports/verification.json)。
4. 因握手源码修复，本轮重新运行 Vivado 2017.4 综合/HDF 导出及 SDK 2017.4 两档速率的 READY=0/READY=1 共四项链接，再核对当前源码、导入源码、依赖、ELF 符号及 88 个保存证据的哈希。保留 SDK 退出时的通道关闭日志和官方 BSP 警告；应用 stdio 由确定性无串口实现接管，不宣称工具日志零警告。
5. 对 Git 选中内容进行敏感凭据特征、私人聊天标识、文件大小和 Git 暂存字节一致性检查；对当前六类报告的源码哈希进行校验。特征扫描并不保证识别所有未知类型的秘密。

本轮 `drone_comm_deep_review_20261006.zip` 含 1379 个文件，在新目录重新执行完整编译和回归通过，源码一致性及主动修改源码后的旧证据拒绝检查通过，见 [复现报告](../reports/handoff_portability.json)。ZIP SHA-256 为 `307f2009f802d41e8b6ae224c032458ea9a41175470639d353312cd2961156e1`；该包的清单保存在 `reports/history/deep_review_20261006/package_manifest.json`。这一步重建 PC/HAL/Keil/CubeMX 基底，不重复 Vivado/SDK 生成；真实 Zynq 生成由本轮独立步骤完成并保存证据。

首轮候选的报告和清单保留在 `reports/history/github_candidate/`。根 `reports/package_manifest.json` 对应最终 Git 文件集合（不包含清单自身），会包含候选验证完成后新增的结果记录和说明；源码与被验证候选一致。历史 ZIP 的通过结论仅适用于其记录的源码，不能复用旧清单冒充新版本。

## 复现与持续集成

仅运行软件测试时，Python 3 与 GCC 足够：

```bash
python3 tools/run_checks.py --gcc /usr/bin/gcc
```

Windows 的完整适配/链接验证还需要 [工具链文档](toolchain.md) 中的 Keil 和 Xilinx 安装，按本机路径配置后执行：

```powershell
python tools/run_checks.py --platforms
```

GitHub Actions 的 `Native communication tests` 工作流仅执行 Linux PC 测试并上传当次报告；其结果应在 Actions 页面查看。它不会烧录开发板，也不覆盖需要本地商业工具链的编译。单独执行 PC 测试会把工作目录的 `reports/verification.json` 更新为仅 PC 的当次结果，这是运行记录，不能据此宣称所有平台已重新编译。

## 作品提交仍缺少的实测材料

尚未完成：实物接线/供电与波形核验、UART/E07 实板闭环、重启和断链恢复、距离与长时间测试；视频采集、YOLO/PL 视觉计算及飞控也未验收。遥测和视频状态仍是模拟值。三份完整通信工程默认 BOARD_READY=0，最终烧录包、FSBL/BOOT.bin 尚未作为作品固件交付。

四节点模拟验证节点协议与虚拟总线，射频寄存器、FIFO 和事务重试由独立 radio_mock 测试验证。两者都不能代替真实射频链路或开发板测试。
