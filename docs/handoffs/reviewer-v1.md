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

## 0.4 后端首轮审查：待修复复查

主管通知后端真实MySQL集成已运行后，本角色独立读取api/src的validation/repository/server、api/tools/setup-mysql.mjs、全部三个测试、package/lock、有效api-contract和backend交接；读取已装mysql2与相关依赖的实际源码/生命周期配置。Qt客户端正在由主管实现，不在此轮范围；不对进行中的Qt差异预评，也不将0.4后端局部结论当0.4发布通过。

需修复事项已发主管：

| 编号 | 位置、触发与影响 | 修正及复验要求 |
| --- | --- | --- |
| R04-01 | repository.mjs:134-137，SQL失败后rollback自身抛错仍finally release。mysql2 3.24.5的resetOnRelease默认false；若连接尚可复用但事务未确定回滚，会把事务状态及锁带回池 | 回滚失败销毁连接、不得正常归还；保留安全原错误。覆盖SQL失败+回滚失败与之后合法请求得到干净连接 |
| R04-02 | repository.mjs:31-32/120及各execute/commit/rollback只有握手connectTimeout，无连接获取、SQL或事务总截止。server.mjs:153的requestTimeout只约束收到HTTP请求，完整GET/POST后DB挂起可耗尽池与队列，close等待未完成SQL | 明确总预算与有界连接获取/命令/事务收尾；超时销毁连接，晚到获取连接也释放；不能仅Promise.race返回而把SQL留在后台。覆盖锁等待、数据库断连/不响应、排队晚到、关闭有界与后续合法请求 |
| R04-03 | repository.mjs:70-74，外键只检查引用表名equipment及列id，不取REFERENCED_TABLE_SCHEMA。已有part若引用另一库同名equipment会通过兼容检查，违反本库身份与资料边界 | 校验引用数据库为当前DATABASE()，在隔离测试中制造其他库引用或等价受控元数据并拒绝；不修改/清空正常库 |

接口文档小项：server已实现GET /api/equipment?id=<encoded>别名及严格decode/重复参数拒绝，但当前api-contract路由只记列表与path详情；主管需同步冻结契约，便于Qt读取包含点/斜线/加号/尾空格的精确ID。

独立实际执行：

- 已先读所有测试/脚本，运行 `node --test api/test/validation.test.mjs api/test/http.test.mjs`：5/5通过，报告723.613ms；未重复运行清理表的真实集成测试。
- 受控Repository连接替身制造insert失败后rollback失败，实际得到DATABASE_UNAVAILABLE、releasedAfterRollbackFailure=true、destroyedAfterRollbackFailure=false。是受控错误分支证据，不冒充真实MySQL回滚故障。
- 受控createApp健康查询挂起，将HTTP requestTimeout设50ms，完整GET后250ms数据库操作已进入但HTTP未完成；释放替身数据库promise后才返回200并正常close。说明该接收时限没有覆盖数据库处理，不冒充真实MySQL长等待。
- 对test.local.json仅在数据库ev_test/port3310/user ev_app/host127.0.0.1 guard通过后建立只读连接，查询@@datadir/@@port及表元数据：数据目录与LOCALAPPDATA专属marker匹配，剩余测试表0；关闭连接。初次探针从工程根import mysql2无法解析模块，未建立连接；改用api工作目录后上述检查成功，未改配置。
- `Get-Service MySQL80`实际为Running；没有停止、重启或改原实例。
- Git check-ignore确认service.local.json、test.local.json、node_modules被忽略；Git未跟踪api清单只有9个源码/测试/package/tool文件，没有runtime秘密。读取锁定12个依赖的实际package.json，未发现preinstall/install/postinstall/prepare；锁文件来源为npmjs及integrity，mysql2固定3.24.5。npm --ignore-scripts安装/audit0为主管报告，本角色未再次安装或audit。

其余静态路径：完整校验先于写入；ID保留原文、UTF-16及配对代理校验明确；MySQL utf8mb4_0900_bin/NO PAD、按ordinal还原；数据均驱动参数绑定，发布事务提交后直接返回摘要；HTTP错误不输出SQL/stack/凭证，流式body上限及UTF-8拒绝合理。setup默认3310/独立LOCALAPPDATA目录、空目录初始化标记、root认证/@@datadir/port/本配置PID身份检查、SQL SHUTDOWN、不强制kill；账户DDL的query仅用随机hex秘密经驱动绑定，不拼接用户数据。首次初始化成功但标记未保存的中断、Windows mode0600的ACL限制仍按backend交接披露，未宣称恢复或ACL实际验证通过。本角色没有执行setup/stop、更改数据或后台服务。

本轮文件SHA256：repository=3F99391FB2034A7F61C5AB8ED81FC7E62ED4D1CDB78AB465879B0D30B959777F；server=571AE96AD4A59FB9586AF44E2617B1D218ABACA2E24C778DF2E94FD5DA71CA76；validation=125F62A720D16C0A86BAAEA9FF366D9864075820F1E4A2078B938D4F4E5FD40A；setup-mysql=2AD983EC689D6EA863DC2E7BDF21FD3DE1E2E535F839E3BBA1D28349891B0092；integration.test=BDECCB472C4313E6A6CA865DCFB0F3AB7528AE9C93EED6512EC8E9D3E32FC0F7；http.test=74F2A9B09254B7657159C003408630D9E6580EE84B948B8F2B1CEB5A3A94339B；validation.test=6D38CE5354E713025A410B9FFC6BBD4430074A87C8F61C9E96678CDAD8E4538C；package=4CAA8A9B4D0327F8D0D820C2EDFD561FEB06A3936175E68CFF1797556631BE17；lock=D9AAA06C9C82C20DB80C0597629D9B2D3F4073120BEC86D9F5E4C21FFEF8F848；契约=88E58CA19720D1864D9772C633DBB51A7A53966471F5B383263D81D2D30124F0。

下一步：等待R04-01/02/03修正及实际失败回归证据，再独立复查；Qt完成后关联审reply所有权、响应流式限制、总超时/取消/晚回复、状态失败保护和token本机存储。当前不作后端或0.4通过结论。

## 0.4 三项修正复审及Qt首轮（尚未构建）

主管通知修正完成后，完整重读repository/server与新的repository.test、integration失败场景、HTTP看门狗测试和已装固定mysql2的pool_connection/base pool实现；读取ApiClient/CollaborationDialog头与实现、Window入口、CMake/main差异、更新契约。Qt尚未由主管构建，本段是源码审查与后端独立纯测试结果，不作Qt运行通过。

原R04-01/02/03实现复核接受：失败rollback destroy不release；withConnection含获取/全部SQL/commit/rollback总时限，取消实际destroy，晚到连接destroy且不执行SQL；Server独立数据库operation timer及断开取消；FK明确REFERENCED_TABLE_SCHEMA与current_schema相等。独立运行validation/http/repository三组测试11/11通过，790.6257ms，覆盖受控回滚失败、命令挂起、晚到、close兜底、跨schema拒绝和HTTP operation截止。主管报告真实MySQL集成两次通过，新增100ms锁等待断连接/线程退出/不残留发布、受控KILL本账号ev_test连接恢复、持锁close收尾；本角色已逐项读取真实测试与guard，但未自行重跑该清理表测试，避免和模拟用户共用ev_test时竞争。旧主API8765/session89956未被操作。

本轮新增待修/验证项已通知主管：

| 编号 | 位置与触发 | 影响与复验 |
| --- | --- | --- |
| R04-04 | repository.close末尾 `.then(finish, finish)`：pool.end发生错误立即finish清除兜底timer。固定mysql2 endCB遇首个连接错误就回调，但其他握手或QUIT仍可能卡住 | 错误路径也必须destroy残留_allConnections，不能只等end挂起才兜底。本角色受控pool.end拒绝且内部队列仍有socket，实际remainingSocketDestroyed=false；补拒绝/剩余连接关闭回归 |
| R04-05 | apiclient.cpp:95-128 finished只检查取消/timeout/oversized、HTTP状态与JSON，没有检查reply->error。服务发200和有效JSON但宣称更大Content-Length后断连，回复可能携RemoteHostClosedError且仍被视为ok | 2xx成功要求传输NoError，不能导入未完整传输资料；保留合法HTTP业务错误信封。需受控200/有效JSON/截断传输实际测试，此Qt触发尚未运行 |
| R04-06（公开回调边界） | ApiClient::cancelAll:50-54遍历裸reply快照；abort可同步调用callback。若取消callback立即销毁Client，manager删剩余reply后原循环继续访问裸指针 | 当前Dialog回调未看到同步销毁动作；仍应以两个pending+首个取消callback销毁Client验证公开契约，并用Client存活守卫/弱reply或等价方案阻止已销毁后循环。Qt未构建，本项源码推断待实际回归确认 |

Qt其余路径初读：manager归Client、timer归reply、finished先remove pending/stop timer/deleteLater，再调用callback；Client析构先closing并断开reply到this，abort期间不调用窗口。默认8秒、可夹紧50ms至30秒、最多8 pending；6MiB readBuffer限制与累积超限abort避免无界读，已有buffer在超限清空。token拒绝CR/LF、禁base userInfo/query/fragment、ManualRedirectPolicy防止自动带token转向；不输出token。Dialog拥有Client，lambda只捕获this及下载ID值，没有保存局部引用或Part指针。Modal finished先标取消再abort，取消结果不能进入ok/import分支；下载返回equipmentId与请求精确匹配后才Controller导入，本地同ID不会覆盖。主Window拥有Dialog，onLocalImported仅同步Window显示。父Window Escape隔离与关Modal实际回归尚待主管构建/测试。

读取期间Dialog默认配置已从早期service.local.json改为client.local.json，未把较早服务凭证文件观察误报为现存缺陷；默认最小客户端配置符合服务端凭证分离，后续需核对生成文件键和包排除。6MiB积累检查目前发生在有界readAll之后，短暂积累可能超过阈值后立即清空，未将阈值声称为整个进程内存上限。

修正复审SHA256：repository=9C10F7A13E10EF90897779EEBC33235C5D52E67434B2099FE377732BBAACEEE7；server=F5BC6FD4C39B721D21B91766FA9D21AC0C33C6AD6D738E8EF6D669753F5441D1；repository.test=A9F7661C5903CAC482904D10F39F94E0F4A48BC27F772F91CE7DE20B6C7AB1FE；integration.test=92F0F5F34E5923B0B3A3ECC727EB82E5F98FA1805EC52569156ABB6ACDD2EABC。Qt首轮：apiclient.cpp=A2CFDB5A569F5FB168DFC784785B5C9B626C7C1166376897222B7780236909B0，h=B0A4F775390CAFE7CE4540CC1C1DE0382670327497D9E52D9487A63822D5B637；collaborationdialog.cpp=BAD4FFC9C436E0A3D33D67A418B5808CA7A0E24A49B565122A1DDAA2CF73C5FC，h=E1F899EF1F169D736DB1EAAE6EB23B59A58FF0A671E11B56A9EDCBEBC1498A62；Window=A162942214BC12DFDCCD0A4E4A413B00BFDAE983693ACAC0D77DE5DF2397E335；CMake=CF8DC36A408BF6A346C4A770B3F6813681C8F0F544AC834E1C0E698FCB444CCC；main=5B7760C8DEE361DE5453245F7835EB88CE449E87ACEE8DF5A7B40D8D61A3C006。

下一步：复查R04-04/05修正及R04-06回归；主管统一Qt构建/网络生命周期测试与模拟用户双客户端实际验证后审关联行为，当前仍无0.4发布结论。

## 恢复后的回归建议（待实现与运行）

主管恢复额度后明确后端负责end拒绝残留socket修正、主管负责Qt传输错误与回调生命周期修正。本角色继续只读，等待完成通知。主管报告恢复的原11项CTest已通过11.82秒，此旧结果不代表R04-04/05/06已解决；未自行停止旧API8765，独立MySQL3310恢复由主管处理。

对“finished后下一轮事件交付回调”的策略已反馈新竞态：若先从pending移除再排队成功回调，close/cancelAll发生时reply已不在pending，Dialog尚未析构时排队成功回调仍可能导入。取消应使已排队交付失效，使用请求世代或共享取消状态/交付集合等方案；交付捕获ApiResult值，不能继续读已deleteLater的reply。QObject接收上下文消除析构后的回调，不能替代close后、析构前的取消语义。

严格有效回归建议已发主管，尚未实施/执行：

1. 受控HTTP发送200、有效完整JSON，Content-Length声明比实际body多字节，然后断开socket；实际Qt回复必须有传输错误，业务结果不可ok、不可导入。相同JSON与准确Content-Length正常200应ok，以排除测试只验证JSON坏值。
2. 两个真实pending请求，cancelAll使abort触发交付；首个取消回调立即销毁Client，使用QPointer证明Client/回复失效且后续回调不再调用。需要实际异步对象路径，不能用直接调用业务callback代替。无匹配ASan时此回归不能宣称覆盖所有UAF。
3. 对真实reply在ApiClient已有finished连接之后再接一个直接finished观察槽；前一个槽已排队结果、观察槽立即cancelAll或reject Dialog，在下一轮事件交付前确定插入取消。之后结果必须取消或被抑制、不得ok/import。不要只用sleep猜时序，也不把已经交付成功后的关闭作为此竞态。
4. Modal打开后实际Escape应关闭Modal、保留主Window既有选择/搜索/隐藏；请求中reject/close后本地设备与数据库不新增。两Qt客户端实际MySQL共享仍由模拟用户负责，不用受控HTTP模拟代替其证据。

下一步仍是等实际修正与差异，按上述范围复核；这些为建议/计划，未写为运行通过。

## 0.4 R04-04/05/06独立复核关闭

主管通知实际修正已构建后，重读ApiClient头/实现及完整api_client_tests、repository.close及回归、固定mysql2关闭实现、setup配置策略与两个测试、相关CMake入口。没有只凭通过报告接受修正，没有改业务或共享构建，也没有运行争用ev_test的真实集成测试/操作旧8765服务。

复核接受：

- R04-04：pool.end拒绝与看门狗到期均先destroyRemaining后finish；内部_allConnections按当前固定mysql2结构使用，先快照防止destroy改变队列漏掉成员，单个收尾异常不跳过其他连接。新增回归用两个实际从模拟队列删除的socket验证全部收尾，旧end挂起/active取消保护继续保留。
- R04-05：取消、超时、超限优先；2xx再明确要求reply->error为NoError。截断传输不解析为成功，401等合法业务错误信封仍被读取。真实受控对端发送完整有效JSON但Content-Length多1且断开，错误路径已执行；等长200仍成功。
- R04-06：cancelAll先递增世代，拥有者与reply快照均QPointer；finished在abort栈中仅整理结果与排队交付，用户callback不在该栈执行。dispatch上下文为Client、捕获ApiResult值及世代，不抓已deleteLater的reply；世代不同清data并改取消错误，回调末尾无后续Client成员访问。首个交付回调销毁Client后，Qt上下文与QPointer阻止后续交付。
- 新排队取消竞态：真实reply.finished的后接直接观察槽断言pending=0且用户done=false后立即cancelAll，下一轮交付为取消，不送成功。源码也保证Dialog finished触发取消后，未析构的隐藏Dialog不能收到旧ok结果而导入；完整Modal实际交互仍待模拟用户证据。
- 配置：setup新增matchesExpectedCore保留超时/缓存扩展及字段顺序，不改固定账号/密码/数据库；clientDTO严格仅host/port/token，已有多余秘密字段拒绝。入口直接执行guard使纯导入不启动/初始化MySQL。

审查者实际运行：

- build/bin/api_client_tests.exe -platform offscreen：退出0。真实loopback QTcp对端，覆盖正常/401/无效JSON、CRLF及userInfo拒绝、超时、取消、析构无回调、真实Content-Length截断、finished后排队结果取消、双pending首取消回调unique_ptr.reset、6MiB+1拒绝，以及同进程50次成功后pending和QNetworkReply QObject归零。未访问本机真实API/MySQL；offscreen仅是控件平台，其HTTP为真实本机TCP。
- node --test test/validation.test.mjs test/http.test.mjs test/repository.test.mjs test/setup-config.test.mjs：14/14通过，824.7459ms。包含end拒绝残留连接、原回滚/总截止/FK/HTTP截止、客户端配置策略；导入setup没有执行初始化。
- 主管后端真实guarded集成1/1、4.041秒由backend交接记录，本角色读取其代码与guard，未冒充自行执行，也未抢模拟用户正在使用的ev_test。

本轮对应SHA256：ApiClient cpp=EE2F7FA1224606F06916120977C2F24D217A4E5D8DF32E7CF3D89D4943049E58，h=49B5B4C8D1B6F8D86E4DAC3AE78D16632660437A75AD579ACF870260466DFFDE；api_client_tests源=7F0AAF42235AB206092809E44CE509E819E7F3F5C6903C4D63EB66B2CF92EB56，实际运行EXE=C60B53846CAA7414126D967E31CF6DAFF8FCFB50C80A4946699C5BB53142BF92；repository=9F1254140C8F1A9692D92EDDDFAB02982F81DA3AA13B767A4C8A68EFE6267BA2；repository.test=69DD81B8D1FEDC95ECBD11EF03B7ED0CAC05FE445234FF66BE96A7DC0830D2E7；server未变=F5BC6FD4C39B721D21B91766FA9D21AC0C33C6AD6D738E8EF6D669753F5441D1。

结论：原R04-01至06及排队交付取消问题全部独立复核关闭，已审代码范围无待关闭阻断项。0.4完整发布结论仍等待主管最新全量CTest与模拟用户双客户端/Modal实际验证、文档与包来源核对；此结论不替代原生共享交互或真实用户验收，也不作ASan覆盖/零资源泄露声明。本角色继续专职后续审查。

## 0.4 最终源码检查点结论

主管提供全量回归与模拟用户完成通知后，只读核对最终暂存范围、11项LastTest.log、两份shared-user实际日志、共享Qt/Node harness完整源码、0.4反馈/发布记录/指南/契约/恢复状态和关键哈希。基线HEAD为03c0c1a（0.3）；当前0.4尚待主管提交/打包，不提前审0.5。

结论：0.4最终源码范围独立审查通过，没有阻断源码检查点的未关闭项，可按持续授权提交0.4并进入打包/远端同步。原R04-01至06及排队取消已关闭，源文件哈希保持对应复核范围；真实用户体验仍待用户实际操作。最终包尚未提供，不把源码通过写成二进制/依赖隔离通过。

最终只读核对事实：

- 最新LastTest.log实际列11项且每项Test Passed，包括api_client_lifecycle；12.08秒为主管运行报告，本角色没有再次全量运行。
- build/shared-user-0.4-offscreen.log与windows.log均实际包含全部8情境字段为1、SHARED_USER acceptance=1、SHARED_HARNESS accepted=1、cleanup=1。模拟用户亲自执行并查看8张Windows截图；本角色独立查看发布成功、错误令牌、真实pending关闭前、离线新窗四张关键图，状态与日志一致，令牌遮蔽、中文可读。截图不当作socket/SQL释放证据。
- Qt测试两个真实EquipmentWindow各用独立临时SQLite，通过鼠标/键盘操作共享库；精确数据断言按设备/零件ID核对字段、位置和形状不适用值，没有将SQLite ORDER BY id误当JSON数组顺序。原失败与修正过程按模拟反馈保留，未为测试改业务。
- Node harness显式EV_TEST_CONFIG_FILE，guard ev_test/3310/ev_app/本机及空库通过后才标记ownTables；本次API和挂起HTTP端口都是ephemeral。app.close文件握手、真实socket close及最终MySQL两设备/四零件与空库清理断言，对应实际日志。只drop本次part/equipment/schema_version，不操作ev_shared/原MySQL80/正常8765。递归临时目录删除前校验绝对父目录为系统tmp、basename为本次equipment-shared-user前缀。
- client.local.json实际只输出键名称host/port/token、hasMysql=false，没有输出秘密；Git check-ignore确认client/service/test本机配置、node_modules、.runtime忽略。30项暂存文件只含0.4业务源码、固定package/lock、测试/helper、契约/指南/交接/状态，没有数据库、生成物或本机凭证。git diff --check无错误。
- 源码与契约保持0.4，只实现不可变共享设备；0.5批注/冲突和0.6Redis仍是后续范围，没有提前实现或宣称通过。

最终新增范围SHA256：CMake=B9330BDDA256B36FF5694421D724194304B5AB843192CEE2BA658591A537FF9F；setup-mysql=33FB2051B57511EFB401C6D1BF3898CA0B405052A0E7CF71EC4220FC3E3E0FD4；shared_user_tests=2D89991593018A4ABE614DC5513D0E4A21A883E434D4AA8FCE7E551CB42C11F0；run_shared_user=931F33D93BE1013499FC3FC28A91415C6D2995A7CA3466FE5C327FE273C7A27D；api-contract=934BFD5CA8B7BB7E92EDC425BA7C88F152913BED5005765FC01DDFF5AA73997E；package=B9ACBB3DC8BE3556195101D9786EEBA3093BE8B33B1B72755380BB6790F733EE；lock=D9AAA06C9C82C20DB80C0597629D9B2D3F4073120BEC86D9F5E4C21FFEF8F848。ApiClient/Window/Dialog/main/repository/server保持前述接受哈希。

两项旧文案已通知主管订正，非源码行为阻断：user-guide仍写网络服务不属于当前版本，应与0.4共享章节一致；README版本管理仍写尚未推送，应反映0.3已同步及0.4实际后续状态。本角色不改公共文档；本交接新段尚需主管重新暂存。

下一步：主管生成0.4包、验证依赖隔离启动、核对manifest/EXE/hash/敏感文件和提交一致后提供包meta，本角色再独立只读复核包；当前不提前作包通过结论。

## 0.4 最终包复核与交付范围

已只读核对C:/Users/23871/Documents/Codex/2026-10-07/new-chat/outputs/equipment-viewer-0.4.0-20261008-082917目录及同名ZIP。manifest为version0.4.0、sourceCommit=aef7499d05f0f9db4535fd33fdb4718b84487d1d、sourceHasUncommittedChanges=false、databaseIncluded=false。目录EXE、ZIP内实际EXE流及build/bin EXE的SHA256均为720CDF8993BFEB36ACCE8772094FC35A3379CD291922FBBF2FF3979C58D62093；aef7499的CMake实际0.4.0。

目录/ZIP包含启动入口、指南、两份虚构样例、QtNetwork/OpenGL/Widgets/Sql与MinGW运行库、qwindows/qsqlite插件；扫描无数据库、key/pem、local.json或.env。0.4图像依据为此前独立查看的原生共享关键图及模拟用户八图记录；包清Qt PATH/plugin变量后的native smoke退出0为主管实际执行，本角色没有再次启动包。

结论：0.4源码与对应桌面包独立复核通过，交付为本机Windows桌面、本地SQLite、HTTP团队共享设备/独立MySQL能力；双客户端证据为同一进程两个真实窗口与各自本地库，不扩大为跨机器/独立桌面进程验收。真实用户未验收；0.5批注与Redis不属于此包。0.4包无需重复制作，后续按0.5范围推进。

## 0.5 后端首轮独立审查（Qt尚未完成）

已读新增0.5冻结契约、annotations/repository/server/schema及完整真实integration增量、纯测试/0.4夹具、backup工具及api README。未预判正在实现的Qt草稿UI，未争用ev_test，未操作ev_shared迁移或主API。相对0.4实现保护已复查，17项纯检查由本角色实际运行通过（928.1483ms），backup-mysql语法检查退出0。真实ev_test1/1为backend报告，本角色读取其守卫与断言，不冒充亲自运行。

目前批注业务源码未发现阻断项：UUID规范小写；精确scope和UTF-16/Unicode边界；创建原始指纹只INSERT、不随PUT修改；相同原UUID/原payload重试在修改后返回当前存续DTO。并发INSERT唯一键失败后FOR UPDATE越过RR旧快照，CAS为单条id+revision UPDATE，再锁定读DTO，事务提交前取结果，作者/关联/创建时间保持。DTO从MySQL epoch转UTC ISO；本库设备FK与设备内零件联合FK兼容校验明确。先验证v1再增建/验证annotations，最后marker1→2；DDL隐式提交及中断重入不drop行为记录真实测试。原截止、迟到获取、取消、回滚失败destroy、end拒绝收尾保持；真实测试收尾先annotations后part/equipment/schema_version。

备份独立核对：首次副本.recovery/sql/2026-10-08T00-37-09-996Z-40aa75a9，3780字节；SQL实际SHA256 E03D77E23C363F01CDCA802C313F431E93FFF464B91D99546755288DFB5DFE3E与manifest一致，restoreVerified=false。未执行恢复，不声明恢复验证。工具本机ev_shared/3310/ev_app/marker/@@datadir/账号/InnoDB守卫、无密码命令行、single-transaction和partial处理合理；必须保持暂停结构变化的运行约定。

新增需修R05-01已立即通知主管：backup-mysql.mjs:51-53在await writeFile(flag wx)完整成功后才设置privateFileCreated。如果独占open已经成功但后续写入ENOSPC/I/O失败，本次新建文件可能含部分或完整密码，而flag仍false，finally跳过unlink也不提示残留。本角色只读本机Node24 internal/fs/promises源码确认writeFile依次open→writeFileHandle→close，不替调用方unlink。应open wx成功即记录本次拥有，再FileHandle.writeFile/close；finally只unlink拥有文件。受控替身验证open成功/write失败仍清理、EEXIST不删既有文件、正常路径关闭并清理；不制造真实磁盘耗尽。

契约文案建议已通知：POST“返回200原记录”改明确“当前存续DTO，后续PUT后原POST重试不回退内容”；现README/真实测试已明确，避免未来Qt把重试理解为返回冻结原文本。

本轮哈希：annotations=A04788F1111BC67263F37B6E01EE43F4EF996A653F8B393ADF73A3460FDE2D6B；repository=27AEC0CDC7A8D154D3E17CA91C187C3AEE006D52CE00A29CC6D1E335C75E7769；server=F86225EA6C8EE0BFD319EF7A184AD17BF10121A005199BA04051067458E2ACB2；backup=3678E6DC33210E2DAF74CF562C8D5D15F032CDE2E7020EA76C2A7D640D4CFE3A；integration.test=CCB64AF3BDC2E2618FFC003CE5C5A14C026DBB565CAAE7345F743E5E8A384D3B；annotations.test=41A4730419558C5FFFD1FCB0BB500939D7223BD875EB2BFA6BE3FE3058CA1EBF。

下一步：R05-01修正复查后给后端范围结论；主管Qt草稿/CAS/取消和真实双用户完成后再关联审0.5。不把17项pure或0.4包通过写为0.5完整发布通过。

## 0.5 R05-01关闭与Qt首轮待修事项

已完整重读preparePrivateOptionFile、backup main外层finally和新增三项FS替身测试。open wx成功后立即owned，写失败通过cleanup关闭并删除自己文件；EEXIST未取得拥有权不删除，正常返回幂等cleanup；connection.destroy用嵌套finally不跳过文件收尾。R05-01接受修正并关闭。

审查者独立实际运行20项pure全部通过（891.8549ms）。另用纯FS替身检查close/unlink异常，不写真实文件或凭证：close持续拒绝会尝试两次close、一次unlink，并一次安全提示；成功准备后的unlink拒绝会尝试一次并一次安全提示。系统删除自身失败仍需主管按唯一文件路径手工核实，未宣称所有I/O故障下能保证文件消失；不打印内容或错误对象。backup新SHA256=1F88CE37F69F9A73A4B314AFD6DB7943F1B3C9A1339E91B4F26368196FC02BE3，setup-config.test=696EA588B81B1D98B68BDBC3397314C4A51829960972791C4982615AD82A9E61。未重复dump或ev_test真实运行。

主管通知Qt首次编译成功后，本角色完整只读CollaborationDialog头/实现及Window创建/销毁关系。以下事项已立即通知主管，待修改与实际回归；没有自行改业务：

| 编号 | 位置与触发 | 影响与修正/复验 |
| --- | --- | --- |
| R05-02 | Dialog:154-155仍无条件reject/finished取消，Window:74仍WA_DeleteOnClose；有未保存草稿或保存pending时Escape/关闭 | draft/nonce/currentNote随Dialog销毁，未经显式放弃丢失文本。按取消保留约定，可先取消等待并保留Dialog/草稿，提示先保存/放弃；覆盖Escape、按钮、标题X/close，不能只保护一种关闭入口 |
| R05-03 | loadPartScope:303-318同eq刷新时若原loadedPartId不在返回parts，自动选择整设备并更新loadedPartId；dirty新草稿文本仍保留 | 异常/不完整但可解析的HTTP设备DTO会让下一POST静默改为设备级批注。dirty时原part缺失应报错并保留原scope/cache，禁止自动降级；受控HTTP制造同eq缺原part验证，不改正常geometry |
| R05-04 | saveNote:448-455只校验equipmentId及DTO形状；refreshNotes:341-345未检查限定partId与实际返回note partId | 同eq其它批注DTO/其它零件列表可能被接纳并用于后续修改。捕获本次POST requestId或PUT note.id和目标partId，严格匹配响应；限定part查询结果必须匹配该part，整设备查询仍允许全部part。错误保持草稿、当前DTO与scope |
| R05-05（主管已计划校准） | refreshNotes:347-364当前note不在recent200且dirty，保留旧currentNote，但仍提示“最新保存版本已刷新” | 未取得该note最新版本却给出已刷新的承诺；应明确未找到/未取最新、保留旧metadata/草稿且不自动覆盖。用recent200不含当前ID场景验证 |

其余初读：catalog/part/notes请求失败未清dirty；catalog dirty且缺当前eq会在改列表前拒绝。scope切换脏草稿恢复旧精确ID，New/其他note切换不自动覆盖脏稿；QPlainTextEdit与PlainText标签避免富文本执行。现有回调仅捕获this/ID/text值，临时parts仅同步使用，不保存Part借用。Client仍由Dialog拥有，ApiClient哈希保持0.4已复核的EE2F7FA1224606F06916120977C2F24D217A4E5D8DF32E7CF3D89D4943049E58，原generation取消不回归。POST同语义payload保持UUID，修改语义才换UUID；返回更高修订/不同text保留草稿，PUT使用currentNote revision不盲目重发。显示名是展示标签，时间从UTC DTO解析后转本地展示，未把它当认证身份。

Qt初轮SHA256：Dialog cpp=2AB1360F5F0190A846D2BBE3AECAE6BBF5A188CDCB06D12B24B6C843CB875E3A，h=1C17737B319E24D198A229784BE8BDE11897E6050B746BA6D72072188F9396C6；CMake=17564F201A2830F674C6062F40F747CBF5DEFAC9FA0F2751FEC02E8330A7D36F；main=480DAF0EFD2655C00F4DD6DDAE8F1384B0E7E2D13C70C69C17B00A986259D081。

下一步：等R05-02至05修正/必要控件回归与模拟双用户实际验证，独立复查关联失败保护；后端业务源码当前无未关闭项，0.5整体仍未通过/发布。

## 0.5 Qt四项修正静态复核接受（运行复验待完成）

主管通知最小修正增量编译成功后，独立重读Dialog h/cpp的全部修正与请求调用、Window拥有关系、main退出入口；未改业务或构建，未用未完成的GUI测试结果作通过依据。

- R05-02：公开override reject在dirty时先递增operation、cancelAll、恢复非busy并保留窗口/文本/nonce/currentNote，提示保存或放弃；closeEvent对dirty调用该路径并ignore，阻止WA_DeleteOnClose。干净窗口沿用QDialog关闭和已审ApiClient取消。Escape、关闭按钮及标题close路径均有源码入口，实际控件复验仍待。
- 新取消/新请求竞态：所有共享与批注HTTP都通过唯一request封装递增operation，wrapper仅当前operation调用业务callback；dirty关闭在取消前递增，使已经排队的旧取消callback不再setBusy(false)或更新旧数据。串行链式catalog→parts→notes在上一callback内创建下一operation，未看到跨局部引用保存；ApiClient QObject上下文与generation机制保持0.4哈希不变。
- R05-03：dirty、同eq且原loadedPartId非空时，先检查返回parts包含精确ID；缺失在替换scope/cache前返回错误，不自动降整设备。合法刷新仍保留原part选择/草稿。
- R05-04：限定part查询对每个DTO检查返回partId，整设备scope可读全部；保存前以值捕获本次POST nonce或PUT currentNote.id及原partId/null，返回DTO需严格匹配后才展示/清nonce。错误不调用displayNote、不清dirty/nonce或旧metadata。
- R05-05：recent200未含当前ID且dirty时明确“未取得新修订”，保留旧currentNote/草稿；不再宣称目标最新保存版本已刷新。

父窗口适用范围：当前普通UI由WindowModal阻父操作，没有额外Exit/quit动作绕过Dialog；main.app.exit只在显式smoke入口。此次不要求为程序式owner析构或进程终止实现无限内存草稿保留。若以后新增正常“退出”菜单或程序式父close，应在优雅退出协调中先让子Dialog尝试reject，若其因dirty仍可见则拒绝父close；强制终止后的保留需要另行持久草稿范围，不作当前完成声明。

已给主管有效受控HTTP回归建议，尚未由本角色执行：

1. dirty实际Escape与close保留，显式放弃后可关闭；保存pending关闭也保留原文本/nonce。
2. 取消旧pending立即开始新pending，在旧queued callback送达后仍为新请求busy；不能只等旧callback全部排空再测。
3. 同eq有效DTO缺原part，刷新后原P scope与dirty文本不变，后续显式POST仍绑定P。
4. 错UUID/part保存DTO及限定查询错part列表拒绝，旧metadata/草稿不变。
5. recent200不含选中note时提示未取得新修订，旧revision不改、草稿保留。

修正范围SHA256：Dialog cpp=4E0FC49D62AADC3AB61E3310F90F7E32D27530BE69653F57A0B39CD6E31E5747，h=AB3EC5F78C1340BAA668ECBD64656F81E74243943B1D60E101F3E871262CC60D；ApiClient仍EE2F7FA1224606F06916120977C2F24D217A4E5D8DF32E7CF3D89D4943049E58。限定git diff --check无错误。

结论：R05-02至05的源码修正复核接受，新增operation防护合理，未发现新的阻断源码问题。待实际行为与模拟双用户回归后再关闭运行复验并给0.5整体结论，未提前宣称发布。

## 0.5 独立受控HTTP故障测试交付（待主管运行）

主管明确授权本角色新增唯一测试文件tests/reviewer_annotation_faults.cpp；已落盘，未改业务或CMake。文件有独立QApplication/main，建议目标reviewer_annotation_faults，链接equipment_window与Qt6::Test，Network由equipment_window传递；不使用自定义Q_OBJECT，无额外moc资源。建议运行build/bin/reviewer_annotation_faults.exe -platform offscreen，正常结束打印REVIEW_ANNOTATION_FAULTS accepted=1 controlledHttp=1 realMysql=0；当前尚未运行，不能将此预期输出记为通过。

测试用真正的QTcpServer/Socket绑定loopback端口0，累积完整HTTP头及Content-Length body后才返回受控DTO；模板为合法设备/零件/批注，仅在指定故障下返回缺part、错误UUID/part或200条不含当前目标。临时SQLite初始化EquipmentWindow，公开控件对象名发现Dialog及编辑器；鼠标/键盘/close事件、公开Dialog reject和控件click触发操作，不直接调用ApiClient/业务callback造假，不接ev_test、8765或正常SQLite。

已实施断言：

- dirty Escape、关闭按钮、真实closeEvent均保留窗口/草稿，父选择保持；显式放弃后允许窗口回收。
- 同eq detail突然缺当前P时scope和草稿不变，后续POST仍带P，不降设备级。
- POST/PUT分别错UUID与part DTO拒绝，草稿/旧metadata/原revision保持；相同POST payload连续受拒时requestId仍相同。
- 限定P的GET notes返回Q批注拒绝，并保留先前P条目/草稿。
- 返回200条合法P批注而不含当前ID，提示未取得新修订，草稿/保存内容/旧revision保持。
- 真正旧reply finished排队交付后取消，公开reject与控件click在同一事件栈中立即开启新pending；旧排队事件经过后新请求仍busy。对端实际持有两个请求并只释放最新socket，不用sleep猜网络结果或直接执行业务callback。

实际仅执行MinGW GCC13.1 -fsyntax-only -Wall/-Wextra/-Wpedantic：补齐与Qt目标一致的QT_WIDGETS_LIB/GUI/Network/Core/Sql/Test宏后退出0，无诊断。较早手工探针未配置MinGW运行PATH、随后未带QtWidgets宏，分别退出1；补齐编译环境后通过，未因此改业务。运行级验证仍由主管统一接入/编译/执行，本角色负责修自己的测试或反馈真正业务失败。

测试源SHA256=F86F0E4CBD7F8BA4D5DF84A3D7A0A06FC59694BE4E853CBFC0C3356231F7D4B1。主管已收到依赖与运行入口；本角色将据实际结果更新，不空等大场景或假记green。

## 0.5 最终源码独立结论与运行证据

额度恢复后先重读交接/实际Git/当前协议与哈希。主管已构建并运行独立故障测试通过；审查者又亲自运行reviewer_annotation_faults -platform offscreen，六组字段全部1、最终accepted=1 controlledHttp=1 realMysql=0、退出0。早期恢复运行EXE为0DF677905263ED5F85A1451CF6FD2528A165E945FD3A4708AE1DD1C38BF73A48；随后主管完整重构建生成2B83FDF34D6FA8C373A91AA5100B6D5E724C0B1E14B61B37DAC4348E525B1098，本角色因当前二进制变化再运行该最新EXE亦退出0。测试源F86F0E4CBD7F8BA4D5DF84A3D7A0A06FC59694BE4E853CBFC0C3356231F7D4B1保持不变。R05-02至05与取消后立即新请求busy的运行复验完成，全部审查项关闭。

最终源码结论：0.5独立审查通过，没有阻断源码检查点/tag的未关闭项。允许主管按持续授权提交/同步0.5并进入版本打包检查。0.5为不可变几何上的工艺批注、原payload幂等及CAS修订/草稿保护；不把0.6Redis工具准备或0.9资源计划纳入本版完成范围。真实用户验收仍待用户操作，0.5最终包尚未审查。

本角色实际只读核对：

- build/build-0.5-resume.log含完整构建及12/12 CTest通过，总13.40秒；包括annotation_client_faults。全量构建由主管执行，本角色未再次执行整套。后端20项pure由本角色此前亲自通过，最新主管20/20、856ms另有日志，不替换执行主体。
- build/shared-user-0.5-offscreen-semantic.log及windows.log都实际输出创建/精确scope/多行/UTC、CAS冲突/刷新保草稿/显式rev3、同UUID丢已提交响应重试当前DTO、脏scope/错DTO/缺part/旧回调、断服与重建持久等全部1；ANNOTATION_HARNESS accepted=1和SHARED_HARNESS cleanup=1。模拟用户亲自运行，审查者没有争用ev_test再跑。
- 独立逐张查看11-cas-conflict-draft、12-refreshed-separate-draft、14-idempotent-current-revision-draft三张Windows图：冲突时乙草稿与旧保存内容分开；刷新后保存内容/版本变为甲rev2但乙草稿原文仍在；原POST重试返回乙已更新rev2同时保留甲原新增草稿。作者、更新者和UTC明示清楚，令牌遮蔽。图像不冒充SQL/幂等计数证明。
- chooseCombo helper从错误popup鼠标定位改为逐次真实键盘Up/Down，每次等待异步idle且断言实际索引变化。没有调用控制器/网络回调或直接setter绕过业务；首次失败属于测试前提，仍应由模拟反馈保留过程并更新最终状态。
- 完整读新增harness代理/暂停重建/最终SQL断言及收尾：显式ev_test/3310/ev_app/本机/空库守卫；真实POST201完成后再故意丢响应，重试UUID/原payload严格相同、返回当前rev2；最终3批注的关联/作者/revision1/3/4与geometry2设备4parts核对。关闭本次ephemeral app/proxy/stall、清proxyTimers/socket，再按annotations→part→equipment→schema_version清自己四表；临时目录父路径/专属前缀校验不变。正常ev_shared/8765/原MySQL80不属于测试目标。
- 最终协议明确原POST返回当前存续DTO、UTC显示、dirty关闭取消等待并保留窗口；修复与实现一致。Dialog cpp=9EA7BBA3CD79625C0CBFBB00B21DED2E20CD0682C79D48F20C67B872F5CD657A，相对前次只校准UTC明示/ISO显示；h=AB3EC5F78C1340BAA668ECBD64656F81E74243943B1D60E101F3E871262CC60D。ApiClient、repository/server/annotations与已审哈希不变。shared_user_tests=D8F5090FD93DA628F1926AACCE0BC5BCB104052D4AB28CDA5398830AE06753A2，run_shared_user=ACC806F08E2B28DEDC200A5846DD8CACFC282B4861AC67319DA2BA529AA450A5，契约=BD1622CFB944AFD5A52D8A314820F148D8FDAB1ADDE0A7744A415B91EC73A2F4。限定diff --check无错误。

主服务备份/迁移：主管报告已有备份后ev_shared正常eq/part为空基线保持、schema2及health up。此实际主库操作由主管执行，本角色未迁移共享库；backup文件/hash已核对但restoreVerified=false、恢复实测未完成的事实继续保留，不宣称完整恢复验证。

下一步：主管0.5包/依赖隔离/敏感文件与manifest提交一致核对后提供metadata，再作包复核。源码检查点明确排除未启用的0.6 runtime工具/cache-plan与后续resource tests；本角色继续独审这些下一阶段准备文件。

## 0.5 最终包发布门槛通过

已只读核对C:/Users/23871/Documents/Codex/2026-10-07/new-chat/outputs/equipment-viewer-0.5.0-20261008-131825目录/ZIP及outputs/0.5包启动验证.png。manifest为0.5.0、sourceCommit=d3d720e8a063a4a24d70fa64c924ed9dfce1ffe8、dirty=true、databaseIncluded=false。dirty为尚未提交0.6准备工具/cache-plan的真实标记，不在桌面包；Qt src/CMake相对d3d无差异。

包EXE、ZIP内实际EXE流、build/bin EXE三者SHA256均177D203D03D6314DB694631FADDC42A0E9721AC27593F14C540087DFE029A629。目录只有1exe/26dll/3json（manifest与两样例）/1cmd/1md，目录和ZIP的sqlite/sqlite3/db/key/pem/local.json/local.ini/.env扫描0；含QtNetwork、qwindows/qsqlite、启动入口、指南与两份虚构样例。独立查看包启动图：模型/清单/共享入口/中文正常；清Qt PATH/plugin后的native smoke0为主管实际执行，本角色未重复启动包。

结论：0.5源码及对应桌面包独立复核通过，已满足本版发布门槛；恢复副本尚未实测、真实用户未验收、跨机器/独立桌面进程未测仍保留。未把Redis/资源准备计入0.5功能。指南“批注范围/提交草稿”是概括措辞，已建议后续与实际“关联零件/新增批注/提交修订(版本N)”同步，非功能阻断。主管报告远端main/feature/tag peeled均d3d，此远端核对执行主体为主管，本角色未推送。

## 0.6 runtime初轮意见与执行边界

已完整读setup-redis.mjs、redis-bridge.mjs、redis-stdio.py和redis-bridge.test.mjs及cache-plan。此阶段只审准备工具，不启用缓存，不调用WSL、setup/stop或真实Redis，不占ev_test。主管已取消自己挂起的wsl --list会话，未shutdown/restart全局WSL；发行版Registry实际Ubuntu-24.04，后端正在按此修正默认值。

R06-01：首版bridge的complete(false)把未确认退出child移出sessions；之后close current为空会成功、maximumConnections不再计实际未确认child。必须保留未确认状态与实际子进程预算，真实close才释放，后续close仍报未确认。已即时通知主管并由backend修正。

执行记录需准确：最初完整读4项pure及bridge哈希2D7D53E11A80A49611B1FDFF9BBCF88019B2E38704577A9670FBA7EBF45DFCC2；本角色开始运行时backend并行修改到08DE版本并增加第五项回归。前4过，第5失败且未结束，未把处理中代码判最终通过/失败。本角色仅Ctrl-C终止自己的session91215（exit1），后续inline替身复现命令没有执行；没有任何真实WSL/Redis/MySQL动作。

主管现通知4文件冻结：setup722DE9…B9D01、bridge5386EE…4CBB3、py6370D5…C584、test6C78F…1F7BF；false保留预算、晚到realclose释放、失败测试t.after收尾及socket EOF排空timer已补，最新5/5为backend报告。本角色下一步重新完整读取冻结范围并独立纯测试后再给工具结论；当前不预写修复通过。

后续0.9准备授权：主管另授权本角色独立tests/reviewer_resource_tests.cpp和tests/measure_resources.ps1，Qt临时库/重复窗与网络/SQL/QObject计数、本测试PID句柄/PrivateBytes暖机批次观察；尚未落盘或运行，不计入0.5。Node堆增长等0.6后实施；数据库恢复先规划，不占ev_test或正常资料。

## 0.6 runtime冻结修正独立接受

已完整重读冻结三工具/新第五项bridge测试；独立node --test redis-bridge.test.mjs 5/5通过（506.5695ms），两Node工具语法检查通过。R06-01关闭：未确认child保留sessions预算/diagnostics，真实close才移除；late close可清预算，close在未确认期间继续拒绝；EOF增加有界排空，发行版默认Ubuntu-24.04。

Python身份R06-02补强已完整读并独立WindowsPython3.12 -B运行redis-stdio.test.py，2/2通过，不读真proc/不启动WSL。check_owned_pid在认证与转发前同时核marker、PID、固定binary、固定runtime cwd及监听inode归属；同binary/port/inode但另一cwd明确拒绝，随后原INFO/config_file/dir认证核验仍保留。R06-02关闭。

接受哈希：setup722DE988579EB4DC8941FB74AD3372D6E8EA86CCE88B7ACF5CACE873863B9D01；bridge5386EEB406259F66869DFD5383B8B1069812ACD7AD40802C936FDFA320D4CBB3；py3C450123122641C0EB22B9B16C74FE32CDB818E9E8B802B0F895893A2295EA96；bridge.test6C78F383946F21538E9E61FA316B41BC2F835D7E7B1E14CC77D502170011F7BF；Python test8ACBBD0641CAE4CD064C49E9F7582A62672E29DB6BBB252E28F990060E5A2FAD。本角色仅pure/只读，真实Redis7.4.7 ready为主管随后报告，用户自行启动WSL；本角色没有restart/shutdown或setup动作。

## 提前0.9恢复模拟独立核对与CLI修正

已完整读verify_mysql_restore初版F4D3C00463686718781E10F2401B115FBDA5A25591EBEB85CB752A44F2EA8064、实际mysql-restore-simulation.log、.recovery/tests/2026-10-08T05-30-45-888Z-c7cfdc59的manifest和SQL哈希。6269 bytes，SQL SHA B8CAE4C023BE8DF999C95677164A1DFA9F4CE32738F91E4C6A7BE1A50BC99AAD一致，日志accepted=1/cleanup=1，manifest restored/restoreVerified=true、normalDatabaseRestore=false。实际执行主体为模拟用户，本角色未争用ev_test或恢复正常库。

守卫与证据合理：ev_test/3310/ev_app/marker/实际datadir/账号/空库/GET_LOCK；仅生成本次小样例，dump校验hash和不切DB后drop own4；CLI指定ev_test恢复，fresh API精确geometry/ID/批注DTO/UTC存储epoch、原指纹幂等、旧CAS拒绝及显式rev3验证，再清空自己表。成功结果支持恢复能力的受控模拟，不意味着ev_shared真实备份已经恢复。

R09-01指出CLI pipeline失败kill即throw、未确认close就可能进入DROP。模拟用户已冻结新helper/main守卫并由本角色独立完整复读及node --test tests/mysql_restore_cli.test.mjs 6/6通过（669.4081ms）：失败/超时破坏管道、kill后有界等真实close；未确认只报告本次PID/path并main cleanup=0保留own表不DROP；kill抛错不绕守卫；import不运行DB主流程。R09-01修正复核接受，正常旧实际恢复证据保留，尚未以新helper再次运行真实恢复。新script SHA F5AC5A9785CD631112A2BE0BA9FB94334EAC94AE897D7AFA7818AB3DBF427EA2，fake test DD6A0867721E045E05765EE51570FFE1F32B0BE233424BB82687AA456CEFD3C1。

## Qt资源测试冻结、待主管运行

本角色已落盘并冻结tests/reviewer_resource_tests.cpp（2F87B1EC285DACA574B7C0CD988613814C946EC4D2A4A9B737DD6B356160DDB1）和tests/measure_resources.ps1（F6C9D0B7F273C7B4490D310724A045AF1BBEB24C87F942305AD0EE09AF26F148）。建议target reviewer_resource_tests link equipment_window+Qt6::Test，无额外MOC/lib；不修改业务/CMake，不与主管构建争用。

暖机3窗+8请求，默认5批各20真实Window/Scene/共享对话框、每窗5次设备加载（每批100loads）、40请求均分正常/401/真实超时/到达对端后取消。已知QObject子树QPointer全失效、SQL连接回初始、所有reply/PeerSocket清理、暖机后widgets/windows/applicationDescendants等计数归基线；1400ms排空Scene有界探针。只临时SQLite/client配置和loopback端口0。

PS已先自读并Parser0/UTF8 BOM EF-BB-BF，未执行；只启动本测试，按PID+出生时间采样Handles/PrivateBytes/WorkingSet，stdout JSONL/CSV/manifest留忽略build目录，异常或超时只终止自己Process并有界wait。逻辑计数与OS趋势分开，不宣称ASan或零泄漏。主管已全文读且正在串行构建session82643；等待实际结果，当前不记green。

## 0.6 cache初轮关键待修R06-03

已读cache/server/repository/package、官方@redis/client6.3.0实际destroy/queue实现与受控RESP测试。详细审查继续，暂不发布0.6。cache哈希D115605E97E2001A7790575363D2689966BA4FB8A59C3A48837ACDB2450340E3。

已即时报告并用纯替身实际复现R06-03：command abort只cancelConnect/invalidate，ensureConnection等待秘密文件读取时没有取消cancelCredentials；HTTP signal已abort且lookup结束BYPASS/inFlight0之后，晚reader仍可返回有效凭证，创建并connect新client。实际输出connectsAfterAbort=1、cacheReadyAfterAbort=true；未用真实Redis/DB。应取消当前读取/使冷连接世代失效，晚读取不得创建socket，并加Signal.abort在credential读期间的回归。主管已派backend最小修复，待冻结代码后复审，不以之前31pure替代此失败路径。

## 0.6 R06-03关闭、主场景通过与剩余发布门槛

已完整重读冻结cache79DF7A6DB47A95782746FA036FDAD94D7A254C6D4C7F25E50E405A1AE4797F84、cache.test EB6A9080DE9D205A504C8F6C605036A98D9574EB94BCDF8105470A5C083999F7。abort先cancelCredentials再失效connectionGeneration；ensureConnection局部读取结果在clientFactory前检查closed/代际，阻止合作读取取消、忽略abort晚返回、已读但await continuation尚未运行三类冷连接复活。审查者实际cache9+官方client/真实TCP受控RESP4共13/13通过（1212.6961ms），R06-03关闭。

源代码范围未发现其余缓存业务阻断：官方@redis/client6.3.0实际destroy幂等、flush队列并毁socket；offline queue禁用、排队上限/共享连接/连接与命令截止/冷却恢复/close保护合理。鉴权/ID先于lookup，状态安全枚举不含key/namespace/path/password/token；Lua原子TYPE/STRLEN/GET防巨型值进入parser，UTF8/JSON/geometry/id再验，坏值定向删除。SET失败保留已成功MySQL200/MISS；列表/批注/POST/PUT不缓存，404不负缓存；实际get调用计数不以MISS猜测。server=8658709A9EE935C758BD383CFEFDE80A4BE661655ABB68A3E159EFB3C30588DE，repo=2385647E62FC86B3F280708127DA9CCA36EF28049A0D6D1E97625F5FDE2F086A；Qt Dialog仍0.5接受的9EA哈希，没有新业务改动。

已独立读cache-user-0.6-offscreen/windows实际日志及完整验收台账/harness：两轮accepted=1/cleanup=1，真实Redis/MySQL详情MISS读尝试+1、HIT+0、401先于缓存、几何持久、断桥BYPASS保留草稿/恢复HIT、最新批注仍读MySQL。独立看03BYPASS、04断桥最新批注、05恢复草稿三图，保存区更新至他人版本2/UTC而原中文多行草稿保留。harness只独占自己6380桥、空ev_test/随机namespace两个精确key，确认bridge children退出后清四表；未确认Qt退出不DROP。正常服务/库不属于测试目标。

延迟范围：offscreen冷246.773ms/热16.796ms，windows冷254.318ms/热17.366ms，均各1次2零件样本。只记本轮路径和读次数，不能推大型设备统计加速。真实执行主体为模拟用户，审查者未重复争用真实服务。

发布门槛尚需补齐：协议明确要求真实Redis TTL到期、坏JSON/错ID/非string/巨型值Lua删除回源；当前用户harness TTL60且不测过期/这些值，现证据仅fakeRESP模拟相关Lua。已通知主管由backend使用自己随机namespace/owned keys作小型真实测试，不需占MySQL；未用37pure替代真实Lua边界。源码与已测主场景可接受，完整0.6发布结论等此证据及包核对。

## R09-02 backup kill抛错修正关闭

主管指出并由审查者独立复现原backup31BE helper：pipeline失败后kill抛EPERM直接跳过后续收尾，实际stdout/stderr/output destroyed均false。已重读冻结803CCD1E8131F8D120FA47C54C1C9146DFE150C885AA41EF447706AF1D730E24与test54D3EF48730F23D8E815898851E7B302F1E53856924983EFD3D362219F182117：kill抛错仍继续destroy三流/有界wait，未确认报DUMP_EXIT_UNCONFIRMED；独立fake4/4通过（561.3918ms），R09-02关闭。仅fake，不重复备份正常库。

## 提前0.9 Qt资源实际观察核对

主管已串行构建并运行本角色两文件：offscreen与windows均exit0/未超时/逻辑计数accepted=true。证据在build/resource-observations-20261008-182251-448c67b3及182658-8701af43，实际EXE SHA BAC08317996AD376BDA229FBB79E53BCDED6E6A50249206D98A372F54EC7E9DC。执行主体为主管，本角色只读manifest/JSONL/CSV后独立重算。

两平台暖机和5批的widgets/windows/sqlConnections/peerSockets都0，applicationDescendants都14且不增加；每批20真实窗×5加载、40请求矩阵及已知QObject弱引用检查按源码执行。offscreen明确软件投影；windows明确原生OpenGL，不能用软件结果替原生。

按DateTimeOffset强转JSON事件与CSV时间作最近UTC匹配（每点距最近采样14.5..198.6ms），offscreen句柄294→293、PrivateBytes11.30→6.80MiB，中间6.73..11.71MiB波动；native610→598、182.18→117.10MiB，中间句柄628/624后回落。未观察持续上升，不能宣称ASan覆盖或零泄漏。初次分析用Parse重解释ConvertFrom-Json的DateTime造成时区错选最早采样，已在写结论前改强转纠正；采样CSV本身没有修改。

此为0.9提前资源检查，不改变0.6已验收范围。仍需后续Node heap与完整稳定性/恢复/包检查；不能由这5批推断其他电脑或复杂CAD性能。

## 0.6 最终源码发布门槛通过

已全文读冻结test/cache-redis.integration.test.mjs（896746EB0F93E46C246C837BF6E246148860452A6525AE92CE7692173FCDF7BB），核实只拥有本次6380 Windows bridge与随机namespace固定3key；明确不建MySQL连接，不停LinuxRedis/globalWSL，不scan/FLUSH，不接普通API。清理先精确DEL/EXISTS3为0，再cache/client/HTTP关闭，最后bridge active/unconfirmed children0。已获主管此时6380空闲的独占运行授权。

审查者亲自运行node --test test/cache-redis.integration.test.mjs，退出0、1/1通过，报告2909.3229ms；日志build/reviewer-cache-real-0.6.log。真实PING/INFO7.4.7、SET EX1/PTTL正且≤1000/1200ms真实过期，坏JSON/UTF8/geometry/错ID/LIST/6MiB+1目标值MISS/定向删除；直接真实EVAL巨型值只返[2]，guard保持；自己桥断开BYPASS/重建MISS-fill-HIT；真实Redis+受控空Repository的HTTP404不负缓存。此404子项没有MySQL，不冒充真实DB。最终invalidValues6、ownedKeys0、bridgeChildren0、cleanup1，6380已释放。

backend另报告同脚本真1/1、完整pure38/38、严格空ev_test旧业务集成1/1（5.088s并清四表）；与本角色独立13cache/4backup/5bridge/2Python及专职模拟用户双平台真实Redis/MySQL/GUI证据合并，明确执行主体。所有R06-01/02/03、R09-01/02及原R04/R05问题已独立复核关闭。

最终结论：0.6已审业务/运行工具源码独立审查通过，真实缓存门槛齐全，没有阻断源码检查点/commit/tag的未关闭项。主管可按持续授权升级正常API、打包与同步远端。最终桌面包尚未给meta，不预记包通过；真实用户体验与跨机器/复杂设备性能仍未验收。0.7新三维/CAD/交互位于另一工作树，不纳入本版通过范围。

接受范围以此前最终cache79DF/server8658/repo2385/runtime722D/5386/Python3C45/backup803C及各实际测试哈希为准；Qt Dialog9EA保持0.5行为，CMake/main仅0.6版本/测试目标增量。后续改动需按影响重新审，不把本结论套未完R07。

## 0.6 桌面包最终独立核对通过

本角色重新读取outputs/equipment-viewer-0.6.0-20261008-191409的manifest及ZIP：version0.6.0、sourceCommit30b5fcc2b27415a3f2ea88eeaee8aed751edc873、sourceHasUncommittedChanges=false、databaseIncluded=false。目录EXE、当前build EXE和ZIP内EXE独立SHA256均为4B1D55E6786438D408DE572F9CC52F844CBE94F85D93EA4FBE4FB6755CE1C690。ZIP32个条目，未发现db/sqlite/key/pem/local配置/env；范围为运行文件、虚构JSON样例和指南。

已独立查看outputs/0.6包启动验证.png：原生窗口正常中文、三零件模型、共享入口；主管报告清Qt PATH/plugin环境native smoke退出0，本角色不冒充再次运行smoke。核对时HEAD为30b5，当前仅docs/user-feedback-0.7.md变化，属于后续CAD设计，不改变本包manifest构建时clean事实。

最终发布门槛结论：0.6源码与桌面包独立审查通过，无本版阻断项，可依持续授权推送main/feature并建立v0.6.0。正常API升级/cache ready及无正常库测试写入由主管执行记录，本角色没有停止服务或操作普通库。真实用户体验、复杂模型性能和0.7/0.8尚未实施内容不属于此结论。
## R07 独审中：R07-01 圆柱次正规径向失真（待修复）

范围为隔离树Scene c27ff641bd97bf6e4fdf6c6ff4cfed98541059e4及未提交Controller/Window，主树0.6不纳入。已实际读取真实反馈规范、图形/模拟用户handoff、Scene与Window测试和原生日志/截图；原生日志报告绘制FBO samples=4，contextFormat=0，正确区分。双击只读取item的值ID并以Window上下文timer延迟清单重建；Controller显式隔离flag、失败加载保留、showAll/成功切换退出逻辑暂未见阻断。

R07-01：src/sceneview.cpp圆柱leftA/leftB/rightA/rightB（581..584行）径向表达式仍先以double求radius*cos/sin，再传long double point。当radius=double denorm_min、length=2*radius时，合法输入通过long double包络/1%float跨度检查，但径向点先量化成0或一个最小double，形成失真几何；例如sin18°应为归一化0.309017却实际为0。bounds与毫米数值仍声称正常圆柱，未满足极端几何一致性。

本角色创建独立临时Qt探针C:/Users/23871/AppData/Local/Temp/reviewer-r07/subnormal.cpp，直接编译当前实际Scene源码，不改业务/共享构建。Qt offscreen真实绘制unit radius1 length2与等比例denorm_min radius/length，模型区（x180..619/y110..469）59153个像素不同；Scene无范围错误且annotations=4；数值radialDoubleFirst=0、radialExtendedFirst=0.309017；探针主动退出2表示复现，result.log和unit/subnormal.png保留。首次编译缺QtSql include只属探针编译配置，补齐后成功运行。

最小建议：乘cos/sin前先将radius提升long double，与现length/box提升策略一致；新增unit圆柱与次正规圆柱等比例几何/实际绘制比较（屏蔽不同单位文字），不能只检查annotation数量。修复后独立复审，不用之前13CTest/次正规box通过替代。当前R07源码待该项关闭，尚未给发布结论。CAD未实现范围不审。

### R07窗口范围独立审查与亲自回归

实际Controller C3E2E1945D16CEB92527E0E2B0DCF263BF440296B8DA83A9ABAA7D95E67DEF30/h C71CF40FD742D882549AA49D4F25AD423DE0B783519742121C800E778CE40CC3；Window AE682A6C6146A1A14E44681880AE1D6596006D278D6D8CDDE848F53953C65948；测试54E627462FE61E44BCC1A017F427BD60C24EB036074B879318B70737ADC5A57A。显式isolationActive不根据剩余可见件数推断，隐藏隔离件可留模式便于后续双击切换，显示第二件/showAll/成功加载退出；失败加载前完整查询保护不变。单击仍仅选中。Window同步读取借用Part只在无嵌套事件的当前调用中使用，双击不捕获item/Part借用；延迟timer以Window为context自动随销毁取消，只在equipmentID匹配时读取当前状态重建，不恢复旧选择/显隐。设备或操作在timer前改变时只重读最新状态，没有旧数据覆盖。

已亲自运行现build/bin/r07_window_tests.exe -platform offscreen --snapshot-dir build/reviewer-r07-window-offscreen，退出0，全部R07_WINDOW字段1。EXE SHA5A621655D6F096B517E64C9D06C113553460236C9DF9B1058884C8346C34AD11，日志build/reviewer-r07-window-offscreen.log。软件投影结果不能代替原生。模拟用户两平台日志全部1、原生FBO samples4，执行主体为模拟用户；本角色独立查看原生02隔离B、03过滤C及06回正保留显隐图，正文与毫米标注一致，短投影明确线未显示。

此窗口范围未发现阻断项，可以保持当前最小实现。全R07仍待R07-01径向次正规修正及复验，不据本段提前记0.7发布或用户再次验收。

### R07-01最小修正独立复核

已读取Scene509324D3173F8528B887AD7C3B552A9FDB3E9A7ED4F1022C1FC9A6C6C0986CF7与test36321604720D77C7C370CD1EA92DE74469FE09B5210AC1D0657124DA350CFE82实际diff：radius先提升long double，四径向顶点乘法均不再先发生double下溢；原长度/包络约束保持。测试实际绘制unit圆柱和等比例次正规圆柱，裁掉不同毫米文字再像素比较，并核对三轴包络毫米/投影端点；能发现原形变而非只检查数值。

本角色亲自用同一临时Qt探针重新直接编译当前Scene（独立subnormal-fixed.exe），实际offscreen退出0；模型区像素差由59153归零、annotations4/无范围错误，result-fixed.log已保留。输出中的radialDoubleFirst=0是探针用来展示原表达式的独立算术对照，并非修正后产品仍用double。R07-01业务缺陷修正独立接受；全R07仍待作者正式新代码两平台回归证据与最终范围核对。

已向主管说明新像素test.copy(modelRegion)的坐标当前为逻辑像素；若高DPI实际image DPR大于1，应按DPR缩放裁剪rect，避免不同毫米文字进入裁剪造成伪失败。该项为测试跨DPI稳健性建议，不是产品业务阻断。

### R07-01关闭与0.7交互源码最终结论

已独立核对作者正式修正证据：build/r07-01-before.log在新像素测试/旧代码上不同像素92415并退出1；新软件与原生日志不同像素均0、完整尺寸/交互测试通过，原scene原生回归退出0，受影响两项CTest通过3.71s。原生报告仍requested4/contextFormat0/真实FBO actual4；独立查看r07-01-native-after/r07-subnormal-cylinder-model.png，模型已恢复20段圆柱形状，与正常比率圆柱一致。此为作者实际运行，本角色另亲自直接编译当前Scene用独立探针确认0差异。

R07-01已关闭。结合本角色实际源码审查、窗口offscreen亲自回归、专职模拟用户两平台控件结果、原生图形/标尺与修后证据，当前R07三维/隔离/窗口范围独立审查通过，无未关闭源码阻断项。可以进入主管集成、最终构建回归和0.7包核对；不把未打包/未推送记为交付完成，真实用户需求来源与自动验收主体仍分别记录。

通过范围为Scene509324/h33A82、test363216、ControllerC3E2/C71C、WindowAE68、窗口test54E6及相应CMake测试接入；尺寸中心截面参考与外包络语义应继续在指南说明。高DPI crop测试建议不影响此本机通过结论，后续若调整测试须再次核实际差异；产品MSAA支持依驱动如实报告。CAD0.8未审/未包含，不将本结论套到新网格或转换流程。

## 0.7桌面包最终独立核对通过（2026-10-09）

已亲自读取outputs/equipment-viewer-0.7.0-20261009-000738(.zip) manifest：version0.7.0/sourceCommit8f2a0b250a1ec8022974627152f3e884af5ead69/databaseIncluded=false，sourceHasUncommittedChanges=true如实反映当时未来0.8未提交worker/test/handoff；核对时主树HEAD8f2a且未提交仅这些未来文件。worker不在桌面包/CMake0.7，不能把dirty旗标误写clean。

目录EXE、build EXE、ZIP内EXE独立SHA256均80DD310C45247D04127A564FF003CE3B52BDBA110C49D93CD9EA70D05BD40086。ZIP32条目，未发现数据库/秘密配置/key/pem/env或未来CAD worker/runtime。已独立查看outputs/0.7包启动验证.png：正常中文、三件基础模型和视角回正文案；该启动抓图不作为完整GL叠加标尺的像素证明，原生FBO标尺/采样及隔离已由R07专项证据验证。主管报告清Qt PATH/plugins原生smoke退出0，本角色不冒充再次运行。

结合已关闭R07-01与已接受R07源码范围，0.7包独立核对通过，无本版发布阻断，可按持续授权tag/push。主树最终14CTest16.50秒/原生专项由主管运行，不套未实施0.8；真实用户再次体验验收仍未声称发生。

### 0.7新版文档包meta复核（001651）

主管README更新后重新打包outputs/equipment-viewer-0.7.0-20261009-001651(.zip)。本角色实际核对manifest source3e3bb57fc05a464e4e5b85f46842065ec5a313d4/version0.7/dirty=true/noDB，目录及ZIP内EXE同80DD310C45247D04127A564FF003CE3B52BDBA110C49D93CD9EA70D05BD40086，与此前已核build/旧0.7包一致；32条目无DB/秘密/CAD worker。8f2a..3e3差异仅README与review handoff，README现明确0.7及0.8开发状态，不涉及业务行为。无需重复smoke冒充新业务验证，原相同EXE清Qt原生smoke和R07证据继续有效。新版0.7包meta复核通过，可tag/push；dirty来自未来worker如实记录。

## R08 worker独审进行中：R08-W01混合开放几何静默丢失

已完整读冻结tools/cad/convert_step.py E6B6A692DB74260EB8112C3425E5CAB6E851125344702B7421282F35562BBCB3及test E0AD0E5E8C4C24CFF1A780976C4388EB0AEAC58268031609E9325DE8D899D517/requirements与cad-converter交接；按入口使用hostPython3.12 -I、explicit private modules/samples亲自运行正式10/10测试，退出0，20.653秒，build/reviewer-cad-worker-0.8.log。没有下载依赖或执行样例来源库脚本，没有替代后续Python3.14嵌入式包/WindowsJob约束测试。

R08-W01已真实实证：read_solids仅提取每leaf/compound内SOLID，略过非实体几何，有任何solid则全局成功。OCCT受控生成compound包含closed box2×2×2及X100处开放plane face，写STEP后真CLI exit0/published=true/parts1，只剩box，开放面静默丢失。冻结规范开放曲面拒绝和失败保留约定下，混合输入不应成功部分导入。旧正式test只单独开放面，无solid时失败，未覆盖混合。

临时证据C:/Users/23871/AppData/Local/Temp/reviewer-r08-worker/mixed.py/mixed.log与mixed-a1b97cb910184413b0284e63e94167ba/raw STEP和result.json保留。建议最小检查所有leaf/compound有无未被closedsolid拥有的几何；拒绝不支持的开放面/壳/线，允许合法多个closedsolids，不能把solid自身面/边误判。已即时通知主管及backend，未改worker业务。此项待修复复验，worker尚不结论通过；C++数据独审结论保持独立。

### R08-W01进一步影响：旧NEMA probe不能证明整文件完整支持

backend在严格Topo覆盖处理中实际发现NEMA原文除6个located SOLID外另有4个Closed=false开放SHELL，每壳250faces，且solid-owned map覆盖0/250；此分类执行主体为backend，本角色未重复原文件分类。该发现支持旧worker6part结果曾丢弃非实体几何，而非仅测试夹具特殊情况。不得为保持旧10tests或interop结果而盲放严格检查。

C++数据层与Scene独审通过仍仅说明既有有效DTO能精确保存/重启/绘制；此前6实体probe来源于真实STEP，但不能由这份probe声称原NEMA所有几何被完整支持。当前worker R08-W01继续阻断。按原冻结“开放曲面拒绝”，正式0.8公开电机/装配样例需换受支持纯closed实体文件，再生成新probe和更新实际预期/许可/sourceHash；若决定支持shell，则须另明确冻结其语义/平面维度/校验和验证范围，不以隐式丢弃或bbox替代实现。

已同步主管；backend修复未冻结期间不对其未完成代码给通过，不与CAD UI抢原生焦点。新嵌入式3.14.8+cp314 OCP安装/导入由主管执行，host3.12当前10tests不是嵌入式包验收。

## R08 CAD UI/Windows进程初轮独审：R08-U01树退出确认顺序

范围主树UI提交63d15a4da6001c6bc0eae920907b84ec08f9f28a及其真实主构建16CTest。已读CadImportProcess全部源/header、Window/Controller/Collab diff和完整process tests、cad-ui交接，不用旧worker部分NEMA历史套新完整转换。

初读正确：Job在CreateProcess JOB_LIST出生附加，2GiB JOB_MEMORY/KILL_ON_CLOSE，启动核IsProcessInJob；只显式绝对运行文件/modules/-I，输出/输入/合计日志有界；QProcess语句排队完成callback采用context+generation，自毁/重启后无后续state访问。Window额外cadBusy覆盖process NotRunning到业务queued提交间隙；cancel可使queued callback失效并恢复控件，析构先删转换对象/清callback，CAD发布在构造基础API文档前拒绝。Controller成功保存后读取失败提示已保存而不是伪回滚。

R08-U01（待修）：finish只看主QProcess NotRunning/exit0就读取结果；complete busy=false、关闭Job、删QTemporaryDir，没有确认Job中全部子孙退出。因此剩余helper可以仍写/持有结果文件，消费/清目录/允许新start早于全树确认。KILL_ON_CLOSE最终发终止请求不是本代码中的明确ActiveProcesses=0证据；现有孙进程测试只有cancel后外部WaitForSingleObject，不覆盖主成功0留活孙。

已亲自独立编译当前CadImportProcess Qt临时探针C:/Users/23871/AppData/Local/Temp/reviewer-r08-ui/tree.cpp/exe/log，无共享构建。受控主Python启动自己的孙进程，孙CreateFileW share=0写完2字节结果并记录PID后持文件sleep；主确认写完/PID后exit0。当前类返回done=1/busy=0/bytes=0，错误“结果缺失、为空或超过64MiB”，实际上文件非空且存在，读取时仍被活孙排他持有。此为确认顺序实证，没有宣称本次永久孤儿；library随后的Job关闭会终止该孙，没有外部PID或正常服务操作。

建议：保留ownJob handle，终态先处理/终止残留后有界核QueryInformationJobObject ActiveProcesses==0，再消费结果/解除busy/清临时目录；查询/终止/确认失败保留job/temp/busy可重试，不用WaitForSingleObject(job)简单代替默认job信号语义。加入主exit0+孙持结果或目录回归，并断言callback前owned子孙句柄已signaled。已通知主管等待最小修，不改业务。

参考已查Microsoft主源Job Objects文档：https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects；相关资源语义只用于上述Code Review，不宣称本机已复现任意IO永不退出。

### R08-W01关闭与worker源码最终独立结论

已完整复查worker88735E2CB496E28FAAB48E347A393B12C7546104240B8139A847978754064FB6/testC39ACB301B568E921EED5A5D1776DB72505842054508433152F82EBDC8679A8E实际新函数和全部新增测试。COMPOUND/COMPSOLID累计TopLoc递归分别收集SOLID与extras，保持原全局位置/名称；所有located Solid有效性检查后建TopExp.MapShapes全拓扑map，extra必须确实属于solid拓扑+location，shell/wire wrapper逐子项检查，不按bbox猜。空shape记unsupported，不再静默丢失；无实体仍NO_SOLIDS。原XCAF循环/镜像绕序/单位/资源/前后hash/partial发布约束保持，CLI/协议未变。

审查者亲自使用主嵌入式Python3.14.8/私有cp314 OCP7.9.3.1执行正式14tests，显式--modules/--samples/--assembly真实Yellow文件，14/14通过27.484秒，build/reviewer-cad-worker-fixed-3.14.log。正式测试包括原NEMA负例、真正mixed face/line整体拒绝无partial、已有结果不覆盖+失败后合法重试、纯多个closedsolid成功、owned重复拓扑不误拒、真实轴承孔/曲面/单位/名称/源SHA/全局placement与独立STEP读取。缺省受控box夹具没有冒充公共电机；本次明确用了公开Yellow。

本角色另用原独立失败fixture mixed-a1b97...的同一原STEP再次真实CLI：现在exit1/UNSUPPORTED_GEOMETRY/outputExists=false，build/reviewer-cad-worker-mixed-fixed.log，原旧成功半模型证据保留。R08-W01关闭。backend另亲自host3.12与embed3.14各14tests通过，执行主体分别记录；主Qt过程+临时SQLite实际新Yellow6/4162v4332t由主管执行，原NEMA不打包。

结论：当前worker源码独立审查通过，无其未关闭阻断项。仅支持完整closedsolid拓扑，开放/游离几何整体拒绝，不再把旧NEMA部分6solid结果作为完整模型；新Yellow来源许可与包路径由主管/运行库负责人维护。准确120秒/2GiB/进程树退出归Qt WindowsJob，R08-U01修复另复核；跨机器和最终桌面包不由本CLI结论代替。依赖requirements09DC78713B5A49B275CE2E33EA23BFDE3A5567D1EA9B618E18CD3F12BC88EC11保持固定novtk/proxy7.9.3.1。

### R08-U01关闭与CAD Qt流程源码独立结论（dd149bc/诊断8ca9a11）

已独立完整复查a3bf781主树cherry dd149bcf5d39158c31f7c902015254d5ff98d849：finish和complete在读result/解除busy/释放目录前核自己Job BasicAccounting ActiveProcesses==0，残留子孙主动终止并用有界预算确认；转换即使主exit0有残留也作为失败，不取半结果。Query/Terminate失败或等待超时保留busy/job/temp并拒新start，可重试cancel。只有TotalProcesses=1的短暂主计数更新等待归零，不把所有主退出瞬间误判子孙。确认全树后显式remove临时目录成功才放行，失败按同一退出预算保留，RAII不假报清理成功。

受控Config钩子只能让本对象真实查询/终止失败，不能伪造active0，析构清钩子真实收尾。无外部PID/Job handle授权路径。STOP的waitForFinished同步finished在stopping时返回，由stop使用剩余总预算确认，不双重业务回调。complete仍通过QObject上下文/代际零延迟投递，callback可自毁或重启，之后无成员访问；取消queued成功不会导入。Window独立cadBusy避免processDone到业务提交间隙新导入；析构在成员有效时先删转换器，关闭未确认则ignore；Controller保存提交后load故障明确已保存，失败前状态不替换。CAD发布明确拒绝而非basic假模型。

审查者亲自用原独立Qt tree.cpp重新直接编译新业务源，tree-fixed.log现在给“主进程退出后仍有子进程，转换已取消”，done1/busy0/bytes0，确认策略已从读锁冲突假缺失转为先终止全树+拒半结果。原probe失败日志保留，未改共享构建/服务。

又亲自执行主build/bin/cad_import_process_tests.exe -platform offscreen，清EQUIPMENT_CAD_*环境参数使用当时默认build/bin内嵌入式运行库，退出0，Job ActiveProcesses0/child-after-exit/query-failure/terminate-failure/retry/destroy及atomic/window全部accepted1。日志build/reviewer-cad-ui-fixed-default.log；EXED4773EB56A67906E3778F08D63145AB02B48C794EE250C19EC01C264FDE50F4E；业务cpp2BEB37833A64AA2B82CEF69D5B1AF1D823CF3F0763A24BF0D1587CEA8B409579/header43677876508E3648232DCC956C2EFDE8E6358605744B09444F8342FBF24787F7；诊断testF4006F80C5C00824DAFA779A49EF104928B74086EB5068BE50EF21750A5BB2C9。

旧主全CTest在selfdelete line175曾单次失败，原build/ctest-cad-integrated-final.log保留；诊断8ca9a11仅加强notified/error/busy输出，无业务修复。作者/主管及本角色后续相同业务复验均未重现，不能把它表述为已找到或修复原因；本结论关闭的是已识别R08-U01树确认缺陷并接受当前生命周期源码/有效回归。release整体仍需主管必要最终检查，不用此段删除历史失败。

当前UI/Job/Controller/Window/Collab已审源码独立通过，无其未关闭已识别业务阻断项。主新Yellow实际process+临时SQL重启由主管验收，worker88735本角色已单独通过；模拟用户新公开608/Yellow双平台与5资源cycles由其执行待本角色进一步证据核对。POSIX/系统API持续真实失败/复杂400k性能/最终runtime许可与复制/干净包不由本源码结论替代。主管后续临时移走build/bin runtime作新可复现安装准备属于生成物变化，不把暂缺依赖错误当旧UI产品失败。

## R08-R01缓存信任缺陷关闭及runtime脚本独审结果

已读冻结setup6F892...与sync66D94479BBBEF46FDD08CDD983B9D5AD59EF266944464AE146C05CA696D34D1D完整源码及build/package插入点。初稿verify跳过任何__pycache__/*.pyc，probe另设空cachePrefix但正常worker禁写不禁读。审查者在完全独立私有canonical副本仅增加unchecked-hash OCP init缓存，固定zip/wheel所有payload不变，inventory+verify通过而实际load_kernel执行未核cache，cause为REVIEWER_UNVERIFIED_CACHE_EXECUTED。原源runtime未改；C:/Users/23871/AppData/Local/Temp/reviewer-r08-runtime/cache.py/log/copy保留。

主管最小修后setup SHA0ABD3A067E1250B7014984C7ACBF090544E24707398C897686413FD013EB9BA6只放行固定payload，不再给cache例外；sync/worker未变。本角色用同一原cache-67e646...完整fixture复验verify明确unknown-pyc拒绝，所有475文件SHA/数量（含固定payload和恶意cache）前后完全相同，未删除或覆盖；clean新canonical又验证通过。cache-fixed.log保留。R08-R01关闭；当前复制/验证只信任固定归档库存，不把禁写当成禁读保障。

其他已读保护：固定3.14.8 embed/7.9.3.1 cp314 novtk+proxy URL/SHA，缓存与下载stream预算，ZIP路径/重复/软链接与512MiB展开限制；ordinary拒路径及所有祖先reparse；先逐文件匹配payload再probe，以空cache/tmp及最小env运行 -I -B，版本确为3.14.8/7.9.3.1。新stage独立UUID、完整许可/源码/manifest核对及probe后发布；已有不同/未知目标拒绝并保留，已核已有目标只加缺失同内容文件，不Force覆盖/不删除原runtime。唯一本脚本递归删除为自己本次新建stage，删除前重新核ordinary/parent/prefix；失败stage保留。

PowerShell wrapper保UTF8 BOM、source/parent/child及叶名cad-runtime guard，PSF签名+全部官方embed根文件hash后才启动自带Python，bootstrap再校全部wheel；明确BootstrapPython需绝对文件，不从PATH找解释器。build调用新canonical同步到bin/cad-runtime，package仅version>=0.8同步到freshbundle/cad-runtime，旧7包不包含未来runtime。旧legacy与旧bin由主管先备份，未覆盖正常资料。

许可/NOTICE为技术交付清单：30固定官方来源及SHA+原Python LICENSE，包含OCP/OCCT与附带DLL/相关Header及MSdocx；NOTICE给源码/构建配方链接和已读取PE版本边界，明确未证明的DLL/工具链对应，不把文本清单称法律合规保证。最终包仍需实际文件/完整manifest哈希与不依赖系统Python验证，不以source verify代替包。

结论：当前setup0ABD/sync66D9及已读build/package接入源码独审通过，无其已识别源码阻断项，可继续fresh/default wrapper与最终包核对。作者/主管实际offline prepare/freshsync/repeat/unknown拒绝及全16CTest由其记录，本角色实际独证为完整payload cache攻击及修正拒绝/保留与clean验证。源码通过不冒充所有执行由本角色完成。

## 提前0.9 Node有限资源检查独立审查

已全文读取新增api/test/resource.test.mjs（FF78D39EB0B8116BA3DB33D3CCFB761B3D916897051BE3AD52C83A5D95D8F5FB）、backend-stability交接与实际日志node-resource-final.log（D68A217C60B20EB69E26080030C5EA3C87696E27E454A7A9C4624351D0EC0844）。日志对应server5810761...0800CA/cache79DF/repo238564，已亲自核当前文件一致。作者执行Node24.18 --expose-gc实际1/1通过6.640秒；本角色按主管要求不重复未变化业务的真实DB/服务检查。

有效边界：真实createApp/start随机loopback port、内置http keepalive Agent，请求解析/鉴权/operation timeout/cancel/close使用实际实现；仅Repository为受控边界，pending只因实际AbortSignal取消，不用替身timer或close代取消。每周期18请求，401不进入仓储；500不露内部细节；503实际timeout；真实客户端断Socket在250ms时限前传取消；失败后正常读回；close先abort全部pending再store.close且只调用一次。server/client close事件set、server.getConnections、pending及Agent三池均实际归零，无正常MySQL/Redis/端口操作。

两周期暖机、5批各3周期/54请求共270测量请求。公开getActiveResourcesInfo与process.report.libuv形状未知先失败，不把API缺失当0；不使用私有_getActiveHandles，不把loop记录计为handle，Windows全部OS句柄明确null。每批TCP资源/活动libuv句柄回暖机基线。监听归零是网络server.listening，不扩大为所有JS listener全局计数保证。

独立核对实际五批GC后heapUsed：16513136→16707408→16793144→16942208→16994328→17030784字节，累计+517648。该小样本堆值是逐批上升，不能写为无持续增长或堆已平稳；测试只记录，不用任意字节阈值判zero leak，也没有由此证明泄漏原因。RSS/external及活动句柄分别记录，socket/task正确归零不等于全内存无泄漏。是否长时平台稳定可在后续有需要时另测，不靠本5批外推。

结论：新测试与作者证据独立审查通过，可用于0.9有限受控HTTP资源/取消收尾检查；如实保留堆增加与非真实DB/Redis/有限时长范围。当前不由这一测试提前宣称0.9全部发布或1.0稳定性保证。

## 0.8候选桌面包最终独立核对通过（055807）

独立核对outputs/equipment-viewer-0.8.0-20261009-055807(.zip)：manifest version0.8.0/source63806e0dd7fd50d6645b1aa4bb7c34dfe6731d8b/dirty=true/databaseIncluded=false。核对时HEAD63806e0，仅review/sim/release文档与未来Node资源test/handoff变化，不把dirty误记clean。目录EXE、build EXE及ZIP streamed EXE三处SHA均527A4793F0433DA431E4A72A8F921E0E70C1C3ACBBE6A3AE35171DF3B2043670；包内worker88735/setup0ABD与已审源相同。

本角色独立Python读实际固定zip/wheels、核缓存制品SHA、重算440个官方payload库存并用当前verifier核候选runtime；473个runtime-manifest文件均hash匹配，实际runtime474个文件（含manifest）集合完整无额外cache。逐流核ZIP的全部510文件（511条目含目录）与目录每个SHA相同，无缺失/多余；30固定许可正文逐文件SHA与冻结清单一致。build/reviewer-package-0.8-deep.log和reviewer-package-0.8-allzip.log均accepted1。不是只信任自写manifest或用主管自查代独审。

SDK包含嵌入式Python/私有modules/worker/bootstrap/30许可+Python LICENSE/NOTICE与来源/manifest。两公开STEP及其说明/许可在目录与ZIP一致，source raw文件Git对象等于63806e0已提交对象；NEMA不打包。递归目录与ZIP扫描未发现DB/sqlite/key/pem/local配置/env/partial或未核pyc。没有普通data/服务凭证进入交付。

已独立读build/cad-package-native.log全部验收结果和查看01真孔/02六件装配/10新SQLite重开三图：完整1320x820客户区，真实环孔/Yellow曲面、六件位置与毫米包络/来源明确，原生实际FBO samples4；CAD_SQLITE_RECOVERY onlinebackup/freshRepo精确mesh/source fingerprint及全部CAD_USER acceptance1。执行主体为主管使用包内Python/modules/worker与模型+当前同源Qt业务harness，不冒充bundle主EXE完成导入。bundle主EXE另由主管清Qt PATH只System32原生smoke退出0，已独立看0.8-package-startup.png正常启动。裸--version的GUI弹窗probe未当作控制台验证通过。

结论：0.8已审源码与本候选包独立核对通过，无本版未关闭发布阻断，可依持续授权tag/push。W/U/R及数据三项均已闭，旧175单次测试失败和旧NEMA部分历史均保留且限制准确，后续正式默认/包内必要复验由对应角色记录。复杂极大CAD/跨机器/ASan及真人再次验收没有虚称完成。0.9仅已有资源检查、入口与版本文档/包变更按范围复核，不扩本阶段业务；最终1.0另核meta。

## 0.9有限稳定性范围源码独审通过

已实际独读从已发布0.8检查点之后的全部小diff：CMake project/main QApplication/server exportVersion/api package与lock顶及root package均0.9.0；依赖版本与其余锁节点不变，仅增npm test:resources= node --expose-gc --test test/resource.test.mjs。Qt/SQL/worker/API业务实现无新变更。resource.test仍FF78D39EB0B8116BA3DB33D3CCFB761B3D916897051BE3AD52C83A5D95D8F5FB，沿用已完成独审有效HTTP/受控Repo边界。

独立读主管新入口实际build/node-resource-0.9-entry.log，1/1退出0、270请求，heap16512152→17029832/+517680仍递增。执行主体主管，未把旧6.640秒与当前18.342秒相混、不重复旧全suite/真实DB/正常服务。文档区分模拟/主管/独审、正常ev_shared未恢复、正常记录仅已授权backup后迁移保留、临时CAD/ev_test恢复以及有限资源观察；未写跨机/复杂模型/零泄漏已验证。

源码结论：0.9当前有限资源检查入口、已验证范围整合与版本metadata独立通过，无源码阻断。新Qt EXE构建和0.9包尚待主管meta，不预记交付通过。已向主管提示最终release文档更新旧“0.8发布后/将提供npm命令”的待办语气，并把驱动/Qt缓存造成波动改为可能解释/原因未独立定位；此是证据用语准确性，不新增业务或重复测试要求。

## 0.9桌面包独立核对通过（104701）

本角色实际核候选outputs/equipment-viewer-0.9.0-20261009-104701(.zip) manifest：0.9.0/sourcefa82fd99656173d014562c16dcff175de165f613/dirty=false/databaseIncluded=false，核对开始HEAD同fa82且工作区clean。目录EXE/build EXE独立SHA85339235AB0673EC9FEB3FA8D03EEAFD3477CC126235DD85D2DE29A584F84741，ZIP逐文件核对应EXE一致。相对发布0.8的src/tools/cad/scripts差异仅main版本字符串，未改变已审业务。

独立逐流核全部ZIP510文件（511条目含目录）与目录SHA和集合一致；runtime474文件含manifest逐SHA与已完整审过0.8 runtime完全相同，473个manifest哈希亦全部匹配，因此worker88735/setup0ABD/固定SDK440库存/30许可+Python LICENSE/NOTICE/源码沿用同一已审字节。扫描无DB/sqlite/secret key/pem/local配置/env/pyc/partial。日志build/reviewer-package-0.9.log accepted1，未重复14/16/完整CAD。

已亲自查看outputs/0.9-package-startup.png：原生中文窗口/基础三件模型/JSON-STEP入口正常；主管清Qt PATH原生主EXE smoke实际0，本角色只读图和记录，不冒充运行。0.9npm资源入口新日志npm-resources-0.9.log实际1/1退出0、270请求、6.685秒/heap+520568仍递增；与旧6.640及前次18.342分别记录。指南/release已将待提供命令/0.8待发布状态校准，并不确定解释资源波动，不声称长时平台或zero leak。

最终结论：0.9已审有限稳定性源码、入口与此包独立核对通过，无本版发布阻断，可按持续授权tag/push。实际真人验收范围未扩大；1.0整合与最终meta仍独立核对，不能用本结论提前记1.0完成。

## 1.0最终整合源码独审通过（待包）

实际读取git diff v0.9.0、全1.0汇总release与README/guide/AGENTS/working-state。CMake/main/server/package/lock顶与root包均1.0.0，仅版本字符串变化，锁其余节点/依赖/业务/测试未改。新增1.0文档把0.3..0.9已实现和发布的检查点汇总，没有跳过阶段用改编号替代实现；Qt/SQL/worker/HTTP语义沿用各版已审代码。

交付scope准确：CAD完整closed实体/真实曲面与位置/外包络非制造标注/本地持久化，开放几何整体拒绝、CAD团队发布明确不支持；共享Bearer与显示作者名不是独立账户体系，无公开部署或新增费用。环境/记录/F01-1真实验收与后续Agent模拟/技术独审清楚分开。GC堆递增、资源有限/原因未定位、无ASan/跨机/复杂大模型/生产长压均保留限制；原一次selfdelete等待失败保留不声称定位原因。正常共享库未恢复，Git与资料快照分开。

包使用说明data/viewer.sqlite与实际package launch命令核对一致，源码data/records.sqlite不同用途已在README明确。1.0已编译/正常API受控重启及只读health/status/404由主管执行，未将磁盘版本等同服务状态、没有本角色重复真实服务或正常库操作。final文档仍标包/远端待完成，不提前声称1.0发布。

结论：当前1.0小diff/文档来源与scope独立审查通过，无源码阻断，可以生成最终包并提交实际meta供独核。未变化的旧fullsuite不无依据重跑；最终runtime库存、EXE/ZIP、原生启动及最终包模拟试用/远端检查点仍要对应新包记录，不用此前包结论套新artifact。
