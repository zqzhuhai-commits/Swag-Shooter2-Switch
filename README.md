# SWAG Shooter 2 — Switch 移植研究

作者：Xiao Fan。基于 [Swag-Shooter-Switch](https://github.com/zqzhuhai-commits/Swag-Shooter-Switch) 评估第二部的兼容性。

## 当前状态

**已加入 0.0.1 ARMv7 执行核心测试程序，尚不是可玩游戏版，Switch 实机执行待验证。** 程序测试 ARM / Thumb 运算、未知 SVC 拒绝、执行预算限制，并从第二部真实运行库中查找、校验和执行一个不依赖初始化的纯函数。电脑端验证通过；Unity 初始化、完整动态链接与 Android ABI 仍未实现。

测试说明见 [0.0.1 测试指南](docs/test-0.0.1.md)，电脑验证记录见 [日志](audit/host-armv7-probe-0.0.1.txt)。

原移植包使用 Unity 2019.4.0f1、ARM64 运行库；第二部 APK 使用 Unity 2018.3.0f2，仅包含 ARMv7 和 x86 的 32 位运行库。原框架的 ELF 加载器仅接受 ELF64 / AArch64，因此不能直接加载本作运行库。

## 已验证的复用线索

在命名空间 `brijesh.CountryWar2` 中：

| 类 | 原游戏字段数 | 第二部字段数 | 同名字段数 |
|---|---:|---:|---:|
| PlayerManager | 113 | 112 | 111 |
| PlayerCarInteractionHandler | 8 | 10 | 8 |
| GameManager | 47 | 45 | 44 |

驾驶识别使用的 `_instance`、`_localPlayer`、`carInteractionHandler`、`drivingCar` 均在对应类中找到。以上仅验证字段名称，尚未验证字段类型、对象布局、方法签名或运行行为。

## 工具与复现

自行准备原始移植 ZIP 和第二部 APK，运行：

```sh
python3 tools/compare_swag_packages.py /path/to/original.zip /path/to/SwagShooter2.apk > comparison.json
```

工具使用 Python 标准库，只读检查压缩包。支持本次核实的 Unity 2018.3 / 2019.4 元数据 v24 布局；不执行游戏代码，也不生成游戏安装包。

本次结果见 [检查报告](audit/swag2-static-feasibility.json)。报告保留压缩包条目名，原 ZIP 的中文文件名编码可能显示为乱码，不影响 ELF 架构检测。

## 后续技术关卡

1. 获取本作 ARM64 构建，或实现 ARMv7 指令执行及跨架构 ABI 桥接。
2. 适配 Unity 2018.3 的 Android/JNI、线程、TLS、图形和音频接口。
3. 独立分析本作 IL2CPP GC；原游戏的固定地址和指令校验不可直接套用。
4. 验证初始化、主菜单、场景加载后，再校准手柄与触屏。

不能将原游戏 ARM64 `libil2cpp.so` 与第二部元数据、资源直接配对并据此声称移植完成。

仓库不包含 APK、游戏素材、游戏运行库、用户密钥或签名下载链接。
