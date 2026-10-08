# 后端Agent交接

2026-10-08。本Agent重新核实执行环境，不沿用旧Agent的失效检查。

- 已读取AGENTS.md、engineering-standards v1.1、working-state和release-plan-1.0。
- 实际只读检查：Node v24.18.0、npm 11.16.0、Windows MySQL Server 8.0.42；原MySQL80服务Running，未做修改。
- Git安全目录检查阻止普通git命令；使用单次`git -c safe.directory='D:/作业/简历/工作模拟'`读状态成功，未改全局配置。
- 已读取冻结docs/api-contract.md，0.4源码与纯/集成测试已落盘。
- 0.4方案：内置HTTP处理鉴权/请求限制/明确错误，严格校验设备JSON；mysql2固定版本参数绑定；设备与有序零件在同一事务中写入，设备ID不可变、重复409，ID使用utf8mb4_0900_bin NO PAD。纯校验与真实MySQL测试分别记录。
- 主管负责独立MySQL实例3310、依赖安装审批与执行、Qt集成、脚本、公共文档和Git；本Agent只维护api/src、api/test、package/lock及本交接，不推送。
- 0.5协作批注/并发版本冲突及0.6Redis仅规划，待0.4真实独立验收后实施。

## 0.4实现与检查点

- api/package.json：版本0.4.0、mysql2固定3.24.5、start/test/test:integration，主管检查后安装依赖（无生命周期脚本）；不自行推送。
- api/src/validation.mjs：5MiB/10000/ID128单元限制、严格文本/数值/几何、代理字符校验、保留身份原文、完整复制。
- api/src/repository.mjs：只增建三张表、检查类型/键/FK/InnoDB/NO PAD/schema版本；参数绑定，事务失败回滚finally释放，提交后直接返回摘要；错误不暴露SQL。
- api/src/server.mjs：配置读取、hash恒定时间鉴权、三设备路由/health、JSON错误信封、流式上限及读取超时、HTTP/池关闭。
- api/test/validation.test.mjs与http.test.mjs：2026-10-08 npm test实际5/5通过（4.37秒工具执行，测试报告2.94秒），包含中文/同名/引号、类型几何、Unicode与ID、10000边界、HTTP错误、Content-Length及chunked体积、无效UTF-8、内部错误不泄露、关闭句柄。该结果仅纯检查，不等于真实MySQL运行或独立审查。
- api/test/integration.test.mjs：必须EV_TEST_CONFIG_FILE，guard数据库ev_test/端口3310/本机/ev_app/空数据库，才创建和收尾删本次表。2026-10-08实际运行最终1/1通过（报告4.246秒）：真实触发器第三零件失败后设备/零件均0、错误不泄触发器消息、随后合法发布成功、并发201+409、精确身份/query点ID/64emoji、API重建读取、10000零件与5MiB边界。本次表已收尾清理。
- 尚未验证：Qt集成/模拟用户/独立审查由主管协调；setup stop/真实MySQL进程重启尚未操作。API重建已测，不冒充数据库进程重启。未写0.5/0.6。
- 复验曾发现超限上传时提前Connection:close造成客户端ECONNRESET；已改为停止缓存后排空流并受HTTP请求时限约束，明确413保持可接收。修复后一次及连续三次纯5/5通过。
- 自查补充合法ID为`.`/`..`的路径保护：使用原始request.url提取path，避免WHATWG URL点路径归一化破坏身份；纯HTTP用原始%2e路径验证。

下一步：主管完成独立实例与test配置，执行真实集成；准备实际差异给独立审查，修复和复验后交主管集成。

## 后续明确授权：独立实例工具与query详情

- 主管追加负责api/tools/setup-mysql.mjs；主管完整读取后首次执行失败，已授权Agent定位/最小修复/执行。Agent安全阶段诊断确认root-password ER_PARSE_ERROR，三处账户密码DDL仅bootstrap采用mysql2.query(sql,[随机hex密码])驱动绑定转义，其余业务数据仍prepared。保留已有datadir/initialized标记成功恢复，setup退出0并二次配置重入成功，未重置密码。
- setup固定现有MySQL8.0.42程序，LOCALAPPDATA/EquipmentViewerSimulation/mysql-data，3310，127.0.0.1；原MySQL80/3306不操作。非空无标记目录、未知占用端口、配置/标记不兼容均拒绝；初始化只空目录且--no-defaults --initialize-insecure。
- 持久凭证标记在LOCALAPPDATA专属目录；随机root/app/token不进入参数/stdout/仓库。独立mysqld --defaults-file首参数、detached与windowsHide；后续用root认证、@@datadir/port、PID进程路径和配置命令行核实本实例。stop用核实连接SQL SHUTDOWN，不强制kill。首次创建ev_shared/ev_test及限定ev_app权限；已配置重启不改密码；不启动HTTP API。
- 风险如实记录：initialize已成功但标记尚未写入时中断，遗留无标记非空目录会拒绝自动恢复，须主管核实；Windowsmode0600不等于完整ACL；秘密依赖当前用户本机权限与Git忽略。
- 新增GET /api/equipment?id=<encoded>单文档别名，无id仍列表；严格decode，无效/重复/空id 400。原:id保留。pure验证'.'/'..'/中文斜线尾空格/加号已通过；integration已真实通过点ID保存/query读取。
- 集成环境实测：MySQL8 CREATE/DROP TRIGGER不支持prepared，测试使用固定无用户值query DDL。独立mysqld默认开启binlog，普通ev_app创建触发器需本实例设置log_bin_trust_function_creators=1；helper在root认证、datadir/port和PID身份核实后执行SET GLOBAL该开关，不授予ev_app全局SUPER、不影响原MySQL80。每次setup恢复该项目本机测试开关。
- 本机配置service/test已生成并被Git忽略；服务HTTP8765未由本Agent启动，主管负责启动。独立MySQL3310仍运行。

## 0.4独立审查阻断项修复

独立审查指出三项实际问题：rollback失败后默认无reset的连接仍release；握手/收body时限不能限制连接排队/SQL/事务；外键同名表可能来自另一schema。均按实际实现修复，待独立复审，不以本Agent自查替代审查。

- repository新增统一withConnection总deadline（默认10000ms）与AbortSignal：覆盖排队、每条SQL、commit/rollback，超时/取消实际destroy连接；排队晚到连接销毁且不执行SQL。publish rollback失败立即destroy，任何已销毁连接不release。方法调用前后检查取消，防止超时后再发下一条SQL。
- Repository.close取消活动请求、销毁实际连接，pool.end有默认1000ms看门狗；mysql2固定3.24.5内部_allConnections队列仅用于握手/QUIT卡住时最后强制销毁socket，此耦合点已注释且有mock回归。
- HTTP数据库操作使用独立deadline与取消控制器，超时503/DATABASE_TIMEOUT；response异常关闭传取消信号；读取body仍独立限制15秒。配置可选operationTimeoutMs(1..120000，默认10000)、closeTimeoutMs(1..10000，默认1000)。HTTP/pool关闭有界并共享关闭Promise。
- FK元数据明确读取REFERENCED_TABLE_SCHEMA和DATABASE()，要求非空当前schema且完全一致；跨库同名元数据测试只mock，不创建/删除别人schema。
- api/test/repository.test.mjs新增五项纯回归，api/test/http.test.mjs新增挂起health50ms返回503与挂起close；npm test实际11/11通过（最终报告0.798秒）。覆盖rollback失败destroy、不release、SQL/commit/rollback挂起、晚到获取连接不执行、池end卡住实际socket销毁、跨schema FK拒绝。
- 真实integration实际1/1通过两次（最终报告3.947秒）：保留原0.4全部检查，新增ev_test 100ms受控锁等待503且池实际连接数0、释放锁后MySQL线程消失且无残留发布；合法重试201；仅KILL核实DB=ev_test/USER=ev_app的受控测试连接验证断连后恢复；持锁发布进行中close有界取消、释放锁后无半份。测试自建表按守卫收尾。
- 主管主API8765/session89956保持未操作；所有新服务测试使用ephemeral端口。主管后续统一重启并做Qt/模拟用户/独立复审。
- 事务commit返回前发生连接失效可能具有远端已提交的不确定性，界面应通过不可变ID查询确认；总时限不会承诺撤销已经在服务器完成的commit，仍保障不留下半份几何。

## 客户端配置隔离补充

- 主管明确要求GUI不得读取MySQL凭证；helper新增api/runtime/client.local.json，严格仅host/port/token。service/test仍含后端mysql配置。既有clientDTO含额外字段会拒绝并保留，不把后端配置静默当客户端配置。
- helper核对service/test改为递归核对固定核心字段，允许字段顺序变化和新增operationTimeoutMs/closeTimeoutMs/redis等扩展字段，不改写既有配置。身份/密码/数据库不一致仍拒绝。
- setup工具入口加直接执行检查，测试导入纯策略不会运行初始化。新增setup-config.test.mjs两项：核心一致的扩展字段保留、clientDTO没有MySQL/其他秘密。
- 已按已核实实例实际运行setup重入退出0、生成clientDTO；仅检查并输出key名称host/port/token，未打印内容。git check-ignore确认client文件忽略。npm test实际13/13通过，报告0.924秒。
- 主API/session89956未操作，未写0.5。

## 第二轮审查R04-04：pool.end拒绝收尾

- 恢复时先读取本交接和reviewer-v1第二轮记录。主管核对MySQL3310未监听，原API前台仍在；主管负责恢复已标记MySQL实例，本Agent未操作主API或helper。
- 独立审查R04-04指出pool.end第一次QUIT/握手错误会reject，旧`.then(finish,finish)`立即清看门狗，可能遗漏其他连接。repository.close现把reject和看门狗超时统一为destroyRemaining后finish；使用剩余连接快照避免destroy修改队列导致漏处理，单个收尾异常不跳过其他连接。正常end成功路径仍完成，原总deadline/FK/回滚保护保留。
- repository.test新增end拒绝且队列仍有两个socket的受控回归，destroy实际移除队列成员；close即时完成且两个socket均销毁，不等看门狗。纯npm test实际14/14通过，报告0.901秒；其中新回归0.55ms。
- 主管恢复后只读确认127.0.0.1:3310/PID63920已监听，实际运行guarded ev_test全量integration 1/1通过（报告4.041秒）：原0.4精确身份/并发/触发器回滚/API重建读取/边界以及100ms锁超时/实际连接销毁/受控断连恢复/持锁close均通过，测试自建表已收尾。此为本轮实际检查，非复用旧结果。
- 修复文件SHA256：repository=9F1254140C8F1A9692D92EDDDFAB02982F81DA3AA13B767A4C8A68EFE6267BA2；repository.test=69DD81B8D1FEDC95ECBD11EF03B7ED0CAC05FE445234FF66BE96A7DC0830D2E7。git diff --check限定两文件与本交接通过。待独立复审，不冒充审查通过。未改Qt/其他api文件，未写0.5。

## 0.5工艺批注实施检查点

主管确认0.4已独立验收并提交/推送aef7499、tag v0.4.0后明确启动0.5。已读新增冻结契约；本节覆盖此前“未写0.5”的旧状态。本Agent不启用Redis、不编辑geometry、不操作主API/session43076、不直接迁移ev_shared。

- 新增api/src/annotations.mjs：UUID规范小写、精确equipment/可空partId、text2000/author与editor80 UTF-16单元、配对Unicode、expectedRevision正整数上限、严格scope查询与原始业务字段SHA256。
- repository新增annotations增量DDL/字段/索引/本库设备与设备内零件联合FK校验；旧结构/schema1先验证，新增DDL隐式提交后验证，最后标记2。不drop升级；表已创建但marker1可重入核对后继续。原事务deadline/取消/rollback失败destroy/pool.close拒绝销毁/FK跨schema保护保留。
- POST幂等原始指纹original_payload_hash永久保存，PUT不改；同UUID同原始业务字段返回200当前DTO，不同409/IDEMPOTENCY_CONFLICT。并发同UUID由唯一键+锁定读解决，无重复；修改后重发原POST仍200同ID/revision和现存文本，不改回原稿。
- PUT单条id+revision CAS，成功revision+1/updatedBy/updatedAt，冲突409/REVISION_CONFLICT提示刷新后显式提交；author/createdAt/关联身份保持。DTO从UNIX_TIMESTAMP epoch转UTC ISO。GET按updated_at DESC/id ASC最近200，geometry不改变。
- server/package/lock版本0.5.0；依赖仍固定mysql2 3.24.5，无新第三方依赖。新增annotations纯测试3项，旧14项保留。
- api/tools/backup-mysql.mjs先落盘并主管完整读取后由主管真实执行成功，副本.recovery/sql/2026-10-08T00-37-09-996Z-40aa75a9；manifest restoreVerified=false。此为主管执行证据，本Agent没有执行共享库备份/迁移。
- backup限定本机ev_shared3310/ev_app+marker/实际datadir/账号/库/InnoDB；mysqldump首参数ASCII本机随机ini传密码，stdout binary pipeline+single-transaction/no-tablespaces/GTID关闭/no锁/no DROP，失败partial保留，成功SHA256/manifest。自查补强仅删除本次成功创建的ini，避免wx碰撞时删除别人文件；若临时ini删除失败给安全路径警示，不输出内容。该最后补强仅node --check，主管后续运行前应重读变化。
- 新增api/README.md迁移/备份/重入/运行导读，使用technical-documentation技能；明确DDL自动提交、schema2旧程序不能直接回退、dump不等于恢复实测、客户端DTO分离与后端/GUI验收边界。

本轮实际验证：

- npm test 17/17通过，最新报告0.902秒，保留旧14项并补批注校验3项。
- EV_TEST_CONFIG_FILE=api/runtime/test.local.json运行真实integration 1/1，两次通过，最新报告4.615秒。ev_test严格空库/3310/ev_app guard，创建真实0.4夹具和旧geometry，再验证无损迁移与schema2；模拟DDL已建但marker1重入不丢geometry/批注；同UUID并发200+201/原POST→PUT→原POST200当前修订；CAS并发200+409/显式rev2重提交；精确同名设备/零件关联与数据库FK；作者/UTC epoch；重建读取；最近200稳定排序。原0.4全部错误/边界/事务/锁超时/断连/close回归继续通过。
- finally先DROP本次annotations，再part/equipment/schema_version；已通知模拟用户负责人收尾次序及测试互斥，目前ev_test已释放。测试既不操作ev_shared，也不停止MySQL实例/主API。

下一步：主管统一新主API启动/共享库迁移（已有dump）、Qt草稿集成、专职模拟双用户和独立review；本Agent可处理后端审查意见。未独立审查/未真实用户验收/未发布0.5。

## 0.5备份凭证临时文件所有权修复

- 首轮独立审查接受批注/CAS/原始指纹/迁移，指出backup的writeFile(wx)可能已创建文件却在写入ENOSPC时失败，旧privateFileCreated未置true会遗漏自己的秘密ini。
- tools/backup-mysql.mjs新增preparePrivateOptionFile：open('wx',0600)成功立即owned=true，再handle.writeFile/close；任何写入失败都finally关闭并unlink自己拥有的文件。open EEXIST未取得所有权，绝不unlink已有文件。正常返回幂等cleanup，dump结束finally执行；外层连接destroy用嵌套finally保证不跳过文件cleanup。收尾失败仅给唯一文件路径提示，不输出内容/错误对象/密码。
- 在既有setup-config.test.mjs增加3项受控FS替身回归：open成功+写ENOSPC失败close/unlink、EEXIST不unlink、正常先close再cleanup unlink且重复不再删。没有写真实凭证文件或制造真实磁盘耗尽。
- 实际npm test 20/20通过，报告0.819秒；原17项全部保留，deadline/close/FK测试仍通过。backup node --check通过；本修复不改变SQL/DDL/事务实现，本轮未重复真实MySQL测试、未抢占ev_test、未运行ev_shared备份/迁移或主API。
- 仅修改backup工具、该纯测试与本交接，待独立复核所有权与收尾。主管后续执行backup前应读取最终工具内容。

## 0.6仅环境准备（0.5发布前不启用缓存）

主管明确分配Redis环境工具准备，要求先落盘供完整检查，不运行setup或启动服务、不改API/cache/version/service/test、不碰MySQL/主API/ev_test。恢复后已重读AGENTS/规范v1.1/working-state及本交接；只读实际Windows监听为8765/PID16200、3310/PID63920，6380未监听；未操作这些进程。已读docs/cache-plan-0.6.md，官方7.4.7编译结果属于主管证据，非本Agent再次编译。

- api/tools/setup-redis.mjs：独立redis.local.json持久purpose/端口/目录/version与随机64hex密码；缺失时排他open成功即拥有，写失败close/unlink，旧配置不同拒绝。通过隐藏wsl.exe/root/python3执行redis-stdio.py --setup，秘密仅stdin；安全状态JSON最多32KiB，stderr不转发，30秒子进程预算。默认已注册发行版名称Ubuntu，可首次设置EV_REDIS_WSL_DISTRO（需主管核实真实注册名）；不改全局WSL网络配置、不启用API缓存。
- api/tools/redis-stdio.py：Linux专属/opt/equipment-viewer-redis/runtime-6380、purpose.json、redis.local.conf(600)/pid/log，compiled7.4.7固定binary；非空无标记/未知端口/未知活PID拒绝。Redis配置requirepass、127.0.0.1:6380、64MiB/allkeys-lru、save空/appendonly no。配置和实例身份核实后才AUTH/INFO/CONFIG，检查/proc PID exe及监听socket inode属于PID，stop只对核实实例SHUTDOWN NOSAVE，不kill未知PID。
- stdio原始桥双向各64KiB队列，selectors按可读/可写事件与背压处理；默认30秒空闲、EOF后1秒排空预算，最终关闭socket/selector。正常bridge入口也先核实专属PID拥有监听socket，stdout只原始RESP，错误走不含内容的stderr；密码不进argv/log。
- api/tools/redis-bridge.mjs：Windows前台127.0.0.1:6380，每TCP一个windowsHide WSL stdio子进程，最多8连接，pipe自动背压；stderr丢弃。断开/错误/空闲/close先清socket/pipe，EOF后1500ms未退出只kill本次wsl.exe，另500ms未确认返回明确close失败，不伪造确认。未知占用端口EADDRINUSE直接失败，不替换原监听。必须由主管managed foreground session启动，没有Start-Process后台绕过。
- api/test/redis-bridge.test.mjs：独立node --test运行，不加入当前0.5 package脚本。使用fake ChildProcess与临时loopback端口，无真实WSL/Redis/数据库；4/4通过（报告0.348秒）。验证秘密仅stdin/参数无密码、RESP字节往返与stderr隔离、断开/空闲挂子进程终止、未知占用端口保留、连接数上限与2MiB输入背压缓冲<=256KiB、close资源归零。
- Node两工具node --check通过；已找到本机Python3.12并用ast.parse检查redis-stdio.py语法，通过且未生成__pycache__。Linux/proc身份/真实Redis启动认证/真实WSL桥/性能尚未运行，本记录不等于真实环境验收或独立审查。

下一步：主管/独立review完整读取三工具和资源测试；核实WSL发行版名后主管执行setup，必要时前台启动bridge；0.5正式发布后再定义实际0.6缓存接口与配置并实施。当前service/test、API源码、版本保持0.5，0.6工具/测试应排除0.5发布提交范围。

## Runtime未确认子进程审查修复与0.6冻结提案

- 主管/审查发现complete(false)过早移除未确认child，后续close和预算可能误判。现waiter的一次settlement与真实ChildProcess close分开：超时保留session/unconfirmed计数及连接预算，真实close才移除；后续close仍明确失败，晚到close后资源计数归零。新增kill不发close的替身回归验证预算拒绝新child、反复close不误报成功、晚到real close回收。
- 新回归首次暴露allowHalfOpen收到TCP EOF后Node层未限制子进程排空；已加EOF预算timer，允许Python1秒排空，之后有界stop/kill。该测试加t.after清fake close并close，即使断言失败也收尾；首次挂起只中断本Agent测试session75136，不操作其他测试/WSL/API。
- 默认发行版改为主管Registry核实的Ubuntu-24.04；本轮没有激活WSL，没有shutdown/terminate/restart。主管实测显式发行版/bin/true15秒挂起属于主管环境证据；全局WSL重启许可仍未回复，不自行操作。
- 最新独立资源5/5通过，报告0.484秒；四文件从此冻结供review复跑，避免同时修改：setup SHA256722DE988579EB4DC8941FB74AD3372D6E8EA86CCE88B7ACF5CACE873863B9D01；bridge5386EEB406259F66869DFD5383B8B1069812ACD7AD40802C936FDFA320D4CBB3；py6370D5AF900888D765C36B45058F8B199771E46E8484F3D3877A81C90AD5C584；test6C78F383946F21538E9E61FA316B41BC2F835D7E7B1E14CC77D502170011F7BF。
- 0.5正式发布d3d720e/tag v0.5.0由主管完成，0.6业务仍等待冻结契约。已只读核实官方核心@redis/client6.3.0注册元数据：Node>=20、dependencies空、无安装生命周期，未安装/改package。官方command-options明确AbortSignal/timeout仅覆盖未发送命令，因此拟用外部deadline销毁整个cache client/实际socket，而非仅race返回；read-only/best-effort缓存关闭优先destroy清队列，不在无界close等待上依赖原生超时。参考官方node-redis docs/command-options.md和client-configuration.md。
- 已发主管详细待冻结提案：可选redis enabled默认false、仅引用专属redis.local秘密配置、独立namespace/TTL60/有限connect-command-close预算/冷却重连/有限队列；仅鉴权验证后的不可变详情使用cache；X-Cache HIT/MISS/BYPASS；auth状态诊断只计数不含keys/token；annotation永远MySQL。原子Lua限制读取值大小再返回，避免官方parser先分配巨大GET值和STRLEN/GET竞态；namespace+mysql.database+DTO版本+精确UTF8 ID SHA256，测试随机namespace不flush。

下一步：runtime按冻结hash独立复审；主管最终docs/api-contract冻结后才写cache业务/pure测试。真实Redis/WSL验证仍是0.6发布门槛，用户重启许可缺失时不重启全局WSL，也不伪造真实缓存结果。

## 0.6正式缓存实现与最终冻结（覆盖前述未实施状态）

主管确认0.5正式发布并在docs/api-contract.md冻结0.6后已实现本节代码。root负责Qt/CMake版本与正常API切换；本Agent没有停止正常API、改Qt、启用service/test缓存或争用ev_test。恢复时磁盘核对实际Windows8765/PID51656，3310/6380当时未监听。用户已自行启动WSL属于主管最新证据，不据此自行重启/关闭全局WSL；真实Redis运行仍待工具独审后主管协调。

- package/lock为0.6.0；只读registry核实官方核心@redis/client6.3.0（Node>=20，dependencies空，无安装生命周期），读取package/lock后npm install --ignore-scripts实际新增1包/audit0。已安装源码检查destroy清队列/真实socket、close等待pending、命令AbortSignal/timeout不覆盖已发命令。官方依据：https://github.com/redis/node-redis/blob/master/docs/command-options.md 与 docs/client-configuration.md；不能用native选项冒充总截止。
- api/src/cache.mjs：配置结构/未知字段拒绝，默认disabled不读秘密/连Redis；enabled秘密文件缺失/读取不可用仅config_unavailable降级，文件读有AbortSignal预算。key固定ev:geometry:schema2:<encoded db>:<namespace>:<exact UTF8 ID sha>，TTL默认60，maxValue6MiB。TYPE/STRLEN/GET固定原子Lua限制reply，坏类型/巨型值在服务端删本key，不进入parser；客户端校验UTF8/JSON/geometry/精确ID，错误不日志或泄key/秘密。
- 单次共享connect尝试、disabled offline queue、有限pending、自动重连false，后续请求冷却重连；握手/已发命令挂起实际destroy socket及队列。close取消pending与文件读、销毁client、有界connecting收尾；开始connect的微任务前检查closed/代际，避免close后新建socket，旧命令截止不销毁已恢复的新client。
- server0.6只给鉴权/ID合法后的两详情路由cache；成功X-Cache HIT/MISS/BYPASS，404不负缓存；MISS SET失败仍原200/MISS。列表/annotations/POST/PUT不使用cache。鉴权status只安全字段，MySQLprobe失败200/ready false；health仍503。repository的equipmentDetailReadAttempts在get实际调用入口计数，含失败，不以MISS代替。
- cache.test.mjs纯6项，cache-network.test.mjs官方client+受控真实TCP/RESP4项，resp-cache-peer.mjs明确仅模拟Lua；新增repo读取失败计数1项。覆盖禁用/结构/精确key、MISS/HIT/TTL/非法/巨型/namespace隔离、SET错不改业务、401/非法ID零缓存、状态字段/最新批注、真实AUTH/已发EVAL挂起毁socket/冷却恢复/close。此为受控网络结果，不等于真实RedisLua或性能验收。
- tools/redis-stdio.py根据独审补转发前/proc/PID/cwd严格等于固定runtime，same binary/port/inode但其他cwd必须拒绝；保留AUTH后INFO/config_file/dir核验。Windows Python3.12 -B纯proc替身2/2通过，不调用WSL/真proc。py冻结SHA2563C450123122641C0EB22B9B16C74FE32CDB818E9E8B802B0F895893A2295EA96，test/redis-stdio.test.py=8ACBBD0641CAE4CD064C49E9F7582A62672E29DB6BBB252E28F990060E5A2FAD；其余runtime文件未改变。
- 备份tools/backup-mysql.mjs新增pipeDumpUntilClosed：管道失败/120秒总时限后kill本次CLI，并最多5秒等待真实ChildProcess close；未确认则DUMP_EXIT_UNCONFIRMED而不报收尾成功，stdio/output关闭、partial保留；成功也必须pipeline+close0均结束。新增backup-process纯替身3项验证失败等待真实close/kill无close有界失败/正常close后才成功；没有再次运行ev_shared dump或修改sim恢复脚本。
- api/README更新0.6导读和明确0.5/磁盘0.6/实际运行版本的边界。

最终实际检查：npm test **34/34通过**，恢复复验报告1.431秒（此前1.361秒）；保留旧20项，新增cache10/repo计数1/backupCLI3。node syntax与限定git diff --check通过。没有运行ev_test integration/真实Redis/桥/缓存性能测试，正常API仍0.5由root管理；0.7三维/CAD反馈与本任务无代码交叉。

以下业务/工具/test文件现在冻结供root与独立review，不与审查并行改动：

| 文件 | SHA256 |
| --- | --- |
| src/cache.mjs | D115605E97E2001A7790575363D2689966BA4FB8A59C3A48837ACDB2450340E3 |
| src/server.mjs | 8658709A9EE935C758BD383CFEFDE80A4BE661655ABB68A3E159EFB3C30588DE |
| src/repository.mjs | 2385647E62FC86B3F280708127DA9CCA36EF28049A0D6D1E97625F5FDE2F086A |
| tools/backup-mysql.mjs | 31BE3846DE4B8710F146662F12A64F3AFAB587A383F32D98BB67A1235FA17365 |
| test/cache.test.mjs | BFDD44048246CB95FB8C55759F8E4507D45AFA796F95B9B662FC34881433DB71 |
| test/cache-network.test.mjs | CB24BD76FF6D52E4D49F1055628B0844B594363E6FFFF2F5A93923C5A0505486 |
| test/resp-cache-peer.mjs | 730A19EDF44B96A7268CEE4C4B018AC7F6C18938E4DF568F4EF845CBC4B0E032 |
| test/backup-process.test.mjs | F61D9FD945C20701FB2A9083990106DA126845E7348D954CAD73341BE1127455 |
| package.json | BF36F8C313B5F723100FC79A8D8CEC46A1CDD3B1CD8D3C05FE4D561DED01F423 |
| package-lock.json | 4826A97EBA9EE5A97D5DE4B1ADDB14E66CF1A3784455D4279B31764D24459B2C |

下一步：独审冻结cache/backup/runtime，主管协调WSL/独立Redis和MySQL恢复后再允许真实ev_test与缓存验收；来源如实分开，不先宣称0.6发布或1.0完成。

## 独审R06-03冷凭证读取取消修复

- 独审在D115…cache复现：HTTP请求Signal取消后lookup已BYPASS/inFlight0，但冷credential读取晚到仍创建连接并ready。只改cache.mjs与cache.test.mjs及本交接，其他冻结文件未动。
- command.abort现在取消当前credential读取再取消connect/destroy；invalidate递增connectionGeneration。ensureConnection在读取前捕获代际、用局部凭证结果，并在创建client前核对代际/closed；读取刚完成但await continuation尚未执行的取消也不能创建socket，不保存该失效结果。
- 新增真实内置AbortController的纯回归3项：reader遵循abort；reader故意忽略abort并晚返回有效凭证；读取已完成而冷连接continuation仍排队时abort。都要求clientFactory调用0、cache ready=false/inFlight0。测试不是实际HTTP/Redis请求，不冒充真实环境。
- cache纯9/9通过（0.584秒），完整npm test37/37通过（1.325秒）。真实Redis/WSL/ev_test均未运行，root主API和sim环境未操作。待独立复审后才允许真实缓存验收。
- 本修复现在冻结：cache SHA256=79DF7A6DB47A95782746FA036FDAD94D7A254C6D4C7F25E50E405A1AE4797F84；cache.test=EB6A9080DE9D205A504C8F6C605036A98D9574EB94BCDF8105470A5C083999F7。覆盖上一表这两文件旧hash，其他hash不变；限定diff --check通过。

## 0.6最后真实技术验收脚本与backup kill失败补强

- 主管报告R06-03已独审接受，专职模拟用户真Redis/真MySQL/GUI两平台accepted1/cleanup1并释放ev_test/Windows6380。本Agent收到直接真实Redis技术检查任务；首次脚本已落盘/全文自读、送主管检查，尚未执行，不把模拟角色证据冒充本Agent运行。
- 新增test/cache-redis.integration.test.mjs，仅读专属redis.local并拥有自己的Windows6380前台bridge及随机namespace固定3个key。计划真实PING/INFO7.4.7、TTL EX1实际1200ms到期、坏JSON/UTF8/geometry/错ID、LIST类型、6MiB+1值直接原子Lua只返[2]/DEL并class拒绝，guardKey保持；关闭重建仅自己的bridge验证BYPASS恢复MISS/HIT。HTTP404不负缓存以真Redis+受控空Repository验证，不连接任何MySQL。只DEL自身3keys，不scan/FLUSH，不停LinuxRedis/正常API/global WSL；坏值和guard额外EX30限制失败遗留寿命。finally逐步收尾并验证keys/client/桥children确认0，不能误报cleanup1。
- 该新脚本node --check通过，冻结hash896746EB0F93E46C246C837BF6E246148860452A6525AE92CE7692173FCDF7BB；由主管全文审阅后再执行，未加默认npm test避免纯检查自动启真实环境。
- 同时独审实际发现backup child.kill抛EPERM会跳过3管道destroy和有界close，已最小try/catch kill失败继续收尾；新增fake killThrows回归要求DUMP_EXIT_UNCONFIRMED、3管道destroy和预算内完成，无真磁盘/CLI。backup-process纯4/4通过（0.591秒），全npm test38/38通过（1.314秒），cachehash保持79DF不变。
- backup最终hash803CCD1E8131F8D120FA47C54C1C9146DFE150C885AA41EF447706AF1D730E24；backup-process.test=54D3EF48730F23D8E815898851E7B302F1E53856924983EFD3D362219F182117。需要独审复核后再复用dump工具，无需重复正常ev_shared备份。

## 0.6真实技术门槛完成（覆盖前述未执行状态）

主管全文读取896746…技术脚本并授权6380独占运行；已确认backup killThrows4替身被独审接受、R06-03业务修复独审接受。专职模拟用户真Redis/真MySQL/GUI两平台结果由其台账记录，不冒充本Agent操作GUI。

- 本Agent实际运行`node --test test/cache-redis.integration.test.mjs`退出0/1项通过，报告2.949秒（案例2.497秒）。真实PING/INFO确认Redis7.4.7；EX1的PTTL正且<=1000，1200ms实际等待后key不存在；坏JSON/非法UTF8/不合法geometry/错设备ID/LIST类型/6MiB+1值均MISS/定向删除，guardKey保持。直接真实EVAL巨型值只返[2]，证明确实服务端限制而非仅Node后置丢弃。
- 只关闭/重建本次Windowsbridge验证BYPASS后恢复MISS/fill/HIT，不停止LinuxRedis/globalWSL。真实Redis+临时HTTP/受控空Repository404两次不写负缓存，sets不增加；该项不访问MySQL，不把Repository替身称作真实DB。
- 实际日志：`invalidValues=6，ownedKeys=0，bridgeChildren=0，cleanup=1`。cleanup精确DEL本轮随机namespace固定3keys、EXISTS3=0；关闭client/cache/HTTP以及自己的bridge并确认active/unconfirmed children均0。只读复核Windows6380无监听，Redis daemon保持未操作。
- 随后完整npm test实际38/38通过（1.339秒），覆盖旧纯检查/缓存受控TCP/冷读取取消/backup EPERM收尾；旧真实`npm run test:integration`在确认test配置缓存disabled、ev_test空库guard下1/1通过（5.088秒）。包含原0.4事务/边界/锁断连/close和0.5无损迁移/幂等/CAS/作者UTC/重建读取。finally annotations→part/equipment/schema_version已清理，另只读DB查询确认ev_test tables=0；已释放ev_test与6380。
- 没有运行正常ev_shared dump/迁移，没有停止正常API/原MySQL80或全局WSL；未涉及0.7三维/后续CAD角色变更。api/README已更新真实与受控边界。
- 本轮业务hash保持cache79DF7A6DB47A95782746FA036FDAD94D7A254C6D4C7F25E50E405A1AE4797F84、server865870…/repo238564…不变；backup803CCD…/backup-process54D3…/新真实技术script896746…为最新冻结范围。技术测试完成不等于0.6已发布，主管负责最终独审范围、commit/tag、桌面包与远端。
