#include "cadresultparser.h"
#include "jsonutil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <cmath>
#include <limits>


bool CadResultParser::parse(const QByteArray &bytes, Equipment &equipment,
                           QList<Part> &parts, QString &error)
{
    error.clear();
    auto fail = [&error](const QString &message) { error = message; return false; };
    if (bytes.isEmpty() || quint64(bytes.size()) > MeshGeometryCodec::maxBytes)
        return fail(QStringLiteral("CAD结果为空或超过64MiB。"));
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder(bytes);
    if (decoder.hasError()) return fail(QStringLiteral("CAD结果不是有效UTF-8。"));
    QJsonParseError jsonError;
    const auto document = QJsonDocument::fromJson(bytes, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("CAD结果JSON无效：%1").arg(jsonError.errorString()));
    const auto object = document.object();
    if (!JsonUtil::hasExactKeys(object, {"protocol", "version", "unit", "sourceName", "sourceSha256", "parts"})
        || object["protocol"] != QJsonValue("equipment-cad-mesh")
        || !object["version"].isDouble() || object["version"].toDouble() != 1
        || object["unit"] != QJsonValue("mm") || !JsonUtil::isNonBlankString(object["sourceName"])
        || !object["sourceSha256"].isString()
        || object["sourceSha256"].toString().size() != 64
        || !QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$"))
                .match(object["sourceSha256"].toString()).hasMatch()
        || !object["parts"].isArray())
        return fail(QStringLiteral("CAD协议版本、单位、源文件或字段不符合v1。"));
    const QString sourceName = object["sourceName"].toString();
    const QString sha = object["sourceSha256"].toString().toLower();
    QString basename = sourceName;
    basename.replace('\\', '/');
    basename = basename.section('/', -1);
    if (basename.trimmed().isEmpty()) return fail(QStringLiteral("CAD源文件名无效。"));
    Equipment resultEquipment{QStringLiteral("CAD-") + sha, basename};
    const auto array = object["parts"].toArray();
    if (array.isEmpty() || array.size() > 1000)
        return fail(QStringLiteral("CAD实体数必须为1至1000。"));
    QList<Part> result;
    QSet<QString> ids;
    quint64 vertexCount = 0, triangleCount = 0;
    for (qsizetype index = 0; index < array.size(); ++index) {
        const QString prefix = QStringLiteral("CAD实体%1：").arg(index + 1);
        if (!array[index].isObject()) return fail(prefix + QStringLiteral("必须为对象。"));
        const auto item = array[index].toObject();
        if (!JsonUtil::hasExactKeys(item, {"id", "name", "material", "shape", "dimensions", "position", "mesh"})
            || !JsonUtil::isNonBlankString(item["id"]) || !JsonUtil::isNonBlankString(item["name"])
            || !JsonUtil::isNonBlankString(item["material"])
            || item["shape"] != QJsonValue("mesh") || !item["mesh"].isObject())
            return fail(prefix + QStringLiteral("文本、shape或字段无效。"));
        const QString id = item["id"].toString();
        if (ids.contains(id)) return fail(prefix + QStringLiteral("重复零件ID。"));
        ids.insert(id);
        std::array<double, 3> size{}, position{};
        if (!JsonUtil::readFiniteTriple(item["dimensions"], size)
            || !JsonUtil::readFiniteTriple(item["position"], position))
            return fail(prefix + QStringLiteral("dimensions/position必须含三个有限数值。"));
        const auto meshObject = item["mesh"].toObject();
        if (!JsonUtil::hasExactKeys(meshObject, {"vertices", "triangles"})
            || !meshObject["vertices"].isArray() || !meshObject["triangles"].isArray())
            return fail(prefix + QStringLiteral("mesh字段无效。"));
        const auto vertices = meshObject["vertices"].toArray();
        const auto triangles = meshObject["triangles"].toArray();
        vertexCount += quint64(vertices.size());
        triangleCount += quint64(triangles.size());
        if (vertexCount > MeshGeometryCodec::maxVertices || triangleCount > MeshGeometryCodec::maxTriangles)
            return fail(prefix + QStringLiteral("CAD合计顶点/三角形超过限制。"));
        auto mesh = std::make_shared<MeshGeometry>();
        mesh->vertices.reserve(vertices.size());
        mesh->triangles.reserve(triangles.size());
        for (const auto &value : vertices) {
            std::array<double, 3> vertex{};
            if (!JsonUtil::readFiniteTriple(value, vertex))
                return fail(prefix + QStringLiteral("顶点必须含三个有限数值。"));
            mesh->vertices.append(vertex);
        }
        for (const auto &value : triangles) {
            std::array<double, 3> numbers{};
            if (!JsonUtil::readFiniteTriple(value, numbers))
                return fail(prefix + QStringLiteral("三角索引必须含三个整数。"));
            std::array<quint32, 3> triangle{};
            for (int axis = 0; axis < 3; ++axis) {
                const double number = numbers[axis];
                if (number < 0 || number > std::numeric_limits<quint32>::max()
                    || std::floor(number) != number)
                    return fail(prefix + QStringLiteral("三角索引不是uint32整数。"));
                triangle[axis] = quint32(number);
            }
            mesh->triangles.append(triangle);
        }
        QString meshError;
        if (!MeshGeometryCodec::validate(*mesh, meshError)
            || !MeshGeometryCodec::validateBounds(*mesh, size, position, meshError))
            return fail(prefix + meshError);
        Part part;
        part.equipmentId = resultEquipment.id;
        part.id = id; part.name = item["name"].toString(); part.material = item["material"].toString();
        part.shape = QStringLiteral("mesh");
        part.positionX = position[0]; part.positionY = position[1]; part.positionZ = position[2];
        part.sizeX = size[0]; part.sizeY = size[1]; part.sizeZ = size[2];
        part.mesh = std::move(mesh); part.cadSourceName = sourceName; part.cadSourceSha256 = sha;
        result.append(std::move(part));
    }
    equipment = std::move(resultEquipment);
    parts = std::move(result);
    return true;
}
