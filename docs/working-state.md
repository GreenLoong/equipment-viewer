# 当前工作状态与中断恢复入口

更新日期：2026-10-08。实际工程：D:/作业/简历/工作模拟。

## 恢复前必读

1. AGENTS.md 的“持续多 Agent 开发规则”及恢复规则。
2. docs/engineering-standards.md，含注释与中断恢复第11节。
3. 本文件、docs/handoffs/ 中各角色已落盘记录，再检查 Git 状态。

不得根据本文件推测某 Agent 仍存活。先核对文件与实际工具进程；同一文件只允许一个负责人。

## 目标与授权

全部由Agent连续开发到可交互本地桌面包，允许明确标记的模拟用户反馈驱动需求。用户无需编写代码，局部功能无须逐项等待人工验收；保留既有真实验收记录。最终真实体验仍由用户决定。

本轮目标：SQLite资料、JSON导入、多设备选择、只读属性、三维旋转缩放、拾取高亮、显隐、查看全部、搜索和隔离查看。网络/MySQL/Redis单列后续扩展，本轮无需安装外部服务。

允许局部实现、验证、文档和本地Git检查点；不执行远程推送、发布、付费服务或删除正常数据库。

## 基线

当前本地HEAD原为8aa7c51，存在用户前序F01相关已保存但未提交的改动，必须保留。主管正在将已检查的前序资料形成本地基线提交，完成后更新哈希。

F01-1用户于2026-10-08明确验收通过；F01-2完整属性与清除已在实际工程实现，已有构建及静态审查记录，本轮不伪造新的用户验收。

## 当前文件负责人

| 角色 | 状态与范围 | 接手入口 |
| --- | --- | --- |
| 主管/集成 | Controller、Window、main、CMake、脚本、公共文档、Git和交付包 | 本文件与规范 |
| 数据工程师 | 等待完整规范同步，负责Importer、import_tests、samples | docs/handoffs/data-engineer.md |
| 图形工程师 | 已确定SceneView接口，等待完整规范同步；负责SceneView、几何辅助、scene_tests | docs/handoffs/graphics-engineer.md |
| 独立审查 | 在最终集成时审查，不能并行修改实现 | docs/handoffs/reviewer.md |

## 固定接口

- EquipmentImporter::parse(const QByteArray&, Equipment&, QList<Part>&, QString& error)，静态方法；失败不改输出。
- SceneView : QWidget；setParts(QList<Part> const&)、setSelectedPart(QString const&)、setHiddenParts(QSet<QString> const&)、fitAll()、rendererDescription() const；std::function<void(const QString&)> onPartPicked。
- 主管扩展Controller维护可见性、隔离、导入及设备枚举，Window依ID关联，Scene不访问数据库。
- OpenGL/软件回退共用几何、相机和拾取；原生GL需要单独真实验证。

## 下一步

1. 同步完整规范、建立基线提交及一致性数据库快照。
2. 释放数据/图形实现，主管实现Controller与Window整合。
3. 先读取更新后的构建脚本再执行构建与受影响测试。
4. 独立审查、修复复验；更新实际结果和模拟反馈台账。
5. 打包到原工作目录outputs，核实打包版本实际运行后交付。

## 验证与恢复状态

截至本文件首次建立：本轮新增功能未构建或验证；检查点、数据库快照正在准备，不把进行中记录为完成。

恢复指令示例：继续项目，先读取AGENTS.md和docs/working-state.md，核对实际Git状态及各角色handoff，按技术规范从未完成项继续，不丢弃现有文件，不重复创建远程仓库。
