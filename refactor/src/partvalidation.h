#pragma once

#include "equipmentrepository.h"
#include "numericutil.h"

#include <QString>

#include <array>
#include <cmath>

/**
 * 零件形状、尺寸字段与网格包络的共用校验规则。
 * 设备仓库保存前与三维场景装载时都必须使用同一判定，避免两处各写一份
 * 后在边界值上出现“能存不能画”或“能画不能存”的分歧。
 */
namespace PartValidation {

/**
 * 该形状要求的正尺寸是否齐全：box/mesh 需XYZ三项，cylinder 需半径与长度。
 * 只判断尺寸与网格是否存在，不判断是否夹带其他形状字段。
 */
inline bool hasPositiveShapeDimensions(const Part &part)
{
    const bool solidDimensions = NumericUtil::finitePositive(part.sizeX)
        && NumericUtil::finitePositive(part.sizeY) && NumericUtil::finitePositive(part.sizeZ);
    if (part.shape == QStringLiteral("box"))
        return solidDimensions;
    if (part.shape == QStringLiteral("mesh"))
        return solidDimensions && bool(part.mesh);
    if (part.shape == QStringLiteral("cylinder"))
        return NumericUtil::finitePositive(part.radius) && NumericUtil::finitePositive(part.length);
    return false;
}

/** 中心位置三个分量均为有限值；非有限坐标不能进入渲染或存储。 */
inline bool hasFinitePosition(const Part &part)
{
    return std::isfinite(part.positionX) && std::isfinite(part.positionY)
        && std::isfinite(part.positionZ);
}

/**
 * 形状字段是否互斥：box 不带半径/长度/网格，cylinder 不带XYZ尺寸/网格，
 * mesh 不带半径/长度。基本几何零件夹带CAD字段会隐藏资料来源，必须拒绝。
 */
inline bool usesOnlyShapeFields(const Part &part)
{
    if (part.shape == QStringLiteral("box"))
        return !part.radius && !part.length && !part.mesh;
    if (part.shape == QStringLiteral("cylinder"))
        return !part.sizeX && !part.sizeY && !part.sizeZ && !part.mesh;
    if (part.shape == QStringLiteral("mesh"))
        return !part.radius && !part.length;
    return false;
}

/**
 * 网格拓扑与外包络一致性：先校验索引/退化三角形，再核对离散曲面与源包络。
 * 失败写入可定位错误并返回false；三个阶段使用的网格规则必须一致。
 */
inline bool validateMeshPart(const MeshGeometry &mesh, const std::array<double, 3> &size,
                             const std::array<double, 3> &position, QString &error)
{
    return MeshGeometryCodec::validate(mesh, error)
        && MeshGeometryCodec::validateBounds(mesh, size, position, error);
}

} // namespace PartValidation
