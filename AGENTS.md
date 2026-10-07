# 项目约定

- 本项目是 Qt Widgets + SQLite 的最小记录示例；需求和人工验收步骤见 README.md 的“功能与验收”。
- 环境验证及首个功能（新增记录、列表读取、刷新、SQLite 持久化）已于 2026-10-07 通过用户人工验收；依据见 验证记录.md 的“实际结果”。
- 后续功能按用户确认的需求和验收标准推进，逐功能交付；尚未确定下一项功能，不自行扩展编辑、删除、搜索。
- 使用 Qt 6 MinGW 套件与匹配的 MinGW 编译器，不混用 MSVC 库。
- 常用检查：`scripts/build.ps1`（配置、构建、复制运行库、CTest）；`build/bin/environment_probe.exe`；`qt_sqlite_demo.exe --smoke-test <截图文件>`。
- Qt Creator 的运行目标选择 `qt_sqlite_demo`；`demo_tests` 是执行完即退出的自动检查程序。需要与启动脚本共享记录时，用 `--database` 指定项目 `data/records.sqlite` 的绝对路径。
- 执行脚本前检查内容；Qt 模块和运行插件都使用本机已有安装，当前无需下载依赖。
- 数据库读写集中在 RecordStore；输入使用参数绑定，不拼接 SQL 值；保留已有数据库，不执行清库。
- 运行方式、实际环境和排错入口见 README.md 对应章节；修改行为或检查结果时同步更新文档。
- PowerShell 脚本保留 UTF-8 BOM，以兼容启动入口使用的 Windows PowerShell 5.1。
- 后续功能交付前按 ai-dev-workflow 做独立子代理审查；影响行为的修复需复验和独立复查。审查者只读取需求、代码与验证依据，不改写实现文件。
- GitHub `origin` 已绑定到 `https://github.com/GreenLoong/equipment-viewer.git`；绑定不包含推送授权，远程同步按用户明确授权的范围执行。
