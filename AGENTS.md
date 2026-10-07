# 项目约定

- 本项目是 Qt Widgets + SQLite 的最小记录示例；需求和人工验收步骤见 README.md 的“功能与验收”。
- 当前功能人工验收前，不扩展编辑、删除、搜索等后续功能。
- 使用 Qt 6 MinGW 套件与匹配的 MinGW 编译器，不混用 MSVC 库。
- 常用检查：`scripts/build.ps1`（配置、构建、复制运行库、CTest）；`build/bin/environment_probe.exe`；`qt_sqlite_demo.exe --smoke-test <截图文件>`。
- 执行脚本前检查内容；Qt 模块和运行插件都使用本机已有安装，当前无需下载依赖。
- 数据库读写集中在 RecordStore；输入使用参数绑定，不拼接 SQL 值；保留已有数据库，不执行清库。
- 运行方式、实际环境和排错入口见 README.md 对应章节；修改行为或检查结果时同步更新文档。
- PowerShell 脚本保留 UTF-8 BOM，以兼容启动入口使用的 Windows PowerShell 5.1。
- 本功能交付前按 ai-dev-workflow 做独立子代理审查；审查者只读取需求、代码与验证依据，不改写实现文件。
