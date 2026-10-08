#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <array>

/** CAD局部毫米坐标与三角拓扑；通过Part的const共享所有权传递。 */
struct MeshGeometry {
    QList<std::array<double, 3>> vertices;
    QList<std::array<quint32, 3>> triangles;
};

/** 校验及EVM1二进制编解码；失败保留调用方输出，不接受压缩载荷。 */
class MeshGeometryCodec {
public:
    static constexpr quint64 maxBytes = 64ULL * 1024 * 1024;
    static constexpr quint32 maxVertices = 600000;
    static constexpr quint32 maxTriangles = 400000;
    /** 检查资源上限、有限坐标、索引和退化三角形。 */
    static bool validate(const MeshGeometry &mesh, QString &error);
    /** 核世界有限、全部顶点containment及实际三角引用点外包络；允许worker离散公差。 */
    static bool validateBounds(const MeshGeometry &mesh, const std::array<double, 3> &size,
                               const std::array<double, 3> &position, QString &error);
    /** 写入固定Qt6_5、LittleEndian、DoublePrecision的EVM1格式。 */
    static bool encode(const MeshGeometry &mesh, QByteArray &bytes, QString &error);
    /** 分配前核对计数和64位期望长度，完整校验成功后替换mesh。 */
    static bool decode(const QByteArray &bytes, MeshGeometry &mesh, QString &error);
};
