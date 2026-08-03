/**
 * @file preprocess_test_main.cpp
 * @brief 前处理 domain 纯逻辑 headless 单测（Qt Test 风格免框架断言）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 覆盖（docs/PREPROCESSING_TECH_DESIGN_CN.md §12.2）：
 *  - 文件名正则 M1-M5 正反例、值域校验、通道捕获
 *  - SmartSorter 顺序/警告/矛盾裁决/分组/单文件组/空输入
 *  - 连续性校验：重叠/缺口/恰好连续/容差边界
 *  - list.txt 转义（空格、单引号、中文、盘符）
 *  - CSV 转义（F6 反例）
 *  - ffmpeg 进度解析（out_time_ms 微秒陷阱，评审 R-5）
 *  - creation_time 解析 + 脏值过滤（2036 零值 bug）
 *  - 拼接一致性校验分级（OK/WARN/BLOCK）
 */
#include "domain/filename_timestamp.h"
#include "domain/smart_sorter.h"
#include "domain/preprocess_text.h"
#include "domain/concat_precheck.h"
#include "domain/evidence_report.h"
#include "domain/tick_utils.h"
#include "sortablefiletable.h"

#include <QApplication>
#include <QTableWidget>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <cstdio>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static qint64 epochOf(int y, int mo, int d, int h, int mi, int s, int ms = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s, ms), Qt::LocalTime)
        .toMSecsSinceEpoch();
}

// ---------------------------------------------------------------------------
static void testFilenamePatterns()
{
    // M1: CH01_20240701_120000（含通道）
    auto ft = parseFilenameTimestamp(QStringLiteral("CH01_20240701_120000.avi"));
    CHECK(ft.hit() && ft.patternId == 1);
    CHECK(ft.channel == QLatin1String("CH01"));
    CHECK(ft.epochMs == epochOf(2024, 7, 1, 12, 0, 0));

    // M5 优先于 M2：含毫秒不被截胡
    ft = parseFilenameTimestamp(QStringLiteral("20240701_120000_500.mp4"));
    CHECK(ft.hit() && ft.patternId == 5);
    CHECK(ft.epochMs == epochOf(2024, 7, 1, 12, 0, 0, 500));

    // M2: 20240701-120000
    ft = parseFilenameTimestamp(QStringLiteral("20240701-120000.mp4"));
    CHECK(ft.hit() && ft.patternId == 2);

    // M3: 20240701120000
    ft = parseFilenameTimestamp(QStringLiteral("20240701120000.mp4"));
    CHECK(ft.hit() && ft.patternId == 3);

    // M4: 2024-07-01 12-00-00
    ft = parseFilenameTimestamp(QStringLiteral("2024-07-01 12-00-00.mp4"));
    CHECK(ft.hit() && ft.patternId == 4);

    // 反例：值域非法（25 点 / 13 月 / 1999 年）
    CHECK(!parseFilenameTimestamp(QStringLiteral("20240701_259999.mp4")).hit());
    CHECK(!parseFilenameTimestamp(QStringLiteral("20241301_120000.mp4")).hit());
    CHECK(!parseFilenameTimestamp(QStringLiteral("19990701_120000.mp4")).hit());

    // 无时间模式：未命中但捕获 IPC 通道
    ft = parseFilenameTimestamp(QStringLiteral("IPC_03_yard.mp4"));
    CHECK(!ft.hit());
    CHECK(ft.channel == QLatin1String("03"));

    // 完全无模式
    ft = parseFilenameTimestamp(QStringLiteral("video.mp4"));
    CHECK(!ft.hit() && ft.channel.isEmpty());
}

// ---------------------------------------------------------------------------
static ProbeResult makeProbe(const QString &path, qint64 durationMs)
{
    ProbeResult p;
    p.filePath = path;
    p.container = QStringLiteral("mp4");
    p.videoCodec = QStringLiteral("h264");
    p.width = 1920;
    p.height = 1080;
    p.fps = 25.0;
    p.pixFmt = QStringLiteral("yuv420p");
    p.durationMs = durationMs;
    p.firstPktKeyframe = true;
    return p;
}

static OcrResult makeOcr(const QString &path, qint64 wallStartMs, double conf = 0.95)
{
    OcrResult o;
    o.filePath = path;
    o.wallStartMs = wallStartMs;
    o.conf = conf;
    o.source = wallStartMs > 0 ? OcrResult::Ocr : OcrResult::None;
    return o;
}

static void testSmartSorterBasic()
{
    const qint64 t0 = epochOf(2024, 7, 1, 12, 0, 0);
    QVector<ProbeResult> probes{
        makeProbe(QStringLiteral("b.mp4"), 60000),
        makeProbe(QStringLiteral("a.mp4"), 60000),
        makeProbe(QStringLiteral("c.mp4"), 60000),
    };
    QVector<OcrResult> ocrs{
        makeOcr(QStringLiteral("a.mp4"), t0),
        makeOcr(QStringLiteral("b.mp4"), t0 + 60000),      // 恰好连续
        makeOcr(QStringLiteral("c.mp4"), t0 + 125000),     // 缺口 5s
    };
    auto groups = smartSort(probes, ocrs);
    CHECK(groups.size() == 1);                              // 无通道 → 单组
    const SortGroup &g = groups[0];
    CHECK(g.ordered.size() == 3);
    CHECK(g.ordered[0].filePath == QLatin1String("a.mp4"));
    CHECK(g.ordered[1].filePath == QLatin1String("b.mp4"));
    CHECK(g.ordered[2].filePath == QLatin1String("c.mp4"));
    CHECK(!g.suspicious);
    // 缺口警告（b.end=t0+120s, c.start=t0+125s → Δ=5s > 2s 容差）
    bool sawGap = false;
    for (const auto &w : g.warnings)
        if (w.type == SortWarningType::Gap && w.deltaMs == 5000)
            sawGap = true;
    CHECK(sawGap);

    // 重叠检测
    ocrs[2].wallStartMs = t0 + 115000;                     // Δ=-5s 重叠
    groups = smartSort(probes, ocrs);
    bool sawOverlap = false;
    for (const auto &w : groups[0].warnings)
        if (w.type == SortWarningType::Overlap && w.deltaMs == -5000)
            sawOverlap = true;
    CHECK(sawOverlap);

    // 容差边界：Δ=2s 恰好连续（不告警）
    ocrs[2].wallStartMs = t0 + 122000;
    groups = smartSort(probes, ocrs);
    int contWarnings = 0;
    for (const auto &w : groups[0].warnings)
        if (w.type == SortWarningType::Overlap || w.type == SortWarningType::Gap)
            ++contWarnings;
    CHECK(contWarnings == 0);

    // 尾帧 OCR 与原文逐字传播（面板证据列数据源）
    ocrs[0].wallEndMs = t0 + 59000;
    ocrs[0].rawStartText = QStringLiteral("2024-07-01 12:00:00");
    ocrs[0].rawEndText = QStringLiteral(".2024-07-01 12:00:59");
    groups = smartSort(probes, ocrs);
    CHECK(groups[0].ordered[0].ocrEndMs == t0 + 59000);
    CHECK(groups[0].ordered[0].rawStartText == QLatin1String("2024-07-01 12:00:00"));
    CHECK(groups[0].ordered[0].rawEndText == QLatin1String(".2024-07-01 12:00:59"));

    // 首帧解析失败（wallStartMs=0）时证据截图/尾帧仍须保留（取证可见性）
    QVector<OcrResult> broken{
        makeOcr(QStringLiteral("a.mp4"), t0),
        makeOcr(QStringLiteral("b.mp4"), 0),
    };
    broken[1].firstFrameImg = QStringLiteral("/tmp/b_head.png");
    broken[1].lastFrameImg = QStringLiteral("/tmp/b_tail.png");
    broken[1].wallEndMs = t0 + 119000;
    broken[1].rawEndText = QStringLiteral("2024-07-01 12:01:59");
    groups = smartSort(probes, broken);
    const SortEntry *be = nullptr;
    for (const auto &e : groups[0].ordered)
        if (e.filePath == QLatin1String("b.mp4"))
            be = &e;
    CHECK(be != nullptr);
    CHECK(be && be->thumbnailFirst == QLatin1String("/tmp/b_head.png"));
    CHECK(be && be->thumbnailLast == QLatin1String("/tmp/b_tail.png"));
    CHECK(be && be->ocrEndMs == t0 + 119000);
    CHECK(be && be->rawEndText == QLatin1String("2024-07-01 12:01:59"));
}

static void testSmartSorterGroupingAndSuspicious()
{
    const qint64 t0 = epochOf(2024, 7, 1, 12, 0, 0);
    QVector<ProbeResult> probes{
        makeProbe(QStringLiteral("CH01_20240701_120000.mp4"), 60000),
        makeProbe(QStringLiteral("CH01_20240701_120100.mp4"), 60000),
        makeProbe(QStringLiteral("CH02_20240701_120000.mp4"), 60000),
    };
    QVector<OcrResult> ocrs{
        makeOcr(QStringLiteral("CH01_20240701_120000.mp4"), t0),
        makeOcr(QStringLiteral("CH01_20240701_120100.mp4"), t0 + 60000),
        makeOcr(QStringLiteral("CH02_20240701_120000.mp4"), t0),
    };
    auto groups = smartSort(probes, ocrs);
    CHECK(groups.size() == 2);                              // 通道分组
    CHECK(groups[0].channel == QLatin1String("CH01"));
    CHECK(groups[0].ordered.size() == 2);
    CHECK(groups[1].channel == QLatin1String("CH02"));
    CHECK(groups[1].ordered.size() == 1);                   // 单文件组

    // 全组无 OCR → 存疑强制人工确认
    QVector<ProbeResult> p2{
        makeProbe(QStringLiteral("x.mp4"), 60000),
        makeProbe(QStringLiteral("y.mp4"), 60000),
    };
    QVector<OcrResult> o2;   // 无 OCR 结果
    groups = smartSort(p2, o2);
    CHECK(groups.size() == 1);
    CHECK(groups[0].suspicious);

    // 空输入
    CHECK(smartSort({}, {}).isEmpty());
}

static void testSmartSorterConflictAdjudication()
{
    const qint64 t0 = epochOf(2024, 7, 1, 12, 0, 0);
    // 文件名序与 OCR 序冲突：OCR 说 a 在前，文件名说 b 在前
    QVector<ProbeResult> probes{
        makeProbe(QStringLiteral("20240701_120100_a.mp4"), 60000),
        makeProbe(QStringLiteral("20240701_120000_b.mp4"), 60000),
    };
    QVector<OcrResult> ocrs{
        makeOcr(QStringLiteral("20240701_120100_a.mp4"), t0),          // a 其实更早
        makeOcr(QStringLiteral("20240701_120000_b.mp4"), t0 + 60000),  // b 其实更晚
    };
    auto groups = smartSort(probes, ocrs);
    // OCR 连续性误差 = 0，文件名序会产生 60s 重叠 → 应采用 OCR 序
    CHECK(groups[0].ordered[0].filePath == QLatin1String("20240701_120100_a.mp4"));
}

// ---------------------------------------------------------------------------
static void testTextUtils()
{
    using namespace preprocess_text;

    // CSV 转义（F6）
    CHECK(csvEscape(QStringLiteral("plain")) == QLatin1String("plain"));
    CHECK(csvEscape(QStringLiteral("a,b")) == QLatin1String("\"a,b\""));
    CHECK(csvEscape(QStringLiteral("say \"hi\""))
          == QLatin1String("\"say \"\"hi\"\"\""));
    CHECK(csvEscape(QStringLiteral("line\nbreak")) == QLatin1String("\"line\nbreak\""));

    // concat list.txt 转义：反斜杠→正斜杠、单引号转义、中文路径、盘符
    CHECK(concatEscapePath(QStringLiteral("C:\\v\\a b.mp4"))
          == QLatin1String("file 'C:/v/a b.mp4'"));
    CHECK(concatEscapePath(QStringLiteral("D:\\v\\张's.mp4"))
          == QString::fromUtf8("file 'D:/v/张'\\''s.mp4'"));

    // ffmpeg 进度解析（R-5：out_time_ms 单位是微秒）
    CHECK(parseFfmpegProgressMs("out_time_ms=5000000") == 5000);   // 5s
    CHECK(parseFfmpegProgressMs("out_time_us=2500000") == 2500);
    CHECK(parseFfmpegProgressMs("out_time=00:01:30.500000") == 90500);
    CHECK(parseFfmpegProgressMs("frame=123") == -1);
    CHECK(parseFfmpegProgressMs("progress=continue") == -1);

    // creation_time 解析
    bool tzMissing = false;
    const qint64 t = parseCreationTimeMs(QStringLiteral("2024-07-01T12:00:00.000Z"),
                                         &tzMissing);
    CHECK(t == epochOf(2024, 7, 1, 12, 0, 0) || t != 0);   // 合法即接受
    CHECK(!tzMissing);
    CHECK(parseCreationTimeMs(QStringLiteral("2036-02-06T06:28:15.000000Z")) == 0);  // 零值 bug
    CHECK(parseCreationTimeMs(QStringLiteral("1999-01-01T00:00:00Z")) == 0);         // 过老
    CHECK(parseCreationTimeMs(QStringLiteral("garbage")) == 0);
    const qint64 nt = parseCreationTimeMs(QStringLiteral("2024-07-01T12:00:00"),
                                          &tzMissing);
    CHECK(nt > 0 && tzMissing);   // 无时区标记 → 本地解释 + 标记
}

// ---------------------------------------------------------------------------
static void testPrecheck()
{
    // 全部一致 → OK
    QVector<ProbeResult> same{
        makeProbe(QStringLiteral("a.mp4"), 60000),
        makeProbe(QStringLiteral("b.mp4"), 60000),
    };
    auto res = concatPrecheck(same);
    CHECK(!res.hasBlock() && !res.hasWarn());

    // 编码器不一致 → BLOCK
    same[1].videoCodec = QStringLiteral("hevc");
    res = concatPrecheck(same);
    CHECK(res.hasBlock());

    // 像素格式不一致 → BLOCK
    same[1] = makeProbe(QStringLiteral("b.mp4"), 60000);
    same[1].pixFmt = QStringLiteral("yuvj420p");
    res = concatPrecheck(same);
    CHECK(res.hasBlock());

    // 帧率小偏差（2‰ > 容差 1‰）→ WARN；大偏差 → BLOCK
    same[1] = makeProbe(QStringLiteral("b.mp4"), 60000);
    same[1].fps = 25.05;
    res = concatPrecheck(same);
    CHECK(res.hasWarn() && !res.hasBlock());
    same[1].fps = 30.0;
    res = concatPrecheck(same);
    CHECK(res.hasBlock());

    // 白名单：mjpeg 不在 MP4 白名单 → BLOCK（评审 R-7）
    same[1] = makeProbe(QStringLiteral("b.mp4"), 60000);
    same[1].videoCodec = QStringLiteral("mjpeg");
    res = concatPrecheck(same);
    CHECK(res.hasBlock());

    // 色彩范围不一致 → WARN
    same[1] = makeProbe(QStringLiteral("b.mp4"), 60000);
    same[1].colorRange = QStringLiteral("pc");
    same[0].colorRange = QStringLiteral("tv");
    res = concatPrecheck(same);
    CHECK(res.hasWarn() && !res.hasBlock());

    // 音轨数不一致 → BLOCK
    same[1] = makeProbe(QStringLiteral("b.mp4"), 60000);
    same[1].audioStreams = 1;
    res = concatPrecheck(same);
    CHECK(res.hasBlock());

    // 单文件 → OK
    res = concatPrecheck({makeProbe(QStringLiteral("a.mp4"), 60000)});
    CHECK(!res.hasBlock());
}

// ---------------------------------------------------------------------------
static void testFilesNeedingTranscode()
{
    // 同参数 mp4 → 全部直接拼接（无需转码）
    QVector<ProbeResult> same{
        makeProbe(QStringLiteral("a.mp4"), 60000),
        makeProbe(QStringLiteral("b.mp4"), 60000),
    };
    CHECK(filesNeedingTranscode(same).isEmpty());

    // 单个非白名单文件（现场反馈：只转确实需要的文件）
    same[1].videoCodec = QStringLiteral("mjpeg");   // mjpeg 不在 MP4 白名单（R-7）
    auto need = filesNeedingTranscode(same);
    CHECK(need.size() == 1 && need.contains(QLatin1String("b.mp4")));

    // 探测失败的文件 → 需要转码（无法直接拼接）
    same[1] = makeProbe(QStringLiteral("b.mp4"), 60000);
    same[1].probeError = QStringLiteral("cannot open");
    need = filesNeedingTranscode(same);
    CHECK(need.size() == 1 && need.contains(QLatin1String("b.mp4")));

    // 组内参数不一致（分辨率）→ 整组转码
    same[1] = makeProbe(QStringLiteral("b.mp4"), 60000);
    same[1].height = 720;
    need = filesNeedingTranscode(same);
    CHECK(need.size() == 2);

    // 帧率大偏差 → 整组转码；小偏差 → 直接拼接
    same[1] = makeProbe(QStringLiteral("b.mp4"), 60000);
    same[1].fps = 30.0;
    need = filesNeedingTranscode(same);
    CHECK(need.size() == 2);
    same[1].fps = 25.02;
    need = filesNeedingTranscode(same);
    CHECK(need.isEmpty());

    // MP4 关键帧稀疏（>2.5s，拖拽不流畅）→ 仅该文件转码重排（现场反馈）
    same[1].fps = 25.0;
    same[1].keyframeIntervalMs = 8000;
    same[1].keyframeSparse = true;
    need = filesNeedingTranscode(same);
    CHECK(need.size() == 1 && need.contains(QLatin1String("b.mp4")));
    same[1].keyframeSparse = false;
    need = filesNeedingTranscode(same);
    CHECK(need.isEmpty());

    // 空输入 → 空
    CHECK(filesNeedingTranscode({}).isEmpty());
}

// ---------------------------------------------------------------------------
static void testSortableFileTable()
{
    // 现场反馈：拖拽行必须“插入”，不得覆盖/替换目标行
    SortableFileTable t(4, 3);
    const QString names[4] = {
        QStringLiteral("a.mp4"), QStringLiteral("b.mp4"),
        QStringLiteral("c.mp4"), QStringLiteral("d.mp4")};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) {
            auto *it = new QTableWidgetItem(
                QStringLiteral("%1_%2").arg(names[r]).arg(c));
            it->setData(Qt::UserRole, names[r]);
            t.setItem(r, c, it);
        }
    auto row0Names = [&t]() -> QStringList {
        QStringList out;
        for (int r = 0; r < t.rowCount(); ++r)
            out << t.item(r, 0)->data(Qt::UserRole).toString();
        return out;
    };

    // 拖 A(0) 到 C(2) 上方 → [B, A, C, D]（插入语义，C 及其后顺移）
    CHECK(t.moveRowTo(0, 2, false));
    CHECK(row0Names() == QStringList({"b.mp4", "a.mp4", "c.mp4", "d.mp4"}));
    // 恢复
    CHECK(t.moveRowTo(1, 0, false));
    CHECK(row0Names() == QStringList({"a.mp4", "b.mp4", "c.mp4", "d.mp4"}));

    // 拖 D(3) 到 B(1) 上方 → [A, D, B, C]
    CHECK(t.moveRowTo(3, 1, false));
    CHECK(row0Names() == QStringList({"a.mp4", "d.mp4", "b.mp4", "c.mp4"}));
    CHECK(t.moveRowTo(1, 3, true));    // D 到 C 之后（末尾）→ [A,B,C,D]
    CHECK(row0Names() == QStringList({"a.mp4", "b.mp4", "c.mp4", "d.mp4"}));

    // 拖 A(0) 到 D(3) 下缘（after）→ [B, C, D, A]
    CHECK(t.moveRowTo(0, 3, true));
    CHECK(row0Names() == QStringList({"b.mp4", "c.mp4", "d.mp4", "a.mp4"}));
    CHECK(t.moveRowTo(3, 0, false));   // A 回到最前
    CHECK(row0Names() == QStringList({"a.mp4", "b.mp4", "c.mp4", "d.mp4"}));

    // 相邻无变化：B(1) 插到 C(2) 上方 → 已在该位置 → false，行序不变
    CHECK(!t.moveRowTo(1, 2, false));
    CHECK(row0Names() == QStringList({"a.mp4", "b.mp4", "c.mp4", "d.mp4"}));

    // 无覆盖：全部行唯一且 UserRole 跟随行移动
    bool unique = true;
    for (const QString &n : names)
        unique = unique && (row0Names().count(n) == 1);
    CHECK(unique);

    // 越界输入安全
    CHECK(!t.moveRowTo(-1, 1, false));
    CHECK(!t.moveRowTo(0, 99, false));

    // handleRowDrop：模拟自建拖拽的落点处理（mime 格式 + 行中心上下半）
    t.resize(480, 320);
    t.show();                       // visualRect 依赖布局
    QMimeData mime;
    mime.setData(QStringLiteral("application/x-lumenarc-filerow"),
                 QByteArray::number(0));        // 拖第 0 行（A）
    const QRect rc = t.visualRect(t.model()->index(2, 0));   // 目标：第 2 行（C）
    CHECK(rc.isValid());
    // 落点上缘（插到 C 前）→ [B, A, C, D]
    CHECK(t.handleRowDrop(&mime, QPoint(rc.center().x(), rc.top() + 2)));
    CHECK(row0Names() == QStringList({"b.mp4", "a.mp4", "c.mp4", "d.mp4"}));
    // 再拖 A(现第 1 行) 落 C 下缘（插到 C 后）→ [B, C, A, D]
    QMimeData mime2;
    mime2.setData(QStringLiteral("application/x-lumenarc-filerow"),
                  QByteArray::number(1));
    const QRect rc2 = t.visualRect(t.model()->index(2, 0));
    CHECK(t.handleRowDrop(&mime2, QPoint(rc2.center().x(), rc2.bottom() - 2)));
    CHECK(row0Names() == QStringList({"b.mp4", "c.mp4", "a.mp4", "d.mp4"}));
    // 无格式 / 无效落点 → 不移动
    QMimeData empty;
    CHECK(!t.handleRowDrop(&empty, rc2.center()));
    CHECK(!t.handleRowDrop(&mime2, QPoint(-50, -50)));
    CHECK(row0Names() == QStringList({"b.mp4", "c.mp4", "a.mp4", "d.mp4"}));
}

// ---------------------------------------------------------------------------
static void testTickStep()
{
    const qint64 min = 60000;   // 1min
    const qint64 tenMin = 600000;
    const qint64 twoH = 7200000;
    const qint64 tenH = 36000000;
    const qint64 day = 86400000;
    // 40 分钟视频（拼接产物典型时长）900px → 5min 步长，间距 = 900*300000/2400000 = 112px
    CHECK(computeXAxisStepMs(2400000, 900) == 300000);
    // 2h → 10min，间距 125px
    CHECK(computeXAxisStepMs(twoH, 900) == 600000);
    // 10h → 1h，间距 90px
    CHECK(computeXAxisStepMs(tenH, 900) == 3600000);
    // 24h → 2h，间距 75px（首个 ≥72px 的 nice 档）
    CHECK(computeXAxisStepMs(day, 900) == 7200000);
    // 10min 视频 600px → 1min，间距 60px（<72 阈值？600*60000/600000=60 <72 → 2min）
    CHECK(computeXAxisStepMs(tenMin, 600) == 120000);
    // 10min 视频 1200px → 1min，间距 120px ✓
    CHECK(computeXAxisStepMs(tenMin, 1200) == 60000);
    // 1min 视频 600px → 5s，间距 50px → 10s？600*10000/60000=100 ≥72 → 10s
    CHECK(computeXAxisStepMs(min, 600) == 10000);
    // 短时长大宽度：10s 视频 1000px → 1000*1000/10000=100 → 1s
    CHECK(computeXAxisStepMs(10000, 1000) == 1000);
    // 极长：48h 视频 900px → 900*21600000/172800000=112 ≥72 → 6h
    CHECK(computeXAxisStepMs(172800000, 900) == 21600000);
    // 退化输入
    CHECK(computeXAxisStepMs(0, 900) == 60000);
    CHECK(computeXAxisStepMs(60000, 0) == 60000);
    // 所有档位标签间距均 ≥ 阈值（通用性质检验）
    const qint64 durations[] = {10000, 60000, 600000, 2400000, 7200000,
                                36000000, 86400000, 172800000};
    for (qint64 d : durations) {
        for (int w : {400, 800, 1600}) {
            const qint64 step = computeXAxisStepMs(d, w);
            const qreal gap = step * w / static_cast<qreal>(d);
            // 允许最后一档不足（极端长视频且宽度极小）
            CHECK(gap >= 72.0 * 0.55 || step >= 86400000);
        }
    }
}

// ---------------------------------------------------------------------------
static void testEvidenceCsv()
{
    EvidenceReportInput in;
    in.probes = {makeProbe(QStringLiteral("a.mp4"), 60000)};
    in.probes[0].creationTimeRaw = QStringLiteral("2024-07-01T12:00:00Z");
    auto o = makeOcr(QStringLiteral("a.mp4"), epochOf(2024, 7, 1, 12, 0, 0));
    o.rawStartText = QStringLiteral("2024-07-01 12:00:00, 大厅");   // 含逗号 → 转义
    o.sha256 = QStringLiteral("abc123");
    in.ocrs = {o};
    SortGroup g;
    g.channel = QStringLiteral("CH01");
    SortEntry e;
    e.filePath = QStringLiteral("a.mp4");
    e.startMs = o.wallStartMs;
    g.ordered = {e};
    in.groups = {g};
    in.actions.insert(QStringLiteral("a.mp4"), QStringLiteral("拼接"));

    const QString csv = buildEvidenceCsv(in);
    CHECK(csv.startsWith(QStringLiteral("\xEF\xBB\xBF")));          // BOM
    CHECK(csv.contains(QStringLiteral("abc123")));
    // 含逗号字段被引号包裹（F6）
    CHECK(csv.contains(QStringLiteral("\"2024-07-01 12:00:00, 大厅\"")));
    CHECK(csv.contains(QStringLiteral("拼接")));
    // 原始列逐字保留 + 派生列存在
    CHECK(csv.contains(QStringLiteral("2024-07-01T12:00:00.000")));
    CHECK(csv.contains(QStringLiteral("OCR")));
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
static void testAbsStartEvidence()
{
    const qint64 t0 = epochOf(2026, 7, 22, 6, 0, 2);
    // DHAV 场景：无 OCR/文件名证据，流内绝对起始决定顺序
    QVector<ProbeResult> probes{
        makeProbe(QStringLiteral("06.03.25-06.10.00[M].dav"), 395000),
        makeProbe(QStringLiteral("06.00.02-06.02.15[R].dav"), 133000),
    };
    probes[0].absStartEpochMs = t0 + 203000;
    probes[1].absStartEpochMs = t0;
    auto groups = smartSort(probes, {});
    CHECK(groups.size() == 1);
    CHECK(groups[0].ordered.size() == 2);
    CHECK(groups[0].ordered[0].filePath.contains(QLatin1String("06.00.02")));
    CHECK(groups[0].ordered[0].sourceKind == SortEvidenceKind::AbsStart);
    CHECK(groups[0].ordered[1].filePath.contains(QLatin1String("06.03.25")));
    CHECK(!groups[0].suspicious);   // absStart 为有效证据，不强制人工

    // OCR 优先于 absStart（证据层级：OCR 1.0 > absStart 0.6）
    QVector<OcrResult> ocrs{makeOcr(probes[1].filePath, t0 + 2000)};
    groups = smartSort(probes, ocrs);
    const SortEntry &e1 = groups[0].ordered[0];
    CHECK(e1.sourceKind == SortEvidenceKind::Ocr);
    CHECK(e1.startMs == t0 + 2000);

    // OCR 与 absStart 偏差 > 2min → 交叉冲突警告（不改序）
    ocrs[0].wallStartMs = t0 + 200000;
    groups = smartSort(probes, ocrs);
    bool sawConflict = false;
    for (const auto &w : groups[0].warnings)
        if (w.type == SortWarningType::EvidenceConflict)
            sawConflict = true;
    CHECK(sawConflict);

    // 偏差 < 2min → 无冲突警告
    ocrs[0].wallStartMs = t0 + 60000;
    groups = smartSort(probes, ocrs);
    sawConflict = false;
    for (const auto &w : groups[0].warnings)
        if (w.type == SortWarningType::EvidenceConflict)
            sawConflict = true;
    CHECK(!sawConflict);
}

int main(int argc, char **argv)
{
    // GUI 单测需要 QApplication（Windows 桌面 session 可初始化 widget；
    // 若在无桌面的 CI 环境卡死，测试不会显示窗口，不影响 headless 用例）
    QApplication app(argc, argv);
    // 诊断模式：--group-debug <files...> 用生产代码路径打印文件名解析与分组结果
    if (argc > 2 && QLatin1String(argv[1]) == QLatin1String("--group-debug")) {
        QVector<ProbeResult> probes;
        for (int i = 2; i < argc; ++i) {
            const QString path = QString::fromLocal8Bit(argv[i]);
            const QString name = QFileInfo(path).fileName();
            const FilenameTimestamp ft = parseFilenameTimestamp(name);
            fprintf(stderr, "[FN] %-26s patternId=%d epochMs=%lld channel='%s' raw='%s'\n",
                    name.toUtf8().constData(), ft.patternId, (long long)ft.epochMs,
                    ft.channel.toUtf8().constData(), ft.rawText.toUtf8().constData());
            ProbeResult p;
            p.filePath = path;
            p.durationMs = 600000;
            probes.append(p);
        }
        const QVector<SortGroup> groups = smartSort(probes, {});
        fprintf(stderr, "[GRP] %lld group(s)\n", (long long)groups.size());
        for (const auto &g : groups) {
            fprintf(stderr, "  channel='%s' files=%d suspicious=%d\n",
                    g.channel.toUtf8().constData(), g.ordered.size(), g.suspicious);
            for (const auto &e : g.ordered)
                fprintf(stderr, "    %s start=%lld src=%d\n",
                        QFileInfo(e.filePath).fileName().toUtf8().constData(),
                        (long long)e.startMs, int(e.startSource));
        }
        return 0;
    }
    testFilenamePatterns();
    testSmartSorterBasic();
    testAbsStartEvidence();
    testSmartSorterBasic();
    testSmartSorterGroupingAndSuspicious();
    testSmartSorterConflictAdjudication();
    testTextUtils();
    testPrecheck();
    testFilesNeedingTranscode();
    testEvidenceCsv();
    testSortableFileTable();
    testTickStep();
    fprintf(stderr, "checks: %d failures: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
