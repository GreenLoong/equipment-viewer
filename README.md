# Qt + SQLite 第一个可运行示例

适用范围：本机 Windows、Qt Widgets、C++17。2026-10-07 创建并通过用户人工验收。当前功能是本地记录的新增与读取。

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

`environment_probe` 使用临时数据库验证建表、参数绑定写入、关闭后重新读取，成功输出 `ENVIRONMENT PASS`。CTest 包含环境检查、功能检查和主程序启动截图检查。功能检查覆盖中文与引号、SQL 片段按文本保存、记录顺序、空输入、长度边界、错误路径、损坏文件、按钮和回车保存、重新打开窗口后的列表。测试数据库自动清理，不使用用户记录。

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
| 保存失败 | 先读窗口错误；确认显示的数据库位置可写，文件没有被其他程序长期锁定。写入失败时输入内容保留，可修正后重试 |
| 记录已保存但列表读取失败 | 点击“刷新”；此状态下记录已经写入，直接重复保存会增加重复记录 |

运行库通过本机 `windeployqt` 复制；依据为 [Qt 6.11 Windows 部署文档](https://doc.qt.io/qt-6.11/windows-deployment.html)。本机验证不代表已在其他电脑测试。

## 版本管理

源码和说明保存在本地 Git；`build/`、数据库及 Qt Creator 个人配置被忽略。Git 不能恢复忽略的数据库文件；需要保留记录时，先关闭程序再复制数据库文件到备份位置。`origin` 已按用户提供的地址绑定到 [GreenLoong/equipment-viewer](https://github.com/GreenLoong/equipment-viewer)，尚未执行推送或发布。
