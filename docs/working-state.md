# 当前工作状态与中断恢复入口

更新日期：2026-10-08。实际工程：D:/作业/简历/工作模拟。

## 当前目标与授权

用户要求全部Agent开发，按0.3→0.4→0.5→0.6→0.9→1.0连续迭代、模拟用户反馈、独立审查和测试，并推送GitHub。不等待逐功能实现或确认；技术检查、模拟验收与真实验收分开。允许下载必要依赖与本机独立服务，不包含付费或公开部署。

有效规范docs/engineering-standards.md v1.1尤其第12节；路线docs/release-plan-1.0.md。保留正常数据、用户改动和远端历史，不force/reset --hard/clean。凭证、数据库、runtime、生成物不提交。

## 已发布检查点

0.2功能8ebe748/文档3bb65dd；远端初始历史已由1b6ba2b保留合并。0.3为03c0c1a，0.4为aef7499，0.5为d3d720e，均已推送main/feature及各版本tag。最后ls-remote已核main/feature/v0.5.0 peeled均d3d720e8a063a4a24d70fa64c924ed9dfce1ffe8。分支feature/release-1.0，origin=https://github.com/GreenLoong/equipment-viewer.git（私有）。Git单次safe.directory=D:/作业/简历/工作模拟，不改全局。

包位于原聊天outputs独立时间戳目录；0.4 manifest对应aef7499、dirty=false，已清Qt环境原生smoke及独审通过。证据见docs/releases/0.4.md和review handoff。真实用户仅验收环境/记录与F01-1，后续不伪造用户验收。

## 当前进行0.6

0.5已技术/模拟验收、独审、打包及推送。0.6缓存契约/纯代码准备，runtime工具未确认child预算缺陷修复与独审中；WSL挂起，重启许可问题仍等用户明确回答，不执行全局restart/shutdown。专职review准备0.9资源测试，模拟用户准备受控ev_test备份恢复测试，首次执行前root检查。0.6真实Redis验收尚未完成，不宣布1.0。

## 已发布0.5证据

批注后端、MySQL v1→v2增量迁移、CAS冲突、原UUID幂等重试、UTC与Qt草稿界面已落盘，版本0.5.0。后端pure20/20、真实ev_test integration1/1已通过；review R05-01至05修正源码复核接受。

恢复后shared_user_tests重编成功。独立reviewer_annotation_faults实际退出0，dirty关闭、缺part、错UUID/part、recent200和旧回调取消全部accepted=1（controlledHttp，非真实MySQL）。全量12CTest通过13.40s，pure20通过856ms。真实MySQL双窗口offscreen/windows均全部accepted=1/cleanup=1；首轮combo鼠标及提示措辞测试预设已修测试helper并保留失败证据，没有改业务。

主API旧0.4会话43076已受控关闭，新前台78001运行0.5.0，health/mysql up，ev_shared schema2。迁移前后eq/part逐项核对一致（正常共享库原本为空，非空无损迁移由ev_test验证）。已有一致性备份.recovery/sql/2026-10-08T00-37-09-996Z-40aa75a9，3780字节、SHA核对通过，restoreVerified=false；不声称恢复验证。backup R05-01写失败凭证文件归属修复已独审/替身回归通过。最终包独审和远端同步已完成。

## 负责人

root负责Qt集成/CMake/公共文档/Git/交付。backend_v1负责api及自己的handoff，当前仅准备Redis runtime工具，不启用缓存、不改版本、不占ev_test。simulated_user_v1负责shared_user_tests/run_shared_user与反馈，目前独占ev_test串行测试，不改业务。review_v1只读独审业务，拥有reviewer_annotation_faults及审查handoff。各文件单一负责人，构建串行。

## 服务与数据安全

最近核对：独立MySQL3310 PID63920，正常API8765前台session78001。操作前重新核实，不能假设PID仍有效。原MySQL80的3306实例不操作。独立数据/标记在LOCALAPPDATA/EquipmentViewerSimulation；api/runtime配置忽略，不打印内容。

ev_shared正常共享库；ev_test只从空库取得本轮所有权，非空拒绝测试和清理，清理annotations→part→equipment→schema_version。不删除正常SQLite。快照在.recovery/databases，Git不恢复数据库。

## 0.6准备与后续

Redis官方7.4.7下载校验并在WSL /opt/equipment-viewer-redis/redis-7.4.7编译成功（MALLOC=libc、无TLS），未启动或接API。归档SHA256 c97e57b0df330a9e091cacff012bebe763c275398cf36ff44cdba876814b595b。WSL网络None，计划受控前台TCP桥接，不改全局WSL配置。规则见docs/cache-plan-0.6.md。

下一步：0.5真实模拟验收→独审→正常API增量升级→文档/Git/包/远端；再0.6缓存HIT/MISS/断开回源；0.9资源/数据恢复/包装；全部通过后1.0交付。不得只改版本号。

## 恢复与检查入口

先读AGENTS/规范/本文/handoffs，再核Git状态、实际进程与Agent存活；已停Agent须followup唤醒或重新分配。新/变化脚本执行前阅读。scripts/build.ps1 fresh配置、编译、Qt部署、CTest；原生OpenGL另验。package.ps1从CMake读取版本、独立目录不覆盖旧包、不含数据库/令牌。

每个已验证里程碑检查diff --check/cached/status后提交并推送feature/main/tag。无部署工作流这一前提变化时复核。额度突然中断不保证最后记录，依最近落盘恢复，不能保证刷新自动唤醒Agent。

## 0.5源码检查点与0.6环境阻碍

0.5源码提交d3d720e8a063a4a24d70fa64c924ed9dfce1ffe8已完成；独立源码结论通过。包outputs/equipment-viewer-0.5.0-20261008-131825(.zip)，EXE SHA256 177D203D03D6314DB694631FADDC42A0E9721AC27593F14C540087DFE029A629，清Qt环境原生smoke0，无敏感文件。manifest dirty=true来自未提交0.6预备工具，桌面包不含这些工具。正在最终独审包/远端同步，不提前记已推。

WSL2.6.3 --version退出0；--list --quiet挂数分钟已取消仅此次命令，显式Ubuntu-24.04 /bin/true在15s预算内挂起并仅终止自己child，无残留wsl.exe。Registry真实发行版Ubuntu-24.04，WSLService Running；未改全局.wslconfig。异步问题已询问是否允许重启WSL（会终止现有Linux任务），答复前不restart/shutdown/terminate，不把等待当批准。仍可继续0.6纯代码与0.9受控测试准备。

runtime review初读发现未确认child被从连接预算移除的问题，backend修复中，未经复审不启动。review资源测试与simulated-user的0.9受控备份恢复工具仍是下一阶段，不混入0.5发布。

## 0.5已发布（覆盖此前待发布状态）

0.5独立包核对通过，源码/ZIP/EXE哈希一致，目录仅应用运行库/样例/指南，不含数据/凭证。d3d720e已成功push feature、HEAD:main及v0.5.0。Git默认OpenSSL首次SSL_ERROR_SYSCALL，单次-c http.sslBackend=schannel -c http.version=HTTP/1.1推送成功，未禁用TLS或改全局；最后ls-remote遭临时握手失败，push输出成功与额外远端读取验证分开记录。

当前进入0.6，仍待WSL重启问题用户明确回答。下一阶段工具/代码可自主准备并测试；不得提前发布0.6或1.0。0.5交付入口/预览在原聊天outputs；git源码与数据备份均已保留。

最新远端复核：使用单次schannel/HTTP1.1再次ls-remote已退出0，main和feature均d3d720e8a063a4a24d70fa64c924ed9dfce1ffe8，v0.5.0 peeled同提交；前一额外读取握手失败已恢复。0.5远端一致已核实。
