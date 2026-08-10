# 提交核心到 libretro-super（官方发布）

> 目标：让 FM Towns 核心进入 libretro 官方 buildbot，使所有平台的 RetroArch
> 都能一键下载、自动更新。本文件说明完整流程与所需材料。

## 0. 先决条件（务必确认）

向 libretro 官方提 PR 前，核心必须先满足官方规范，否则会被拒：

- [x] `.info` 文件齐全（`src/main_libretro/towns_libretro.info`）
- [x] 声明支持无内容运行（`supports_no_game = true`，代码里第一个调用
      `RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME`）
- [x] 单线程同步步进、无内部 sleep
- [x] 声明需要完整路径（`needs_fullpath = true`）
- [x] 不依赖第三方 DLL / 运行时（Windows 只依赖系统库）
- [x] 有标准 `Makefile` 入口（`src/main_libretro/Makefile`，包装 CMake 构建）

> ⚠️ 现实提醒：libretro 官方对**首次提交的核心**审查较严，通常要求核心已
> 稳定运行一段时间、功能基本完整。刚发布的 v20260522 建议先在社区/自行
> 充分测试，再提交官方，成功率更高。

## 1. 需要的材料（已备好在 `libretro-super/` 目录）

| 文件 | 用途 |
|------|------|
| `libretro-super/tsugaru.libretro` | 构建配方（recipe）：核心名、仓库地址、构建方式 |
| `libretro-super/tsugaru.info` | 核心元数据（供 buildbot 生成下载信息） |

两个文件内容与 `src/main_libretro/` 下对应文件一致，只是核心名规范化为
`tsugaru`（产物 `tsugaru_libretro`）。

## 2. 提交流程（照做）

### 2.1 fork libretro-super

打开 `https://github.com/libretro/libretro-super`，点右上角 **Fork**，得到
`你的账号/libretro-super`。

### 2.2 克隆并添加 recipe

在 PowerShell：

```powershell
git clone https://github.com/你的账号/libretro-super.git
cd libretro-super

# 新建 recipes 目录（如果还不存在）
# 把两个文件复制进去
copy E:\FMTOWNS\libretro-super\tsugaru.libretro recipes\tsugaru.libretro
copy E:\FMTOWNS\libretro-super\tsugaru.info   recipes\tsugaru.info

git add recipes\tsugaru.libretro recipes\tsugaru.info
git commit -m "Add Tsugaru (FM Towns) core"
git push origin master
```

### 2.3 提交 PR

1. 回到 GitHub，在 `libretro/libretro-super` 页面点 **New pull request**
2. 选择 compare 你的 fork 分支
3. 标题：`Add Tsugaru (FM Towns) core`
4. 说明里写清：核心基于 TOWNSEMU、功能、BIOS 需求、测试情况
5. 提交 PR，等待官方维护者 review

## 3. 常见疑问

**Q：为什么还需要 `Makefile`？**
A：libretro-super 的 buildbot 通过 `make -f Makefile` 构建核心。我们的
`src/main_libretro/Makefile` 是标准入口，内部转调 CMake（`build_libretro.sh`）。

**Q：核心名用 `towns` 还是 `tsugaru`？**
A：建议 `tsugaru`（与 info 的 `corename = "Tsugaru"` 一致，可读性好）。
最终产物名是 `tsugaru_libretro.so` / `.dll`。

**Q：官方审核要多久？**
A：视维护者忙闲，几周到几个月不等。如果核心有明显问题会被打回修改。

**Q：被拒了怎么办？**
A：根据 review 意见修改后重新提交即可。PR 是公开可迭代的。

## 4. 提交前自检清单

- [ ] DLL 能在一台"干净"的 Windows（无额外运行时）上直接运行
- [ ] Towns OS 桌面、CD 游戏、软盘镜像都能加载
- [ ] 键盘/手柄/鼠标输入正常
- [ ] 长时间运行不崩溃、不卡死
- [ ] `towns_libretro.info` 与 `tsugaru.info` 内容一致
- [ ] README 完整（说明 BIOS、加载方式、操作、选项）
