#include "report_docx_builder.h"

#include "infrastructure/docx_writer.h"
#include "domain/report_fmt.h"

#include <QDateTime>
#include <QFileInfo>

namespace {
const QString BLANK = QStringLiteral("____________________");
const int IMG_W = 5400000;   // 15cm ≈ 版心宽（EMU）

QString blankLine(const QString &label, const QString &value = QString())
{
    return label + QStringLiteral("：") +
           (value.isEmpty() ? BLANK : value);
}

QString extraVal(const ReportData &rd, const QString &key)
{
    return rd.extraFields.value(QStringLiteral("report/") + key);
}
} // namespace

QString ReportDocxBuilder::build(const ReportData &rd, const QString &outPath)
{
    DocxWriter dw;
    const QString today =
        QDateTime::fromMSecsSinceEpoch(rd.generatedAtMs).toString(QStringLiteral("yyyy 年 M 月 d 日"));

    // ================= 封面 =================
    dw.addCentered(QString(), 28);
    dw.addCentered(QStringLiteral("火灾视频分析报告"), 44, true);
    dw.addCentered(QString(), 28);
    dw.addCentered(rd.caseNo, 28);
    dw.addCentered(QString(), 28);
    dw.addCentered(blankLine(QStringLiteral("起火单位/场所"), rd.unit), 28);
    dw.addCentered(blankLine(QStringLiteral("火灾名称"), rd.title), 28);
    dw.addCentered(blankLine(QStringLiteral("送检/制作单位"), rd.unit), 28);
    dw.addCentered(blankLine(QStringLiteral("分析人"), rd.investigator), 28);
    dw.addCentered(blankLine(QStringLiteral("审核人"), extraVal(rd, QStringLiteral("reviewer"))), 28);
    dw.addCentered(blankLine(QStringLiteral("批准人"), extraVal(rd, QStringLiteral("approver"))), 28);
    dw.addCentered(QStringLiteral("制作日期：") + today, 28);
    dw.addPageBreak();

    // ================= 目录（静态，无页码——拍板） =================
    dw.addHeading(QStringLiteral("目  录"), 1);
    for (const QString &t : {QStringLiteral("一、基本情况"),
                             QStringLiteral("二、检材视频资料"),
                             QStringLiteral("三、分析依据与方法"),
                             QStringLiteral("四、时间校准"),
                             QStringLiteral("五、分析过程"),
                             QStringLiteral("六、分析意见"),
                             QStringLiteral("七、附件")})
        dw.addParagraph(t);
    dw.addPageBreak();

    // ================= 一、基本情况 =================
    dw.addHeading(QStringLiteral("一、基本情况"), 1);
    dw.addHeading(QStringLiteral("（一）简要案情"), 2);
    dw.addParagraph(rd.description.isEmpty()
        ? QStringLiteral("（火灾发生时间、地点、过火面积、人员伤亡、财产损失等基本情况，待填写）")
        : rd.description);
    dw.addHeading(QStringLiteral("（二）起火单位/场所"), 2);
    dw.addParagraph(blankLine(QStringLiteral("单位名称")));
    dw.addParagraph(blankLine(QStringLiteral("地址"),
        (rd.city + rd.district + rd.locationDetail).trimmed()));
    dw.addParagraph(blankLine(QStringLiteral("建筑结构")));
    dw.addParagraph(blankLine(QStringLiteral("使用性质")));
    dw.addHeading(QStringLiteral("（三）报警时间"), 2);
    dw.addParagraph(reportfmt::fmtWall(rd.incidentTimeMs));
    dw.addHeading(QStringLiteral("（四）视频分析日期"), 2);
    dw.addParagraph(today);
    dw.addHeading(QStringLiteral("（五）送检人/分析人"), 2);
    dw.addParagraph(blankLine(QStringLiteral("送检单位"), rd.unit));
    dw.addParagraph(blankLine(QStringLiteral("送检人")));
    dw.addParagraph(blankLine(QStringLiteral("分析人"), rd.investigator));
    dw.addParagraph(blankLine(QStringLiteral("审核人"), extraVal(rd, QStringLiteral("reviewer"))));

    // ================= 二、检材视频资料 =================
    dw.addPageBreak();
    dw.addHeading(QStringLiteral("二、检材视频资料"), 1);
    dw.addHeading(QStringLiteral("（一）视频来源清单"), 2);
    {
        QVector<QVector<QString>> rows;
        rows << QVector<QString>{QStringLiteral("序号"), QStringLiteral("监控点位"),
                                 QStringLiteral("设备编号"), QStringLiteral("拍摄方向"),
                                 QStringLiteral("提取方式"), QStringLiteral("备注")};
        int i = 1;
        for (const ReportVideoRow &v : rd.videos)
            rows << QVector<QString>{QString::number(i++), v.cameraLabel, v.id,
                                     v.shootDir, v.extractMethod, v.fileName};
        dw.addTable(rows, true, {8, 20, 14, 16, 16, 26});
    }
    dw.addHeading(QStringLiteral("（二）视频物理属性"), 2);
    for (const ReportVideoRow &v : rd.videos) {
        dw.addParagraph(QStringLiteral("检材编号：%1（%2）").arg(v.id, v.cameraLabel), true);
        dw.addParagraph(blankLine(QStringLiteral("文件名"), v.fileName));
        dw.addParagraph(blankLine(QStringLiteral("文件格式"), v.format));
        dw.addParagraph(blankLine(QStringLiteral("文件大小"),
                                  reportfmt::fmtSizeMB(v.sizeBytes)));
        dw.addParagraph(blankLine(QStringLiteral("分辨率"),
            v.width > 0 ? QStringLiteral("%1×%2").arg(v.width).arg(v.height)
                        : QString()));
        dw.addParagraph(blankLine(QStringLiteral("帧率"),
            v.fps > 0 ? QStringLiteral("%1 fps").arg(v.fps, 0, 'f', 2) : QString()));
        dw.addParagraph(blankLine(QStringLiteral("编码方式"), v.codec));
        dw.addParagraph(blankLine(QStringLiteral("视频记录时间范围"),
            v.hasCalib ? reportfmt::fmtWall(v.wallStartMs) + QStringLiteral(" 至 ")
                           + reportfmt::fmtWall(v.wallEndMs)
                       : QStringLiteral("（未校时，暂缺）")));
        dw.addParagraph(blankLine(QStringLiteral("时长"),
                                  reportfmt::fmtDuration(v.durationMs)));
        dw.addParagraph(blankLine(QStringLiteral("MD5 值"), v.md5));
        dw.addParagraph(blankLine(QStringLiteral("SHA-256 值"), v.sha256));
    }
    dw.addHeading(QStringLiteral("（三）监控点位图"), 2);
    if (!rd.sitemapPng.isEmpty()) {
        dw.addImage(rd.sitemapPng, IMG_W);
        dw.addCentered(QStringLiteral("监控点位平面示意图"), 21);
    } else {
        dw.addParagraph(QStringLiteral(
            "（监控点位平面示意图待绘制——案件菜单「编辑监控点位图」，绘制后重新生成报告自动嵌入）"));
    }

    dw.addHeading(QStringLiteral("（四）前处理拼接记录"), 2);
    if (rd.concatRecords.isEmpty()) {
        dw.addParagraph(QStringLiteral("（本案无前处理拼接产物）"));
    } else {
        for (const ReportConcatRecord &rec : rd.concatRecords) {
            dw.addParagraph(QStringLiteral("产物：%1%2（会话 %3）")
                .arg(rec.productFile,
                     rec.productId.isEmpty() ? QString()
                         : QStringLiteral("，案内编号 %1").arg(rec.productId),
                     rec.sessionTs), true);
            for (const QString &h : rec.logHighlights)
                dw.addParagraph(h);
            QVector<QVector<QString>> rows;
            rows << QVector<QString>{QStringLiteral("序号"), QStringLiteral("源文件"),
                                     QStringLiteral("时长"), QStringLiteral("处理动作")};
            for (const auto &r : rec.sourceRows)
                rows << r;
            dw.addTable(rows, true, {10, 52, 18, 20});
            dw.addParagraph(QStringLiteral(
                "拼接证据原件：%1（report.csv / operations.log / concat_list）")
                .arg(rec.evidenceDir));
        }
    }

    // ================= 三、分析依据与方法 =================
    dw.addPageBreak();
    dw.addHeading(QStringLiteral("三、分析依据与方法"), 1);
    dw.addHeading(QStringLiteral("（一）法律法规及技术标准"), 2);
    for (const QString &t : {QStringLiteral("《中华人民共和国消防法》"),
                             QStringLiteral("《火灾事故调查规定》（公安部令第121号）"),
                             QStringLiteral("GA/T 1020-2013《视频中事件过程检验技术规范》"),
                             QStringLiteral("SF/Z JD0300001-2010《声像资料鉴定通用规范》"),
                             QStringLiteral("SF/Z JD0302002-2015《图像资料处理技术规范》")})
        dw.addParagraph(t);
    dw.addHeading(QStringLiteral("（二）分析方法"), 2);
    for (const QString &t : {QStringLiteral("视频反复观看与逐帧分析"),
                             QStringLiteral("时间校准与同步比对"),
                             QStringLiteral("光亮/烟气变化曲线分析"),
                             QStringLiteral("监控画面标记与测向定位"),
                             QStringLiteral("多监控交叉验证")})
        dw.addParagraph(t);
    dw.addHeading(QStringLiteral("（三）分析仪器/软件"), 2);
    dw.addParagraph(QStringLiteral("追光者火灾调查音视频分析系统 %1").arg(rd.appVersion));

    // ================= 四、时间校准 =================
    dw.addPageBreak();
    dw.addHeading(QStringLiteral("四、时间校准"), 1);
    dw.addHeading(QStringLiteral("（一）校准方法"), 2);
    for (const QString &t : {QStringLiteral("与标准北京时间比对"),
                             QStringLiteral("多监控之间特征事件同步比对（接力对时）")})
        dw.addParagraph(t);
    dw.addHeading(QStringLiteral("（二）各校准结果"), 2);
    {
        QVector<QVector<QString>> rows;
        rows << QVector<QString>{QStringLiteral("监控编号"), QStringLiteral("监控显示时间"),
                                 QStringLiteral("校时方式"), QStringLiteral("时间差"),
                                 QStringLiteral("校准公式")};
        for (const ReportVideoRow &v : rd.videos)
            rows << QVector<QString>{v.cameraLabel,
                                     v.hasCalib ? v.osdSampleText : QStringLiteral("—"),
                                     v.calibWayText,
                                     v.hasCalib ? v.timeDiffText : QStringLiteral("未校时"),
                                     v.hasCalib ? v.formulaText : QStringLiteral("—")};
        dw.addTable(rows, true, {14, 22, 18, 18, 28});
    }
    dw.addHeading(QStringLiteral("（三）时间校准截图"), 2);
    {
        int shown = 0;
        for (const ReportVideoRow &v : rd.videos)
            for (const QString &p : v.evidencePhotos) {
                if (shown >= 8)
                    break;   // C1：证据帧上限，防爆版
                dw.addImage(p, IMG_W * 2 / 3);
                dw.addCentered(QStringLiteral("%1 校准证据").arg(v.cameraLabel), 21);
                ++shown;
            }
        if (shown == 0)
            dw.addParagraph(QStringLiteral("（无校准证据帧存档）"));
    }
    if (!rd.chains.isEmpty()) {
        dw.addHeading(QStringLiteral("（四）接力对时取证链"), 2);
        for (const ReportChain &c : rd.chains) {
            dw.addParagraph(QStringLiteral("被校时路：%1").arg(c.laneLabel), true);
            for (const QString &line : c.hopLines)
                dw.addParagraph(line);
            dw.addParagraph(QStringLiteral(
                "声明：经逐跳校准确认，上述各跳对帧均在标注容差范围内；"
                "整条链的累积容差 %1 已如实呈现。").arg(c.totalToleranceText));
        }
    }

    // ================= 五、分析过程 =================
    dw.addPageBreak();
    dw.addHeading(QStringLiteral("五、分析过程"), 1);
    dw.addHeading(QStringLiteral("（一）火势发展情况分析"), 2);
    if (rd.nodes.isEmpty()) {
        dw.addParagraph(QStringLiteral("（未标注关键时间节点——可在图表上打标签后重新生成）"));
    } else {
        dw.addParagraph(QStringLiteral("按时间顺序（北京时间口径）："));
        QVector<QVector<QString>> rows;
        rows << QVector<QString>{QStringLiteral("序号"), QStringLiteral("时间"),
                                 QStringLiteral("监控来源"), QStringLiteral("事件标注")};
        int i = 1;
        for (const ReportNodeRow &n : rd.nodes)
            rows << QVector<QString>{QString::number(i++),
                                     reportfmt::fmtWall(n.wallMs),
                                     n.sourceLabel, n.text};
        dw.addTable(rows, true, {8, 34, 18, 40});
    }
    dw.addHeading(QStringLiteral("（二）关键节点视频分析"), 2);
    if (rd.nodes.isEmpty()) {
        dw.addParagraph(QStringLiteral("（无）"));
    } else {
        int i = 1;
        for (const ReportNodeRow &n : rd.nodes) {
            dw.addHeading(QStringLiteral("节点 %1：%2").arg(i++).arg(n.text), 3);
            dw.addParagraph(blankLine(QStringLiteral("时间（北京时间）"),
                                      reportfmt::fmtWall(n.wallMs)));
            dw.addParagraph(blankLine(QStringLiteral("监控来源"), n.sourceLabel));
            dw.addParagraph(blankLine(QStringLiteral("画面描述")));
            dw.addParagraph(blankLine(QStringLiteral("分析结论")));
            dw.addParagraph(QStringLiteral("附图：") + BLANK);
        }
    }
    dw.addHeading(QStringLiteral("（三）光亮/烟气分析"), 2);
    dw.addParagraph(QStringLiteral(
        "使用视频分析软件进行光亮曲线分析，标注烟气首次出现位置与扩散方向。"));
    {
        int charts = 0;
        for (const ReportVideoRow &v : rd.videos)
            if (!v.chartPng.isEmpty()) {
                dw.addImage(v.chartPng, IMG_W);
                dw.addCentered(QStringLiteral("%1 亮度变化曲线（横轴：北京时间）")
                               .arg(v.cameraLabel), 21);
                ++charts;
            }
        if (charts == 0)
            dw.addParagraph(QStringLiteral("（无亮度分析数据——请先对视频执行亮度分析）"));
    }
    dw.addParagraph(QStringLiteral("（关键帧截图见附件）"));
    dw.addHeading(QStringLiteral("（四）人物/车辆活动情况（如有）"), 2);
    dw.addParagraph(BLANK);
    dw.addHeading(QStringLiteral("（五）起火部位/起火点分析"), 2);
    dw.addParagraph(BLANK);

    // ================= 六、分析意见 =================
    dw.addPageBreak();
    dw.addHeading(QStringLiteral("六、分析意见"), 1);
    dw.addHeading(QStringLiteral("（一）主要结论"), 2);
    dw.addParagraph(blankLine(QStringLiteral("起火时间"),
        rd.nodes.isEmpty() ? QString()
        : QStringLiteral("不晚于 %1").arg(reportfmt::fmtWall(rd.nodes.first().wallMs))));
    dw.addParagraph(blankLine(QStringLiteral("起火部位")));
    dw.addParagraph(blankLine(QStringLiteral("起火点")));
    dw.addParagraph(blankLine(QStringLiteral("火势蔓延方向")));
    dw.addParagraph(blankLine(QStringLiteral("其他关键发现")));
    dw.addHeading(QStringLiteral("（二）依据说明"), 2);
    dw.addParagraph(BLANK);
    dw.addHeading(QStringLiteral("（三）局限性说明"), 2);
    if (rd.limitationNotes.isEmpty())
        dw.addParagraph(QStringLiteral("（无特别说明）"));
    else
        for (const QString &t : rd.limitationNotes)
            dw.addParagraph(t);

    // ================= 七、附件 =================
    dw.addPageBreak();
    dw.addHeading(QStringLiteral("七、附件"), 1);
    dw.addParagraph(QStringLiteral("1. 视频检材清单及物理属性表（见本报告第二节）"));
    dw.addParagraph(QStringLiteral("2. 监控点位平面示意图") +
                    (rd.sitemapPng.isEmpty() ? QStringLiteral("（待绘制）")
                                             : QStringLiteral("（见二（三））")));
    dw.addParagraph(QStringLiteral("3. 时间校准截图（见四（三））"));
    dw.addParagraph(QStringLiteral("4. 关键帧截图："));
    if (rd.snapshotPaths.isEmpty()) {
        dw.addParagraph(QStringLiteral("（案内无快照存档）"));
    } else {
        int shown = 0;
        for (const QString &p : rd.snapshotPaths) {
            if (shown >= 12)
                break;
            dw.addImage(p, IMG_W * 2 / 3);
            dw.addCentered(QFileInfo(p).fileName(), 21);
            ++shown;
        }
    }
    dw.addParagraph(QStringLiteral("5. 前处理拼接记录原件（report.csv/operations.log，见案内 preprocess/ 目录）") +
                    (rd.concatRecords.isEmpty() ? QStringLiteral("（本案无）")
                                                : QStringLiteral("")));
    dw.addParagraph(QStringLiteral("6. 光亮/烟气分析曲线图（待补）"));
    dw.addParagraph(QStringLiteral("7. 视频片段："));
    if (rd.exportClips.isEmpty())
        dw.addParagraph(QStringLiteral("（无导出片段）"));
    else
        for (const QString &n : rd.exportClips)
            dw.addParagraph(n);

    // ================= 落款 =================
    dw.addParagraph(QString());
    dw.addParagraph(blankLine(QStringLiteral("分析人（签名）")));
    dw.addParagraph(blankLine(QStringLiteral("审核人（签名）")));
    dw.addParagraph(blankLine(QStringLiteral("批准人（签名）")));
    dw.addParagraph(blankLine(QStringLiteral("制作单位（盖章）")));
    dw.addParagraph(QStringLiteral("制作日期：") + today);

    if (!dw.save(outPath))
        return QStringLiteral("无法写出文件：%1").arg(outPath);
    return QString();
}
