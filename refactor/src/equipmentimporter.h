#pragma once

#include "equipmentrepository.h"

#include <QByteArray>
#include <QList>
#include <QString>

/** 将虚构设备 JSON 校验并转换为数据对象，不读取文件或访问数据库。 */
class EquipmentImporter {
public:
    /**
     * 解析 UTF-8 JSON，最多接受 10000 个零件，允许同名零件与空设备。
     * 成功后替换 equipment/parts 并清空 error；失败只更新可定位的 error，
     * 原有 equipment/parts 保持不变。未知字段忽略，所有 ID 按原文保留。
     */
    static bool parse(const QByteArray &json, Equipment &equipment,
                      QList<Part> &parts, QString &error);
};
