#include "domain/concat_naming.h"

#include <QFileInfo>

namespace {

/// 通道是否是"默认组"（无通道信息）——此时不带通道前缀
/// 2026-08-20 实测修复：运行期实际默认组名为半角 "(默认组)"（smart_sorter/
/// coordinator 两侧均是），此前仅匹配全角 "（默认组）" 导致拼接产物带上
/// 字面 "(默认组)" 前缀（LAMerged_(默认组)_87_21.mp4，越秀案实测实锤）
bool isDefaultGroup(const QString &channel)
{
    return channel.isEmpty() || channel == QStringLiteral("(默认组)")
        || channel == QStringLiteral("（默认组）")
        || channel == QStringLiteral("_默认组_");
}

/// 文件名为通道/序号后缀（监控惯例 "HHMMSS_通道"）时，通道段是共同的后缀
/// 部分（如 _100）；也可能整名相同（异常）。仅截断安全部分。
/// 监控惯例名 HHMMSS_<通道>："时间戳以外的重复段" = 最后一个 '_' 之后的
/// 部分（如 _100）。仅当两名字该段完全一致（含下划线）才去重——字符级
/// 尾对齐会误吃时间戳尾数字（如 000446/234556 尾部都含 6）。
QString trimCommonSuffix(const QString &firstName, const QString &lastName)
{
    const int fi = firstName.lastIndexOf(QLatin1Char('_'));
    const int li = lastName.lastIndexOf(QLatin1Char('_'));
    if (fi >= 0 && li >= 0) {
        const QString fs = firstName.mid(fi);   // "_100"
        const QString ls = lastName.mid(li);
        if (fs == ls && fs.size() >= 2)
            return lastName.left(li);
    }
    return lastName;
}

} // namespace

QString concatOutputName(const QString &channel,
                         const QString &firstFilePath,
                         const QString &lastFilePath)
{
    const QString firstName =
        QFileInfo(firstFilePath).completeBaseName().trimmed();
    const QString lastNameRaw =
        QFileInfo(lastFilePath).completeBaseName().trimmed();
    const QString lastName = trimCommonSuffix(firstName, lastNameRaw);

    QString name = QStringLiteral("LAMerged_");
    if (!isDefaultGroup(channel))
        name += channel + QLatin1Char('_');
    name += firstName + QLatin1Char('_') + lastName;
    return name;
}