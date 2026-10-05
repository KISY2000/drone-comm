# GitHub 提交审查与复现

审查日期：2026-10-06。本仓库提交无人机通信子系统的软件基线，包含 F103、地面/机载 F407、Zynq 适配及 PC 测试。完整视觉、飞控和最终实板作品另阶段验收。

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
4. 核对当前 Zynq HDF/BSP/ELF 报告的源码及 88 个保存的证据文件哈希。此轮未重复运行 Vivado/SDK 硬件生成，原证据与当前源码匹配。
5. 对 Git 选中内容进行敏感凭据特征、私人聊天标识、文件大小和 Git 暂存字节一致性检查；对当前六类报告的源码哈希进行校验。特征扫描并不保证识别所有未知类型的秘密。

公开候选 ZIP 解压后的完整重建、源码一致性和旧证据拒绝检查均通过，见 `reports/handoff_portability.json`。候选 ZIP 当时的清单另存于 `reports/history/github_candidate/package_manifest.json`；根 `reports/package_manifest.json` 更新为本次 Git 文件集合的校验清单（不包含清单自身）。候选包验证完成后只补充发布说明和报告，通信源码未再改变。后续修改或重新运行验证会改变报告；新的提交应重新生成对应清单，不能复用旧清单冒充当前结果。

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
