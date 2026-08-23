/**
 * @file report_test_main.cpp
 * @brief P-28 批次②：ReportDocxBuilder 合成数据 → DOCX 章节断言
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#include "app/report_docx_builder.h"
#include "domain/report_fmt.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond, msg) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

static QByteArray zipReadEntry(const QByteArray &blob, const QByteArray &name)
{
    int pos = 0;
    while (true) {
        pos = blob.indexOf("PK\x03\x04", pos);
        if (pos < 0)
            return {};
        const auto u16 = [&](int off) {
            return quint16(quint8(blob[pos + off]))
                   | (quint16(quint8(blob[pos + off + 1])) << 8);
        };
        const auto u32 = [&](int off) {
            return quint32(quint8(blob[pos + off]))
                   | (quint32(quint8(blob[pos + off + 1])) << 8)
                   | (quint32(quint8(blob[pos + off + 2])) << 16)
                   | (quint32(quint8(blob[pos + off + 3])) << 24);
        };
        const quint16 nlen = u16(26);
        const quint16 xlen = u16(28);
        const quint32 dlen = u32(18);
        if (blob.mid(pos + 30, nlen) == name)
            return blob.mid(pos + 30 + nlen + xlen, dlen);
        pos += 4;
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // reportfmt 人读口径
    CHECK(reportfmt::fmtTimeDiff(134000) == QStringLiteral("慢 2 分 14.0 秒"),
          "fmtTimeDiff slow");
    CHECK(reportfmt::fmtTimeDiff(-5000) == QStringLiteral("快 5.0 秒"), "fmtTimeDiff fast");
    CHECK(reportfmt::fmtTimeDiff(0) == QStringLiteral("一致"), "fmtTimeDiff zero");
    CHECK(reportfmt::fmtDuration(3661000) == QStringLiteral("1 时 1 分 1 秒"), "fmtDuration");
    CHECK(reportfmt::calibWayText(TimeCalibration::Source::CrossCamEvent)
              == QStringLiteral("多机同事件间接校时"), "calibWayText crosscam");

    // 合成 ReportData
    ReportData rd;
    rd.caseNo = QStringLiteral("20270813-广州天河-a");
    rd.title = QStringLiteral("天河火灾");
    rd.investigator = QStringLiteral("张三");
    rd.unit = QStringLiteral("天河消防救援大队");
    rd.incidentTimeMs = 1755000000000LL;
    rd.city = QStringLiteral("广州");
    rd.district = QStringLiteral("天河");
    rd.generatedAtMs = 1755900000000LL;
    rd.appVersion = QStringLiteral("V1.14.0");

    ReportVideoRow v1;
    v1.id = QStringLiteral("V001");
    v1.cameraLabel = QStringLiteral("明景");
    v1.fileName = QStringLiteral("明景拼接.mp4");
    v1.format = QStringLiteral("mov,mp4,m4a");
    v1.codec = QStringLiteral("h264");
    v1.sizeBytes = 123456789;
    v1.width = 2560; v1.height = 1440; v1.fps = 20.0;
    v1.durationMs = 3600000;
    v1.hasCalib = true;
    v1.calibWayText = QStringLiteral("OCR 自动识别");
    v1.osdSampleText = QStringLiteral("2026-07-22 17:25:28");
    v1.timeDiffText = QStringLiteral("慢 2 分 14.0 秒");
    v1.formulaText = QStringLiteral("标准时间 = 录像时间 × 1.0 + 偏移 134.0 秒");
    v1.md5 = QStringLiteral("d41d8cd98f00b204e9800998ecf8427e");
    v1.sha256 = QStringLiteral("e3b0c44298fc1c149afbf4c8996fb924");
    rd.videos << v1;

    ReportVideoRow v2;
    v2.id = QStringLiteral("V004");
    v2.cameraLabel = QStringLiteral("D17");
    v2.fileName = QStringLiteral("D17.mp4");
    v2.calibWayText = QStringLiteral("多机同事件间接校时");
    rd.videos << v2;

    ReportNodeRow n1{1755000123000LL, QStringLiteral("明景"), QStringLiteral("首次出现烟气")};
    ReportNodeRow n2{1755000456000LL, QStringLiteral("D17"), QStringLiteral("首次出现明火")};
    rd.nodes << n1 << n2;

    ReportChain c;
    c.laneLabel = QStringLiteral("D17");
    c.hopLines << QStringLiteral("D17 ← 参考「V001」（事件：爆燃声响，本跳容差 ±25 ms）")
               << QStringLiteral("明景：绝对校时锚（本路独立校时）");
    c.totalToleranceText = QStringLiteral("±25 ms");
    rd.chains << c;
    rd.limitationNotes << QStringLiteral("明景 路存在 2 处 OSD 疑似错读点（时间不可信），校时已自动剔除");

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "tmp dir");
    const QString out = tmp.filePath(QStringLiteral("report.docx"));
    const QString err = ReportDocxBuilder::build(rd, out);
    CHECK(err.isEmpty(), "build ok");
    QFile f(out);
    CHECK(f.open(QIODevice::ReadOnly), "open docx");
    const QByteArray doc = zipReadEntry(f.readAll(), "word/document.xml");
    CHECK(!doc.isEmpty(), "document.xml");

    // 封面
    CHECK(doc.contains("火灾视频分析报告"), "cover title");
    CHECK(doc.contains("20270813-广州天河-a"), "cover case no");
    CHECK(doc.contains("张三"), "investigator");
    // 目录静态无页码
    CHECK(doc.contains("目  录"), "toc");
    // 章节一~七
    for (const char *ch : {"一、基本情况", "二、检材视频资料", "三、分析依据与方法",
                           "四、时间校准", "五、分析过程", "六、分析意见", "七、附件"})
        CHECK(doc.contains(ch), ch);
    // 检材表 + 哈希双列
    CHECK(doc.contains("明景拼接.mp4"), "file name in table");
    CHECK(doc.contains("d41d8cd98f00b204e9800998ecf8427e"), "md5 present");
    CHECK(doc.contains("e3b0c44298fc1c149afbf4c8996fb924"), "sha256 present");
    CHECK(doc.contains("2560×1440"), "resolution");
    // 校准表
    CHECK(doc.contains("2026-07-22 17:25:28"), "osd sample");
    CHECK(doc.contains("慢 2 分 14.0 秒"), "time diff");
    // 取证链
    CHECK(doc.contains("接力对时取证链"), "chain section");
    CHECK(doc.contains("爆燃声响"), "chain event name");
    CHECK(doc.contains("±25 ms"), "chain tolerance");
    // 节点
    CHECK(doc.contains("首次出现烟气"), "node label");
    CHECK(doc.contains("节点 1"), "node heading");
    // 起火时间预填（最早节点）
    CHECK(doc.contains("不晚于"), "prefilled fire time");
    // 局限性
    CHECK(doc.contains("OSD 疑似错读点"), "limitation note");
    // 落款留白
    CHECK(doc.contains("分析人（签名）"), "signature block");
    CHECK(doc.contains("____________________"), "blank fills");
    // 软件版本
    CHECK(doc.contains("追光者火灾调查音视频分析系统 V1.14.0"), "app version");
    // 点位图占位
    CHECK(doc.contains("监控点位平面示意图待绘制"), "sitemap placeholder");

    fprintf(stderr, "report_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
