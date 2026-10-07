# QA / 独立审查交接

更新日期：2026-10-08。工程：D:/作业/简历/工作模拟。基线：cbc879f2c6c867b551e5d004adf254de98bfc769，分支 feature/interactive-viewer。

## 当前状态

已读取 AGENTS.md 的持续多 Agent 规则、docs/engineering-standards.md v1.0 与 docs/working-state.md。当前实现阶段，不对未集成代码作交付结论。仅落盘本交接文件；未改业务、未运行构建测试。待主管通知集成验证完成后审查实际文件及差异。

## 待审路径

- src/equipmentimporter.*、tests/import_tests.*、samples/：解析类型、限制、字段定位、原输出保护、虚构样例。
- src/equipmentcontroller.*、src/equipmentwindow.*、tests/equipment_window_tests.*：导入持久化、设备切换、选择、显隐、隔离、搜索与属性同步。
- src/sceneview.*、几何辅助、tests/scene_tests.*：有限数值安全、几何方向、相机、拾取、拖动阈值、隐藏排除和资源生命周期。
- src/main.cpp、CMakeLists.txt、scripts/：入口、Qt模块和部署依赖、检查点/恢复工具。
- README.md、验证记录.md、docs/模拟反馈与交付记录、最终outputs包：实际运行、模拟来源、真实验收区分、限制披露。

## 最终关键验收清单

1. 正常启动、重启读取原资料，不重复种子、不覆盖资料；旧记录功能仍可运行。
2. 导入至少两份虚构样例；已有设备ID拒绝；缺字段/类型错/尺寸错/未知形状/重复零件/超5MiB/超10000项拒绝。失败保留解析输出和当前设备，不留下部分数据库资料，之后仍可合法操作。
3. 多设备切换成功才提交状态，成功清除选中、显隐、隔离；失败保持原完整显示。
4. 清单和三维均按零件ID关联；同名不混淆；点击三维与清单/属性同步；隐藏零件不可拾取。
5. 隐藏保持选中与属性；选中隐藏对象不自动显示；显示全部清除隔离并保留选择；无选择时禁用隐藏/隔离。
6. 搜索仅过滤清单；过滤掉选中项清选择、保留模型可见性；恢复搜索后列表与模型状态一致。
7. box/cylinder尺寸和X轴圆柱方向正确，毫米/Z向上/中心坐标明确；切换属性无残留，文本按纯文本处理。
8. 原生GL窗口实际验证与截图；旋转、缩放、resize、fitAll、拖动后不误拾取；空场景可解释；极端有限数值安全归一化或明确报错。软件回退测试单独记录。
9. GL资源随上下文正确释放，回调和Part借用指针无悬空；Qt对象/Repository销毁安全。
10. Doxygen公开接口和关键约束注释齐全；文件负责人范围遵守；文档不预写通过。
11. 干净依赖环境启动打包版本，Qt DLL/qwindows/qoffscreen（若测试需要）/qsqlite及实际GL模块齐全；包内数据库与凭证排除，至少两份样例及启动入口/操作指南可用。
12. 先读取修改脚本再执行；SQLite online backup关闭目标连接、校验可读与quick_check，快照不提交；恢复不清库、不reset/clean，保留未提交工作。

## 审查方法与结论边界

读取 git diff 基线及新文件；对应源码hash或提交。审查只读实施文件，意见列位置、触发、影响和必要修正。主管运行结果与审查者独立静态判断分别记录。阻断问题修复后复查受影响路径，未验证原生/包运行不可写通过。

## 暂停/恢复

中断时保留已落盘文件。本记录不意味着旧Agent仍存活。恢复者先读 AGENTS、working-state、handoffs，核对分支、git status和实际差异，再确认实现负责人。先等待或重新获得主管“集成验证完成”的通知；不要把进行中实现误判为缺陷或验收通过。当前下一步：等待最终集成后独立审查。

## 2026-10-08 集成核心审查进展

已只读审查Importer/Controller/Window/SceneView、import_tests/scene_tests/workflow_tests、CMake、main、构建运行脚本、checkpoint-data.py。查询基线cbc879f对应差异并完整读取新文件；未跑构建测试、未改业务。

发现并反馈：GlSurface上下文aboutToBeDestroyed闭包原先可能在派生成员销毁后由基类析构再次触发cleanup。图形负责人已保存QMetaObject::Connection，cleanup先disconnect，initializeGL重新连接；实际源码复查接受此修复，父窗口拥有SceneView的3次销毁回归已加入。实际原生复验由主管记录，不能声称首次非零退出已证实同因。

其他核心实现暂未发现阻断问题。小故障提示建议：Window::importData成功分支不应覆盖refreshEquipmentChoices失败提示；可与loadEquipment一样保留枚举错误而仍声明资料保存成功。

待审：最终启动/打包入口、交付依赖/样例/指南、恢复文件和真实检查记录；修复后的最终范围与hash。本轮尚未宣布完整交付验收通过。

## 最终脚本静态复查（待打包实际验证）

已读取最终scripts/package.ps1、scripts/run.ps1、启动设备查看器.cmd、README、user-guide、simulated-feedback、working-state与AGENTS更新。package独立新建bundle，不覆盖已有目录，不复制data或凭证；DLL/插件/样例/指南范围明确，manifest记录sourceCommit/dirty及exe hash，Git每次safe.directory与退出码检查。允许主管继续打包和实机依赖验证，本结论不等于包已运行。

已复查importData修正，refreshEquipmentChoices失败提示不再被成功文本覆盖。GL清理断开连接的修复与3次父窗销毁检查仍保留。暂无阻断打包验证问题。

待统一的小项：main的applicationVersion仍为0.1.0而包为0.2.0；使用指南应明确普通复制备份前关闭应用（运行中用online backup）。已反馈主管。最终待核对包文件/ZIP、manifest与exe哈希、依赖隔离运行结果及最终Git检查点，随后落最终审查结论。

## 最终交付复核结论（2026-10-08）

结论：未发现阻断当前本地Windows桌面包交付的问题；真实用户体验尚未验收。实现对应功能提交8ebe748244bd1adc7b282e3e19db2b68d71c12bf，相对cbc879f基线的实现、规范、恢复、测试和脚本已独立只读审查。已反馈GL生命周期与导入错误提示问题，实际修复后均已复查。

更正前次版本建议：最新src/main.cpp第21行实际为0.2.0，与CMake、manifest、指南一致；前次0.1.0观察属于较早文件内容，现已失效。最终指南已明确普通复制备份前关闭所有使用数据库的程序，运行中使用一致性备份工具。

实际只读核对：
- bundle为C:/Users/23871/Documents/Codex/2026-10-07/new-chat/outputs/equipment-viewer-0.2-20261008-031644，ZIP同名。
- manifest对应sourceCommit 8ebe748244bd1adc7b282e3e19db2b68d71c12bf，sourceHasUncommittedChanges=false，databaseIncluded=false。
- bundle与build/bin的EXE SHA256均为C111C6A52729850D8A43871967682BC7478EE3DF595C1EB699D946709F7C672F，与manifest一致。
- 目录和ZIP均含启动入口、指南、两份虚构JSON、Qt及MinGW运行库、OpenGL/OpenGLWidgets、qwindows/qoffscreen/qsqlite插件；没有data数据库、.env或密钥文件。
- 已实际查看outputs/设备查看器预览.png，设备选择、导入、三个零件、三维模型、显隐/隔离/查看全部、只读属性、未选中提示和搜索均可见。
- 最后只读Git status为空；主管将继续保存最终验证与交付文档，不表示可据旧状态断言之后永远干净。

主管实际验证（非审查者自行运行）：
- 最终九项CTest全通过，7.49秒，临时数据。
- 串行原生Scene（包括3次父窗析构）、Window五步、main smoke均退出0，原生OpenGL。
- bundle在PATH仅Windows/System32、清除Qt插件环境变量后原生smoke退出0。
- bundle在同一临时库运行两次equipment-check，均一台设备三个零件、退出0。

审查者仅运行只读文件/Git/hash/ZIP清单检查及查看截图；没有自行启动应用、构建或运行测试，未改业务文件。最终记录需继续区分主管执行验证、独立静态审查、明确标记模拟反馈以及真实用户验收。

已知限制保留：复杂设备性能和其他Windows电脑未验证；软件投影遮挡排序及极端跨度微小零件精度有限。网络/MySQL/Redis为后续扩展，无本轮运行依赖。恢复先读工作状态/角色交接并核对实际文件，不清库、不reset/clean，不要求用户重新编写已授权逻辑或逐项确认。
