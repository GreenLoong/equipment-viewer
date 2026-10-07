# reviewer-v1 独立审查交接

日期：2026-10-08。工程：D:/作业/简历/工作模拟。角色：专职只读 code-review/QA；只能修改本文件与 docs/review-plan-1.0.md，业务代码及公共交付文档由主管维护。

## 当前状态和接手入口

已读取 AGENTS、engineering-standards v1.1、working-state、release-plan-1.0、旧 reviewer handoff 与已有 review-plan。逐版 0.3 → 0.4 → 0.5 → 0.6 → 0.9 → 1.0；授权下载与远端同步由主管执行，保留正常资料和既有历史，不直接改为 1.0。独立审查等待主管确认对应版本实现及验证完成；尚未作 0.3 通过或缺陷结论。

新执行环境 PowerShell 可运行，旧审查 helper 的 sandbox setup refresh 限制已经失效。只读 Git 查询使用单条命令 safe.directory 参数，未修改全局 Git 配置。初始实际分支 feature/release-1.0，HEAD 1b6ba2b；工作区存在后端/模拟用户/审查计划新文件，属于其他角色工作，不清理或覆盖。

## 已落盘和实际检查

- 更新 docs/review-plan-1.0.md 第7节，保留既有跨版本计划，补充新的工具调查事实与边界。
- 新建本交接；未改业务、测试、脚本、CMake，也未下载检测器、构建或运行应用测试。
- 实际执行 GCC 版本/ASan 库查找、已装工具文件查询、Windows 进程指标采样、Node v24.18.0 的 expose-gc/heap/memory 探针，以及 Git status/branch/log。
- MinGW GCC 13.1.0 未找到匹配 ASan 库；发现 Visual Studio 的 MSVC ASan 库及 clang-tidy，不能据此宣称 MinGW Qt 检测覆盖。
- Windows HandleCount/WorkingSet64/PrivateMemorySize64 读取可行；Node global.gc/v8.getHeapStatistics/process.memoryUsage 可行。现有系统其他 Node/MySQL 进程不可归因本项目。
- 阅读 SceneView 所有权/上下文销毁回调和 scene_tests 的三次父窗销毁入口，只为设计检查计划；未对正在实现的代码预评，未复跑旧版测试。

初始探查哈希：sceneview.cpp = 83A7AF64608392BCADD21CE50BF4FB9739589D181DF77E38E8B0777D775F8F13；scene_tests.cpp = 4798FBCAF49DBF94E7C280EA80887F1C8A9AA479EDACD8711A289B49B6D8EEE2。正式版本审查必须更新实际范围和哈希。

## 后续审查执行

收到版本完成通知后，读取对应功能卡/接口契约、实际 diff 与未跟踪新源码、测试结果和必要全调用上下文。优先检查借用 Part/状态指针、Qt 捕获及回调、GL 与父窗销毁，网络 reply 超时/取消/逆序/释放，SQL 连接和事务，缓存故障回源、输入限制与凭证。报告具体位置、触发、影响、必要修正；行为修复独立复查关联路径，重大问题未闭环前不交付通过。

0.9 建议建立同一受控进程预热后的重复窗口/加载/故障循环，统计 QObject、具名 SQL 连接、待释放网络回复、计时器及 Windows PID 指标。Node 后端压力在同一服务实例内完成批次并主动 GC，记录堆、缓冲及连接数；正常缓存/驱动/分配器保留需有解释。三次父窗关闭回归或多进程退出不能代替这些计数。计划尚未执行，不作零泄露保证。

恢复先核对主管是否完成新版本、实际 Git/源码/验证证据及运行中的受控进程。不要中止系统其他 Node/MySQL 服务，不删除正常库，不沿用旧哈希为新版结论。

## 0.3 独立审查（首轮）

主管通知实现与10项CTest完成后，独立读取相对1b6ba2b的 equipmentwindow.cpp、main.cpp、CMakeLists.txt、package.ps1 实际差异，完整读取 simulated_user_tests.cpp、0.3反馈、Controller/Window相关调用及Scene输入/回调上下文。0.4 api 在建文件不在本轮范围，没有预评其完整性。

结论：当前0.3默认操作路径未发现阻断问题，允许继续原生验证与打包；原生和最终包尚未由本角色核对，不能等同完整交付通过。该结论不作零泄露保证。主要依据：

- equipmentwindow.cpp:208-230 先selectPart验证后清过滤；无效ID返回时搜索、清单和选择保持。空白ID被视为listed，不清搜索；已有Controller对无效ID不改选择。
- 成功选择被过滤目标时，阻塞search信号后清空并rebuildList；列表信号同样阻塞，恢复同一ID再同步属性/场景；隐藏集合不被搜索或选择改写。
- 新QShortcut由Window拥有，activated接收上下文也为Window。WindowShortcut满足当前四个控件焦点，clearSelection只清选择；没有改为ApplicationShortcut。独立子对话框的Escape隔离属于未来回归需求，本轮未实际打开对话框验证，不声称该路径实测通过。
- 新回调内部引用捕获的listed lambda立即执行、不跨调用保存；外层this回调保存在Window子SceneView中，未引入新的异步保存或借用Part指针。既有Window借用Controller寿命约定继续适用。
- 测试用临时SQLite与虚构单模型，通过真实QTest鼠标和键盘输入验收；observe模式的退出0不被当作0.3通过，默认模式需两项条件都通过。

实际执行与外部证据分开记录：

- 审查者自行运行 `build/bin/simulated_user_tests.exe -platform offscreen`：退出0，软件投影，filteredPick=1，list/canvas/search/button四个Escape accepted=1。未自行复跑全部CTest或原生GL。
- 审查者用PowerShell Parser解析package.ps1：0错误；原始前三字节EF-BB-BF，保留UTF-8 BOM。未执行打包脚本。
- 读取主管产生的build/Testing/Temporary/LastTest.log：10项均Test Passed，最后模拟验收与本角色复跑一致。LastTestsFailed.log残存旧selection记录，最新完整LastTest.log显示该项通过，不将陈旧失败清单误判为当前失败。
- 主管报告整轮CTest为8.44秒；本角色未据此声称自己的全套运行结果。主管原生模拟操作正在串行验证，待实际输出与截图核对。

非阻断建议已通知主管：package.ps1:1-10 允许显式 `-Version` 与CMake不一致，随后包名与manifest会使用覆盖值；例如0.3源码可被标成1.0.0。当前默认调用读取CMake为0.3.0，符合本版路径。建议始终读取项目版本，并拒绝不同的显式参数；若修改，复查相应版本校验与脚本解析。

本轮审查SHA256：

| 文件 | SHA256 |
| --- | --- |
| src/equipmentwindow.cpp | 87DEF3D7D09DAB695BA4CA72149211A4DC17F6FA7457CE334BBC8EE2D628095C |
| src/main.cpp | 5D430A80500F336F5B18674BB0909AD9EFC87F3FBE0E38C587E4A69457DFE34F |
| CMakeLists.txt | 71B4883576FEA87B6F0BD3D550A91974A3860496DF41C5770C061BF686276BD3 |
| tests/simulated_user_tests.cpp | 60268533B6FB984823B7641E8D58AD44BA39EE6A9F969D614BD9F676B959BD6D |
| scripts/package.ps1 | 857EE437D68B3C22319621E9C38C72192398197C7E16FF671C89FED061B725FA |
| build/bin/simulated_user_tests.exe（审查者运行） | 3E12AE3AC02E1BCBBDFC0C34A8C312933E4CECB0CA211B538F00B236AA24CC42 |

下一步：如主管调整打包校验，独立复查；取得原生及包的真实结果后核对与当前范围对应。真实用户0.3体验仍单列待验收。

## 0.3 修正复核与发布前结论

主管采纳版本建议后，重新读取package.ps1完整内容、0.3发布记录、使用指南及模拟反馈/交接。Window、main、CMake和模拟测试的SHA256与首轮相同，业务行为未额外改变；package.ps1新SHA256为 C9032E560C35FF18BB15E8E5125D9FA6117BA84451383424E3CD83DB00E4984D。

修正已闭环：package始终读取CMake版本，显式不同Version在建立输出目录或复制文件之前throw；包名与manifest使用同一个实际版本。审查者先读脚本后实际调用 `scripts/package.ps1 -OutputRoot build/reviewer-version-rejection -Version 1.0.0`，捕获“禁止用打包参数伪造与源码不同的版本。”；目标目录调用前后均不存在。重新Parser检查0错误、BOM仍EF-BB-BF。该检查只走拒绝分支，未生成包。

主管报告原生 `-platform windows` 实际退出0，renderer为原生OpenGL，filteredPick与四焦点Escape全部accepted=1。审查者独立打开build/simulated-user-0.3-native的六张截图：过滤前搜索not-found、清单为空而模型可见；过滤后搜索清空、清单SU-TARGET与属性一致、模型橙色高亮；四张Escape图均保留target搜索与已隐藏标记，清单无选择、属性提示请选择零件、隐藏/隔离禁用、模型保持隐藏。中英文可读，未观察到截断和遮挡。图像支持状态核对，原生执行本身由主管完成，不记为审查者再次运行。

结论：本次范围的0.3实现、测试和打包脚本修正独立审查通过，无待关闭的阻断项，可按持续授权建立0.3检查点并打包、同步远端。完成包仍需主管核对manifest/EXE哈希、依赖隔离启动和敏感文件排除；本结论不提前把尚未生成的包记为已验收。真实用户体验仍待用户实际验收；子对话框Escape隔离和长期资源压力维持首轮未实测边界，列后续关联回归，不扩大为本轮已验证。

0.4 api在建代码、MySQL、网络、Redis和批注不在此0.3通过范围，本角色继续承担后续版本独立审查。发布记录反馈编号写SU-003-01/02而源台账为SU-03-01/02，已通知主管统一文档追踪编号，属于非行为小项。
