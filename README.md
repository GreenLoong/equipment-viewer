# 设备模型查看器 · 0.8.0

工业软件工作模拟项目：Qt/C++桌面查看设备和零件，SQLite保存本地资料，HTTP与MySQL提供团队共享库及工艺批注，Redis缓存不可变几何。

**当前源码：0.8.0，交付包核对中。** 稳定远端仍为[v0.7.0](https://github.com/GreenLoong/equipment-viewer/tree/v0.7.0)。真实STEP导入已完成实现、主构建及独立业务审查，剩余交付门槛见[0.8记录](docs/releases/0.8.md)。

## 已交付能力

- 导入JSON设备、重启读取、多设备切换、只读零件参数与毫米坐标。
- 三维旋转、缩放、模型拾取高亮、显隐、隔离与清单搜索。
- HTTP发布/下载共享设备，MySQL事务持久化，重复ID拒绝覆盖。
- 设备/零件工艺批注、独立草稿、并发修订冲突、幂等重试与UTC时间。
- Redis命中/缺失/故障回源；缓存断开时继续读MySQL，批注始终从MySQL读取。
- 按真实反馈修正水平旋转方向、原生抗锯齿、隔离双击切换、“视角回正”及毫米尺寸标尺。
- STEP/STP闭合实体和装配导入，真实曲面/通孔网格、毫米外包络、本地持久化、重启和失败保护；公开轴承与六件电机样例带来源署名。

JSON支持box/cylinder；STEP读取闭合实体，独立开放面/壳/线与混合文件整份拒绝。CAD当前只在本地查看，团队服务明确拒绝CAD发布；制造标注、CAD编辑、IGES和厂商原生格式未实现。显示名用于协作演示，不代表独立登录账号。项目未公开部署。

## 本机快速运行

本项目已验证环境为Windows、Qt6.11.1 MinGW64、GCC13.1、CMake3.30.5和Ninja。构建脚本默认读取本机 `D:/Qt` 工具，可用参数指定其他安装位置。

```powershell
# 首次源码构建需准备私有CAD运行库；路径替换为本机受信Python3.12+。
& 'C:/Users/23871/AppData/Local/Programs/Python/Python312/python.exe' -I tools/cad/setup_runtime.py prepare
.\scripts\build.ps1
.\scripts\run.ps1 -EquipmentView
```

也可双击根目录“启动设备查看器.cmd”。设备资料保存在项目 `data/records.sqlite`，已有资料保留；“启动示例.cmd”继续打开原记录示例。`samples`有两份虚构JSON和`samples/cad`公开STEP样例，通过“导入 JSON / STEP”打开，重复设备ID或CAD内容不覆盖。通用包已附私有Python/OCP，不需要系统Python；源码准备目录为`.runtime/cad-distribution/cad-runtime`，不会改系统Python。

本地查看不需要服务器。团队共享功能需启动独立MySQL3310、Node HTTP8765及可选Redis6380；配置和令牌保存在忽略的本机文件中。服务启动、故障处理和迁移说明见[服务说明](api/README.md)，接口见[API契约](docs/api-contract.md)。正常MySQL80实例不被项目工具操作。

## 版本与验证

| 版本 | 主要变化 | 状态 |
| --- | --- | --- |
| 0.3 | 搜索后模型点选、Escape联动修复 | 已发布 |
| 0.4 | HTTP与MySQL团队共享库 | 已发布 |
| 0.5 | 工艺批注、草稿保护与修订冲突 | 已发布 |
| 0.6 | Redis缓存、真实TTL/坏值测试与故障回源 | 已发布 |
| 0.7 | 旋转方向、抗锯齿、隔离双击、视角回正与尺寸标尺 | 已发布 |
| 0.8 | 真实机械STEP模型导入、显示与持久化 | 完整构建及业务独审通过，包核对中 |
| 0.9 / 1.0 | 稳定性、恢复、完整包装与最终交付 | 后续门槛 |

0.6实际验证包括12项CTest、38项Node纯检查、真实MySQL兼容回归、真实Redis TTL/坏值/超限/恢复，以及Windows与offscreen实际GUI验收。执行主体与限制见[0.6验证记录](docs/releases/0.6.md)。技术检查、自动模拟验收与真实用户验收分别记录，不声称零泄漏或其他电脑性能已验证。

0.7集成后14项CTest与两项原生图形/窗口验收通过，绘制FBO实际4 samples，隔离双击及尺寸标尺有实际像素和状态断言。详见[0.7验证记录](docs/releases/0.7.md)。

0.8当前16项CTest通过23.75秒，转换器14项实测、Node38项及真实MySQL回归通过；轴承真实通孔与六件电机位置/拾取/隔离/毫米标尺经软件和原生两路径验证。严格运行库库存、失败保护、恢复与包证据单独记录，不声称复杂工业装配、所有驱动或零泄漏已验证。

## 文档与恢复

- [使用说明](docs/user-guide.md)：操作、保存位置和支持范围。
- [技术导读](docs/architecture.md)：模块职责与数据流。
- [工程规范](docs/engineering-standards.md)：注释、生命周期、数据库和独立审查规则。
- [版本路线](docs/release-plan-1.0.md)：逐版本派发任务和完成门槛。
- [当前工作状态](docs/working-state.md)：额度中断后的接手入口，配合 `docs/handoffs`。
- [真实用户反馈](docs/user-feedback-0.7.md)：0.7/0.8新需求及冻结设计。
- [历史README](docs/history/README-v0.2.md)：原0.2与F01教学记录。

源码按Git提交和版本标签保留；运行数据库与私密配置另行备份，Git回退不能恢复数据库。不要删除正常库或使用 `reset --hard`/`clean` 丢弃未完成工作。Windows桌面包保存于本次聊天的本机outputs目录，不包含数据库和令牌。
