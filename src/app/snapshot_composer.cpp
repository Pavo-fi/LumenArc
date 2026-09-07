/**
 * @file snapshot_composer.cpp
 * @brief v1.17.0 P-80：onSnapshotQuick 渲染主体逐字迁入（行为冻结）
 *
 * 依赖纪律：仅 Qt Gui + domain 模型 + FrameAnnotation（纯模块）+ i18n/theme；
 * 不接触任何 QWidget（R1 方向）。
 */
#include "app/snapshot_composer.h"

#include "frame_annotation.h"
#include "domain/roi_model.h"
#include "domain/guide_line_model.h"
#include "i18n.h"
#include "theme.h"

#include <QFont>
#include <QPainter>
#include <climits>

QPair<QString, QString> SnapshotComposer::timeCode(const TimeCalibration &cal,
                                                    qint64 posMs)
{
    // ---- 时间码：校时后用北京时间；否则相对时间（原 onSnapshotQuick 逐字）----
    QString timeText, fileTimeTag;
    if (cal.dateKnown) {
        const QDateTime dt =
            QDateTime::fromMSecsSinceEpoch(cal.beijingMsOf(posMs));
        timeText = dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        fileTimeTag = dt.toString(QStringLiteral("yyyyMMdd_HHmmss"));
    } else {
        qint64 ms = posMs + cal.offsetMs;
        if (ms < 0) ms = 0;
        const int s = int(ms / 1000);
        timeText = QStringLiteral("%1:%2:%3")
            .arg(s / 3600, 2, 10, QChar('0')).arg((s % 3600) / 60, 2, 10, QChar('0'))
            .arg(s % 60, 2, 10, QChar('0'));
        fileTimeTag = QStringLiteral("t%1-%2-%3")
            .arg(s / 3600, 2, 10, QChar('0')).arg((s % 3600) / 60, 2, 10, QChar('0'))
            .arg(s % 60, 2, 10, QChar('0'));
    }
    return {timeText, fileTimeTag};
}

QImage SnapshotComposer::compose(const SnapshotInputs &in)
{
    const QImage frame = in.frame;
    if (frame.isNull())
        return QImage();
    const int rotation = in.rotation;

    const QPair<QString, QString> tc = timeCode(in.calibration, in.posMs);
    const QString &timeText = tc.first;   // fileTimeTag 由调用方经 timeCode() 取用（拼文件名）

    // ---- 视频部分：覆盖层烧录（§14 Q3）→ 放大镜标识框（Q4）→ OSD（Q5）----
    QImage videoPart = frame.convertToFormat(QImage::Format_ARGB32);
    const QSize videoSize = in.videoSize;
    // 线宽/字号随分辨率缩放（屏上 1~2px 观感 → 原生全分辨率等比）
    const int annoPen = qBound(1, qRound(videoPart.width() / 1280.0), 4);
    const int magPen  = qBound(2, qRound(videoPart.width() / 640.0), 8);
    const int magFont = qBound(10, qRound(videoPart.width() * 12.0 / 1280.0), 28);
    {
        QPainter p(&videoPart);
        p.setRenderHint(QPainter::Antialiasing);
        // Q3：ROI 矩形/多边形/辅助线按模型颜色全分辨率烧录
        FrameAnnotation::burnAnnotations(p, videoPart.size(), videoSize, rotation,
                                         in.regions, in.regions,
                                         in.guideLines, annoPen);
        // Q4：放大镜来源标识框（与屏上同款金色四角括号 + 倍率徽章）
        if (in.hasMagnifier && in.magnifierSourceRect.isValid()) {
            const QRect magRect = FrameAnnotation::mapStoredRectToFrame(
                in.magnifierSourceRect, videoPart.size(), videoSize, rotation);
            if (magRect.isValid() && !magRect.isEmpty())
                FrameAnnotation::drawMagnifierIndicator(
                    p, magRect, in.magnifierZoom, magPen, magFont);
        }
        p.end();
    }
    {
        // Q5：OSD = 文件名（白色小字）+ 标签（金色）+ 时间码，黑底阴影保证可读
        QPainter p(&videoPart);
        p.setRenderHint(QPainter::Antialiasing);
        const int fs = qBound(14, videoPart.height() / 40, 40);
        const int pad = fs;
        int y = videoPart.height() - pad;
        const QString line2 = (in.calibration.dateKnown
            ? lang("北京时间 ", "Beijing ") : lang("相对时刻 ", "Stream ")) + timeText;
        QStringList lines;
        lines << line2;
        const QString &labelText = in.labelText;
        if (!labelText.isEmpty())
            lines << labelText;
        const QString &fileName = in.fileName;
        if (!fileName.isEmpty())
            lines << fileName;   // 最上方一行
        for (int i = 0; i < lines.size(); ++i) {
            const QString &txt = lines[i];
            const bool isLabel = (i == 1 && !labelText.isEmpty());
            const bool isFile = (i == lines.size() - 1 && !fileName.isEmpty());
            const int thisFs = isFile ? qMax(10, fs * 3 / 4) : fs;
            p.setFont(fontSans(thisFs, QFont::Bold));
            const QColor fg = isLabel ? QColor(Theme::Accent) : Qt::white;
            p.setPen(QColor(0, 0, 0, 200));
            p.drawText(QRect(pad + 2, y - thisFs * 13 / 10 + 2,
                             videoPart.width() - pad * 2, thisFs * 13 / 10),
                       Qt::AlignLeft | Qt::AlignVCenter, txt);
            p.setPen(fg);
            p.drawText(QRect(pad, y - thisFs * 13 / 10,
                             videoPart.width() - pad * 2, thisFs * 13 / 10),
                       Qt::AlignLeft | Qt::AlignVCenter, txt);
            y -= thisFs * 3 / 2;
        }
        p.end();
    }

    // ---- v1.16.0 拍板 A1：放大镜开着 → 视频区改左右分屏（与主页面观感一致）。
    // 左半 = 原生全分辨率原帧（标注+金框+OSD 已烧录）；右半 = 放大视图同高，
    // ROI/辅助线同步烧录进放大坐标系；画幅 = 左W + 间隔 + 右W2（细节零损失）。
    bool magSplitDone = false;
    if (in.hasMagnifier && in.magnifierSourceRect.isValid()) {
        const QImage magRaw = in.magnifiedImage;   // 含旋转+调节
        if (!magRaw.isNull()) {
            QImage right = magRaw.scaledToHeight(videoPart.height(),
                                                 Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_ARGB32);
            const QRect magRect = FrameAnnotation::mapStoredRectToFrame(
                in.magnifierSourceRect, videoPart.size(),
                videoSize, rotation);
            {
                QPainter p(&right);
                p.setRenderHint(QPainter::Antialiasing);
                if (magRect.isValid() && !magRect.isEmpty()) {
                    // 标注映射：存储坐标 → 旋转后整帧 → 裁剪放大图
                    const double sx = double(right.width()) / magRect.width();
                    const double sy = double(right.height()) / magRect.height();
                    p.scale(sx, sy);
                    p.translate(-magRect.topLeft());
                    FrameAnnotation::burnAnnotations(
                        p, videoPart.size(), videoSize, rotation,
                        in.regions, in.regions, in.guideLines, annoPen);
                }
                p.end();
            }
            // v1.16.0 用户拍板：右半不烧录「倍率·时刻」注记——左半 OSD 已有
            const int gap = qBound(4, videoPart.width() / 320, 12);
            QImage combined(videoPart.width() + gap + right.width(),
                            videoPart.height(), QImage::Format_ARGB32);
            combined.fill(Qt::black);
            QPainter cp(&combined);
            cp.drawImage(0, 0, videoPart);
            cp.drawImage(videoPart.width() + gap, 0, right);
            cp.end();
            videoPart = combined;
            magSplitDone = true;
        }
    }

    // ---- 分析数据区（§14 v2：调用方离屏预渲染传入，本模块只负责堆叠）----
    QImage chartImg = in.chartImg;
    QImage specImg = in.specImg;
    QImage magImg;
    QString magCaption, magSub;
    if (in.hasMagnifier && !magSplitDone) {   // 分屏时已融入右半，不再单列小节
        const QImage raw = in.magnifiedImage;
        if (!raw.isNull()) {
            magImg = raw.scaledToHeight(320, Qt::SmoothTransformation);
            const QRect src = in.magnifierSourceRect;
            magCaption = lang("源区域 (%1, %2) · %3 × %4", "Source (%1, %2) · %3 × %4")
                .arg(src.x()).arg(src.y()).arg(src.width()).arg(src.height());
            magSub = lang("倍率 %1× · 时刻 %2", "Zoom %1× · At %2")
                .arg(in.magnifierZoom, 0, 'f', 1).arg(timeText);
        }
    }

    // ---- 竖向全宽堆叠 + 分段标题条（视频 / 曲线 / 语谱图 / 放大镜）----
    struct Section {
        QString title;
        QImage img;
        QString caption;   // 非空 = 放大镜段（图左文右）
        QString sub;
    };
    QVector<Section> sections;
    if (!chartImg.isNull())
        sections << Section{lang("亮度 / 音量曲线", "Luminance / Volume"),
                            chartImg, {}, {}};
    if (!specImg.isNull())
        sections << Section{lang("语谱图", "Spectrogram"), specImg, {}, {}};
    if (!magImg.isNull())
        sections << Section{lang("放大镜视图", "Magnifier"), magImg,
                            magCaption, magSub};

    const int titleH = 34, divH = 2;
    int outH = videoPart.height();
    for (const auto &s : sections)
        outH += divH + titleH + s.img.height();
    QImage out(videoPart.width(), outH, QImage::Format_ARGB32);
    out.fill(Qt::black);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        const int W = out.width();
        int yy = 0;
        p.drawImage(0, yy, videoPart);
        yy += videoPart.height();
        for (const auto &s : sections) {
            // 分隔线
            p.fillRect(0, yy, W, divH, QColor(0x2A, 0x2A, 0x2A));
            yy += divH;
            // 标题条：Accent 竖条 + 次级色标题
            p.fillRect(14, yy + (titleH - 16) / 2, 4, 16, QColor(Theme::Accent));
            p.setPen(QColor(Theme::TextSecond));
            p.setFont(fontSans(15, QFont::Bold));
            p.drawText(QRect(30, yy, W - 30, titleH),
                       Qt::AlignLeft | Qt::AlignVCenter, s.title);
            yy += titleH;
            if (s.caption.isEmpty()) {
                p.drawImage(0, yy, s.img);
            } else {
                // 放大镜段：裁剪图 + 金色描边 + 右侧信息块（垂直居中两行）
                p.drawImage(0, yy, s.img);
                p.setPen(QPen(QColor(Theme::Accent), 2));
                p.drawRect(1, yy + 1, s.img.width() - 2, s.img.height() - 2);
                const int tx = s.img.width() + 32;
                if (tx + 120 < W) {
                    const int lineH = 34;
                    const int blockH = lineH * 2;
                    int ty = yy + (s.img.height() - blockH) / 2;
                    p.setPen(QColor(Theme::TextPrimary));
                    p.setFont(fontSans(20, QFont::Bold));
                    p.drawText(QRect(tx, ty, W - tx - 24, lineH),
                               Qt::AlignLeft | Qt::AlignVCenter, s.caption);
                    ty += lineH;
                    p.setPen(QColor(Theme::Accent));
                    p.setFont(fontSans(18, QFont::Normal));
                    p.drawText(QRect(tx, ty, W - tx - 24, lineH),
                               Qt::AlignLeft | Qt::AlignVCenter, s.sub);
                }
            }
            yy += s.img.height();
        }
        p.end();
    }
    // 取证留痕：旋转档位写入 PNG 文本元数据（0 不记，保持旧产物字节级一致）
    if (rotation != 0)
        out.setText(QStringLiteral("LumenArc:displayRotation"),
                    QString::number(rotation));
    return out;
}