/**
 * @file docx_test_main.cpp
 * @brief P-28 地基测试：ZipStoreWriter CRC/结构 + DocxWriter OPC 良构
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * store 模式 zip 数据紧跟局部头，测试用最小局部头扫描回读条目；
 * document.xml 用 QXmlStreamReader 验良构 + 关键文本在档。
 */

#include "infrastructure/zip_store_writer.h"
#include "infrastructure/docx_writer.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QImage>
#include <QPainter>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond, msg) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

// store zip 最小回读：扫局部头取条目内容（本写出器产物专用）
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

    // CRC32 已知向量："123456789" → 0xCBF43926
    CHECK(ZipStoreWriter::crc32("123456789") == 0xCBF43926u, "crc32 known vector");
    CHECK(ZipStoreWriter::crc32(QByteArray()) == 0u, "crc32 empty = 0");

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "tmp dir");

    // zip 结构：两条目 + 中文内容
    const QString zipPath = tmp.filePath(QStringLiteral("t.zip"));
    {
        ZipStoreWriter zw;
        zw.addFile(QStringLiteral("a.txt"), "hello");
        zw.addFile(QStringLiteral("dir/b.xml"),
                   QStringLiteral("<x>中文 %1</x>").arg("测试").toUtf8());
        zw.addFile(QStringLiteral("a.txt"), "hello2");   // 重复名覆盖
        CHECK(zw.writeTo(zipPath), "zip writeTo ok");
    }
    QFile zf(zipPath);
    CHECK(zf.open(QIODevice::ReadOnly), "zip open");
    const QByteArray blob = zf.readAll();
    CHECK(blob.startsWith("PK\x03\x04"), "zip magic");
    CHECK(zipReadEntry(blob, "a.txt") == "hello2", "dup name overwritten");
    CHECK(zipReadEntry(blob, "dir/b.xml").contains("测试"),
          "utf8 entry round trip");

    // DOCX：标题/正文/表格/图片/分页 → OPC 结构 + XML 良构
    const QString pngPath = tmp.filePath(QStringLiteral("pic.png"));
    {
        QImage img(64, 48, QImage::Format_RGB888);
        img.fill(Qt::darkBlue);
        QPainter p(&img);
        p.setPen(Qt::yellow);
        p.drawLine(0, 0, 63, 47);
        CHECK(img.save(pngPath), "png saved");
    }
    const QString docxPath = tmp.filePath(QStringLiteral("t.docx"));
    {
        DocxWriter dw;
        dw.addHeading(QStringLiteral("火灾视频分析报告"), 1);
        dw.addHeading(QStringLiteral("一、基本情况"), 2);
        dw.addParagraph(QStringLiteral("正文段落 <含&转义> \"字符\""));
        dw.addTable({{QStringLiteral("监控编号"), QStringLiteral("时间差")},
                     {QStringLiteral("D17"), QStringLiteral("慢 2 分 14 秒")}});
        dw.addImage(pngPath, 5000000);
        dw.addPageBreak();
        dw.addParagraph(QStringLiteral("第二页"));
        CHECK(dw.save(docxPath), "docx save ok");
    }
    QFile df(docxPath);
    CHECK(df.open(QIODevice::ReadOnly), "docx open");
    const QByteArray doc = df.readAll();
    CHECK(doc.startsWith("PK\x03\x04"), "docx is zip");
    const QByteArray ct = zipReadEntry(doc, "[Content_Types].xml");
    CHECK(ct.contains("wordprocessingml.document.main"), "content types main");
    CHECK(ct.contains("image/png"), "content types png");
    const QByteArray docXml = zipReadEntry(doc, "word/document.xml");
    CHECK(!docXml.isEmpty(), "document.xml present");
    {
        QXmlStreamReader xml(docXml);
        while (!xml.atEnd())
            xml.readNext();
        CHECK(!xml.hasError(), "document.xml well-formed");
    }
    CHECK(docXml.contains("火灾视频分析报告"), "heading text in doc");
    CHECK(docXml.contains("&lt;含&amp;转义&gt;"), "xml escaping");
    CHECK(docXml.contains("w:tbl"), "table present");
    CHECK(docXml.contains("w:br w:type=\"page\""), "page break");
    CHECK(docXml.contains("r:embed=\"rId100\""), "image reference");
    const QByteArray rels = zipReadEntry(doc, "word/_rels/document.xml.rels");
    CHECK(rels.contains("media/img0.png"), "image rel present");
    CHECK(!zipReadEntry(doc, "word/media/img0.png").isEmpty(),
          "image payload embedded");

    fprintf(stderr, "docx_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
