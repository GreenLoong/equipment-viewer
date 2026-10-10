#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <cmath>

/**
 * JSON 严格校验的共用小块：字段集合、非空白文本与三元有限数值。
 * 设备JSON导入器与CAD结果解析器原先各写一份同名判断，集中在
 * 此头文件后两条链路的严格程度保持一致。
 */
namespace JsonUtil {

/** 对象字段集合必须与期望完全一致；多一个或少一个字段都拒绝。 */
inline bool hasExactKeys(const QJsonObject &object, const QStringList &expected)
{
    const QStringList actual = object.keys();
    return QSet<QString>(actual.cbegin(), actual.cend())
        == QSet<QString>(expected.cbegin(), expected.cend());
}

/** 字符串且去除首尾空白后非空；其他类型或纯空白返回false。 */
inline bool isNonBlankString(const QJsonValue &value)
{
    return value.isString() && !value.toString().trimmed().isEmpty();
}

/** JSON数值且为有限值；非数值类型与NaN/Inf都拒绝。 */
inline bool isFiniteNumber(const QJsonValue &value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}

/** 恰好三个有限数值；成功时写入输出，失败不修改输出以外的内容。 */
inline bool readFiniteTriple(const QJsonValue &value, std::array<double, 3> &output)
{
    if (!value.isArray() || value.toArray().size() != 3)
        return false;
    const QJsonArray array = value.toArray();
    for (int axis = 0; axis < 3; ++axis) {
        if (!isFiniteNumber(array[axis]))
            return false;
        output[axis] = array[axis].toDouble();
    }
    return true;
}

} // namespace JsonUtil
