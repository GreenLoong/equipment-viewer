# 设备模型查看器 · 去重重构版（refactor/）

本目录 2026-10-11 生成，自 v1.2.0 起是**项目的正式源码树**。它按“重新审查代码、去掉重复实现”的
要求产出：公开类、公开接口签名、对象名与用户可见文案保持不变，消除 18 类重复实现，抽出 4 个内部
共用头文件（`numericutil.h`、`partvalidation.h`、`jsonutil.h`、`sqliteconnection.h`），并承接
1.1.x 之后的正式产品改动（应用图标、默认打开设备查看器、方向轴与抗锯齿修复）。

v1.2.0 按用户要求**整体移除“团队共享库/工艺批注”**这一可选服务：客户端对话框、HTTP 客户端、
Node/MySQL/Redis 服务端、相关测试与接口契约文档一并删除，主程序不再链接 Qt Network。
1.1.1 及更早的历史实现仍完整保存在 Git 标签 `v1.1.1`，需要时可用
`git checkout v1.1.1 -- api` 等命令取回参考。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| `src/` | 去重后的正式实现源码 |
| `tests/` | 测试源码（原 1.1.1 `tests/` 的副本）；3 个文件（`cad_user_tests.cpp`、`cad_import_process_tests.cpp`、`reviewer_resource_tests.cpp`）因移除共享库而对 1.1.1 有意分叉，其余与标签 `v1.1.1` 内同名文件逐字节一致 |
| `tools/` | 构建用 `environment_probe.cpp` 副本，以及重复度审查与副本一致性校验脚本 |
| `CMakeLists.txt` | 本目录的独立构建入口；目标名、测试名与根工程保持一致 |
| `build.ps1` | 独立构建脚本（工具链参数与 `scripts/build.ps1` 相同，只改 `-S` 指向） |
| `package.ps1` | 交付目录打包脚本（只复制运行所需文件，不复制源码、测试、资料库与凭证） |
| `resources/` | 应用图标（`app.ico`、`app.rc`、`app.qrc`、图标预览） |
| `docs/dedup-report.md` | 重复实现审查报告：证据、处理方式、残留项与验证结果 |
| `docs/harness-sha256.txt` | 测试/探针副本的 SHA256 清单 |
| `docs/package/` | 交付文档：使用说明、技术说明、验证记录、README |

## 构建与验证

```powershell
# 方式一：一键配置 + 构建 + 部署 Qt + 同步 CAD 运行库 + 全量 CTest
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

原生图形验收（真实窗口与 FBO，需要桌面会话）：

```powershell
build-refactor\bin\r11_desktop_tester.exe -platform windows --snapshot-dir build-refactor\r11-native
build-refactor\bin\cad_user_tests.exe -platform windows --assembly-preview-check `
  --assembly samples\cad\Yellow_gearmotor_straight.step --expected-assembly-count 6 `
  --snapshot-dir build-refactor\cad-native
```

## v1.2.0 实际验证结果（2026-10-11）

- 编译：115 个构建步骤、59 次编译、27 次链接全部通过，`-Wall -Wextra -Wpedantic` 零警告零错误
  （日志 `build-refactor/build-log-1.2.0.txt`）。
- 自动检查：**17/17 CTest 通过**（22.22 秒）。比 1.1.1 少 2 项，且不是削弱：移除的
  `api_client_lifecycle`、`annotation_client_faults` 两项随功能整体删除，其余用例断言未改动。
- 原生图形：`r11_desktop_tester -platform windows` → `accepted=1`，实测 FBO `samples=4`、
  超采样 `2.0x`，坐标轴三轴、受限网格、四视图与鼠标契约全部通过。
- 真实 STEP：6 件齿轮箱装配 `--assembly-preview-check` 原生通过（`parts=6 vertices=4162
  triangles=4332 realStepImport=1 fourViews=1 databaseRestartExact=1 native=1`）。
- 资源基线：`reviewer_resource_tests.exe -platform offscreen --batches 2 --windows 6 --loads 3`
  通过：窗口与 Scene 反复创建销毁后，控件、窗口、QObject 子树与 SQL 连接数回到基线。
- 主程序依赖：`qt_sqlite_demo.exe` 已不再导入 `Qt6Network.dll`，交付目录相应去掉
  `Qt6Network.dll`、`tls`、`networkinformation`。

## 与 1.1.1 历史版本的关系（2026-10-11 仓库整理后）

去重重构当时是在原工程之外新建本目录，原 `src/`、`tests/`、根 `CMakeLists.txt`、旧 `scripts/`、
`docs/` 过程文档与共享库服务端 `api/` 都保持原样用于对照。仓库整理时这些历史文件已从工作树删除
（提交历史与标签不受影响），现在只用标签 `v1.1.1` 读取：

```powershell
git show v1.1.1:src/sceneview.cpp | less          # 读单个历史文件
git worktree add ..\ev-1.1.1 v1.1.1               # 需要整树对照时用临时 worktree
git diff --no-index ..\ev-1.1.1\src refactor\src  # 逐文件比较去重前后的实现
git worktree remove ..\ev-1.1.1
```

`refactor/tools/verify-harness-copy.ps1` 在根目录同名文件缺失时会自动改用 `v1.1.1` 的 blob 比对，
所以“重构版测试没有被改弱”这条证据依然成立（当前结果：`checked=15 mismatches=0`）。

仓库顶层现在只保留 `README.md`、`refactor/`、`samples/`、`scripts/sync-cad-runtime.ps1`、
`tools/cad/`、`AGENTS.md` 与忽略规则。

## 使用建议

- 日常开发、构建与打包都以本目录为入口：`.\refactor\build.ps1` 与
  `.\refactor\package.ps1 -OutputRoot <交付目录> -BuildDirectory build-refactor -Force`。
- 测试侧仍存在大量重复的窗口/临时库样板（详见报告“残留与后续项”），改动需要单独的功能卡与复验。
- 迁移候选：标签 `v1.1.1` 内的 `tests/cad_converter_tests.py`（转换器 17 项检查）尚未迁入本目录，
  如需纳入正式验证流程应连同运行库准备脚本一起迁入并复跑。
