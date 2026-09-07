# BasicSanguosha

一个使用 C++17、Qt 6 Widgets 与 CMake 开发的 Windows 卡牌游戏。目前处于 **Pre-v1.0 / Friend Testing** 阶段，当前项目进度为 **Stage 14A-R2**。

当前主要功能：

- 2～4 人自由混战（FFA）
- 5～8 人身份模式（Identity）
- 本地 AI
- 局域网联机
- 断线重连
- Qt 6 Windows 客户端
- 复杂选牌与多目标交互浮层

局域网功能仍处于好友测试阶段。当前版本不是 v1.0，也不包含尚未进入 Stage 15 的扩展内容。

在 Windows / Qt 6 环境中构建：

```powershell
cmake -S . -B build-vs2022 -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build build-vs2022 --config Release
ctest --test-dir build-vs2022 -C Release --output-on-failure
cmake --build build-vs2022 --config Release --target deploy
```

部署后的运行目录为 `dist/BasicSanguosha/`。部署目标会从当前 CMake 已配置的 `Qt6::qmake` 推导同一 Qt 安装目录中的 `windeployqt.exe`。对外发布时还应确认 MSVC Runtime 已随便携包提供。
