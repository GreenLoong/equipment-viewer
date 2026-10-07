#include "equipmentimporter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <cmath>

namespace {
constexpr qsizetype maximumParts = 10000;

bool readText(const QJsonObject &object, const QString &key, const QString &path,
              QString &text, QString &error)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        error = QStringLiteral("%1.%2 必须是非空白字符串。").arg(path, key);
        return false;
    }
    text = value.toString();
    return true;
}

bool readNumbers(const QJsonObject &object, const QString &key, qsizetype count,
                 bool positive, const QString &path, QList<double> &numbers, QString &error)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray() || value.toArray().size() != count) {
        error = QStringLiteral("%1.%2 必须是包含 %3 个数值的数组。")
            .arg(path, key).arg(count);
        return false;
    }
    const QJsonArray array = value.toArray();
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QJsonValue item = array.at(i);
        if (!item.isDouble() || !std::isfinite(item.toDouble())
            || (positive && item.toDouble() <= 0)) {
            error = QStringLiteral("%1.%2[%3] 必须是%4有限数值。")
                .arg(path, key).arg(i)
                .arg(positive ? QStringLiteral("大于零的") : QString());
            return false;
        }
        numbers.append(item.toDouble());
    }
    return true;
}
}

bool EquipmentImporter::parse(const QByteArray &json, Equipment &equipment,
                              QList<Part> &parts, QString &error)
{
    error.clear();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QStringLiteral("JSON 格式错误，字节位置 %1：%2")
            .arg(parseError.offset).arg(parseError.errorString());
        return false;
    }
    if (!document.isObject()) {
        error = QStringLiteral("JSON 根节点必须是设备对象。");
        return false;
    }

    // 完整结果暂存在局部对象中，后续零件失败也不会破坏调用方正在查看的资料。
    Equipment parsedEquipment;
    QList<Part> parsedParts;
    const QJsonObject object = document.object();
    if (!readText(object, QStringLiteral("equipmentId"), QStringLiteral("设备"),
                  parsedEquipment.id, error)
        || !readText(object, QStringLiteral("equipmentName"), QStringLiteral("设备"),
                     parsedEquipment.name, error))
        return false;
    const QJsonValue partsValue = object.value(QStringLiteral("parts"));
    if (!partsValue.isArray()) {
        error = QStringLiteral("设备.parts 必须是零件数组。");
        return false;
    }
    const QJsonArray array = partsValue.toArray();
    if (array.size() > maximumParts) {
        error = QStringLiteral("设备.parts 含 %1 个零件，超过上限 %2。")
            .arg(array.size()).arg(maximumParts);
        return false;
    }
    parsedParts.reserve(array.size());
    QSet<QString> ids;
    for (qsizetype i = 0; i < array.size(); ++i) {
        QString path = QStringLiteral("parts[%1]").arg(i);
        if (!array.at(i).isObject()) {
            error = QStringLiteral("%1 必须是零件对象。").arg(path);
            return false;
        }
        const QJsonObject source = array.at(i).toObject();
        Part part;
        part.equipmentId = parsedEquipment.id;
        if (!readText(source, QStringLiteral("id"), path, part.id, error))
            return false;
        path += QStringLiteral("（零件 %1）").arg(part.id);
        if (ids.contains(part.id)) {
            error = QStringLiteral("%1.id 重复；同一设备内零件 ID 必须唯一。").arg(path);
            return false;
        }
        ids.insert(part.id);
        if (!readText(source, QStringLiteral("name"), path, part.name, error)
            || !readText(source, QStringLiteral("material"), path, part.material, error)
            || !readText(source, QStringLiteral("shape"), path, part.shape, error))
            return false;
        if (part.shape != QStringLiteral("box") && part.shape != QStringLiteral("cylinder")) {
            error = QStringLiteral("%1.shape 不支持 %2，仅接受 box 或 cylinder。")
                .arg(path, part.shape);
            return false;
        }
        QList<double> position;
        QList<double> dimensions;
        const bool box = part.shape == QStringLiteral("box");
        if (!readNumbers(source, QStringLiteral("position"), 3, false, path, position, error)
            || !readNumbers(source, QStringLiteral("dimensions"), box ? 3 : 2,
                            true, path, dimensions, error))
            return false;
        part.positionX = position.at(0);
        part.positionY = position.at(1);
        part.positionZ = position.at(2);
        if (box) {
            part.sizeX = dimensions.at(0);
            part.sizeY = dimensions.at(1);
            part.sizeZ = dimensions.at(2);
        } else {
            part.radius = dimensions.at(0);
            part.length = dimensions.at(1);
        }
        parsedParts.append(part);
    }
    equipment = parsedEquipment;
    parts = parsedParts;
    return true;
}
