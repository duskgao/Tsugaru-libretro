# FMTOWNS —— TOWNSEMU 的 libretro core 移植项目

本项目将 [TOWNSEMU](https://github.com/captainys/TOWNSEMU)（FM Towns / Marty 模拟器
"Tsugaru"，BSD 3-Clause）移植为 **libretro core**，使其可在 RetroArch 及任意 libretro
frontend 中运行。

本仓库基于 upstream `captainys/TOWNSEMU` 建立，`master` 分支保持与 upstream 同步，
全部移植改动集中在 `libretro` 分支的 `src/main_libretro/` 目录。

## 环境

- 模拟器：TOWNSEMU（Tsugaru），C/C++，BSD 3-Clause
- 移植目标：libretro API（`libretro.h`，MIT/RetroArch）
- 调试环境：`E:\RetroArch\RetroArch-Win64`（完整 RetroArch 调试安装）
  - `cores/`：核心 DLL 部署目录
  - `info/`：core `.info` 描述文件
  - `system/`：BIOS ROM 目录（已放置 `FMT_SYS.ROM` 等全套 ROM）
- 构建工具链：MSYS2 MinGW-w64 64-bit（`C:\msys64\mingw64\bin`）

## 移植结构

```
src/main_libretro/
├── libretro.cpp      # core 主体：LibRetro_World（Outside_World 实现）+ 全部 retro_* API
├── keymap.h          # RETROK_* → TOWNS_JISKEY_* 键盘映射
├── libretro.h        # 官方 libretro.h（MIT）
├── libretro.def      # Windows DLL 导出符号（23 个 retro_*）
├── CMakeLists.txt    # towns_libretro 库（MODULE/SHARED），仅链纯逻辑库，彻底自包含
├── build_libretro.sh # 跨平台一键构建脚本（MSYS2 / Linux / macOS）
├── towns_libretro.info
└── README.md         # 移植要点 / 构建 / BIOS / 内容 / 操作说明
```

接入点：`src/CMakeLists.txt` 末尾 `if(BUILD_LIBRETRO) add_subdirectory(main_libretro) endif()`。

## 移植设计要点

1. **单线程同步步进**：原 VM 用独立线程 + `sleep` 实时限速；本 core 改为每个
   `retro_run` 推进恰好一帧虚拟时间（`TOWNS_RENDERING_FREQUENCY ≈ 1/60s`），
   由 frontend 的 VSync 控制节奏，core 内部不 sleep。
2. **视频**：`retro_run` 内用自有 `TownsRender` 直接 `BuildImage + MoveImage` 同步抓帧，
   绕开原 VM 窗口线程与渲染锁，转 XRGB8888 投递。
3. **音频**：FM/PCM/CDDA 已在 `TownsSound::ProcessSound` 内混合成一路 int16 立体声
   @44100，经 `FMPCMPlay` 收集进环形缓冲，每帧 `retro_audio_sample_batch` 排空。
4. **输入**：键盘 `RETROK_*`→`TOWNS_JISKEY_*` 经 `PushFifo` 注入；手柄 `SetGamePadState`；
   鼠标 `SetMouseMotion` + `SetMouseButtonState`。
5. **SCSI/CD-ROM 后台 IO 线程改同步模式**：libretro 是单线程模型，TOWNSEMU 原本的
   `SCSIIOThread` / `AsyncWaveReader` 后台线程 + `std::mutex`/`condition_variable`
   在 RetroArch（多线程 GUI）环境下会死锁（RA 主线程卡在 `libwinpthread!WaitForSingleObjectEx`）。
   libretro 构建（`TOWNS_LIBRETRO_SYNC_SCSI` 宏）下禁用后台线程，IO 在调用线程内同步执行。
6. **`retro_deinit` 主动 `StopIOThread()`**：在 CRT 健康（DLL 尚未卸载）时停止 SCSI 线程并 join，
   否则 DLL 卸载时全局 `towns` 对象析构触发 `SCSIIOThread::~join`，在 PROCESS_DETACH 阶段
   CRT 已关闭 → join 永久卡死。
7. **导出 `retro_get_memory_data` / `retro_get_memory_size`**：本 core 不暴露可作弊内存，
   返回 NULL/0。必须导出，否则 RetroArch 1.22 的 `runloop_init_libretro_symbols()`
   因缺符号报严重错误 → 加载内容（ISO）时崩溃。
8. **`retro_load_game(NULL)` 返回 true**：电脑模拟器声明支持无内容运行后，RetroArch 会
   调用 `retro_load_game(NULL)`，必须接受并返回 true（以默认参数启动 VM 到 BIOS），
   否则 RA 认为加载失败。
9. **电脑模拟器声明支持无内容运行**：`retro_set_environment` 内**第一个、无条件**调用
   `RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME`，否则 RetroArch「加载核心」卡死。
10. **winpthread 静态链接（单文件自包含）**：导入表为 `KERNEL32.dll` / `msvcrt.dll` /
    `WS2_32.dll`（全部 Windows 系统 DLL），**不依赖任何第三方 DLL**，单文件 core 即可运行。
    通过 `-static -static-libgcc -static-libstdc++` 把 libgcc/libstdc++/winpthread 全部
    静态链入。
    历史警示：早期在 SCSI/CD-ROM 仍用【异步后台线程】时，静态 winpthread 曾导致
    RetroArch 加载死锁（静态 pthread 在复杂多线程宿主下破坏 `std::thread`/`std::mutex`
    行为）。**本 core 已用 `TOWNS_LIBRETRO_SYNC_SCSI` 将 SCSI/CD-ROM 改为同步单线程，
    且不启用网络线程，运行时不再真正创建线程**，因此静态链入安全，且彻底消除了
    对 `libwinpthread-1.dll` 的外部依赖（不再需要覆盖 RA 根目录旧版 DLL）。

## 构建

### Windows（MSYS2 MinGW-w64）

在 MSYS2 MinGW64 终端：

```bash
cd /e/FMTOWNS/src/main_libretro
export PATH=/c/msys64/mingw64/bin:/usr/bin:$PATH
sh build_libretro.sh
```

产物：`src/build_libretro/main_libretro/towns_libretro.dll`

或直接用项目根部的 PowerShell 一键脚本（自动调用 MSYS2 bash + 部署到 RetroArch）：

```powershell
powershell -File E:\FMTOWNS\build_libretro_win.ps1
```

### Linux / macOS

```bash
cd TOWNSEMU/src/main_libretro
sh build_libretro.sh   # 内部自动加 -DCMAKE_POSITION_INDEPENDENT_CODE=ON
```

## 部署到 RetroArch

1. **彻底退出 RetroArch 进程**（RetroArch 会锁死 `cores/*.dll`，cp 静默失败）。
2. 将 `towns_libretro.dll` 复制到 `cores/`。
3. 将 `towns_libretro.info` 复制到 `info/`（核心描述，供内容关联与显示）。
4. 将 BIOS ROM 放入 `system/`（至少需要 `FMT_SYS.ROM`）。
5. 启动 RetroArch → Load Content 选择 `.cue` / `.d77` / `.hdd` 镜像。

## BIOS

至少需要 `FMT_SYS.ROM`（其余 `FMT_DOS.ROM` / `FMT_FNT.ROM` / `FMT_F20.ROM` /
`FMT_DIC.ROM` 可选），放入 RetroArch `system/` 目录。也可用合并文件 `FMT_ALL.ROM`。
BIOS 来自真实硬件或合法备份，本仓库不附带。CMOS 存到存档目录 `towns_cmos.bin`。

## 已知问题

### CD 启动（中断模式）

- 曾因 CPU 中断/IF/halt 时序在单线程 StepFrame 下与真实不一致（IRET 恢复 IF 被 gcc -O3
  优化忽略），导致 CD 启动盘卡在「システム読み込み中です」。已通过扩展 `ConsumeVariable`
  保护到所有平台修复，CD 游戏（如 Amaranth 3）可正常进入游戏画面。

### 鼠标

- **Towns OS 桌面 / TBIOS 游戏**：同时喂两条路径——绝对 `ControlMouse(hostX,hostY)`
  （官方物理鼠标路径，桌面/系统鼠标依赖）与相对 `SetMouseMotion(1,dx,dy)`（官方
  `MOUSE_BY_KEY` 同款，Amaranth 等已验有效）。后者在 `ControlMouse` 因
  `GetMouseCoordinate` 失败时兜底。
- **键盘 / 手柄模拟鼠标**：方向键/左摇杆移动指针（空格/回车/手柄A 点击），方向符号与
  官方 `MOUSE_BY_KEY` 一致（+X=左、+Y=上）。即使 RA 无法给 Port1 配 Mouse 设备类型也可用。
- **桌面鼠标（Towns OS 桌面）**：桌面不从 gameport 消费鼠标 motion/按钮
  （诊断证实：`SetMouseMotion` 写入的 gameport motion 桌面不读，`gmcXY` 纹丝不动）。
  因此新增 `FMTownsCommon::SetMouseAbsPosition(x,y,lBtn,rBtn,tbiosid)` **直接写 TBIOS
  鼠标结构体**：坐标写到 `TBIOS_physicalAddr + TBIOS_mouseInfoOffset + 0x0C/+0x0E`，
  按钮字节写到 `+0x1C`（bit0=左、bit1=右）。桌面 V2.1(TBIOS_V31L35) 已验证移动与点击
  均生效。`g_mouseAbs` 采用"当前坐标+方向偏移"模式（读 `GetMouseCoordinate` 校准，
  方向键从当前位置小步移动，不累积漂移）。游戏（如 Amaranth）走 gameport 路径。

### 性能与硬件选项

- **CPU 频率 `towns_cpu_freq`**（默认 40MHz）：数值越低 → 每帧指令越少 → 模拟越快、但
  依赖硬件时序的老游戏可能行为异常；越高 → 越贴近真实时序但计算量大。**运行时变更立即
  生效**（`ReadAndApplyCpuFreq` 每帧检查并应用 `currentFreq`/`fastModeFreq`），无需重新
  Load Content。
- 其余硬件选项（与官方 CLI 对应，在 `retro_load_game` 写入 `TownsStartParameters`）：
  - `towns_use_fpu`：FPU（默认 enabled）
  - `towns_mem_size`：主内存 2/4/8/16/32/64MB（默认 4）
  - `towns_pretend_386dx`：报告为 386DX（默认 disabled）
  - `towns_midi_cards`：MIDI 卡数量（默认 0）
  - `towns_highres`：高分辨率 CRTC（默认 enabled）
  - `towns_highres_pcm`：高分辨率 PCM（默认 enabled）
  - `towns_cd_speed`：CD 速度 default/1/2/4/8（默认 default）
  - `towns_boot_fast`：启动到 FAST 模式（默认 enabled）
- 注意：CPU 保真度（default/high）是**编译期模板参数**（core 固定用 medium fidelity，
  `FMTownsWithMediumFidelityCPU`），无法作为运行期选项切换。

## 调试

若「加载核心」卡死 / 黑屏 / 无声，见 `src/main_libretro/README.md` 与 `libretro.cpp`
中的 `TxtTrace` / `Dbg` 插桩（双通道：RA 日志接口 + `C:\Users\gs\towns_trace.log`）。
二分定位 RA 调用到哪个 `retro_*` 网关节点的顺序，再部署硬校验（确认 DLL 未被锁）。
