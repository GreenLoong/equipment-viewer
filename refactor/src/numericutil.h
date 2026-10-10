#pragma once

#include <QString>

#include <cmath>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>

/**
 * 数值判定与格式化的共用实现。
 * 属性面板、三维场景和存储校验原先各写一份，重复的判定/格式化集中在
 * 此头文件，保证同一数值在不同界面显示与接受规则一致。
 */
namespace NumericUtil {

/** 可选数值存在、有限且大于零；用于尺寸、半径与长度等正数校验。 */
inline bool finitePositive(const std::optional<double> &value)
{
    return value && std::isfinite(*value) && *value > 0;
}

/** double 按15位有效数字输出；与只读属性面板的原始数值格式一致。 */
inline QString numberText(double value)
{
    return QString::number(value, 'g', 15);
}

/**
 * long double 按指定有效数字输出。
 * 固定使用经典区域设置，避免中文系统的小数点/千分位改变界面读数；
 * 保留 long double 范围，不能先窄化 double 再格式化极端值。
 */
inline QString extendedNumberText(long double value, int significantDigits)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(significantDigits) << value;
    return QString::fromStdString(stream.str());
}

/** 毫米读数文本：5位有效数字加单位，供标尺、包络尺寸与显示偏移共用。 */
inline QString millimeterText(long double millimeters)
{
    return extendedNumberText(millimeters, 5) + QStringLiteral(" mm");
}

} // namespace NumericUtil
