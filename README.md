# 拾词 MyDict

C# / WPF 中英词典，使用 VS2022 和 .NET Framework 4.8 编译。当前固定版本为 **1.5.1**（Git 标签 `v1.5.1`），程序输出 `bin/1.5/MyDict.exe`，明确使用 x64；需要 Windows 10/11 64 位（使用系统 SQLite）。1.3、1.4、1.5.0 固定版本保持独立。

源码：[Gitee hippo-yf/MyDict](https://gitee.com/hippo-yf/MyDict)。本地便携包为 `release/MyDict-v1.5.1-win-x64.zip`，校验值位于 `release/SHA256SUMS.txt`。

## 查词与单词本

- 输入停顿后查询普通在线来源；DeepSeek 使用鲸鱼开关启用，点击查询、输入框回车或点击释义中的其他英文单词触发查询。开关显示短提示；已有 DeepSeek 释义时，再次联网查询前需要确认。
- 查询按钮重新获取结果；历史箭头或 Alt+左右键切换查词记录。
- 音标、词形、逐义中英文释义、双语例句、语法着色和搭配；摘要优先使用已有 DeepSeek 结果。
- 单词本摘要右侧显示来源图标：DeepSeek 独立图标，普通词典和翻译网站合为一个图标。
- 单词本每页读取 50 条摘要，可筛选、只看收藏、翻页和切换收藏；点击词条才读取完整释义。查词页星标也可取消收藏；单词本行末 X 只允许移除未收藏词。
- Ctrl+加号/减号调整内容及单词本字号，Ctrl+[ / ] 调整行距。

## SQLite 存储与加载

`settings.json` 仍位于 EXE 旁，保存功能、样式、窗口位置和大小。用户内容统一保存在：

```text
profiles/<username>/words.sqlite
```

一个数据库包含单词、收藏状态和修改时间、每个来源的独立释义、查词历史及导入错误。`words` 表管理同一词的身份和收藏，`sources` 表保存多个来源；完整词条使用压缩的二进制载荷存储，不使用 JSON 缓存。SQLite 的 WAL/SHM 文件是临时事务文件，不是额外的收藏或词典副本。

窗口先显示，再后台打开数据库和迁移。列表分页只查询摘要及来源元数据，不解码全部词条；内存只保留当前页和当前选中词的内容。筛选有短暂防抖，数据库读写、导出、迁移和同步都在后台执行。查词及收藏通过小事务立即提交，不再等待 30 分钟重写整份词库，关闭窗口也不扫描全部词汇或等待网络同步。

旧版 `lookup-cache.json`、`favorites.json` 在后台逐条迁移，包含收藏删除记录、查询时间和来源。迁移在事务中完成，成功标记只在提交时写入；出错回滚，原 JSON 保留为备份。迁移后再次启动只检查标记，不再加载整个旧词库。

可在关闭程序后手动维护和检查：

```powershell
.\tools\Migrate-Cache.ps1
```

## 跨设备同步

`CacheSync` 配置 Gitee SSH 仓库、密钥路径和设备标识；用户使用固定 `username`，不同用户隔离。自动同步按首次变更起约 30 分钟批量进行，顶部同步按钮可立即触发。网络失败不影响已提交的本地数据库，可稍后重试。

新版本在 Gitee 只使用每设备 `.sqlite` 快照，按来源查询时间合并词义，按收藏修改时间合并收藏和取消收藏记录；常规同步不再读取或生成 JSON 快照。快照备份和合并在后台完成，不把整份词库加载到窗口内存。需要安装 Git 并配置好 SSH；程序不会上传 `settings.json` 或 API Key。不同设备应升级为支持 SQLite 的同一版本后使用同步。

## DeepSeek 与其他在线来源

在 EXE 同目录 `settings.json` 的 `DeepSeek.ApiKey` 填写密钥。API 地址、模型、超时和输出 token 上限均可配置。无效或空 JSON 最多自动纠正一次；失败保留旧结果。API 查询使用账户额度。普通来源包括 MyMemory / LibreTranslate、Free Dictionary API 和 Wiktionary。

所有配置字段和分组 Styles 带中文说明；修改设置后自动重载。个人设置、密钥、缓存、收藏和 `imex/` 不应提交到公开仓库。

## 导入旧单词本

先关闭拾词，在项目目录执行：

```powershell
# 仅预览，不调用 API
.\tools\Import-Wordbook.ps1 -DryRun
# 先导入 5 个词
.\tools\Import-Wordbook.ps1 -Limit 5
# 4 个并发查询，默认所有请求共享 1.5 秒启动间隔
.\tools\Import-Wordbook.ps1 -Concurrency 4
# 自定义间隔
.\tools\Import-Wordbook.ps1 -Concurrency 4 -DelaySeconds 0.5
```

默认输入为 `imex/1.txt`，自动提取编号词头并去重。`-Concurrency` 范围 1–32，默认 1；`-ForceRefresh` 强制重新查询，`-InputFile`、`-SettingsPath` 指定其他输入或配置。已有 DeepSeek 结果默认复用，所有导入词标记为收藏；保留已有收藏，不改变查词历史位置。

每个成功结果直接写入 SQLite 事务，中断后重新执行即可续传，不再写 JSONL 结果缓存。旧 JSONL 断点会一次性恢复并留作备份。失败记录位于数据库 `import_errors` 表；401/402/403 或连续失败会停止安排新词，已发出的查询完成后保存成功结果。导入不主动上传，下次由应用按同步设置处理。

## 编译、测试、打包

VS2022 安装“.NET 桌面开发”和 .NET Framework 4.8 开发工具；打开解决方案，选择 Release / x64。

```powershell
.\build.ps1
.\tools\Test-Release.ps1
.\tools\Package-Release.ps1
```

回归测试使用模拟 API 和本地 Git 仓库，不消耗付费 API。显式执行 `tools/Test-DeepSeekLive.ps1` 才调用真实 DeepSeek 服务。发布脚本只复制程序、许可和示例配置，不包含个人数据库和密钥。

## 许可

源码采用 [MIT](LICENSE)。在线词典内容遵循各自许可，SQLite 使用 Windows 提供的运行库。详见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。

## 随程序字体

程序优先从 EXE 同目录 `fonts/`（含子目录）按字体族名称加载 `.ttf`、`.otf`、`.ttc`；未找到时自动使用同名系统字体。向该目录添加字体后重新启动即可，无需安装到 Windows。样式中的字体仍按原来的 `font.*` 字段配置。

仓库和发布包包含 CaskaydiaCove NFP 的常规、粗体、斜体和粗斜体以及许可文本。本机使用的 Windows 字体位于 `fonts/local/`，只用于本地构建，不提交到仓库或公开发布包；公开版本缺少这些文件时使用系统字体。
