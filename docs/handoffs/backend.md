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
