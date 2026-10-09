# 本机共享服务、批注与Redis缓存

2026-10-09。供本项目集成/维护Agent使用。运行需要已安装Node24.18.0、mysql2 3.24.5和Windows MySQL8.0.42；默认HTTP127.0.0.1:8765、独立数据库127.0.0.1:3310。共享库、批注和Redis已随0.4..0.6交付；当前运行版本以health为准。磁盘版本与正常已启动进程可能不同，更新源码不自动重启服务。正常共享库由主管统一切换，后端测试不直接升级共享库。

## 配置与命令

从`D:/作业/简历/工作模拟/api`执行。`runtime/service.local.json`由setup生成供后端读取，含本机MySQL账号；`test.local.json`只指ev_test。`client.local.json`严格仅host/port/token供Qt读取。所有配置、node_modules和运行目录均被Git忽略，不能放入桌面通用包。

```powershell
node tools/setup-mysql.mjs setup
node src/server.mjs
npm test
npm run test:resources
$env:EV_TEST_CONFIG_FILE = Join-Path (Get-Location) 'runtime/test.local.json'
npm run test:integration
```

setup只恢复已标记的项目3310实例，不操作原MySQL80/3306；非空无标记数据目录或未知端口占用会拒绝。停止本项目数据库的入口是`node tools/setup-mysql.mjs stop`，需先协调HTTP/GUI使用者；工具核实实例身份后SQL SHUTDOWN，不强制停止未知PID。此停止入口尚未在本轮实际运行。

后端默认操作总时限10000ms，关闭池时限1000ms，可在服务配置追加operationTimeoutMs/closeTimeoutMs。setup核对固定核心并保留扩展配置。数据库超时/取消会销毁连接；commit回复丢失仍可能已完整提交，客户端应刷新确认，不能宣称服务端撤销成功。

## 先备份再升级

保持0.4主API运行且暂停结构变更，由主管检查并调用：

```powershell
node tools/backup-mysql.mjs
```

工具仅允许marker身份一致的本机ev_shared/3310/ev_app，核实实际datadir/账号/数据库以及InnoDB表。它使用安装的mysqldump，密码只进LOCALAPPDATA专属ASCII随机选项文件，不在命令行或输出。single-transaction/no-tablespaces/GTID关闭/no表锁生成一致性SQL，二进制stdout落到忽略的`.recovery/sql/<UTC时间随机后缀>/ev_shared.sql.partial`；成功后记录字节数、SHA256和manifest并改名.sql，失败保留partial。临时选项文件在收尾移除，不能删除预先已有的同名文件。

主管已报告首份dump成功，目录为`.recovery/sql/2026-10-08T00-37-09-996Z-40aa75a9`；manifest的restoreVerified=false。dump成功与恢复实测不同，不能将该副本声明为已完成恢复验证。后续恢复需在独立空库/独立实例中用对应专属账号演练并检查资料/FK，再决定切换；不把dump直接导入正常ev_shared、不以清库回滚。

备份后主管停止旧0.4 API并启动0.5入口。initialize先保留并验证equipment/part/schema_version，再CREATE IF NOT EXISTS annotations，验证字段/索引/InnoDB及引用当前库的FK，最后把schema_version从1升级为2。MySQL DDL会自动提交，因此不声称新增表可随错误回滚；创建后但标记升级前中断会保留表和旧标记，重复启动继续兼容校验并升阶，绝不drop修复。遇不兼容结构或未知版本503/DATABASE_SCHEMA_ERROR，保留数据排查。旧0.4程序不接受标记2，回退必须结合独立恢复副本验证，不能仅换回旧EXE/Node源码。

## 批注实现导读

- `src/annotations.mjs`：UUID小写、精确scope、2000/80 UTF-16单元及Unicode校验、CAS整数边界，创建原始业务字段SHA256。
- `src/repository.mjs`：annotations设备FK与设备内零件联合FK；可空partId为设备级批注。创建指纹original_payload_hash永久保留，后续PUT不改它。相同requestId/原指纹返回当前DTO200，内容不同409/IDEMPOTENCY_CONFLICT。已有修改时重试原POST返回最新存续记录，不把内容改回原稿。
- PUT用id+expectedRevision单条CAS修改text/updated_by/revision/updated_at，一方成功后相同期望版本返回409/REVISION_CONFLICT；创建者、创建时间和关联身份保持。DTO时间由UNIX_TIMESTAMP epoch转UTC ISO，避开本机日期字符串时区解释。
- GET scope按updated_at降序、id升序稳定返回最近200条；设备/零件不存在404，查询不改变geometry。显示名只是协作演示标签，本机Bearer共享令牌仍是授权边界。

公开路由及输入输出见`../docs/api-contract.md`。geometry保持0.4不可变接口；本阶段没有Redis。

## 0.5验证基线

纯测试保留14项0.4检查，新增批注校验3项。真实integration必须显式test配置并检查ev_test/3310/ev_app/空库，先建立0.4夹具与资料，再运行无损升级。已检查幂等并发200+201、原POST→PUT→原POST200当前修订、CAS200+409、联合FK、作者/UTC、重建读取、DDL标记中断重入、最近200，以及原0.4/超时/断连/关闭回归。finally先drop本次annotations，再part/equipment/schema_version，不能与模拟GUI harness并发占用ev_test。

上述后端运行不等于Qt草稿/冲突交互验收，也不等于独立源码审查。具体本轮命令结果与未验证事项见`../docs/handoffs/backend.md`。

## 0.6缓存与运行边界

`src/cache.mjs`只服务合法鉴权后的设备详情，schema仍2。可选redis配置见API契约；默认禁用，不读秘密文件或创建连接。启用时configFile相对于api目录，只读独立忽略的redis.local.json；不可用降级MySQL，配置结构错误拒绝启动。客户端与GUI均不持Redis密码。TTL默认60秒，key使用schema2/库/namespace/精确UTF8身份摘要，404不负缓存。

详情成功响应增加X-Cache HIT/MISS/BYPASS；MISS的best-effort SET EX失败保持原200/MISS。固定Lua原子TYPE/STRLEN/GET先挡掉巨型值，客户端再验UTF8/JSON/geometry/精确ID；坏值仅删本key。批注、列表和发布不缓存，批注CAS继续MySQL。

`GET /api/status`要求Bearer，返回本API进程计数/就绪状态；equipmentDetailReadAttempts是实际get尝试，包含失败，不等于MISS推测。字段不含keys/namespace/token/password/路径/SQL；MySQLprobe失败仍诊断200，而health维持503。官方核心@redis/client固定6.3.0、ignore-scripts安装；无offline queue/自动后台重连，连接合并一次、失败后后续请求冷却重连。已发命令截止会destroy真实socket和队列，关闭优先destroy而不等无界命令drain。

新增cache纯和官方client+受控RESP TCP验证涵盖命中/缺失/坏值/巨型值/认证顺序/SET失败/挂握手/挂已发命令/真实socket关闭/有界恢复。受控peer模拟Lua，不等于真正Redis执行或性能证据。环境工具和专属桥按独立审查后的managed前台启动，不重启/关闭全局WSL。

专门真实技术入口为`node --test test/cache-redis.integration.test.mjs`，不加入默认纯测试。首次执行先全文检查并协调Windows6380空闲；脚本只拥有自己的桥和随机namespace固定三个key，不使用MySQL、不scan/FLUSH、不停止LinuxRedis。已在实际7.4.7验证EX1/PTTL及1200ms实际到期、坏JSON/UTF8/几何/错ID、LIST与6MiB+1值原子Lua只返状态/定向DEL、guard保留、临时HTTP404不负缓存和自己桥暂停/恢复；结束ownedKeys=0/bridgeChildren=0/cleanup=1。HTTP404的Repository是受控替身，不能称作该项真MySQL；旧真实MySQL集成另行完整执行且4表已清理。

最新默认npm test38/38、真实Redis技术1/1、旧MySQL缓存disabled兼容1/1通过。专职模拟用户的真Redis/真MySQL/GUI两平台证据由其单独台账记录。上述技术结果不代替主管发布检查与真实用户体验。

0.9新增独立`npm run test:resources`入口：Node --expose-gc、真实随机端口HTTP、受控Repository，暖机后5批15次应用重建/270请求，验证成功/鉴权/异常/超时/断开/关闭收尾。socket/task与活动libuv回基线；GC堆值小样本逐批增加约0.5MiB，仅观察，不能称无泄漏。实际MySQL与Redis另有验证，资源入口不连接正常数据库或关闭服务，详见../docs/releases/0.9.md。

备份管道失败会终止本次mysqldump并在5秒内等待真实close；未确认退出报DUMP_EXIT_UNCONFIRMED，保留partial，不把kill请求当收尾成功。源码故障分支有替身回归，正常共享库dump无需为此反复执行。
