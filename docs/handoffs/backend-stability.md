# 后端0.9有限资源检查交接

2026-10-09。负责人backend_stability，仅新增`api/test/resource.test.mjs`和本文；业务、package、CMake和正常服务未修改，无本地提交。规范已读AGENTS、engineering-standards v1.1、working-state、release-plan和api-contract；首次执行前全文检查新测试及实际server/validation/cache/repository/annotations调用对象。

## 范围与接口

沿用`createApp(config, { repository })`注入接口，实际`app.start(0, '127.0.0.1')`监听随机loopback端口，使用`node:http`真实客户端和独立keep-alive Agent。受控Repository只保存一个虚构单box模型，不连接MySQL；Redis显式disabled，仍调用实际禁用缓存对象。

Repository暴露initialize/health/list/publish/get/close；get中error抛内部错误，timeout/cancel/closing仅等待实际应用传入的AbortSignal。替身没有自己的超时计时器，也不在close中替应用取消任务；记录close进入时活动任务数，避免替身掩盖应用漏取消。

每周期18个实际HTTP请求：POST成功、3轮401与并发模型/列表/health读取、内部错误500且不泄细节、实际操作超时503/DATABASE_TIMEOUT、实际客户端socket断开及在数据库预算到期前取消、失败后合法读取、关闭时结束在途请求。每周期验证server/client socket close事件归零、server.getConnections为零、仓储任务为零且先取消再仓储关闭、listener关闭、重复app.close只关闭仓储一次、Agent sockets/freeSockets/requests为空。

暖机2周期；测量5批，每批3个create/start/close周期及54请求，共15测量周期/270请求。测试整体30s预算，实际操作超时250ms，客户端socket时限2s，观察等待最多3s；没有流量/内存的任意数量通过门槛。

## 实际验证

Windows、Node v24.18.0，在`D:/作业/简历/工作模拟/api`运行：

```powershell
node --expose-gc --test test/resource.test.mjs 2>&1 | Tee-Object -FilePath '../build/node-resource-final.log'
exit $LASTEXITCODE
```

最终exit0，1/1通过，测试6.117s、总计6.640s。日志`build/node-resource-final.log`（忽略生成物）。五批均socket0/任务0/listening=false。`process.getActiveResourcesInfo()`暖机和各批均PipeWrap2，无TCP活动资源；公开`process.report.getReport().libuv`暖机和各批句柄类型数量相同：async3/check2/idle1/pipe2/prepare1/timer2；活动句柄async3/check2/idle1/prepare1/timer2相同，loop条目不当作句柄。未使用私有_getActiveHandles；API缺失或报告形状未知会明确失败，不能计为0。Windows全部OS进程句柄数量没有受支持Node值，日志明确`osProcessHandleCount:null`。

| 观察点 | heapUsed字节 | rss字节 |
| --- | ---: | ---: |
| 暖机 | 16513136 | 92934144 |
| 第1批 | 16707408 | 95420416 |
| 第2批 | 16793144 | 95567872 |
| 第3批 | 16942208 | 96366592 |
| 第4批 | 16994328 | 96137216 |
| 第5批 | 17030784 | 96407552 |

GC后heapUsed累计增加517648字节；只记录，不设任意字节门槛或称零增长。external4444899→4444939字节。首次执行在报告形状断言失败：Windows loop条目没有is_referenced；保留`build/node-resource-shape-failure.log`，后改为仅验证实际统计用type/is_active，并排除loop。中间通过记录保留`build/node-resource-pre-final.log`；最终又移除替身close兜底取消并验证close入场任务0后重新实跑通过。

## 对应文件身份与接手

- 测试SHA256：`FF78D39EB0B8116BA3DB33D3CCFB761B3D916897051BE3AD52C83A5D95D8F5FB`。
- 最终日志SHA256：`D68A217C60B20EB69E26080030C5EA3C87696E27E454A7A9C4624351D0EC0844`。
- 实际server SHA256：`5810761CD2935ED2495C3DA5E8F2D383D38EDAC9EF3A2ACD3DD909B3130800CA`。
- 实际cache SHA256：`79DF7A6DB47A95782746FA036FDAD94D7A254C6D4C7F25E50E405A1AE4797F84`。
- repository SHA256：`2385647E62FC86B3F280708127DA9CCA36EF28049A0D6D1E97625F5FDE2F086A`；本检查仅import该模块，注入替身，不实例化/连接真实池。

root需全文读取新测试，并由review_v1独立审查实际差异/注入边界/取消/关闭/观察结论；本文为实现者实测交接，不代替独立review或真实用户验收。测试是单独命令入口，尚未加入npm test；主管可在0.9公共入口记该命令。本检查未使用正常端口、配置、MySQL/Redis数据库，无未知进程扫描或终止。

## 结论限制

此次只证明有限受控仓储HTTP负载下指定业务成功/异常与可观察socket/任务/活动libuv句柄收尾。没有测真实MySQL池、Redis启用连接、长期负载、native泄漏或全部Windows句柄；真实MySQL/Redis与Qt GUI已有另项证据，不能把本检查替换或扩张为那些结论。heapUsed确有小幅累计增长，有限批次无法区分运行时缓存与泄漏，不宣称零泄漏。
