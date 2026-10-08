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
