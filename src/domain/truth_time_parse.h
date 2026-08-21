/**
 * @file truth_time_parse.h
 * @brief 北京时间对时（第 2 步）的时间文本解析（v1.12.5 拍板约定）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-21
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 校时图片两框 OCR 原文 / 手动输入文本 → 本地墙钟 ms。
 * 框选约定（2026-08-21 用户拍板，以增城案典型校时照片实证）：
 *   框 1（监控主机时间）：须含完整日期+时分秒，如
 *       「2026年07月22日 星期三 12:25:47」「2026-07-22 12:25:47」；
 *   框 2（北京时间）：优先完整日期+时分秒；允许跨行组合（日期行+时间行，
 *       如授时网页「现在是2026年7月22日星期三，第30周」+「12:39:41」）；
 *       仅时分秒时日期取框 1 同日（跨日疑义由调用方按 ±1 日 |偏差| 取小
 *       并提示用户确认）；
 *   秒位必须可见：仅「12:39」（手机状态栏式）拒识并提示重框。
 */
#ifndef TRUTH_TIME_PARSE_H
#define TRUTH_TIME_PARSE_H

#include <QDate>
#include <QString>
#include <QStringList>

/// 解析结果（C1：error 为类型化前缀，UI 负责映射 i18n 文案）
struct TruthTimeParse {
    bool    ok = false;
    qint64  wallMs = 0;          ///< 本地墙钟 ms（Qt::LocalTime，与 OCR 管线同口径）
    bool    dateFromText = false;///< 日期来自文本（false = 取 assumeDate）
    QString matchedText;         ///< 命中原文（跨行组合以 " | " 连接；留档用）
    QString error;               ///< "nomatch" / "noseconds:<text>" / "invalid:<text>"
};

/**
 * @brief 从 OCR 行组（按置信度降序）解析墙钟时间。
 * @param ocrLines   框内 OCR 文本行（原始大小写；内部做全角冒号等归一化）
 * @param assumeDate 纯时间行的假定日期（框 1 日期；无日期又无可假定 → 失败）
 *
 * 命中优先级：① 单行完整（日期+带秒时间）；② 跨行组合（日期行+时间行）；
 * ③ 纯时间+假定日期。值域校验（年 2000..今年+1，月日时分秒合法）。
 */
TruthTimeParse parseTruthTimeText(const QStringList &ocrLines,
                                  const QDate &assumeDate);

#endif // TRUTH_TIME_PARSE_H
