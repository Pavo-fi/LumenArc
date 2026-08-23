#include "docx_writer.h"

#include "zip_store_writer.h"
#include <QFile>
#include <QImage>

QString DocxWriter::escapeXml(const QString &s)
{
    QString r = s;
    r.replace('&', QStringLiteral("&amp;"));
    r.replace('<', QStringLiteral("&lt;"));
    r.replace('>', QStringLiteral("&gt;"));
    r.replace('"', QStringLiteral("&quot;"));
    r.replace('\'', QStringLiteral("&apos;"));
    return r;
}

/// 文本 run：宋体/黑体口径 + 字号（half-points）+ 加粗
static QString runXml(const QString &text, bool bold, int halfPt,
                      const QString &eastAsiaFont)
{
    return QStringLiteral(
        "<w:r><w:rPr><w:rFonts w:ascii=\"Times New Roman\" w:eastAsia=\"%1\"/>"
        "%2<w:sz w:val=\"%3\"/><w:szCs w:val=\"%3\"/></w:rPr>"
        "<w:t xml:space=\"preserve\">%4</w:t></w:r>")
        .arg(eastAsiaFont,
             bold ? QStringLiteral("<w:b/><w:bCs/>") : QString(),
             QString::number(halfPt), DocxWriter::escapeXml(text));
}

void DocxWriter::addHeading(const QString &text, int level)
{
    level = qBound(1, level, 3);
    const int halfPt = (level == 1) ? 32 : (level == 2) ? 28 : 24;
    const QString jc = (level == 1) ? QStringLiteral("center")
                                    : QStringLiteral("left");
    m_body += QStringLiteral(
        "<w:p><w:pPr><w:jc w:val=\"%1\"/>"
        "<w:spacing w:before=\"240\" w:after=\"120\"/></w:pPr>")
        .arg(jc);
    m_body += runXml(text, true, halfPt, QStringLiteral("黑体"));
    m_body += QStringLiteral("</w:p>");
}

void DocxWriter::addParagraph(const QString &text, bool bold)
{
    m_body += QStringLiteral(
        "<w:p><w:pPr><w:spacing w:line=\"360\" w:lineRule=\"auto\"/>"
        "<w:ind w:firstLineChars=\"200\" w:firstLine=\"240\"/></w:pPr>");
    m_body += runXml(text, bold, 24, QStringLiteral("宋体"));
    m_body += QStringLiteral("</w:p>");
}

void DocxWriter::addTable(const QVector<QVector<QString>> &rows,
                          bool headerRow, const QVector<int> &colWidthsPct)
{
    if (rows.isEmpty() || rows.first().isEmpty())
        return;
    const int cols = rows.first().size();
    const int totalW = 9026;   // 版心宽（dxa）
    m_body += QStringLiteral(
        "<w:tbl><w:tblPr><w:tblW w:w=\"%1\" w:type=\"dxa\"/>"
        "<w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"8\"/><w:left w:val=\"single\" w:sz=\"8\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"8\"/><w:right w:val=\"single\" w:sz=\"8\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\"/>"
        "<w:insideV w:val=\"single\" w:sz=\"4\"/>"
        "</w:tblBorders></w:tblPr>").arg(totalW);
    for (int r = 0; r < rows.size(); ++r) {
        m_body += QStringLiteral("<w:tr>");
        for (int c = 0; c < cols; ++c) {
            int pct = (c < colWidthsPct.size()) ? colWidthsPct[c] : 100 / cols;
            const int w = totalW * pct / 100;
            const bool head = headerRow && r == 0;
            m_body += QStringLiteral(
                "<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/>%2</w:tcPr>"
                "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>")
                .arg(w).arg(head
                    ? QStringLiteral("<w:shd w:val=\"clear\" w:fill=\"D9D9D9\"/>")
                    : QString());
            m_body += runXml(c < rows[r].size() ? rows[r][c] : QString(),
                             head, 21, QStringLiteral("宋体"));
            m_body += QStringLiteral("</w:p></w:tc>");
        }
        m_body += QStringLiteral("</w:tr>");
    }
    m_body += QStringLiteral("</w:tbl>");
    // 表后空段（Word 规范：表格相邻须分段，且视觉留缝）
    m_body += QStringLiteral("<w:p/>");
}

void DocxWriter::addPageBreak()
{
    m_body += QStringLiteral("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");
}

void DocxWriter::addImage(const QString &pngPath, int emuWidth)
{
    QImage img(pngPath);
    if (img.isNull())
        return;   // C1：调用方负责先校验存在性；此处防空图崩
    const int idx = int(m_images.size());
    m_images.append(pngPath);
    ++m_imgSeq;
    const int emuH = int(qint64(emuWidth) * img.height() / qMax(1, img.width()));
    const QString rid = QStringLiteral("rId%1").arg(100 + idx);
    m_body += QStringLiteral(
        "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr><w:r><w:drawing>"
        "<wp:inline xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
        "<wp:extent cx=\"%1\" cy=\"%2\"/>"
        "<wp:docPr id=\"%3\" name=\"图 %3\"/>"
        "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
        "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"图 %3\"/><pic:cNvPicPr/></pic:nvPicPr>"
        "<pic:blipFill><a:blip r:embed=\"%4\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"/>"
        "<a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
        "</pic:pic></a:graphicData></a:graphic></wp:inline>"
        "</w:drawing></w:r></w:p>")
        .arg(emuWidth).arg(emuH).arg(100 + idx).arg(rid);
}

bool DocxWriter::save(const QString &path)
{
    // ---- [Content_Types].xml ----
    QString ct = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "</Types>");

    const QString rootRels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>");

    QString docRels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");
    for (int i = 0; i < m_images.size(); ++i)
        docRels += QStringLiteral(
            "<Relationship Id=\"%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/img%2.png\"/>")
            .arg(100 + i).arg(i);
    docRels += QStringLiteral("</Relationships>");

    // styles.xml：Normal 宋体小四 + 行距（字号在各 run 直给，样式兜底）
    const QString styles = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
        "<w:name w:val=\"Normal\"/>"
        "<w:rPr><w:rFonts w:ascii=\"Times New Roman\" w:eastAsia=\"宋体\"/>"
        "<w:sz w:val=\"24\"/><w:szCs w:val=\"24\"/></w:rPr></w:style>"
        "</w:styles>");

    const QString doc = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:body>%1"
        "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
        "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" "
        "w:header=\"851\" w:footer=\"992\" w:gutter=\"0\"/></w:sectPr>"
        "</w:body></w:document>").arg(m_body);

    ZipStoreWriter zip;
    zip.addFile(QStringLiteral("[Content_Types].xml"), ct.toUtf8());
    zip.addFile(QStringLiteral("_rels/.rels"), rootRels.toUtf8());
    zip.addFile(QStringLiteral("word/document.xml"), doc.toUtf8());
    zip.addFile(QStringLiteral("word/styles.xml"), styles.toUtf8());
    zip.addFile(QStringLiteral("word/_rels/document.xml.rels"), docRels.toUtf8());
    for (int i = 0; i < m_images.size(); ++i) {
        QFile f(m_images[i]);
        if (f.open(QIODevice::ReadOnly))
            zip.addFile(QStringLiteral("word/media/img%1.png").arg(i),
                        f.readAll());
    }
    return zip.writeTo(path);
}
