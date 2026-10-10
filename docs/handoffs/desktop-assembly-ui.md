# 正式Qt装配工作台 UI 交接

日期：2026-10-10。实际工程 `D:/作业/简历/工作模拟`，分支 `codex/desktop-assembly-1.1`，基线 `111e8bb`。已读主树AGENTS、engineering-standards及working-state；独立预览历史未当作原生Qt验证。

## 本角色职责与已落盘范围

- `src/equipmentwindow.h/cpp`：原生Qt控件及只读原/显示参数。
- `src/equipmentcontroller.h/cpp`：控制器唯一选择、隐藏、隔离状态。
- `CMakeLists.txt`、`src/main.cpp`：桌面1.1.0及新原生检查接线；后端API保持1.0兼容，未修改其版本。
- `tests/r07_window_tests.cpp`、`tests/cad_user_tests.cpp`：按新真人契约调整受影响窗口断言，默认尺寸off后测量专项显式开启。
- `scripts/build.ps1`、`scripts/package.ps1`：可选独立BuildDirectory，UTF8 BOM保留。
- `samples/cad/MIT_E_Vent_Pre_Production_Model.step`与README：原字节公开82件STEP与CC-BY3来源说明；未复制预览JSON。

未修改SceneView、数据库/正常资料、后台服务或其它Agent文件；未构建、执行测试、提交或推送。root负责build-1.1独立构建及正式交付，正在首轮构建，业务文件已freeze等待实际输出。

## 窗口接口与交互

4类 `viewModeSelector` data0彩色Isometric、1Engineering、2二维projection、3ThreeViews。`projectionSelector` 使用SceneView枚举Front/Top/Right，仅二维可见。`axisPlacementSelector` Corner/Model/Both/Hidden。`dimensionsCheck`默认false，`gridCheck`true。`explodeAxisSelector` Auto/X/Y/Z；`explosionSlider`与`pullSlider`0..100。`restoreDisplayButton`清全部offset、`restorePartButton`仅pull0；`pullDirectionSelector`数据1/-1。`partColorButton`打开Qt颜色对话框，`autoColorButton`调用Scene.resetPartColor恢复稳定默认色，清单用真实演示色块。`fitVisibleButton`适配当前可见几何保方向，`focusSelectedButton`聚焦可见选中；旧`fitAllButton`“视角回正”仍调用fitAll，保留已有回正操作。

原属性 `positionX/Y/Z`继续展示原始资料，不改变已有名称/ID/material/mesh尺寸字段；新增`displayPositionX/Y/Z`与`displayOffset`显示源position加Scene.displayOffset，只读long double格式避免极端范围转double溢出。模型切换成功后控件与Scene都回彩色、角落轴、尺寸off、gridon、Auto、全部offset0、备用正向，颜色由Scene.setParts重建；失败未执行上述替换。

Controller新增`isolatedPartId()`。隔离中有效选择不同零件直接切隔离；空ID只清选择，保目标及隐藏集合。窗口清单与Scene都通过Controller选中，不自行改变隐藏集合。切换隔离才适配；清选路径不fit。清单视觉就地更新避免信号栈中删除item，移除旧双击二次重建；搜索只过滤清单，清掉被过滤选择时仍保隔离。旧单击不切隔离测试断言按新契约更新，并保留无效ID/同名ID/正常手动隐藏不自动隔离等验证。

## 图形与独立测试接口

Scene API由assembly_state负责并已冻结：Isometric/Engineering/Front/Top/Right/ThreeViews、4轴位置、尺寸/网格、axis/fraction、pull/fallback方向、focusVisible/focusSelected、color/reset、displayOffset。本角色仅调用，未嵌浏览器或新增依赖。

CMake已接`assembly_scene_tests`（graph owner）与`desktop_assembly_user_tests`（独立模拟/审查owner），均offscreen CTest；root另跑Windows原生截图/交互。未把offscreen或独立DemoGUI当作正式原生Qt通过。

## 构建和资料保护

两脚本`-BuildDirectory`默认build，可传相对或绝对的当前工程子目录；GetFullPath解析后拒绝工程根/外部目录及src/tests/samples/docs/scripts/tools/api/.git/data/.recovery。无删除/迁移目录行为。package从指定bin取EXE与DLL，缓存版本须与源码版本相同，避免将原1.0 build直接标为1.1；完整EXE版本/包内容仍由root实际核对。

82 STEP SHA256 `286F8C1C869C05883FD751EE40D8D60A2956C8234C26C3361F1B49A08AC3A338`，与独立树固定公开原件相同。README记录berserkdesign、固定FreeCAD提交与文件提交、CC-BY3授权；MIT E Vent为模型名称，不是MIT资产许可。既有许可证随samples保留，交付包按已有samples复制规则收录。

## 实际检查与待办

PowerShell AST两脚本解析成功；本角色范围git diff --check退出0；复制STEP实际SHA核对如上。未执行构建、测试或实际原生GUI，不能记为通过。root已通知启动首次build.ps1 -BuildDirectory build-1.1，首轮输出后处理真实编译/功能问题；必要行为修复需独审关联复查。

## 真实CAD控件有限专项（已保存，待root实际运行）

root二构建118/118编译链接完成后允许本角色保存测试。仅修改`tests/cad_user_tests.cpp`及必要`r07_window_tests.cpp`；业务和CMake保持冻结，本角色未构建或执行。

新参数`--assembly-preview-check`选择有限1.1专项，要求`--assembly <真实STEP>`及`--expected-assembly-count 6|82`，`--snapshot-dir`与既有受信Python/modules/converter参数仍可用，不要求bearing。通过Window.importFile实际后台转换、事务保存与完整读回后检查原生控件，不塞预生成JSON，不重复旧轴承/取消/长期资源环。

真实键盘combo/slider、CheckBox焦点Space和按钮mouse点击检查：默认彩色/尺寸off；工程轴测、二维俯视、三视图真帧截图；三视图关闭grid/尺寸/XYZ后，三个中央区域都须有实际几何像素，排除只测标题或参考线。Windows必须取得有效QOpenGLWidget，offscreen若运行日志native=0，不能假作原生证据。截图仍来自实屏，不合成。

Auto/X/Y/Z轴：非活动轴offset0、原中心左右的外向符号保留、原排序及间距不压缩，不镜像实现中的具体倍数常量。单件pull仅选中offset变，其它件offset完全不变；单件/整体复原控件归0且原始mesh/source/位置/尺寸fingerprint不变；三视图空白单击保选择，双击清选择保唯一隔离，再清单换件隔离；资料库读回与独立重启连接fingerprint仍完全一致。

首CTest报告r07尺寸未开启：将旧测试测量专项改真实focus/Space并断言checkbox和scene开启，避免QCheckBox留白中心点击未命中；未修改业务默认off。原CAD测量helper同样显式键盘开尺寸。

冻结测试SHA256：cad_user_tests `58FB9571C811B71FCF3ED66ACF3E079F4A7AED5B376F29DC464FB6457760F79F`；r07_window_tests `7EDE319D4A0CED483D579A305EC18828F539B7CD128B09D9CA02A70E30A7243F`。范围diffcheck退出0，已通知root增量编译两target/fullCTest，审查者已获实际diff复审；本角色不预记测试通过。

## CAD偶发失败诊断（恢复后的额外授权）

root报告同一cad_import_process_tests binary一次约0.5秒失败、单项约5.61秒通过，原因未定。本角色仅在`tests/cad_import_process_tests.cpp` successAndRepository原成功断言之前添加失败条件输出：callback error全文、实际bytes.size、期望协议bytes.size、process busy及owned pid。保留原断言、等待时限和测试流程，不自动重试、不修改业务、不把复跑通过等同原因已消除。

该文件范围diffcheck退出0，短diff已发root阅读后编译。本角色未编译/执行此诊断版本，未关闭偶发问题；后续实际失败日志/成功重复观察由root记录。

## Job历史计数误判修复（待实际回归及独审）

root第三完整CTest再次失败在self-delete成功通知，错误为“主退出后仍有子进程”；同fixture未主动创建子进程。root native6新CAD专项通过，native82窗口导入未取得82资料；可信私有worker独立转换82已完整成功（157558顶点/175647三角），不能归咎源模型/预算，未减少实体或替换预制JSON。

已确认代码级边界：旧confirmTreeEmpty依据TotalProcesses>1和Active>0即判livechild。微软官方定义Total含历史退出/关联失败成员，Active在退出引用释放后递减；该条件不足以证明当前活子进程。参考：[Job accounting](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_accounting_information)、[Job完整PID列表](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_process_id_list)。具体失败轮Windows计数/成员尚未观测，不能提前关闭偶发问题。

本角色获root额外授权改src/cadimportprocess.h/cpp与该unit。正常finished先singleShot0退出QProcess信号栈，计时预算从原finished开始不重启，让QProcess句柄/引用完成清理后验证。Active>0时完整读取本Job成员快照，ERROR_MORE_DATA/partial扩容最多4096成员，assigned==returned才可信；OpenProcess仅本Job快照成员，IsProcessInJob核归属后Wait0核实际存活并立刻CloseHandle。只有仍活且非knownmain的本Job子进程才TerminateJob并拒结果；已退出/主退出记账延迟继续等Active=0。查询/归属/句柄等未知失败保busy与临时目录，不提前读结果，不放松livechild门禁。

新增只读jobExitDiagnostic含known主PID、Total/Active、完整成员存活状态，不含输入。unit失败（正常成功、自删、复用）输出该诊断；新增真实child.wait已退出后主成功的8个独立测试轮，首败即停不是失败重试。原活子持文件/查询失败/终止失败/取消重试/析构门禁测试全部保留。cad_user82失败处仅加actual设备ID/count、状态、workerError及Job诊断，不改原断言/预算/实体。

冻结SHA256：cpp `FEED766888349DE4DB35D1A102D1900EEBD15850DBF52E3BB02C76D8EC30D65D`；h `7E94D3EBCE1060FF893E76D6BBD24ED03038C03877ACA20E365120740CE679EC`；unit `77A66FB9644341E25B06595536F45E2BD673314011C326265AD06C7C4D788CDB`；cad_user `81165435145A5467F8D25E35C74094262B82556493850022F77FE01D12CE3AB6`。范围diffcheck退出0；本角色未构建/运行，root已获通知编译并重跑本Job回归和真实82窗口原STEP，独审已获完整差异，尚未记通过。

## 已观察真实活子进程，增加程序映像诊断

root第四full 17/18，self-delete仍失败，但精确诊断已确认main73892、Total2、Active1、完整成员76272为liveChild，终止后Active0。它是真活成员，不能仅以“记账滞后”关闭问题；无spawn自删fixture为何出现此成员尚未解释。

仅进一步加诊断：对已确认在本Job且Wait0=TIMEOUT的非主成员，在仍持有其受信句柄时QueryFullProcessImageNameW读取程序映像路径（固定32768字符上限）；失败记录win32错误，不改门禁或吞掉错误。Job诊断附主finished实际exitCode与NormalExit，未收到finished时明示unobserved。无读取命令行、模型输入、环境凭证；终止并拒绝活子进程逻辑不变。root将新binary实际probe区分WerFault/launcher等原因，本角色未构建/执行，不提前解释为某个进程。

## 显式无控制台后台转换（待回归）

root第五affected仍在历史child-case第1轮失败：main31664、Total3、Active1、owned76044真live，程序映像QueryFullProcessImageNameW错误5，主exitCode0/NormalExit1。映像未确认，不能断言该成员就是console host。源modifier只附EXTENDED_STARTUPINFO_PRESENT，因此root提出控制台辅助进程残留假设并授权按GUI后台转换本应无控制台的需求最小修正。

现modifier明确加CREATE_NO_WINDOW，保STARTUPINFOEX JobList、stdio管道与所有Active0/read-result/livechild-kill拒绝/查询失败门禁；无helper白名单，无等待后吞error。jobExitDiagnostic额外记录modifier实际creationFlags十六进制。未改测试断言、模型预算、输出或子进程检测。root需新8轮、真实活child失败保护、native82原STEP窗口全部回归，才能记观察成功；即使通过也只记录“无控制台创建后观察成功、console hypothesis”，不伪造未知PID映像确认。本角色未构建/执行。

root进一步授权修历史child-case测试前提：其Popen显式creationflags=subprocess.CREATE_NO_WINDOW，避免该用例额外产生不由Python child.wait覆盖的控制台宿主，确保它测“后台Python child已退出后的历史计数”而不是未声明的系统控制台生命周期。保其8轮/原成功断言/Active0检查，原所有live-child/查询失败/终止失败fixture不变，未减少覆盖或修改期待值。

## NoWindow观察未关闭问题，追加受限Toolhelp诊断

root第六full仍17/18，实际flags=0x8080400，但历史child第1轮main18308/Total4/Active1、owned71964仍活，image query错误5、主exit0/NormalExit1。故CREATE_NO_WINDOW生效不能作为此偶发原因已解决的证据；此前console解释保持未确认，不能关闭。

root授权仅增加fallback：映像查询失败时Toolhelp32进程快照只匹配已由句柄确认属于本Job的该PID，输出其szExeFile与th32ParentProcessID；不输出其它进程信息、命令行或环境。快照失败/未匹配只记录诊断，所有活成员终止拒结果、Active0与未知失败门禁保持。没有根据未知helper名称白名单或吞error，等待新实际名/父PID证据。仅cpp新诊断，测试、预算、源模型未改；本角色未编译/运行，范围diffcheck0。

## 已观察诊断期间退出竞态，第二句柄复核（待回归）

root实际Reuse诊断同owned句柄首次WaitTimeout，后Toolhelp snapshotMatch=none，提示进程可能在两次观测间退出。现在映像/快照后同句柄再Wait0，若signalled记录exitedAfterProbe而不判live；仍TIMEOUT的本Job真实child在Term前再查完整Accounting，只有Active0可直接接受，unknown保busy/目录，非0仍kill拒。无白名单、无任意等待、无重开确认预算。若新实际证据仍显示退出阶段歧义，root允许以后在原预算内有限初始确认窗口；当前未盲加。

新增test-only beforeJobMemberRecheck为空生产hook，禁止重入/销毁，不暴露Job/PID。回归通过真实child文件信号在首次alive后退出，真实进程句柄Wait确认；第二probe必须记录exitedAfterProbe且最终Active0，未注入假Accounting/PID/退出结果。Handle RAII防首败泄漏；历史child.wait增强assert returncode0；原长活child持文件、Query/Term失败门禁不变。新版header诊断范围同步真实字段。

冻结cpp `38367A8BDF36992AE0127ACDA0BCEC11E387FDE2D507ACCAC43019DBFE3BB0F8`，h `8A3FE05024DF6AE8E9FCE08F4DD46E90E3EF7DE6C9F25F01191F56D4B79C673C`，unit `811BBBD42077658374C76C5D5FA60FB393172F1C33EE3ED0329EC24B978B0B25`，范围diff0。root已读后将只增量cadtarget实测，review已获差异，本角色未构建/跑。

root另已观察native82 JobActive0完成而严格parser拒实体33共线退化三角，由图形/转换owner另查源转换，不再归入Process退出问题；未用删除实体或改预算避开。

## 正式启动入口分离（额外授权）

scripts/run.ps1新增BuildDirectory，1.1默认build-1.1；安全路径解析/保护目录与build脚本同规则。根“启动设备查看器.cmd”显式 -EquipmentView -BuildDirectory build-1.1，根“启动示例.cmd”显式 -BuildDirectory build继续原记录程序。默认Database仍工程data/records.sqlite，显式-Database照原样传递；未复制/修改DB或覆盖旧binary，无后台helper启动。

run.ps1保UTF8 BOM、cmd保ASCII。AST解析与三文件diffcheck0，本角色未启动入口；root阅读差异并完成正式门槛后再实际启动/包装验证。

## 精确确认conhost后的有限自然清空窗口（最新）

root恢复日志已定位：历史child8轮全过；controlledmemberRace中Python33196实测exitedAfterProbe，同时本Job9196映像确为C:/Windows/System32/conhost.exe仍活，主exit0、NoWindow有效。这是实际成员映像证据，不再仅是假设。它不能按名字白名单：主进程退出后Job成员的系统清理需要有限确认期，而读取结果的门禁仍是整个Job Active0。

新增自然退出窗口min(250ms,本次剩余确认预算/2)，只用于正常主退出，取消/停止路径为0；在原总exitConfirmationMs内复查完整Accounting、成员归属及两次真实Wait状态，Active0才接受。窗口后仍活就TerminateJob并拒结果；unknown立即failclosed保busy/目录。没有无限wait、没有重新开始预算或按conhost名字放行，保另一半预算用于终止/Active0确认。诊断记录实际naturalWindow/elapsed；保留此前实际exitedAfterProbe观察标志，避免后续轮询覆盖回归证据。

原真实持文件长活child/Query失败/Termination失败门禁测试不改；历史8轮、受控首次判活后实际退出Race仍需全部通过。仅cpp行为差异已发root/review，SHA256 `9059888147940B7A6C5C450F20CA5ADFCBF206C51628A37F94208BBB774E1914`，范围diffcheck0，本角色未构建/实跑，不提前关闭。

## 正式入口统一1.1默认（替代此前记录示例旧build建议）

root明确最新正式入口：build.ps1/package.ps1/run.ps1默认全为build-1.1，两根cmd也显式build-1.1，记录示例仍打开原record功能。显式BuildDirectory=build保留回溯旧构建/运行能力，旧目录/1.0包未动；默认Database仍data/records.sqlite与显式Database不变，不迁移或删除DB。仅默认参数/cmdarg变化。三个PS保BOM并AST解析通过，入口文件diffcheck0，未执行入口。

## 82件实屏裁切保护的有界几何收敛与诊断

root最新实际18/18全自动32.07s通过、独立native FBO4通过、6件新原STEP控件专项通过；新导出82原STEP亦完成严格import/sourceID/count82检查，首capture在frame=16,16,1320,851可见屏内后client包含断言失败。这里只记录root报告，本角色未运行这些检查；真实用户验收仍分开。

本次只改cad_user capture测试：native resize/move/raise后最多3000ms等待frame、client(mapToGlobal0+实际size)、window.size及minimumSize相同持续150ms，且frame/client同时处于available。输出全部坐标、实际size/minimumSize与settled。保原两个available.contains严格断言并再要求settled；不降低clip保护、不改产品minimum/layout，不反复resize或重新计等待预算。若仍异常，诊断能区分真实minsize过大与native异步收敛，需据实进一步修产品。

CadProcess保持已通过freeze未改；其析构早已清beforeJobMemberRecheck，已向review指出当前实际代码行218，避免重复或倒退改动。该测试范围diffcheck0；新hash已发root增量编cad_user，实际截图复验待root，不预记通过。
