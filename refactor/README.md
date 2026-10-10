# 去重重构件（refactor/）

2026-10-11 生成。本目录是按“重新审查代码、去掉重复实现”的要求产出的独立重构副本，
**工程原有源码、测试与构建入口未做任何修改**（见下方“原工程未改动”证据）。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| `src/` | 去重后的实现源码；文件与公开接口沿用原文件名，行为保持不变 |
| `tests/` | 原 `tests/*.cpp` 的逐字节副本（21 个），仅供本目录独立构建与验证 |
| `tools/` | 构建用 `environment_probe.cpp` 逐字节副本，以及审查与校验脚本 |
| `CMakeLists.txt` | 本目录独立构建入口；目标名与测试名与根工程一致 |
| `build.ps1` | 独立构建脚本（工具链参数与 `scripts/build.ps1` 相同，只改 `-S` 指向） |
| `docs/dedup-report.md` | 重复实现审查报告：证据、处理方式、残留项与验证结果 |
| `docs/harness-sha256.txt` | 测试/探针副本的 SHA256 清单 |

`src/` 新增 4 个内部共用头文件：`numericutil.h`、`partvalidation.h`、`jsonutil.h`、
`sqliteconnection.h`；它们只服务于去重，不扩展业务功能。

## 构建与验证

```powershell
# 方式一：一键配置 + 构建 + 部署 + 全量 CTest（约 1 分钟）
.\refactor\build.ps1

# 方式二：手动执行
cmake --fresh -S refactor -B build-refactor -G Ninja `
  "-DCMAKE_MAKE_PROGRAM=D:\Qt\Tools\Ninja\ninja.exe" `
  "-DCMAKE_CXX_COMPILER=D:\Qt\Tools\mingw1310_64\bin\g++.exe" `
  "-DCMAKE_PREFIX_PATH=D:\Qt\6.11.1\mingw_64" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-refactor --parallel 4
ctest --test-dir build-refactor --output-on-failure
```

CAD 相关用例需要显式运行库路径：把 `.runtime/cad-distribution/cad-runtime` 复制到
`build-refactor/bin/cad-runtime`（`build.ps1` 用 `scripts/sync-cad-runtime.ps1` 完成），
或设置 `EQUIPMENT_CAD_PYTHON` / `EQUIPMENT_CAD_MODULES` / `EQUIPMENT_CAD_CONVERTER`。

原生图形验收（真实窗口与 FBO，需桌面会话）：

```powershell
build-refactor\bin\r11_desktop_tester.exe -platform windows --snapshot-dir build-refactor\r11-native
```

## 本次实际验证结果（2026-10-11）

- 编译：23 个目标全部通过，`-Wall -Wextra -Wpedantic` 零警告零错误（`build-refactor/build-log-clean.txt`）。
- 自动检查：**18/18 CTest 通过**（首次 31.09 秒、最终复验 27.78 秒、一键脚本含 CAD 运行库同步 30.25 秒），
  与原实现同门槛。
- 一键入口：`.\refactor\build.ps1` 实际跑通（重新配置 → 编译 → 部署 Qt → `scripts/sync-cad-runtime.ps1`
  同步 `build-refactor/bin/cad-runtime` → CTest 18/18），不依赖外部环境变量。
- 原生图形：`r11_desktop_tester` 原生通过（`accepted=1`，实测 `samples=4`、超采样 `1.5x`、
  坐标轴/受限网格/四视图/鼠标契约全部接受，退出码 0）。
- 测试未被削弱：`refactor/tests`、`refactor/tools/environment_probe.cpp` 与根目录同名文件
  逐字节一致（`.\refactor\tools\verify-harness-copy.ps1` → `checked=22 mismatches=0`）。
- 重复度：`sceneview.cpp` 内 ≥80 词的重复实现片段由 7 组降为 0 组；
  全 `src` ≥60 词的报告组数由 60 组降到 50 组（残留均为声明列表形态相似，见报告）。

## 原工程未改动证据

```powershell
git -c safe.directory="D:/作业/简历/工作模拟" status --short   # 只显示新增的 refactor/
git -c safe.directory="D:/作业/简历/工作模拟" diff --stat -- src tests CMakeLists.txt scripts tools samples
# 无输出：原有源码、测试、构建与打包入口逐字节未变
```

## 使用建议

- 需要对照阅读时，用 `git diff --no-index src refactor/src` 查看逐条改动。
- 本目录不是新的发布版本：版本号、打包、远端推送仍按原工程的流程处理；
  若要正式合并，应把 `src/` 的改动按补丁方式落到主树并重新跑独立审查与验收。
- 测试侧仍存在大量重复的窗口/临时库样板（详见报告“残留与后续项”），本次未改，
  因为测试是验收证据，改动需要单独的功能卡与独立复验。
