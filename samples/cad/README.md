# 公开机械STEP样例

从FreeCAD Parts Library固定提交f5ca01cdcaa2f69b9295a6382719c2d90628780c下载，未修改模型正文。资产许可CC-BY-3.0，全文见LICENSE-Assets。模型供学习和查看验证，参数显示外包络而非制造图纸标注。

| 文件 | 作者与来源 | SHA256 |
| --- | --- | --- |
| 608ZZ_Ball_Bearing.step | WladIMirG，文件提交6f071a4bab37fff7784092548e6dbff21499b751；Mechanical Parts/Bearings/608ZZ_Ball_Bearing.step | 45246a4cf1bc63c8186ad8de22a74f44a63aecb450294356592c321bf281e3d3 |
| Yellow_gearmotor_straight.step | Charles Fox，提交6ca91d1dc7aeedc40d40b4d37e65163b332941db；Electrical Parts/Motors/DC motor/Yellow_gearmotor/straight/Yellow_gearmotor_straight.step | 37d7fdea3520a5b04b353a24eb5ff844f1d1e19c77865a52ad26a415a8de59e9 |
| MIT_E_Vent_Pre_Production_Model.step | berserkdesign，提交e84063ab918dd053e16d69ffb634cbf2d7455100；Medical Parts/01 Biomedical/MIT E Vent/MIT E Vent Pre Production Model.step | 286f8c1c869c05883fd751ee40d8d60a2956c8234c26c3361f1b49a08ac3a338 |

来源：[FreeCAD Parts Library](https://github.com/FreeCAD/FreeCAD-library/tree/f5ca01cdcaa2f69b9295a6382719c2d90628780c)。轴承为1个闭合实体；齿轮电机为6个闭合实体，源名称和装配位置保留。没有执行来源库的代码。

1.1增加机械测试装配（MIT E Vent，82个闭合实体），采用上述固定库的CC-BY-3.0资产授权；MIT E Vent是模型名称，不代表资产采用MIT许可证。原STEP正文未修改，只将交付文件名整理为下划线。正式窗口通过“导入 JSON / STEP”导入此样本，不使用预览网页的网格JSON，也不将显示颜色、展开或拉远偏移写回模型资料。包中保留本说明与LICENSE-Assets。

早期技术探针中的NEMA_17_with_connector包含6闭实体以外的4开放壳，旧探针只取实体而不完整，已撤销其成功样例资格并保留为忽略目录中的拒绝输入证据。应用当前整份拒绝独立开放面/壳/线，避免静默丢掉几何。
