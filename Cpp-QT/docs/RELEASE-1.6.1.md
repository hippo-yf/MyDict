# 拾词 MyDict Qt 1.6.1

- 修复查词和今日词汇例句中英文、中文前竖线的位置不一致，保留语法配色。
- 去掉窗口外层白色留边和底部缩放条；Windows 四条边及四个角支持原生拖动缩放，适配高 DPI。
- 优先显示窗口，再加载附带字体、缓存内容和数据库，随后启动同步。
- 补充 Windows 100% / 150% DPI 的八方向缩放及延迟初始化检查。

提供 Windows x64、Linux x64、macOS arm64 / x64 程序包，包含 Qt 运行库及 SHA256，无需安装 Conda。Windows 解压运行 `bin/MyDictQt.exe`；Linux 运行顶层 `MyDictQt`；macOS 解压运行 `.app`（未经过 Apple 公证）。

升级前关闭程序并备份用户数据。保留个人 `settings.json`、字体、SQLite 词库和其他文件，不要用示例设置覆盖。程序版本为 `1.6.1-shared`，标签为 `qt-v1.6.1`；原 1.6 发布保持不变。
