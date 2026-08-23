/**
 * @file docx_writer.h
 * @brief P-28 报告模块地基：极简 DOCX（OPC）文档写出器
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 仅实现报告需要的子集：标题 1-3 级 / 正文段落（可加粗）/ 带框表格
 * （首行表头加底纹加粗）/ 分页符 / 图片（PNG）。远期 HTML 渲染器与本类
 * 同吃 ReportData（渲染器缝——拍板：只出 DOCX，HTML 框架预留）。
 * 字体口径：正文 宋体小四（12pt），标题 黑体加粗（一 16pt/二 14pt/三 12pt）。
 */
#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>

class DocxWriter
{
public:
    void addHeading(const QString &text, int level);   ///< level 1..3
    void addParagraph(const QString &text, bool bold = false);
    /// rows[r][c]；headerRow=true 时首行表头加底纹加粗。colWidthsPct 归一化
    /// 列宽（空=均分）
    void addTable(const QVector<QVector<QString>> &rows, bool headerRow = true,
                  const QVector<int> &colWidthsPct = {});
    void addPageBreak();
    /// PNG 图片（emuWidth 目标宽，1cm = 360000 EMU；16cm ≈ 5760000）
    void addImage(const QString &pngPath, int emuWidth);

    bool save(const QString &path);

    /// XML 转义（供单测/调用方复用）
    static QString escapeXml(const QString &s);

private:
    QString m_body;                ///< w:body 累积 XML
    QVector<QString> m_images;     ///< 待嵌入 PNG 路径（document.xml.rels 序号对齐）
    int m_imgSeq = 0;
};
