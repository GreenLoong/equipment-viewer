# 0.6缓存预备设计（0.5发布后实施，不是已完成功能）

只缓存不可变设备详情，批注和修订永远查MySQL，MySQL仍权威。认证先于缓存。发布成功不因缓存失败报失败；MISS读取MySQL并best-effort SET EX，HIT只接受合法DTO与请求ID一致、JSON大小受限，否则删坏值回源。缓存key含数据库/命名空间和精确UTF8 ID的SHA256，防ev_test污染ev_shared；测试独立随机namespace，不flushall或删别的key。

Redis7.4.7官方源码已下载并检查Makefiles后在WSL/opt/equipment-viewer-redis/redis-7.4.7编译；archive SHA256c97e57b0df330a9e091cacff012bebe763c275398cf36ff44cdba876814b595b。原Ubuntu网络初始化失败fallbackNone，不修改全局WSL配置；如Windows不能直接访问Linuxloopback，使用项目stdio TCP桥，仅本机6380到WSL6380，真实Redis认证，密码只忽略配置不在argv/log。

Redis独立目录/PID/requirepass、maxmemory64MiB、缓存不持久化。connect/每命令/重连/close有期限，不能Promise.race留下无期限命令或socket；断开后清队列/销毁连接，后续可有界重连并降级回MySQL，不关主API。禁止offline queue无界增长。

API在GET详情返回X-Cache HIT/MISS/BYPASS，auth /api/status给真实hits/misses/bypass及就绪状态，不泄密码/SQL。stats只诊断，GUI仍正常显示资料。测试实际HIT/MISS/TTL/坏值/断服回源/恢复/令牌先验证/缓存不影响批注新revision、MySQL读取次数与延迟；只报告实测，不预设Redis一定更快。

本文件为准备范围，source/API/UI仍当前0.5开发，0.6必须另行冻结配置、运行脚本、独立review和版本检查点。