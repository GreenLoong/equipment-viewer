# 共享设备库API契约

2026-10-08。0.4为共享库基线，0.5新增批注，下面按增量说明；0.6缓存尚未生效。有效规范v1.1。实际运行版本以GET /health返回为准，磁盘源码版本不代表已重启服务。

## 服务与配置

Node内置HTTP，mysql2固定3.24.5，package version0.4.0，安装--ignore-scripts。src采用ES modules（.mjs或type=module），内置node:test。

api/src/server.mjs为启动入口；环境EV_CONFIG_FILE指定本机JSON，默认api/runtime/service.local.json。键：host(默认127.0.0.1)、port(8765)、token(秘密非空随机值)、mysql:{host:127.0.0.1,port:3310,user:ev_app,password:秘密,database:ev_shared,connectionLimit:4}。只读本机配置，禁止打印token/password或写仓库。测试EV_TEST_CONFIG_FILE指向同结构api/runtime/test.local.json，database严格ev_test，测试不能操作ev_shared或原MySQL80。

代码支持导出createApp/configurable start与关闭句柄，便于真实测试使用ephemeral HTTP端口。正常HTTP默认loopback，服务签名equipment-viewer、版本0.4.0。MySQL实例由主管建立独立目录/端口/账号；服务不得自行建立/清空数据库。可create-if-not-exists两张业务表及版本标记，不兼容结构必须报错，不能drop升级。

## 输入和身份

除GET /health外都需Authorization: Bearer <token>，无效401。token恒定时间比较或hash比较，日志不含凭证/完整请求体。HTTP头/body错误可解释，最大JSON body5MiB、parts10000；流式上限拒绝413，避免无界缓冲。JSON根对象、equipmentId/equipmentName/parts，零件id/name/material/shape/dimensions/position沿用桌面Importer。必填字符串非空白，ID保留原文、长度最多128个UTF-16 code units（JS .length /Qt QString.size一致），拒绝不成对surrogate，其他文本可用LONGTEXT按整体上限。unknown字段允许，空parts和同名允许，ID大小写及尾空格精确区分，同设备内ID唯一。shape仅box/cylinder，dimensions分别3/2正有限数，position3有限数。发布后geometry不可变，本版无覆盖/编辑/删除。

## 路由与响应

成功JSON为{data: ...}，失败{error:{code,message}}；不返回SQL/密码/stack。

- GET /health（不鉴权）：{data:{service:'equipment-viewer',version:'0.4.0',mysql:'up'}}，数据库不可用503。
- GET /api/equipment：data为按ID稳定排序的[{id,name,partCount,revision:1}]。
- GET /api/equipment/:id：data为原始可导入JSON{equipmentId,equipmentName,parts:[{id,name,material,shape,dimensions,position}]}。不存在404，URL path段正确decode且限制ID。
- POST /api/equipment：body原始JSON；完整校验后事务写入设备与有序零件，201 data={id,name,partCount,revision:1}。重复设备409，校验400、超限413、数据库错误503/500，任何中途SQL失败回滚，不留下半份。提交成功后不要因日志/计数失败改为失败。
- 未知路由404，错误方法405，Content-Type不符415（POST要求application/json，允许charset）。

## MySQL结构

设备id和零件equipment_id/id用VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin（NO PAD）。equipment: id,name LONGTEXT, revision默认1, created_at。part: equipment_id,id,ordinal INT,name/material LONGTEXT,shape VARCHAR(8),dimensions JSON,position JSON，联合PK与外键、按ordinal查询，拒绝重复/孤立。prepared statements绑定所有数据；批量语句可生成程序控制的占位符但不能插入用户值。事务finally release连接；池close和HTTP socket/超时有明确收尾；不使用连接池query之外的隐式全局连接。

## 检查

纯校验、真实MySQL分开。npm test执行纯检查；npm run test:integration使用EV_TEST_CONFIG_FILE测试schema，测试代码必须检查名字ev_test再允许清理自己创建的表。验证401/404/400/405/413/415/409、中文/同名/引号、大小写尾空格ID、10000边界、保存及重新服务读取、触发器制造第三个零件失败全部回滚、以后合法请求可继续。测试不触正常SQLite/ev_shared/原MySQL80。

主管负责依赖下载、环境、Qt客户端和公共文档；后端负责人只改api/src/api/test/package与自己的handoff。不要提前写0.5/0.6业务，0.4通过并形成版本检查点后继续。
GET /api/equipment?id=<percentencoded>为详情query别名，无id仍列表；用于保留点/斜线等精确ID，重复id参数或无效percent编码拒绝400。桌面关闭对话框立即取消等待；不能保证服务器尚未提交，需刷新确认，不自动重发POST。

客户端配置单独为api/runtime/client.local.json，只含host/port/token，不包含MySQL密码；桌面不能读取service.local.json。可选operationTimeoutMs默认10000（1..120000）、closeTimeoutMs默认1000（1..10000），操作超时503/DATABASE_TIMEOUT。提交中断可能已完整提交，客户端刷新不可变ID确认，不宣称远端已撤销。

# 0.5增量：工艺批注与修订冲突（0.4已发布后生效）

模拟需求见user-feedback-0.4。geometry仍不可变。本轮新增annotations，不启用Redis；先备份ev_shared再新增兼容表，不drop已有资料。迁移需显式版本记录/重复启动可恢复，MySQL DDL隐式提交需说明。保留0.4接口与数据不回归。

annotations字段：id UUID（请求requestId作ID，规范小写）、equipment_id(现有精确ID FK)、part_id(nullable，同eq part联合FK)、text LONGTEXT（非空白，最多2000 UTF16单元）、author（创建显示名≤80）、updated_by（最新修改显示名≤80）、revision INT初始1、created_at/updated_at。返回DTO{id,equipmentId,partId:null|string,text,author,updatedBy,revision,createdAt,updatedAt}；时间必须UTC ISO（用UNIX_TIMESTAMP epoch转换，避免系统时区误差）。显示名不是登录账户身份，本机Bearer令牌仍授权协作演示。

GET /api/annotations?equipmentId=...&partId=...：必选equipmentId精确验证，partId可选；省略为全部该设备的最近200条，给定只该零件，必须存在设备/零件（404）。重复或非法参数400；newest-first稳定排序。data为DTO数组，空为[]。

POST /api/annotations：body{equipmentId,partId:null或合法零件ID,text,author,requestId:UUID}，validate后事务写入并返回DTO，201。相同requestId及同一原始payload已存在返回200当前存续DTO（即使已有后续PUT，也不能回退内容或重建；支持不确定结果重试）；同UUID不同payload409/IDEMPOTENCY_CONFLICT，不生成重复。关联设备/零件必须存在，不能按名称，外键和参数绑定。请求整体限制沿用5MiB。

PUT /api/annotations/:uuid：body{text,editor,expectedRevision}，text≤2000，editor≤80非空白，expectedRevision正整数≤2147483646。单条CAS条件id+revision更新revision+1、updated_by、updated_at，成功200 DTO。不存在404；旧revision409/REVISION_CONFLICT，必须保留旧数据，message说明刷新后再显式提交；禁止无条件覆盖。author/createdAt/关联身份不变。除Bearer鉴权无新增用户登录。

客户端：共享库对话框选择设备后读取零件scope和批注；可将本地当前设备/选中零件作为默认scope（仅相同eq）。显示最新已保存内容/作者更新时间revision与独立草稿。新增、选中修改、刷新、显式放弃草稿；冲突/断网不清草稿，不自动重试POST/PUT；刷新可保留草稿并让用户确认最新保存内容再显式重提交。对话框关闭仍取消等待但不承诺撤销已经提交。ID已存在且批注下载无需改变本地geometry。

测试：真实ev_test create/persist/关联同名精确IDs/无效关系404/空白与超限400/同requestId幂等及payload冲突/两客户端同revision并发200+409/保留author/UTC时间/服务重建；UI真实操作两用户同批注、冲突草稿保留、刷新和显式重提交、断服后草稿仍在。现有共享harness收尾必须先drop本轮annotations再part/equipment/schema_version，空库guard保留，不能动ev_shared或原服务。

0.5界面批注元信息明确标UTC并显示ISO时间，不使用无标时区本地时间。dirty关闭只取消等待保留窗口，保存或显式放弃后才关闭；杀进程/额度突断不保证未保存内存草稿跨重启。
