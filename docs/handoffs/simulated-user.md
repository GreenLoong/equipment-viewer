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

## 0.4当前任务

已读取新版AGENTS/规范v1.1、working-state、冻结API契约及ApiClient/CollaborationDialog。新增 `tests/shared_user_tests.cpp`、`tests/run_shared_user.mjs` 和 `docs/user-feedback-0.4.md`；没有修改业务、CMake或API实现。

Qt测试创建两个真实EquipmentWindow、两份临时SQLite，通过控件发布/刷新/下载；覆盖中文/斜线/点/尾空格精确ID、同名零件、重复操作保护、错误令牌、本次真实API关闭、本地离线重开、pending时Escape取消及父窗口选择保护。挂起HTTP端口为受控取消场景，真正确认请求到达与socket关闭，不调用客户端回调伪造。

Node harness已检查源码并执行 `node --check tests/run_shared_user.mjs` 退出0。强guard只允许明确配置的空ev_test/3310/ev_app；异步spawn Qt；finally只drop本轮在空测试库创建的3表，并关闭pool/HTTP和清本轮前缀匹配的临时目录。正常服务与资料不操作。

未完成：主管已接入并编译共享Qt测试目标，并报告旧11项CTest通过；真实共享GUI harness尚未运行。恢复后已重新读取handoff、反馈与两份测试。独立审查提出ApiClient响应截断/取消回调自毁问题，主管先修复；3310测试MySQL待主管helper恢复。等待主管明确通知修复和环境就绪后，串行运行offscreen/windows harness，按真实日志与原生截图更新0.4反馈。不启动harness、不停止8765旧API或操作正常服务进程。

运行入口见 `docs/user-feedback-0.4.md`；构建通过不等于真实链路通过。测试guard保持ev_test/3310/ev_app且数据库必须为空。两客户端目前是同一测试进程内独立窗口和本地数据库，跨电脑/独立桌面进程不在证据范围。真实体验问题需要立即在反馈文件保存具体操作、实际表现、日志与未通过条件，再向主管汇报。

最新执行：主管ApiClient修复并恢复3310后授权开跑，本角色首次offscreen harness退出1。发布/重复发布保护/modal Escape保留父选择通过；下载检查因测试假定JSON顺序失败。读取既有SQLite ORDER BY id确认应按ID核对而非数组索引；已仅修 `exactParts` 为精确ID关联，补位置及不适用字段为空断言。harness cleanup=1，本轮ev_test表已清；windows未运行，等待主管重编共享测试目标后再串行复验。详细失败证据已存0.4反馈，不将测试错误当业务bug。

## 0.4模拟验收完成

主管重编按ID核对的Qt测试后，本角色亲自串行运行offscreen与windows harness，两轮退出0（约6.3/8.35秒）。全部SHARED_USER字段为1，SHARED_HARNESS accepted=1、cleanup=1。harness真实SQL核对两项精确设备ID与四零件，并清自己创建的三表后确认空库。日志在 `build/shared-user-0.4-offscreen.log` 与 `build/shared-user-0.4-windows.log`。正常API8765/ev_shared/MySQL80/正常SQLite未操作。

已独立查看Windows八张截图，发布/重复拒绝/下载/错误令牌/pending/服务断开提示清楚，令牌遮蔽，离线新窗口保留同名零件的精确ID、模型和完整属性；中文/英文可读，无观察到遮挡或截断。八项模拟验收通过，非真人验收，代码review由独立角色另记。第一次顺序断言错误保留为历史，复验未发现业务缺陷。

下一版0.5建议已写入 `docs/user-feedback-0.4.md`：所选零件工艺批注（按设备/零件精确ID关联、作者/时间/修订号、跨客户端持久可读），以及可变批注的修订冲突保护（旧版本拒绝、草稿保留、刷新后显式重新提交）。需求来源是实测0.4只有共享几何和只读属性入口，没有工艺意见入口；不把尚不存在的批注/冲突功能写成已观察到bug。待主管冻结0.5契约后分配新测试，暂未新增0.5文件或业务。
