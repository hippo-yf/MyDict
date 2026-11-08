# 拾词 MyDict — C++ / Qt

本仓库仅维护 **Qt 版本**，支持 Windows、macOS 和 Linux。当前开发分支为 `codex/qt-cross-platform`，作者 **Yance Feng & Codex**，应用源码使用 MIT 许可。

- [Qt 使用、配置和编译说明](Cpp-QT/README.md)
- [Qt 1.7.0 下载](https://github.com/hippo-yf/MyDict/releases/tag/qt-v1.7.0)
- [共享 SQLite 词库与 Gitee 同步](Cpp-QT/docs/SHARED_WORDBOOK.md)
- [三平台自动构建](.github/workflows/qt-build.yml)
- [Qt 及第三方许可](Cpp-QT/QT-NOTICES.md)

## 功能

中英查词、DeepSeek 解释、逐义双语例句与语法样式、收藏与分页、两种加权复习、今日词汇、桌面鹦鹉动画、设置热重载和 Gitee 同步。

程序使用同目录设置和按 username 隔离的 `profiles/<username>/words-shared.sqlite`。缓存格式继续兼容已有公共词库；移除旧客户端源码不改变个人数据或同步格式。

## 开发

源码、CMake、Conda 环境和测试都位于 `Cpp-QT/`。Windows 编译入口为 [build-qt.ps1](Cpp-QT/build-qt.ps1)，跨平台环境定义为 [environment.yml](Cpp-QT/environment.yml)。自动构建仅编译、测试和打包 Qt。

C# 和 Windows 原生 C++ 客户端已停止维护并从当前源码树移除。旧发布标签和历史提交保留供追溯，不属于当前开发分支。个人设置、词库、密钥和本机额外文件不提交到仓库。
