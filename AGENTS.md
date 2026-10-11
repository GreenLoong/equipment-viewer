# 项目协作约定（设备模型查看器 v1.2.0）

## 范围与仓库布局

- 项目：Windows 桌面工业设备 / CAD 三维查看器（C++17 + Qt6 Widgets + OpenGL + SQLite）。
- 正式实现、测试、脚本与文档全部在 `refactor/`：
  `src/`、`tests/`、`tools/`、`resources/`、`CMakeLists.txt`、`build.ps1`、`package.ps1`、
  `docs/package/`（01 使用说明、02 技术说明、03 验证记录、04 去重报告、05 简历与面试演示指南）、
  `docs/working-state.md`（当前状态与恢复入口）。
- 仓库顶层只保留：`README.md`、`refactor/`、`samples/`（样例与许可）、
  `scripts/sync-cad-runtime.ps1`、`tools/cad/`（CAD 运行库准备）、`AGENTS.md`、`.gitignore`、`.gitattributes`。
- 1.1.1 及更早的历史实现与过程记录（原 `src/`、`tests/`、根 `CMakeLists.txt`、旧 `scripts/`、`docs/`、
  共享库服务端 `api/`）已从工作树移除，**完整保存在 Git 标签 `v1.1.1`**。需要对照时用
  `git show v1.1.1:<路径>` 或临时 worktree 检出；不要把历史文件恢复到 `main` 的工作树里。

## 环境与入口

- Qt 6.11.1 MinGW 64、GCC 13.1.0、CMake 3.30.5、Ninja 1.12.1、QSQLITE；不混用 MSVC。
- 构建 + 部署 Qt 运行库 + 同步 CAD 运行库 + 全量 CTest：`.\refactor\build.ps1`
- 打包交付目录（只含运行文件，不含源码、测试、资料库与凭证）：
  `.\refactor\package.ps1 -OutputRoot 'D:\作业\简历\cad查看器' -BuildDirectory build-refactor -Force`
- 原生图形验收（需要桌面会话）：
  `build-refactor\bin\r11_desktop_tester.exe -platform windows --snapshot-dir build-refactor\r11-native`
- 真实 STEP 全链：
  `build-refactor\bin\cad_user_tests.exe -platform windows --assembly-preview-check --assembly samples\cad\Yellow_gearmotor_straight.step --expected-assembly-count 6 --snapshot-dir build-refactor\cad-native`
- CAD 运行库：`scripts/sync-cad-runtime.ps1` 校验签名与哈希后同步 `.runtime/cad-distribution/cad-runtime`（忽略目录）；
  首次准备用 `tools/cad/setup_runtime.py`（需要受信 Python 3.12+）。
- 副本一致性：`.\refactor\tools\verify-harness-copy.ps1`（根目录同名文件不存在时自动改用标签 `v1.1.1` 比对）。

## 修改与验证纪律

- 改源码一律用补丁方式，禁止用脚本做整文件替换（曾因编码转换破坏中文注释）。
- 编译保持 `-Wall -Wextra -Wpedantic` 零警告；改动后跑全量 CTest，涉及图形/交互的改动另跑原生 harness。
- 自动检查、独立审查、模拟验收与真人验收分开记录，不互相替代；没执行的检查不写成通过。
- 生产数据不进仓库、不当测试目标：`data/`、`.recovery/`、`.runtime/`、`api/` 及本机凭证均被忽略。
  禁止 `reset --hard`、`clean` 或删除数据库；源码回退不能恢复数据库。
- 新增公开业务接口用中文 Doxygen；关键约束注释写明原因；遵守既有分层职责
  （Window 不执行 SQL、Scene 不访问数据库、Controller 是状态唯一来源、导入失败不改变输出）。
- 提交前检查 `git status`、`git diff --cached`、`git diff --check`，避免混入构建产物、日志、数据库与本机配置。

## 版本与远端

- 远端 `origin` = https://github.com/GreenLoong/equipment-viewer.git；已验证版本推送 `main` 并打标签，
  保留既有提交历史，**禁止 force push**、禁止重写历史。
- 历史版本标签：`v0.3.0`…`v1.0.0`、`v1.1.0`、`v1.1.1`、`v1.2.0`。取回任何历史文件都从标签读，不重建历史。
