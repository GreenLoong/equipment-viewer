# 项目协作约定

## 当前范围与授权

- 工程：Qt Widgets + SQLite工业设备查看器；有效规范见docs/engineering-standards.md v1.1，当前状态见docs/working-state.md，实施前必读。
- 用户已授权全部由Agent开发、连续推进到可交互桌面交付，并用明确标注的模拟反馈迭代；不再要求用户写关键逻辑或逐小功能确认。
- 当前0.3/0.4/0.5已推送main与版本tag；0.6缓存进入实现，WSL挂起的恢复动作仍等明确重启许可，其他开发/测试自主推进。后续按0.6/0.9/1.0推进。真实用户体验单列；不得自行启动付费服务、公开部署或删除正常数据。
- 既有真实验收：环境和记录示例2026-10-07通过，F01-1用户2026-10-08明确验收通过。自动检查、独立静态审查、模拟需求验收和真实人工验收分开记录。
- 已授权局部排查、实现、验证、文档和本地Git检查点自主推进；重大需求/架构变化、破坏性数据操作、外部发布及新增费用按具体影响确认。
- origin为https://github.com/GreenLoong/equipment-viewer.git；用户已授权远端同步；每个已验证版本推送到origin，最终更新默认main，保留远端历史，禁止force push。

## 开发、规范与审查

- 每阶段执行功能卡、实现、运行验证、独立审查、修复复验、文档导读和交付。正确性/安全/明显维护问题修复后交付；行为改变需独立复查。
- 新增公开业务接口中文Doxygen，关键约束注释原因。遵守C++17、Qt6 MinGW、命名/职责/生命周期及SQL参数绑定等有效规范。
- Controller为设备、选中和显隐状态来源；Window不执行SQL，Importer失败不改输出，Scene不访问DB。名称不作为身份标识。
- 同一文件同时只有一位负责人。主管维护Controller/Window/main/CMake/公共文档与交付；数据负责人维护Importer/导入测试/样例；图形负责人维护Scene/场景测试；独立审查者只读业务实现。
- 保留用户代码、未提交改动和正常数据库。原记录功能保留；不按行数重新补种设备，不以清库处理结构问题。
- 首次执行新增或变化的安装/构建/测试/迁移脚本前检查内容；允许下载必要依赖，但先核实来源与安装脚本。

## 环境与入口

- Qt6.11.1 MinGW64、GCC13.1.0、CMake3.30.5、Ninja1.12.1、QSQLITE SQLite3.53.0；OpenGL/OpenGLWidgets已接入，不混用MSVC。
- scripts/build.ps1：重新配置、构建、部署运行库和当前12项CTest；PowerShell脚本保留UTF-8 BOM以兼容5.1。
- 启动设备查看器.cmd或scripts/run.ps1 -EquipmentView打开设备；启动示例.cmd仍打开原记录窗口。--database可指定文件，--equipment-check只输出数据后退出。
- build/bin/scene_tests.exe -platform windows --snapshot-dir <目录>：真实图形验证；offscreen检查是软件回退，不代替原生OpenGL。
- scripts/package.ps1 -OutputRoot <目录>：只复制运行文件、插件、虚构样例和指南到独立交付目录，不复制正常data或凭证。
- 实际运行步骤见README.md，操作与限制见docs/user-guide.md，模块导读见docs/architecture.md，模拟反馈见docs/simulated-feedback.md，实际检查见验证记录.md。

## 中断、版本管理与数据恢复

- 额度恢复、换会话或重新分配Agent时先读working-state/handoffs，再核对分支、Git差异和实际工具进程，不能假设旧Agent存活。
- 及时保存代码和各角色handoff；主管维护汇总状态。验证阶段建立本地提交；额度突发中断不保证还能写最后记录，用最近已保存状态恢复。
- 基线cbc879f、功能提交8ebe748；最终文档检查点以实际git log为准。当前分支feature/release-1.0，按明确授权同步远端。
- 源码Git检查点与数据库恢复分开。scripts/checkpoint-data.py使用SQLite online backup并验证quick_check，快照在忽略的.recovery/databases。
- 恢复不得reset --hard、clean或删除数据库；优先修复分支或针对指定提交回退，保留未完成工作。普通数据库复制前关闭所有使用程序。
- 提交前检查git diff --check、git diff --cached和git status，避免混入本地配置、生成物、数据库或无关修改；提交不等于真实人工验收。

## 逐版1.0当前执行

- 用户要求逐步更新，当前0.6，按规范第12节推进0.6/0.9/1.0，不能只改版本号或直接宣称1.0。
- 所有新Agent读取docs/engineering-standards.md v1.1、docs/working-state.md和当前版本功能卡。专职模拟用户与专职review分工必须保留。
- 授权下载、独立本机服务、持续开发与GitHub推送，不涉及付费或公开部署；保护既有MySQL80/SQLite资料，运行目录和配置不得上传。
- 当前分支feature/release-1.0。合并origin/main保留初始文档历史，再按阶段推送，不force。不安装未经检查的npm生命周期脚本。
