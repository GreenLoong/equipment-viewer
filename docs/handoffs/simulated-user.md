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

## 0.5准备状态

主管已发布0.4源码aef7499并冻结0.5工艺批注/CAS契约。本角色已读新增契约，并仅修改 `tests/run_shared_user.mjs` 收尾：取得ownTables空库所有权后，先drop annotations，再part/equipment/schema_version；node --check退出0。未执行GUIharness，未改业务、CMake或API。

已与backend_v1协同：他当前独占ev_test进行0.5真实验证，integration先清annotations并确认空库后会通知。本角色等待主管GUI接口/完成通知再约定独占时段，不同时跑ev_test。

新增 `docs/user-feedback-0.5.md` 保存九项待运行用例：精确设备/零件scope、多行意见、同revision冲突、独立草稿保留、刷新后显式重提交、结果不确定时同UUID重试、断线草稿、真实服务重建持久与UTC显示、设备级意见和显式放弃/关闭取消。新GUI控件接口尚未给出，C++测试未提前修改。受控代理丢一次已提交响应的方案用于真实幂等重试，不调用网络回调冒充错误。

## 0.5实际测试已落盘待编译

已收到冻结控件接口并读取当前collaborationdialog.cpp，实际扩展 `tests/shared_user_tests.cpp` 至约31KB，保留0.4用例并加入核心创建→两人同rev→冲突草稿→刷新→显式rev3，dirty Esc/标题close与Discard后回收、中文粘贴/剪贴板还原、同名/跨eq/设备nullscope、已提交201响应丢失同UUID重试（乙先改rev2，甲重试当前DTO保留草稿再显式rev3）、断服草稿和重建持久/UTC。

`tests/run_shared_user.mjs` 约16.6KB，新增真实API转发故障代理、pause/resume同端口服务重建，以及wrong noteId/part、过滤scope、缺part、延迟旧GET后取消的新请求保护。final真实SQL核3批注及revision1/3/4，幂等不重复，仍只清本轮拥有4表。node --check/diff --check退出0。不是仅规划，源文件已经落盘并通知主管立即重编；未运行旧exe、未改业务/CMake/API。

后台已报告0.5integration结束并清空ev_test；本角色待新测试构建通知后协调独占运行。静态发现无时区本地metadata后主管已改明确UTC显示，尚未运行，不当作已复现业务bug。实际测试入口/完整范围见0.5反馈。

首轮0.5实际offscreen执行已发生：退出1（约5.7秒），0.4发布与下载基本路径先通过；设备弹出combo选择 `..` 后实际currentData不符，停止在进入批注前。日志 `build/shared-user-0.5-offscreen.log`，owned4表cleanup=1。先仅为test helper加实际/期望ID、索引、状态与失败截图诊断并请求重编，不提前定性产品bug。windows尚未跑，0.5核心未验收；具体步骤已落盘0.5反馈。

## 0.5模拟验收完成

主管授权仅测试目标增量构建，并通知全量build结束。本角色自行Ninja重编诊断/修正的shared_user_tests，按真实Up/Down逐项选择并等待异步scope，修正冲突文案假设后加真实HTTP409/REVISION_CONFLICT核对；未改业务/CMake/API。诊断日志保留，鼠标popup模拟未选中不判为产品缺陷；本轮范围以实际键盘切换为准。

最终亲自串行offscreen/windows完整退出0：日志 `build/shared-user-0.5-offscreen-semantic.log` 与 `build/shared-user-0.5-windows.log`。九项主用例及wrongID/part/scope/missingpart/旧取消回调保护全1；真实代理确认201已提交丢响应→相同UUID原payload200、另一客户端已改DTO仍保留不同草稿；真实pause/resume服务重建与SQL核3条批注revision1/3/4及author/updatedBy/精确关联通过。旧0.4用例全通过，owned4表cleanup1，已通知backend ev_test空库释放。

独立查看九张新增Windows图：保存与草稿分区清楚，冲突/断线/错身份红字可读，dirty Esc/titleclose后草稿仍在，刷新后最新版本与旧草稿并存，UTC时间明确，令牌遮蔽，持久化新服务读取正确。无阻断体验问题，不需生产修复。列表长预览可横向滚动，完整正文在保存区；200条外选择/极端窗口尺寸/跨电脑尚未本轮实测。0.5模拟验收通过，非真人、非代码review。所有历史失败及测试修正已落盘feedback0.5。

## 0.6模拟用户准备

主管报告0.5源码检查点d3d720e、包native smoke通过，独立包审查/远端同步由主管继续。本角色已读取现有API契约与 `docs/cache-plan-0.6.md`，新增 `docs/user-feedback-0.6.md` 保存七条有限业务用例：同设备MISS/HIT与MySQL读计数、共享操作回归、缓存断开回源保草稿/几何、重连恢复、批注最新修订必读MySQL、无令牌缓存前401、精确ID不串。

0.6当前只计划，缓存契约/配置待主管冻结，WSL挂起且用户重启许可尚待处理；本角色不restart/shutdown、不启动缓存、不抢ev_test、不改生产或旧通过0.5 tests。收到正式接口后建立独立缓存harness，使用随机namespace/空ev_test守卫和本轮接入故障代理，不能停正常服务或FLUSHALL。性能只报告实际耗时/规模，不以HIT推导一定更快。0.9另做交互包体验。

## 0.9受控恢复脚本待主管全文检查

主管另授权唯一新文件 `tests/verify_mysql_restore.mjs`，已落盘并node --check退出0，尚未执行、不占ev_test。先读取已独审的 `api/tools/backup-mysql.mjs`/preparePrivateOptionFile，再编写本次测试。

脚本核marker/实际datadir/账号/ev_test3310与空库后取得本轮4表所有权；真实API写含中文/引号/斜线/尾空格/点ID设备与批注revision2，关闭本次API后mysqldump指定本轮4表、single-transaction/tz-utc二进制落盘至忽略的 `.recovery/tests/<UTC-随机后缀>`。密码只进排他临时ini，不在argv/log；cleanup失败明确非0。manifest含SHA/字节数，恢复前两次核hash并拒绝数据库创建/切换语句，只有本轮生成的dump可用mysql CLI导回ev_test。

重建API后核精确geometry/DTO、UTC epoch、原UUID幂等返回revision2、旧CAS409不覆盖与显式revision3，finally仍只清本轮annotations/part/equipment/schema_version。dump/partial和manifest保留，不能声称正常库恢复。全程不停止MySQL实例或正常API，不恢复/删除ev_shared。已通知主管首次执行前全文检查；收到检查完成及独占ev_test协调通知后才运行。0.5旧测试未修改。

## 0.9受控恢复已实际通过

主管全文检查并确认脚本SHA256 F4D3C00463686718781E10F2401B115FBDA5A25591EBEB85CB752A44F2EA8064及ev_test为空后，本角色亲自执行 `node tests/verify_mysql_restore.mjs`，显式EV_TEST_CONFIG_FILE，exit0（约1.78秒），accepted=1与cleanup=1都成功。日志 `build/mysql-restore-simulation.log`；未恢复正常库或停止实例。

dump/manifest在 `.recovery/tests/2026-10-08T05-30-45-888Z-c7cfdc59`，6269字节，SHA256 B8CAE4C023BE8DF999C95677164A1DFA9F4CE32738F91E4C6A7BE1A50BC99AAD，独立Get-FileHash与manifest一致。manifest restored/restoreVerified=true，normalDatabaseRestore=false；真实freshAPI精确geometry/设备零件ID/批注rev2与UTC epoch/原UUID幂等/旧CAS409/显式rev3均通过。finally只清本轮4表并查空，已通知root/review，ev_test释放；dump正文没有打印，Git check-ignore两证据都忽略。独立审查仍由review角色判断，不把模拟恢复当正常数据恢复或真人验收。

已读取主管新冻结0.6契约并同步0.6计划，正式MySQL详情读计数从鉴权status取得，批注没有额外计数字段不能虚构；固定Redis6380及测试namespace约束保留。0.6实际Redis尚未运行，WSL不由本角色restart/shutdown；下一步独立缓存harness，旧0.5已过测试仍不修改。

## R09恢复CLI收尾补强

独审接受原dump恢复数据证据，另指出CLI管道失败后kill即throw可能先DROP表、没有确认子进程close。本角色修 `verify_mysql_restore.mjs`：导出spawn可注入的runRestoreCli，main入口guard保证import不执行MySQL；失败/总超时destroy管道、kill后有界等待close，未确认只报自己PID/path并让主流程cleanup=0保留ev_test，不DROP。kill抛错也不绕过守卫，error listener保留。最新脚本SHA F5AC5A9785CD631112A2BE0BA9FB94334EAC94AE897D7AFA7818AB3DBF427EA2。

新增 `tests/mysql_restore_cli.test.mjs` 六项纯fake回归全部通过：正常close、pipeline失败先kill后close、kill不close有界未确认、晚close、kill抛错、挂起超时。没有真实进程/DB/WSL操作。主管与review已收到待复核，尚未新版本真实复验；原恢复成功dump记录仍保留，不用fake通过替代新路径真实恢复。

## 0.6实际测试文件与恢复状态

新增 `tests/cache_user_tests.cpp`（SHA1AC5D2AA65AF93096CA0D167A1EC27B1CD04CC7A2B7D52D3D1C8E9C4770F6B67），主管已接CMake并编译成功；真实GUI刷新/下载/临时SQLite/草稿/桥故障回源/最新批注/恢复/Discard关闭。未执行无harness的Qt，未改旧0.5源。

新增 `tests/verify_cache_user.mjs` 最新SHA67AD0D53EC166ACA725993B4A549F34423D2F3E721FEE296AB39A238B0FA2911；仅语法检查通过。真Redis PING→空ev_test/random namespace→MISS/HIT/status读次数/401→GUI观察代理→持有桥暂停/重建→批注最新revision2；按equipmentCacheKey精确DEL2键、owned4表、桥children退出确认。已复用R09helper确认Qt close，未确认则cleanup0不DROP。Root需首次全文读此最新版本，当前未跑WSL/GUI或占DB。

额度恢复时主管报告用户已自行启动WSL且/bin/true0，本角色不global restart/shutdown；3310down由主管恢复。等R09复核、缓存harness全文读与独占ev_test/6380通知后才串行真实验收。新0.7意见由真人用户提出（经主管转述：旋转方向、抗锯齿、隔离双击、视角回正、标尺、CAD导入），主管维护真实反馈；本角色不写成模拟发现，先完成0.6，再协助交互验证。

## 0.6真实模拟验收完成

主管全文检查最新cache harness及runRestoreCli，缓存源码R06-03复审接受、官方TCP13纯回归通过；真实LinuxRedis7.4.7 ready、MySQL3310恢复PID70128、ev_test查空、Windows6380空后放行。本角色持自己的6380桥，不停LinuxRedis/globalWSL/正常API，亲自跑offscreen退出0（约4.16秒），等待root原生资源测试60413退出0后windows退出0（约5.04秒）。

两个日志 `build/cache-user-0.6-offscreen.log` 与 `build/cache-user-0.6-windows.log`：真MISS/HIT、冷MySQL详情+1/热+0、query/path/点ID、缓存前401、真实GUI刷新下载/临时SQLite、桥暂停BYPASS回源保几何/选择/草稿、批注最新revision2、桥恢复HIT全部accepted1。cleanup1确认owned2key/4表/桥children收尾，已向root通知ev_test/6380/焦点释放。延迟为各一次2零件样例：offscreen冷246.773ms热16.796ms；windows冷254.318ms热17.366ms，不泛化性能结论。

独立查看Windows六图：本地模型高亮与完整属性，初始保存内容/多行草稿保持，断桥后最新保存rev2/updatedBy乙/UTC清楚，旧稿保留，重连后仍正常，token遮蔽；无阻断体验问题。0.6七项有限模拟验收通过，非真人/非代码review/非0.7新交互。详情已更新feedback0.6，旧0.5源仍不改。R09新恢复CLI真复验由主管review后另协调，原dump数据证据与fake6保持分开。
