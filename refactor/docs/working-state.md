# 当前状态与恢复入口（2026-10-11）

## 已交付

- **版本 1.2.0**（Git 标签 `v1.2.0`，`main` 同步）：去重重构版成为正式实现，
  团队共享库/工艺批注按用户要求整体移除。
- **交付目录**：`D:\作业\简历\cad查看器`，533 个文件 / 238.5 MB，
  `设备查看器.exe` SHA256 `48B3D7D1316D5684C7081CDAA649F34CA25CD3B4942E82BE78AB7C207AF6FF88`
  （仓库整理后重新构建并打包，`manifest.json` 中的源码提交为整理提交）。
- **验证**：全量重建零警告；CTest **17/17 通过**（22.22 秒）；原生 `r11_desktop_tester`
  `accepted=1`（FBO 实测 `samples=4`、超采样 `2.0x`）；真实 STEP 6 件装配
  `parts=6 vertices=4162 triangles=4332`；`reviewer_resource_tests` 回到资源基线；
  包内 offscreen 与原生窗口冒烟均 `exit=0`。

## 仓库布局（2026-10-11 整理后）

| 路径 | 内容 |
| --- | --- |
| `README.md` | 正式版说明（功能、架构、验证、版本历史、未实现清单） |
| `refactor/` | 正式源码、测试、构建与打包脚本、交付文档 |
| `samples/` | 虚构 JSON 与公开 STEP 样例（含许可） |
| `scripts/sync-cad-runtime.ps1` | CAD 运行库同步（校验签名与哈希） |
| `tools/cad/` | CAD 运行库准备脚本 |
| `AGENTS.md` | 协作约定与常用命令 |

1.1.1 及更早的历史实现与过程文档（原 `src/`、`tests/`、根 `CMakeLists.txt`、旧 `scripts/`、
`docs/`，以及共享库服务端 `api/`）已从工作树删除，完整保存在 Git 标签 `v1.1.1`。
`refactor/tools/verify-harness-copy.ps1` 会在根目录同名文件不存在时自动改用该标签比对，
“重构版测试未被改弱”这条证据因此仍然有效。

## 已知问题与未完成

- `cad_user_tests --bearing` 的像素断言在本机失败（1.1.1 旧构建同样失败，属环境敏感断言，
  不在 CTest 门槛内）；`cad_import_process` 历史上出现过间歇失败，单项复跑通过。
- 真人满意验收未进行；跨机器、无 Qt 环境、高 DPI 多显示器、十万级三角以上装配与长时间运行
  均未量化验证。
- 可选后续项：把标签 `v1.1.1` 内的 `tests/cad_converter_tests.py`（转换器 17 项检查）迁入
  `refactor/tests/` 并接入验证流程。

## 恢复入口

- 取回历史文件：`git show v1.1.1:<路径>`，或 `git checkout v1.1.1 -- <路径>`（后者不要把结果提交进 `main`）。
- 数据库与源码检查点分开：资料库只在本机 `data/`、备份在 `.recovery/`，都不进仓库；
  两者都不能靠 Git 回退恢复。
- 打包与构建命令见 `refactor/README.md` 与 `AGENTS.md`。
