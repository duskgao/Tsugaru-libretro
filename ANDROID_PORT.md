# Android 移植研究报告

> 结论先行：把 Tsugaru libretro core 交叉编译到 Android 的**平台障碍很低**。
> 最大障碍（SCSI/CD 后台线程、网络线程、音频后端、字节序）已经被
> `TOWNS_LIBRETRO_SYNC_SCSI` 宏 + libretro 单线程驱动模型 + 纯内存音频路径
> **基本消除**。剩余工作约 80% 在 CMake/NDK 构建脚本适配，约 20% 是零星
> 的 Android deprecation 警告清理。**基本不需要改 C++ 源码。**

## 一、为什么能移植（四大障碍已被消除）

| 障碍 | 现状 | 结论 |
|------|------|------|
| SCSI/CD 后台线程 | `TOWNS_LIBRETRO_SYNC_SCSI` 在 3 处 CMakeLists + scsi.cpp/cdrom.cpp 完整实现同步模式 | ✅ 无线程/锁/死锁 |
| 网络线程 | libretro 不启用 LAN，`RealNetwork::Start()` 不会被调用 | ✅ 无网络线程 |
| 音频平台后端 | libretro 用纯内存 `LibRetro_Sound` + `yssimplesound_nownd` 占位 | ✅ 无平台音频依赖 |
| 字节序 | 无条件 `YS_LITTLE_ENDIAN`，Android ARM/x86 都是小端 | ✅ 完全兼容 |

## 二、平台 API 依赖评估

### 网络（WinSock？）
**跨平台，不用 WinSock**。`yssocket` 库有完整 `#ifdef _WIN32 / #else` 分支：
非 Windows 用 BSD socket + `poll()`/`close()`。`real_network.cpp` 同理有
POSIX 分支。Android Bionic libc 完整支持，**无需 ws2_32**。

### 非网络 Windows API
极少且都有非 Windows 分支：`towns.cpp` 的 `getcwd`（POSIX 版）、
`real_network.h` 的 `#define closesocket close`。没有
`GetTickCount/QueryPerformanceCounter/CreateThread/__declspec/注册表` 依赖。

### 计时/时钟
`std::chrono` + `std::this_thread::sleep_for`，纯 C++ 标准库。libretro 核心
甚至不用 `townsthread.cpp`（UI 线程），直接由前端 `retro_run` VSync 驱动。

## 三、现有构建系统的适配点（主要工作量）

现有 `src/main_libretro/CMakeLists.txt` 已有**非 Windows else 分支**（链接
`towns osinit outside_world yssimplesound filesys`，产物 `.so`）。适配 Android
需要：

| 优先级 | 位置 | 需要做的 |
|--------|------|---------|
| P0 | `main_libretro/CMakeLists.txt:56-60` | UNIX 分支显式 `-lpthread` 需排除 Android（NDK libc++ 自带 pthread） |
| P0 | 整体构建 | 写 Android NDK toolchain 支持（`-DANDROID_ABI`、`.so` 输出、libretro 符号导出） |
| P1 | `yssocket.cpp` | 非 Windows 的 `bcopy()` 在 Bionic 已 deprecated，建议换 `memcpy` |
| P2 | `yssimplesound` | UNIX 分支 `find_library(asound)` 在 Android 找不到会打 warning（回退 nownd 正确），可加 Android 分支直接 nownd |
| P3 | 符号导出 | Windows 用 `libretro.def`；Android `.so` 需默认导出 `retro_*` 或 version-script |

## 四、需要的工具（本机目前缺失）

- **Android SDK + NDK**：交叉编译必须。推荐 NDK r25+（自带 CMake toolchain）。
  Android Studio 或命令行安装均可。
- **CMake**（已有，MinGW 自带）
- 构建产物：`towns_libretro.so`（arm64-v8a / x86_64 各一个 ABI）

## 五、下一步（等你决定是否推进）

1. **安装 Android NDK**（约 1-2 GB 下载，需你确认）：
   - 官方：https://developer.android.com/ndk/downloads
   - 或 Android Studio → SDK Manager → NDK
2. 我写一个 `build_libretro_android.ps1`（或 `.sh`），用 NDK 的 CMake
   toolchain 交叉编译 `arm64-v8a`（手机）和 `x86_64`（模拟器）两个 ABI。
3. 小改：`yssocket.cpp` 的 `bcopy→memcpy`；CMake 排除 Android 的 `-lpthread`。
4. 产出 `.so` 放到 RetroArch Android 版 `cores/` 验证。

> 说明：Android 上 FM Towns 的 CD 镜像（cue/bin）放手机存储，
> BIOS 放 `system/`，操作同上。
