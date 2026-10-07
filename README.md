# 设备模型查看器 0.2

更新日期：2026-10-08。当前开发已接入 JSON 导入、多设备选择、只读属性、三维旋转缩放/拾取/高亮/显隐、搜索与隔离；本轮交付验证状态以 docs/working-state.md 和验证记录为准。下文保留此前记录示例及分步交付历史。

## 当前运行入口

双击根目录“启动设备查看器.cmd”，或在项目目录执行：

```powershell
.\scripts\run.ps1 -EquipmentView
```

仍使用项目 data/records.sqlite，保留已有设备及原记录资料；原“启动示例.cmd”继续打开记录窗口。设备窗口可切换数据库中已有设备，首次打开 EQ-001。JSON 两份虚构样例在 samples 中，通过窗口“导入设备 JSON”选择。

完整操作、持久化和限制见 [使用指南](docs/user-guide.md)；接口与模块边界、注释和检查要求见 [工程规范](docs/engineering-standards.md)；恢复入口见 [当前工作状态](docs/working-state.md) 和角色 handoffs。

## 检查与打包

构建脚本 scripts/build.ps1 只使用本机依赖；当前 CTest 包含九项业务/回归/启动检查。每次变更先检查脚本再运行。

```powershell
.\scripts\build.ps1
.\build\bin\scene_tests.exe -platform windows --snapshot-dir "$PWD\build\native-scene"
.\scripts\package.ps1 -OutputRoot '用户指定的交付目录'
```

打包脚本生成独立目录和ZIP，仅含运行文件、插件、虚构样例、指南和manifest，不复制正常数据库或凭证。实际构建、原生OpenGL、打包依赖验证和独立审查结果记录在验证记录中，未执行的操作不能记为通过。

数据库一致性快照工具 scripts/checkpoint-data.py 使用 Python 标准库 SQLite online backup，保存到忽略的 .recovery/databases 并检查 quick_check。恢复需先核对快照及当前数据库，不能直接覆盖正常资料。源码本地Git检查点不代替数据备份。

## 模拟反馈

搜索和隔离来自两条明确标注的模拟工艺工程师反馈，详见 [模拟反馈台账](docs/simulated-feedback.md)。真实用户体验仍待你操作；网络、MySQL、Redis为后续单独定义范围的扩展，本地版本不依赖服务器。

## 前序交付历史

# Qt + SQLite 第一个可运行示例

适用范围：本机 Windows、Qt Widgets、C++17。本地记录的新增与读取已于 2026-10-07 通过用户人工验收；F01-1 设备资料保存与读取已于 2026-10-08 通过用户人工验收。

## F01-1：设备资料初始化与重启读取

2026-10-07，F01 已进入开发阶段。环境状态为“你已验证通过”，依据见 [验证记录](验证记录.md) 的“实际结果”。本步加入 `EquipmentRepository`，在程序启动时打开数据库、初始化设备资料，并输出设备名称、零件 ID 和名称。设备清单与属性界面留到下一步；原有记录窗口继续可用。F01-1 自动检查和独立审查已通过，用户于 2026-10-08 明确确认“F01-1 验收通过”；F01 整体尚未验收。

在项目目录的 PowerShell 中运行两次（使用同一个文件）：

```powershell
.\scripts\build.ps1
.\build\bin\qt_sqlite_demo.exe --equipment-check --database "$PWD\data\f01-acceptance.sqlite"
.\build\bin\qt_sqlite_demo.exe --equipment-check --database "$PWD\data\f01-acceptance.sqlite"
```

`f01-acceptance.sqlite` 是单独的人工验收文件，首次使用时应不存在；已有文件保留，不要为重新初始化而删除正常数据。`--equipment-check` 输出后退出，不打开窗口，也不创建记录示例的 `demo_records` 表。正常启动入口仍使用 `data/records.sqlite`，同样会初始化并输出设备资料；已有记录不被清空。Qt Creator 的“应用程序输出”可查看这些输出。

预期输出（DATABASE 后为实际绝对路径）：

```text
DATABASE: D:/作业/简历/工作模拟/data/f01-acceptance.sqlite
EQUIPMENT: EQ-001 输送机构
PART: EQ-001/P-001 底座
PART: EQ-001/P-002 驱动电机
PART: EQ-001/P-003 传动轴
COUNTS: equipment=1 parts=3
```

人工验收：

1. 首次执行得到一台设备、三个零件，输出的文件路径与参数一致。
2. 第二次执行数量仍为 `equipment=1 parts=3`，名称和 ID 不变。
3. 用目录代替文件：`.\build\bin\qt_sqlite_demo.exe --equipment-check --database "$PWD\data"`。应输出数据库路径及 SQLite 打开失败原因，退出码为 1；正常数据库保持完整。
4. 运行 `.\build\bin\equipment_tests.exe`，应输出 `Persistence: 1 Constraints: 1 Rollback: 1 Commit: 1 Errors: 1 Restart: 1`。写入失败与提交失败检查均使用自动清理的临时数据库。

数据和实现入口：

- [数据设计计划](C:/Users/23871/Documents/Codex/2026-10-07/new-chat/outputs/设备查看器-需求与协作计划-v0.2.md) 第 6.3 节规定字段、单位和约束。`equipment.id` 唯一；`part` 的 `(equipment_id,id)` 联合主键允许不同设备使用相同零件 ID。每个 Repository 连接在事务开始前启用并检查外键。
- `src/equipmentrepository.h` 定义 `Equipment`、`Part` 和 Repository。`openDatabase()` 连接文件；`initialize()` 在事务中检查结构，首次建两张表并写入完整样例；`loadEquipment()` 读设备；`loadParts()` 通过 `WHERE equipment_id = ?` 绑定设备 ID 并读取该设备的零件。没有匹配设备时查询成功并返回空集合；执行失败则返回 false 并提供 `lastError()`。
- 建表语句使用 `STRICT`、`NOT NULL`、`CHECK` 和外键；拒绝空白必填文本、重复主键、孤立零件、未知形状、非有限位置或尺寸。box 的三轴尺寸为正，圆柱字段为空；cylinder 的半径和长度为正，box 尺寸为空。保存前也进行 C++ 校验。
- `saveEquipmentWithParts()` 是底层原子保存操作，复用初始化的写入逻辑并用于失败测试；本步没有 JSON 导入、覆盖、编辑或删除入口。设备及全部零件一起提交，任何 SQL 或提交失败都回滚，错误包含操作、相关 ID 和驱动原因。
- 首次初始化以两张业务表均不存在为依据，即使文件已有 `demo_records` 也可初始化。成功后不按行数补种；已有空表或被修改的资料不会被样例覆盖。首版只接受由当前建表语句创建的两张表，缺表或定义（包括约束）不一致时报错并保留文件；暂不进行结构迁移。[SQLite STRICT 表](https://www.sqlite.org/stricttables.html)要求 SQLite 3.37 或以上，本机驱动满足此要求。

样例材料、尺寸和位置为本步补充的学习用示例值，长度单位毫米，位置为几何体中心，Z 向上，圆柱轴线沿 X：

| 零件 ID | 名称 | 材料 | 形状 | 位置 X/Y/Z | 尺寸 |
| --- | --- | --- | --- | --- | --- |
| P-001 | 底座 | Q235 | box | 0/0/20 | X/Y/Z = 600/300/40 |
| P-002 | 驱动电机 | 铝合金 | box | -150/0/120 | X/Y/Z = 200/160/160 |
| P-003 | 传动轴 | 45钢 | cylinder | 120/0/120 | 半径 20，长度 400 |

代码导读：`src/main.cpp` 选择路径 → Repository 打开、初始化 → 读取设备 → 按设备 ID 读取零件 → 输出。`tests/equipment_tests.cpp` 通过真实 SQL 约束、第三个零件触发器失败及读锁阻塞提交来验证错误路径，并启动两个独立进程验证重启。SQL 值采用参数绑定，引号和 SQL 片段会作为文本保存。外键机制见 [SQLite 官方说明](https://www.sqlite.org/foreignkeys.html)，参数绑定见 [Qt QSqlQuery 文档](https://doc.qt.io/qt-6.11/qsqlquery.html)。

## F01-2：零件清单与只读属性联动

2026-10-08，F01-2 第 1～4 步已实现：加载 EQ-001，顶部显示设备名称和 ID，左侧显示三个零件的名称与 ID；首次打开没有选择，右侧提示“请选择零件”。点击后显示只读 ID、原始名称和材料、中文形状、中心位置和适用尺寸，位置与尺寸分组注明毫米。长方体显示 X/Y/Z，圆柱显示半径和长度；切换形状时清空并隐藏不适用字段。“清除选择”按钮同步清单高亮、控制器选中 ID 和全部属性，可重复清除。完整 F01-2 构建、自动检查和独立静态审查通过，人工验收待完成。

从项目目录打开设备窗口：

```powershell
.\build\bin\qt_sqlite_demo.exe --equipment-view --database "$PWD\data\f01-acceptance.sqlite"
```

Qt Creator 可选择 `qt_sqlite_demo`，在运行参数中设置 `--equipment-view --database "D:/作业/简历/工作模拟/data/f01-acceptance.sqlite"`。不带 `--equipment-view` 时仍进入原记录窗口；`--equipment-check` 的输出验收入口保持可用。使用哪个文件就在该文件读取或首次初始化，不复制或删除已有资料。

人工验收：

1. 确认顶部为“输送机构 / EQ-001”，左侧三个零件且首次没有高亮，右侧提示“请选择零件”。
2. 按“底座 → 传动轴 → 驱动电机 → 清除选择 → 再选传动轴”操作。每次核对 ID、名称、材料、形状、中心位置和尺寸，对照本文件 F01-1 的样例表格。
3. 底座与驱动电机只显示长方体三轴尺寸，传动轴只显示半径与长度；相互切换没有前一种形状字段或旧值残留，各位置/尺寸分组注明毫米。
4. 点击“清除选择”后，清单没有高亮，右侧只有“请选择零件”，没有任何旧属性；重复清除结果不变，再选传动轴恢复圆柱资料。
5. 关闭后使用同一文件重开，仍是三个零件且首次未选择；不带 `--equipment-view` 仍可打开原记录窗口。

选择链路：`QListWidgetItem::data(Qt::UserRole)` 保存零件 ID → `EquipmentWindow::selectFromList()` 将 ID 交给 `EquipmentController::selectPart()` → 控制器在已加载的 `parts_` 中按 ID 查找 → `displayProperties()` 通过 `selectedPart()` 将原始文本和对应数值写入 QLabel。所有值按普通文本显示，不解析名称或材料中的 HTML。更新入口先清空所有值并隐藏两类尺寸组，再填入当前资料并显示适用尺寸；借用的零件指针只在本次同步更新使用，不长期保存。`clearSelection()` 屏蔽清单信号，清空高亮和当前项，将空 ID 交给控制器，再调用同一更新入口；清单自行取消选择也同步清空全部属性。

零件名称和行号不用于查找；点击和清除不执行 SQL。完整局部资料加载成功后才替换控制器状态，新加载成功时重置选择；加载失败保留旧设备、零件、选择与属性，同时显示明确错误。不存在设备、读取失败与有效空设备在测试中分别验证。

运行完整选择检查：`.\build\bin\equipment_window_tests.exe -platform offscreen`，预期 `Controller`、`CachedClicks`、`Identity`、`EmptyAndFailure`、`PropertiesAndClear` 均为 1。全部六项 CTest 由 `scripts/build.ps1` 执行，使用临时数据库，构建、完整操作链与原功能回归已通过。原生窗口操作链截图可由 `.\build\bin\equipment_window_tests.exe -platform windows --snapshot-dir "$PWD\build\f01-2-snapshots"` 导出五步图片；原生启动截图入口为 `.\build\bin\qt_sqlite_demo.exe --equipment-view --smoke-test "$PWD\build\equipment-native.png"`。这两个入口均使用临时数据库并退出，不操作正常资料。实际依据见 [验证记录](验证记录.md) 的“F01-2 第 4 步与完整功能检查”。

## 功能与验收

输入一条记录，点击“保存记录”或按回车，列表立即显示新记录。关闭程序、重新打开，记录仍存在。最新记录位于最上方。记录首尾空白自动去除，空白输入不保存，输入框最多接受 200 个 Qt 字符单元。

验收步骤：

1. 双击项目根目录的 `启动示例.cmd`，出现“我的记录”窗口。
2. 输入 `我的第一条记录`，点击“保存记录”，列表新增一行，输入框清空。
3. 输入含中文和引号的文本（例如 `学习 SQLite：O'Hara`），按回车，再新增一行。
4. 只输入空格再保存，应显示错误，行数不增加。
5. 关闭并重新启动，前面的记录仍存在。

当前仅实现新增、列表读取、刷新和持久化；编辑、删除、搜索尚未实现。读写在界面线程执行，列表一次加载全部记录，适合少量数据的学习示例。

## 启动与构建

已构建时直接双击 `启动示例.cmd`。该入口调用 Windows PowerShell 和 `scripts/run.ps1`，数据库固定使用项目的 `data/records.sqlite`。不要单独复制 exe；Qt DLL 和插件需要一同保留。

也可以在项目目录打开 PowerShell：

```powershell
.\scripts\run.ps1
```

重新构建并执行自动检查：

```powershell
.\scripts\build.ps1
```

脚本使用已安装的工具，不下载依赖。构建生成物位于 `build/`；运行库部署在 `build/bin/`。脚本只临时设置当前进程的 PATH，结束时恢复。当前 CMake 在复用本项目中文路径下的缓存时出现异常退出，因此脚本使用 `--fresh` 重新生成 CMake 配置，记录数据位于独立的 `data/`，不会被此操作删除。此脚本要求 CMake 3.24 或更高版本。

安装位置不同时可显式传入四个参数：

```powershell
.\scripts\build.ps1 -QtRoot '你的 Qt MinGW 套件目录' `
    -MinGWBin '你的 MinGW bin 目录' -CMake '你的 cmake.exe 路径' `
    -Ninja '你的 ninja.exe 路径'
```

参数中的文字是占位值，不是本机安装路径。已有构建使用了其他编译器时，先保留源码和 `data/`，再使用匹配的套件重新构建。

## 本机环境与检查

最终构建、原生窗口与独立审查结果见 [验证记录](验证记录.md)。

| 组件 | 实际检测结果 | 路径或验证方式 |
| --- | --- | --- |
| Qt | 6.11.1，64 位 MinGW 套件 | `D:\Qt\6.11.1\mingw_64` |
| 编译器 | MinGW GCC 13.1.0 | `D:\Qt\Tools\mingw1310_64\bin\g++.exe` |
| CMake | 3.30.5 | `D:\Qt\Tools\CMake_64\bin\cmake.exe` |
| Ninja | 1.12.1 | `D:\Qt\Tools\Ninja\ninja.exe` |
| SQLite | 3.53.0 | 经 QSQLITE 实际执行 `SELECT sqlite_version()` |
| Qt 模块 | Core、Widgets、Sql、Test 已找到 | CMake 配置与编译链接 |

SQLite 通过 Qt 的 `QSQLITE` 驱动使用，不需要运行数据库服务器。本机 PATH 中未找到独立的 `sqlite3` 命令；本示例的 SQL 验证通过 Qt 驱动完成。[Qt 6.11 SQL 驱动文档](https://doc.qt.io/qt-6.11/sql-driver.html)说明此驱动的使用方式。

检查入口：

```powershell
.\build\bin\environment_probe.exe
& 'D:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build --output-on-failure
.\build\bin\qt_sqlite_demo.exe --smoke-test "$PWD\build\window.png"
```

`environment_probe` 使用临时数据库验证建表、参数绑定写入、关闭后重新读取，成功输出 `ENVIRONMENT PASS`。CTest 包含环境检查、记录与窗口检查、设备持久化检查、设备清单选择检查、设备窗口启动截图和原记录窗口启动截图。记录与窗口检查覆盖中文与引号、SQL 片段按文本保存、记录顺序、空输入、长度边界、错误路径、损坏文件、按钮和回车保存、重新打开窗口后的列表。设备检查范围见本文件 F01-1、F01-2 章节及验证记录。测试数据库自动清理，不使用用户记录。

`--smoke-test` 使用临时数据库显示窗口，保存窗口截图后自动退出。此检查只证明本机窗口启动和绘制，不代替上面的人工验收。

## 代码入口与数据流

`src/main.cpp` 选择数据库路径、打开数据库、创建窗口。`src/mainwindow.cpp` 连接按钮和回车事件，保存成功后重新加载列表。`src/recordstore.cpp` 负责打开数据库、建表、绑定参数写入和读取；窗口不拼接 SQL。

数据流：输入框 → `RecordStore::add()` 校验并写入 → `RecordStore::load()` 读取 → 表格展示。时间使用 UTC 保存，显示时转换成本机时区。

数据库包含 `demo_records` 表，字段是自增 `id`、文本 `content` 和时间 `created_at`。首次使用自动建表，已有文件不被清空。修改保存规则看 `RecordStore::add()`；修改显示方式看 `MainWindow::refresh()`。

通过启动脚本打开时，记录保存在 `data/records.sqlite`。直接打开 `build/bin/qt_sqlite_demo.exe` 则使用 exe 旁边的 `data/records.sqlite`，因此建议始终使用项目根目录的启动入口。也可以用 `--database` 或 `run.ps1 -Database` 显式指定文件。

## 排错

| 现象 | 检查与处理 |
| --- | --- |
| 找不到工具 | 检查上述安装路径，或通过构建脚本参数指定已有安装位置 |
| 找不到 DLL、Qt 平台插件无法初始化 | 重新运行构建脚本；保留 exe 同目录下的 DLL、`platforms/qwindows.dll` 和 `sqldrivers/qsqlite.dll` |
| `demo_tests` 提示只有 windows 平台插件可用 | 自动测试使用 offscreen；构建脚本需部署 `platforms/qoffscreen.dll`，不要只复制测试 exe |
| CMake 复用缓存时异常退出 | 使用当前构建脚本，它已加入 `--fresh`；具体 CMake 内部根因尚未确认 |
| PowerShell 输出中文变成“杈撻”等乱码 | 程序输出 UTF-8，终端需使用匹配编码；执行下方命令后重新运行程序 |
| 保存失败 | 先读窗口错误；确认显示的数据库位置可写，文件没有被其他程序长期锁定。写入失败时输入内容保留，可修正后重试 |
| 记录已保存但列表读取失败 | 点击“刷新”；此状态下记录已经写入，直接重复保存会增加重复记录 |

PowerShell 中文输出排错（2026-10-08）：在运行程序的同一个 PowerShell 窗口中执行：

```powershell
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$OutputEncoding = [Console]::OutputEncoding
chcp 65001 > $null
```

随后重新执行 `--equipment-check` 验收命令；原来已显示的乱码不会自动更正。`[Console]::OutputEncoding` 设置控制台输出编码，`$OutputEncoding` 同步向外部程序发送文本时的编码，`chcp` 设置当前控制台代码页。该设置用于当前会话，没有修改数据库、系统语言或代码文件。命令在本机 Windows PowerShell 5.1 的临时进程中验证，目录路径失败用例正确显示“打开数据库 D:/作业/简历/工作模拟 失败”，退出码为 1。

需要让以后新开的 PowerShell 自动应用设置时，在该 PowerShell 中执行 `$PROFILE` 获取当前用户、当前宿主的配置文件路径，将上述三行追加到该文件（保留已有内容；文件不存在时先创建父目录和文件），保存后重新打开 PowerShell。项目启动入口使用 `-NoProfile` 时不会读取个人配置。控制台编码机制见 [Microsoft Console.OutputEncoding 文档](https://learn.microsoft.com/en-us/dotnet/api/system.console.outputencoding)，配置文件位置与加载机制见 [PowerShell profiles](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_profiles?view=powershell-5.1)。

运行库通过本机 `windeployqt` 复制；依据为 [Qt 6.11 Windows 部署文档](https://doc.qt.io/qt-6.11/windows-deployment.html)。本机验证不代表已在其他电脑测试。

## 版本管理

源码和说明保存在本地 Git；`build/`、数据库及 Qt Creator 个人配置被忽略。Git 不能恢复忽略的数据库文件；需要保留记录时，先关闭程序再复制数据库文件到备份位置。`origin` 已按用户提供的地址绑定到 [GreenLoong/equipment-viewer](https://github.com/GreenLoong/equipment-viewer)，尚未执行推送或发布。
