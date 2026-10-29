# 0.0.1 ARMv7 执行核心测试

这是移植过程的底层测试程序，**不是可玩游戏版**。不会进入主菜单，不初始化 Unity，不修改原游戏存档，也没有生成 NSP。

## 本版实际执行什么

1. ARM 指令运算，预期返回 12。
2. Thumb 运算及 Thumb → ARM 返回，预期返回 12。
3. 未实现的 SVC 必须停止，不能伪造成功。
4. 无限循环必须在执行预算耗尽后停止。
5. 从你提供的第二部 ARMv7 `libil2cpp.so` 查找 `il2cpp_is_debugger_attached`，校验它只经过无条件分支到 `mov r0, #0; bx lr` 后，再通过转译器执行这个真实导出函数。

第 5 项只证明指定的几条游戏指令能执行，**不证明 IL2CPP 已初始化、其他函数可执行或整个游戏可运行**。程序有意不执行构造函数、不做完整重定位，也不连接 Android 接口。

## 在 Windows 准备

下载并解压 `swag2-armv7-test-0.0.1.zip`。压缩包自带 NRO、准备脚本和许可证，不含游戏库。

在解压目录打开 PowerShell，执行（把 APK 路径替换成你的实际路径）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\prepare_test_data.ps1 -Apk "D:\Downloads\SwagShooter2-1.1-SingleAPK.apk"
```

这会把所需的 32 位库解压到本目录的 `switch/swag2-test/libil2cpp.so`。无需安装 Python。也可以用 Python：

```sh
python tools/prepare_test_data.py SwagShooter2-1.1-SingleAPK.apk switch/swag2-test
```

然后把整个 `switch` 文件夹合并复制到 Switch 内存卡根目录。最终应有：

- `/switch/swag2-test/swag2-armv7-test.nro`
- `/switch/swag2-test/libil2cpp.so`

不要把第一部的 ARM64 库放在这里。已验证的第二部库 SHA-256 为 `0b2828dd3417cf722959c039d0539ee62a564ef0ba0d59028c92497718fadfd4`。

## 在 Switch 测试

使用 Atmosphere，按住 R 打开一个已安装游戏进入完整内存模式 hbmenu，再运行测试 NRO。不要用相册小程序模式。

测试结束后屏幕显示 PASS 或 FAIL/BLOCKED，按 `+` 退出。日志写入 `/switch/swag2-armv7-test.log`，每次运行会覆盖这份测试日志。

请反馈屏幕结果和这份日志。如果启动即退出、来不及写日志，反馈错误画面即可；这种情况可能位于 JIT 内存初始化，不能根据电脑端通过就认定 Switch 端通过。

## 验证边界

- 电脑 x86-64 主机：已用同一 ARM/Thumb 前端执行五项测试并通过。
- 缺失库、损坏 ELF、改变纯函数代码：均被拒绝，见 `audit/`。
- Switch：devkitA64 + libnx 交叉构建成功；NRO 头及段范围检查通过。
- **Switch 实机执行：尚未验证。**

下一阶段仍需完整 ELF32 动态链接、Android/Bionic ABI、线程/TLS、JNI、EGL/GLES、音频及 Unity/GC 适配。这些没有在本版实现。

## 从源码构建

依赖 devkitA64、libnx、switch-tools、CMake、Ninja、Boost 头文件。先取得固定版本依赖并应用补丁：

```sh
sh tools/fetch_dynarmic.sh
export DEVKITPRO=/opt/devkitpro
cmake -S . -B build-switch -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/switch.cmake" \
  -DDYNARMIC_SOURCE="$PWD/_deps/dynarmic" \
  -DBOOST_ROOT=/path/to/boost \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-switch --parallel 3
```

电脑验证可省略 `CMAKE_TOOLCHAIN_FILE`，然后运行输出的 `swag2-armv7-test /path/to/ARMv7/libil2cpp.so`。

Dynarmic 固定版本：`a41c380246d3d9f9874f0f792d234dc0cc17c180`。Switch 补丁使用 libnx JIT 的独立读写/执行映射；开启 W^X 转换，不直接申请 Linux 可执行内存。未使用 ZettaBridge 的代码。
