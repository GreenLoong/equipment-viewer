#include "meshgeometry.h"

#include <QDataStream>
#include <QIODevice>
#include <cmath>
#include <algorithm>

namespace {
void configure(QDataStream &stream)
{
    stream.setVersion(QDataStream::Qt_6_5);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
}
bool fail(QString &error, const QString &message)
{
    error = message;
    return false;
}
}

bool MeshGeometryCodec::validate(const MeshGeometry &mesh, QString &error)
{
    error.clear();
    if (mesh.vertices.isEmpty() || mesh.triangles.isEmpty()
        || mesh.vertices.size() > maxVertices || mesh.triangles.size() > maxTriangles)
        return fail(error, QStringLiteral("网格为空或超过顶点/三角形上限。"));
    for (const auto &vertex : mesh.vertices)
        for (double coordinate : vertex)
            if (!std::isfinite(coordinate))
                return fail(error, QStringLiteral("网格顶点含非有限坐标。"));
    for (const auto &triangle : mesh.triangles) {
        for (quint32 index : triangle)
            if (index >= quint64(mesh.vertices.size()))
                return fail(error, QStringLiteral("网格三角索引越界。"));
        if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[2] == triangle[0])
            return fail(error, QStringLiteral("网格含重复索引的退化三角形。"));
        const auto &a = mesh.vertices[triangle[0]];
        const auto &b = mesh.vertices[triangle[1]];
        const auto &c = mesh.vertices[triangle[2]];
        // 先缩放坐标再叉乘，避免合法大坐标在中间运算溢出。
        double scale = 0;
        for (int i = 0; i < 3; ++i)
            scale = std::max({scale, std::abs(a[i]), std::abs(b[i]), std::abs(c[i])});
        if (scale == 0)
            return fail(error, QStringLiteral("网格含零面积三角形。"));
        std::array<double, 3> u{}, v{};
        for (int i = 0; i < 3; ++i) {
            u[i] = b[i] / scale - a[i] / scale;
            v[i] = c[i] / scale - a[i] / scale;
        }
        const std::array<double, 3> cross{u[1]*v[2]-u[2]*v[1],
                                         u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]};
        if (cross[0] == 0 && cross[1] == 0 && cross[2] == 0)
            return fail(error, QStringLiteral("网格含共线退化三角形。"));
    }
    return true;
}

bool MeshGeometryCodec::validateBounds(const MeshGeometry &mesh,
    const std::array<double, 3> &size, const std::array<double, 3> &position, QString &error)
{
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(size[axis]) || size[axis] <= 0 || !std::isfinite(position[axis]))
            return fail(error, QStringLiteral("网格外包络尺寸或位置无效。"));
        const double half = size[axis] / 2;
        const double tolerance = std::max(1e-7, size[axis] * 1e-7);
        if (!std::isfinite(position[axis] - half) || !std::isfinite(position[axis] + half))
            return fail(error, QStringLiteral("网格世界外包络溢出。"));
        for (const auto &vertex : mesh.vertices)
            if (!std::isfinite(vertex[axis]) || std::abs(vertex[axis]) - half > tolerance
                || !std::isfinite(position[axis] + vertex[axis]))
                return fail(error, QStringLiteral("网格顶点超出局部外包络或世界坐标溢出。"));
    }
    return true;
}

bool MeshGeometryCodec::encode(const MeshGeometry &mesh, QByteArray &bytes, QString &error)
{
    if (!validate(mesh, error))
        return false;
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    configure(stream);
    stream.writeRawData("EVM1", 4);
    stream << quint32(mesh.vertices.size()) << quint32(mesh.triangles.size());
    for (const auto &vertex : mesh.vertices)
        for (double coordinate : vertex) stream << coordinate;
    for (const auto &triangle : mesh.triangles)
        for (quint32 index : triangle) stream << index;
    if (stream.status() != QDataStream::Ok)
        return fail(error, QStringLiteral("网格编码失败。"));
    bytes = result;
    return true;
}

bool MeshGeometryCodec::decode(const QByteArray &bytes, MeshGeometry &mesh, QString &error)
{
    error.clear();
    if (bytes.size() < 12 || quint64(bytes.size()) > maxBytes || bytes.first(4) != "EVM1")
        return fail(error, QStringLiteral("网格BLOB头或长度无效。"));
    QDataStream stream(bytes);
    configure(stream);
    stream.skipRawData(4);
    quint32 vertices = 0, triangles = 0;
    stream >> vertices >> triangles;
    const quint64 expected = 12ULL + 24ULL * vertices + 12ULL * triangles;
    if (!vertices || !triangles || vertices > maxVertices || triangles > maxTriangles
        || expected > maxBytes || expected != quint64(bytes.size()))
        return fail(error, QStringLiteral("网格BLOB计数或期望长度无效。"));
    MeshGeometry result;
    result.vertices.resize(vertices);
    result.triangles.resize(triangles);
    for (auto &vertex : result.vertices)
        for (double &coordinate : vertex) stream >> coordinate;
    for (auto &triangle : result.triangles)
        for (quint32 &index : triangle) stream >> index;
    if (stream.status() != QDataStream::Ok || !validate(result, error))
        return false;
    mesh = std::move(result);
    return true;
}
