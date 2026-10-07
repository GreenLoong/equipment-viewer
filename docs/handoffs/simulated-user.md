# 模拟用户 Agent 交接

更新日期：2026-10-08。职责是模拟工艺工程师进行实际控件操作并提出体验反馈；不承担业务实现或独立代码审查。所有反馈明确为模拟来源，不是客户或真实用户验收。

## 已落盘文件

- `tests/simulated_user_tests.cpp`：仅临时数据库，真实控件鼠标与键盘输入；覆盖搜索过滤模型点选，以及清单/画布/搜索/按钮焦点下Escape清除选择。`--observe` 为基线观察，默认模式为0.3验收。
- `docs/user-feedback-0.3.md`：两项优先模拟反馈、操作、主管执行的真实基线/修复证据与模拟验收通过结论。
- 本交接文件。没有修改业务代码、CMake、正常数据库或公共文档。

## 实际执行与未完成项

- 新Agent的 `exec_command` 只读访问已成功，退出0；没有旧Agent的sandbox setup refresh错误。
- 已读取工程AGENTS、有效技术规范、working-state、用户指南及窗口/场景接口、现有控件测试和构建脚本。
- 默认Git因当前用户与仓库属主不同而拒绝；使用本次命令参数 `git -c safe.directory=D:/作业/简历/工作模拟 status --short` 已成功，没有更改全局配置。
- 早期读取时AGENTS、README、.gitignore存在未解决合并状态，由主管处理；最新Git状态已无未合并标记。本角色没有修改这些文件。
- 主管已接入CMake并实际运行 `simulated_user_tests -platform offscreen --observe --snapshot-dir build/simulated-user-0.2`，退出0；软件投影。基线日志：`SEARCH_FILTERED_CLICK accepted=0 selected='' search='not-found' rows=0`，并提示先清搜索；清单/画布/搜索/按钮四种焦点的Escape均 `accepted=0 selected='SU-TARGET' search='target' hidden=1`。末行 `observed=1 filteredPick=0 escape=0 mode=observe`。
- 本角色根据主管回传的真实基线日志更新证据，并视觉检查 `02-filter-after-pick.png` 和 `03-escape-search.png`：结构状态与日志一致。offscreen截图文字为方框，不据此评价中文可读性。`--observe` 的退出0只说明操作前提完成。
- 主管已修复两项需求，实际执行 `simulated_user_tests -platform windows --snapshot-dir build/simulated-user-0.3-native` 退出0，原生OpenGL；过滤点选 `accepted=1 selected='SU-TARGET' search='' rows=1`；四种焦点Escape全部 `accepted=1 selected='' search='target' hidden=1`；末行 `filteredPick=1 escape=1 mode=accept`。
- 本角色独立查看0.3原生六张截图：过滤点选后搜索清空、清单及属性同步、模型橙色高亮；Escape四场景选择及属性清除，搜索和隐藏保留。中英文可读，无已观察到的截断/遮挡。两项模拟验收通过，非真人验收。
- 主管报告10项CTest全部0、8.44秒，包含offscreen模拟验收。独立代码review仍在进行；本角色不宣称代码审查通过。
- 本轮保留测试主体未修改。原生平台旧版基线未运行，不作旧版原生问题已复现声明。

## 接手入口

先读 `docs/user-feedback-0.3.md` 与测试源文件；0.3两项模拟验收已完成，等待主管的独立代码审查与整体交付。后续0.4拟测试共享设备两客户端及错误场景；收到新的接口/任务后再建立操作用例，不提前声称共享或断线已验证。

当前已验收行为是：有效模型点击被搜索过滤时清空搜索并联动选择；Escape只清当前选择，保留搜索与模型显隐。需求决定及独立代码审查由主管协调；图像与日志覆盖范围详见反馈文档。
