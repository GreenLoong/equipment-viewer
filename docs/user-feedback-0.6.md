# 0.6缓存与回源模拟用户验收计划

日期：2026-10-08。角色：模拟工艺工程师 Agent。0.6七项有限用例已由本角色亲自完成offscreen/windows两轮真实Redis/HTTP/MySQL/GUI操作并通过，六张新增Windows截图独立查看完成。模拟验收、技术检查、独立审查与真人体验分别记录，不把准备或纯TCP检查记成真实Redis通过。

## 用户价值与证据边界

0.5已经实际验证共享设备和批注。重复刷新同一设备时，用户仍需要可靠的模型和最新工艺意见；0.6希望减少不可变几何的重复数据库读取，同时让缓存断开时仍可继续工作。现有小样例没有已观察到的性能瓶颈，不预设Redis一定比MySQL更快，也不从HIT标签推导响应时间改善。

只缓存不可变设备详情；批注和修订从MySQL读取。桌面操作入口沿用共享库及批注，缓存诊断以真实HTTP `X-Cache`、鉴权后的 `/api/status` 和测试读取计数取证，不把实现参数加入工艺用户的操作负担。

## 有限验收用例

| 编号 | 模拟操作与控制 | 用户可见结果及必要证据 | 状态 |
| --- | --- | --- | --- |
| SU-06-01 | 客户端重复刷新/选择同一精确设备，先冷读取再连续读 | 模型资料相同；MISS实际增加MySQL详情读取1，HIT增加0；记录实际耗时，不据标签称加速 | 两平台通过 |
| SU-06-02 | 缓存启用时真实HTTP发布、GUI刷新/下载并打开本次SQLite新连接 | 几何和精确ID正确，资料确实本地持久；GUI操作与HTTP操作来源分别注明 | 两平台通过 |
| SU-06-03 | 选中零件并保留未提交批注草稿，切断本轮Windows桥；继续共享刷新 | 从MySQL回源，有实际BYPASS与详情读取计数增加；草稿、已保存意见、几何和选择保持 | 两平台通过 |
| SU-06-04 | 恢复本轮桥，再次共享刷新 | 后续请求有界恢复HIT，资料与草稿保持，不重启桌面程序 | 两平台通过 |
| SU-06-05 | 缓存已热后在桥暂停期间由另一HTTP客户端更新批注，GUI刷新；桥恢复HIT后再刷新 | 最新版本2、更新者乙、UTC可读，旧草稿仍在；批注没有X-Cache且不改变缓存计数 | 两平台通过 |
| SU-06-06 | 几何缓存已热时，无令牌/错误令牌请求该设备详情与诊断接口 | 401无data/缓存标签，缓存与MySQL详情计数不变，随后授权GUI继续可用 | 两平台通过 |
| SU-06-07 | 在精确中文/斜线/尾空格ID与点ID设备间重复读取，并比较query/path别名 | 冷/热都对应正确设备与零件，别名HIT不串数据，随机测试namespace隔离 | 两平台通过 |

本轮只据这七条反馈推进，不再新增偏离0.6的业务需求。TTL、坏缓存、连接生命周期等关联边界由后端技术测试/独立审查覆盖；如实际操作发现体验缺陷，再保存具体步骤与证据交主管修复。

## 运行前提与安全约束

- 主管已冻结0.6配置、X-Cache/BYPASS语义、鉴权状态字段与可观察读取计数，本角色已读取。下一步建立独立缓存测试harness；0.5已通过的 `shared_user_tests.cpp` 与 `run_shared_user.mjs` 当前保持不动。
- 继续显式配置、空ev_test/3310/ev_app守卫，与后台Agent约定独占时段。只清本轮创建的四张表；Redis使用独立随机测试namespace，只清自己创建的key，不执行FLUSHALL/FLUSHDB，不污染ev_shared缓存。
- 缓存故障拟通过本轮持有的独立TCP故障代理/测试接入链路切断与恢复，不关闭正常Redis、API8765、MySQL80或删除数据库。需要新的运行接口时交主管协调，不自行改生产。
- 准备阶段WSL曾挂起；随后用户自行启动WSL，主管核实Redis就绪后才放行。本角色未调用restart/shutdown，也没有在环境未就绪时尝试绕过前提。
- 凭证只来自忽略的本机配置，不打印Redis密码或Bearer令牌，不把秘密放到argv、截图或仓库。

## 如何记录真实结果

GUI操作继续使用真实QTest控件输入；HTTP层记录缓存标签、状态码和对应请求数。几何MySQL读取次数来自正式鉴权 `/api/status` 的 `mysql.equipmentDetailReadAttempts`，不以MISS数量代替SQL次数。批注最新修订另用两个客户端写入/刷新结果及缓存计数不参与批注请求来核实；契约未提供批注读取诊断计数，不虚构这一字段。若需要额外SQL计数，使用测试观察器而不改生产指标。

每个平台串行运行，记录冷/热/故障/恢复请求的实际耗时与样例规模。短样例和少量请求只证明路径正确，不能据此宣称大型设备性能优化。最终报告命令、退出码、日志、计数、缓存标记、原生截图与独立查看结论。

缓存不可用时用户是否可继续看模型、保留草稿和得到最新批注是主要验收结果；诊断标签是对应证据。0.9再做可交互交付包的体验场景，0.6不提前标包体验或真人验收通过。

## 已读取的冻结约束

只有已鉴权、合法ID的GET设备详情参与缓存，列表/批注/POST/PUT不缓存，404不负缓存。HIT需合法且精确ID匹配；健康缺值/坏值回源MISS；disabled、连接/命令失败或容量限制BYPASS；MISS的SET失败不改原200/MISS。状态计数按当前API进程生命周期分别记录，state=disabled/ready/degraded。

Redis使用官方客户端固定6.3.0；秘密配置仅本机127.0.0.1:6380，namespace1..64个允许字符，测试随机namespace。超时、队列、重连与值大小都有边界；本角色不会通过非法端口改配置绕过守卫。故障接入由主管确认本轮独立6380链路的所有权，再测试断开/恢复，不擅自停止常规缓存服务。

## 独立测试及执行范围

`tests/cache_user_tests.cpp` 使用临时SQLite，真实点击共享库刷新/下载、选零件与批注、粘贴未提交草稿；共享库刷新过程中暂停本轮缓存桥后仍保留几何与草稿；另一HTTP客户端更新批注后由GUI刷新取得版本2，草稿不变；桥恢复后再次真实GUI刷新，显式放弃草稿后关闭。已在两平台真实运行，六张原生截图在 `build/cache-user-0.6-windows`。

`tests/verify_cache_user.mjs` 在主管全文检查且确认真实Redis/空ev_test/空Windows6380后运行。脚本先持有自己6380 listener，真实Redis PING成功后才创建空ev_test。随机namespace，清理仅调用正式 `equipmentCacheKey` 生成的两个精确键，不scan/FLUSH。冷/热详情分别核X-Cache与官方MySQL读尝试计数；无/错令牌401不得给data或增加计数；只诊断短样例冷/热耗时。

GUI流量经过仅观察、不篡改正文的HTTP代理，真实记录缓存标签和精确geometry。临时文件握手由本脚本暂停/重建自己Windows桥，并检查桥子进程退出确认；不关闭LinuxRedis或正常服务。批注更新由另一真实HTTP客户端完成，GUI必须显示最新保存内容和保留原草稿，不能把只HTTP结果叫GUI验收。

Qt子进程复用R09补强后的close确认helper；未确认退出时不DROP测试表，输出cleanup=0保留受控ev_test。恢复helper的六项fake回归已通过，新的真实恢复复验与缓存首次执行都须主管/审查复核及独占协调。WSL现已由用户启动，主管核/bin/true成功；本角色没有restart/shutdown。3310由主管恢复，不能只据WSL可用就宣称缓存通过。

```powershell
$env:EV_TEST_CONFIG_FILE = 'D:/作业/简历/工作模拟/api/runtime/test.local.json'
node tests/verify_cache_user.mjs build/bin/cache_user_tests.exe offscreen
node tests/verify_cache_user.mjs build/bin/cache_user_tests.exe windows
```

执行前主管全文读新harness，确认MySQL3310恢复、ev_test为空、Windows6380可由本轮独占、LinuxRedis7.4.7就绪和后端缓存源复审完成。offscreen先运行；等待主管原生资源测试退出0释放焦点后再windows运行。旧0.5 tests未修改。

## 真实运行证据与延迟样本

本角色执行上述两命令：offscreen退出0，约4.16秒；windows退出0，约5.04秒。日志分别为 `build/cache-user-0.6-offscreen.log` 与 `build/cache-user-0.6-windows.log`，均有：

```text
CACHE_HTTP cold=MISS hot=HIT exactAlias=1 exactDotId=1 mysqlReadsColdDelta=1 mysqlReadsHotDelta=0 unauthorizedBeforeCache=1
CACHE_GUI refresh=1 download=1 localPersistence=1 faultKeepsGeometryAndDraft=1 latestAnnotation=1 reconnect=1 discardClose=1 simulated=1
CACHE_USER accepted=1 httpMissHit=1 actualMysqlCounts=1 authBeforeCache=1 gui=1 bypassDraft=1 latestNotesMysql=1 recovered=1 simulated=1
CACHE_USER cleanup=1 ownedKeysRemoved=1 ownedBridgeChildrenConfirmed=1 ownedTestTablesRemoved=1
```

| 平台 | 同一个2零件设备冷MISS样本 | 热HIT样本 | 每种样本数 |
| --- | --- | --- | --- |
| offscreen轮HTTP测量 | 246.773 ms | 16.796 ms | 1 |
| windows轮HTTP测量 | 254.318 ms | 17.366 ms | 1 |

这是同机小样例各一次，包含API/Redis桥连接等实际开销，只作为该轮测量记录，不是统计基准或大型设备性能结论。降低MySQL详情读取次数由正式计数单独证明，不把时差或HIT数量当数据库次数。

桥断开时，代理实际观察到GUI详情请求BYPASS，status显示degraded且MySQL详情尝试次数增加；桥恢复时只重建本次Windows listener，后续HTTP及GUI详情重新HIT，status为ready。LinuxRedis本身始终未停止，正常API8765、ev_shared、MySQL80及正常SQLite未操作。本次空ev_test创建的四表、随机namespace两个精确key以及自持桥child均确认收尾，已向主管通知库/6380/Windows焦点释放。

最新批注由另一真实HTTP客户端通过PUT从版本1更新为2；GUI在降级时读取该新版本，恢复HIT后共享刷新仍取得版本2并保留未提交草稿。代理确认批注响应没有X-Cache，批注写入/读取未改变缓存计数，真实SQL最终author甲/updatedBy乙、设备和零件ID、版本2及文本正确。没有虚构批注诊断读取计数字段。

## 六张原生截图独立查看

本角色独立查看 `build/cache-user-0.6-windows` 六张：

- 01下载本地模型：两同名零件以不同ID列出，选中零件橙色高亮，材料/位置/100-80-60尺寸完整。
- 02缓存热读取和03BYPASS：保存区保留初始意见，独立草稿仍为中文引号多行，关联零件不变，令牌以圆点遮蔽。
- 04缓存断开最新批注：保存区显示他人最新意见、版本2、更新者乙与UTC Z时间；我的旧草稿原样保留。
- 05恢复缓存：最新保存版本2与旧草稿并存，显示草稿保留/显式提交提示；服务连接和动作入口正常。
- 06收尾本地模型：关闭共享库后本地选择、模型高亮及只读属性保持。

图片中的正文、UTC元信息、提示及控件可读，未发现阻断缓存使用的体验问题。缓存命中和收尾不由截图证明，依据真实头、计数、SQL及桥退出确认。本轮0.6模拟验收通过，不代替真人体验、独立代码审查、Redis极端值/TTL技术检查或0.7新交互验收。
