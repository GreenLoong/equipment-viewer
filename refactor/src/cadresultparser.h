#pragma once

#include "equipmentrepository.h"

/** 严格读取CAD转换器协议v1；只解析数据，不访问文件、数据库或界面。 */
class CadResultParser {
public:
    /** 验证全部结果后替换输出；失败保留equipment及parts并返回可定位错误。 */
    static bool parse(const QByteArray &bytes, Equipment &equipment, QList<Part> &parts,
                      QString &error);
};
