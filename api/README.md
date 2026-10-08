# 本机共享服务与0.5增量迁移

2026-10-08。供本项目集成/维护Agent使用。运行需要已安装Node24.18.0、mysql2 3.24.5和Windows MySQL8.0.42；默认HTTP127.0.0.1:8765、独立数据库127.0.0.1:3310。本轮代码为0.5，纯检查与ev_test真实检查已执行，独立审查及GUI集成由主管推进。正常ev_shared升级由主管先备份后统一启动，后端测试不直接升级共享库。

## 配置与命令

从`D:/作业/简历/工作模拟/api`执行。`runtime/service.local.json`由setup生成供后端读取，含本机MySQL账号；`test.local.json`只指ev_test。`client.local.json`严格仅host/port/token供Qt读取。所有配置、node_modules和运行目录均被Git忽略，不能放入桌面通用包。

```powershell
node tools/setup-mysql.mjs setup
node src/server.mjs
npm test
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

## 验证边界

纯测试保留14项0.4检查，新增批注校验3项。真实integration必须显式test配置并检查ev_test/3310/ev_app/空库，先建立0.4夹具与资料，再运行无损升级。已检查幂等并发200+201、原POST→PUT→原POST200当前修订、CAS200+409、联合FK、作者/UTC、重建读取、DDL标记中断重入、最近200，以及原0.4/超时/断连/关闭回归。finally先drop本次annotations，再part/equipment/schema_version，不能与模拟GUI harness并发占用ev_test。

上述后端运行不等于Qt草稿/冲突交互验收，也不等于独立源码审查。具体本轮命令结果与未验证事项见`../docs/handoffs/backend.md`。
