# 项目约定

- 本项目基于 Qt Widgets + SQLite，保留已验收的最小记录示例，当前推进设备资料查看功能 F01；各功能范围和人工验收步骤见 README.md 对应章节。
- 环境验证及首个功能（新增记录、列表读取、刷新、SQLite 持久化）已于 2026-10-07 通过用户人工验收；依据见 验证记录.md 的“实际结果”。
- F01 已进入开发阶段，环境状态为“你已验证通过”。F01-1 已实现设备资料数据库初始化、事务样例写入、按设备 ID 查询及输出；构建、四项 CTest、独立静态审查通过，用户已于 2026-10-08 明确回复“F01-1 验收通过”。依据见 验证记录.md 的“F01-1 实际检查”，验收入口见 README.md 的“F01-1：设备资料初始化与重启读取”。当前继续 F01-2，F01 整体尚未验收；更新文档或提问不视为评审或验收通过，不自行扩展编辑、删除、搜索。
- 实际环境：Qt 6.11.1（64 位 MinGW）、MinGW GCC 13.1.0，工程使用 CMake 3.30.5 + Ninja 1.12.1，QSQLITE 运行时 SQLite 3.53.0。使用匹配的 MinGW 套件，不混用 MSVC 库；工具路径见 README.md 的“本机环境与检查”。
- 2026-10-08，用户已授权继续同一任务第 4 步。完整 F01-2 已实现设备与零件清单、首次不默认选中、全部只读属性、中文形状、毫米位置/尺寸、形状切换及专用清除选择；六项 CTest、原生五步操作链检查和独立静态审查通过，完整人工验收待完成。运行范围见 README.md 的“F01-2：零件清单与只读属性联动”，依据与最终代码哈希见 验证记录.md 的“F01-2 第 4 步与完整功能检查”。人工验收链为“底座 → 传动轴 → 驱动电机 → 清除选择 → 再选传动轴”；不据此开始 F02。
- `EquipmentWindow::displayProperties()` 为统一属性更新入口：先清空全部值并隐藏两类尺寸组，再显示适用字段；原始名称/材料使用 Qt::PlainText。清除选择同步清单高亮、控制器选中 ID 和全部属性，恢复“请选择零件”。`selectedPart()` 返回借用指针，仅用于当前同步更新，不跨成功重新加载保存。完整任务卡见 `C:/Users/23871/Documents/Codex/2026-10-07/new-chat/outputs/设备查看器-需求与协作计划-v0.2.md` 第 6.7 节。
- 清单项用 Qt::UserRole 携带零件 ID；窗口将 ID 交给控制器，控制器从已加载内存资料按 ID 查找，再返回选中零件供窗口显示，不重查数据库、不按名称或行号匹配。加载完整局部资料后才替换当前状态；失败保留旧资料和选择，无效 ID 拒绝并保持有效高亮。已有检查覆盖同名零件、内存选择、空设备与加载失败的区别，不得将失败伪装为正常空清单。
- 常用检查：`scripts/build.ps1`（配置、构建、复制运行库、六项 CTest）；`build/bin/environment_probe.exe`；`build/bin/equipment_tests.exe`（重启、约束、参数绑定、写入与提交失败回滚）；`build/bin/equipment_window_tests.exe -platform offscreen`（控制器、内存选择、同名零件、错误状态、属性切换与清除）；`qt_sqlite_demo.exe --smoke-test <截图文件>`。原生五步截图用 `equipment_window_tests.exe -platform windows --snapshot-dir <目录>`。自动检查使用临时数据库，不操作正常用户资料。
- Qt Creator 的运行目标选择 `qt_sqlite_demo`；`demo_tests`、`equipment_tests` 和 `equipment_window_tests` 是执行完即退出的自动检查程序。使用 `--equipment-check --database <绝对路径>` 可只初始化、读取并输出设备资料后退出；使用同一路径执行两次验证数量不变。需要与启动脚本共享记录时，用 `--database` 指定项目 `data/records.sqlite` 的绝对路径。
- 设备窗口入口为 `--equipment-view --database <绝对路径>`，固定加载 EQ-001；不带该选项仍打开原记录窗口。`--equipment-view --smoke-test <截图文件>` 用临时数据库检查设备窗口启动；`--equipment-check` 不能与窗口选项或截图选项同时使用。
- 执行脚本前检查内容；Qt 模块和运行插件都使用本机已有安装，当前无需下载依赖。
- 原有记录读写集中在 RecordStore；设备资料读写集中在 EquipmentRepository。输入使用参数绑定，不拼接 SQL 值；每个设备连接显式启用并检查外键；保留已有数据库，不执行清库。equipment/part 首次建表与样例写入使用同一事务，后续启动不补种或覆盖。
- 首版 equipment/part 采用 STRICT 表并严格比对当前建表 SQL；缺表或定义不一致时报告结构异常并保留文件，不自动迁移。设备 ID 为主键，零件使用 (equipment_id, id) 联合主键；形状、尺寸、有限位置和必填文本约束在 SQL 与写入前校验中共同落实。
- 运行方式、实际环境和排错入口见 README.md 对应章节；修改行为或检查结果时同步更新文档。
- PowerShell 脚本保留 UTF-8 BOM，以兼容启动入口使用的 Windows PowerShell 5.1。
- 后续功能交付前按 ai-dev-workflow 做独立子代理审查；影响行为的修复需复验和独立复查。审查者只读取需求、代码与验证依据，不改写实现文件。
- GitHub `origin` 已绑定到 `https://github.com/GreenLoong/equipment-viewer.git`；绑定不包含推送授权，远程同步按用户明确授权的范围执行。

## 2026-10-08 持续多 Agent 开发规则（优先于上面的阶段限制）

- 用户已明确授权全部由 Agent 开发，持续派发任务直到可交互桌面交付，取消用户必须编写关键逻辑和每小功能等待人工验收的旧约定；保留历史真实验收结果，新增模拟验收单独标注。
- 实施前必须读取 docs/engineering-standards.md v1.0；命名、注释、职责、输入校验、测试和技术文档必须遵守该规范。
- 当前主管负责公共接口及 Controller/Window/main/CMake/文档；数据 Agent 只修改 Importer、导入测试和样例；图形 Agent 只修改 SceneView、几何辅助和场景测试；独立审查者只读实现。
- 每个交付阶段执行实现、运行验证、独立审查、修复复验与文档更新；用户中途反馈并入当前任务，无须重新请求已经明确授权的局部开发。
- 首次桌面交付范围见技术规范第1节；后续模拟反馈必须有编号、来源标记、范围和验收条件，不能无限扩展。
- 正常数据库、用户既有源码与未提交改动必须保留；本轮不推送、不发布、不新增付费服务。

## 中断恢复与Git检查点

- 首次进入、额度恢复、换会话或重新分配Agent时，先读 docs/working-state.md 与 docs/handoffs/，再核对实际Git差异，不假设旧Agent仍运行。
- 每个Agent及时落盘自己的handoff，主管统一维护working-state；里程碑检查通过后建立本地提交。额度意外耗尽不保证可再写最后记录，以最近保存状态恢复。
- Git用于源码；scripts/checkpoint-data.py使用Python标准库SQLite在线backup创建独立一致性快照并执行quick_check，正常数据库不提交。备份保存在忽略的.recovery/databases，不能靠git恢复运行资料。
- 恢复不得reset --hard、clean或删除数据库。优先修复分支/明确指定提交回退；保留未提交工作，重大或破坏性操作按具体范围确认。
- 当前目标是连续Agent开发，禁止恢复后重新要求用户完成关键业务代码或每项人工验收；模拟反馈必须注明来源。

## 当前交互版入口
- 设备查看器入口为启动设备查看器.cmd或scripts/run.ps1 -EquipmentView；原记录入口保留。
- CMake已接入Importer与SceneView，九项CTest覆盖导入、场景、完整业务及旧功能。实际验证以working-state和验证记录为准。
- scripts/package.ps1只复制运行库、插件、虚构样例和指南，不打包正常数据库；先检查脚本再运行。
- 当前代码加固及测试正在收尾，未经最终独立复查不得声明交付已通过。
