// manual2docx.cpp — 一次性工具：MANUAL.md → 随包/上云 DOCX（方案 A：git 留真源）
// 复用工程自带零依赖 DocxWriter（P-28 报告同款排版：标题 1-3/段落/带框表格）
// 限制（v1 拍板可接受）：行内 **粗体**/`代码` 标记剥除不保留样式——
// DocxWriter 只有整段加粗；WPS 在线文档以阅读/批注/分享为主。
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QVector>
#include <cstdio>

#include "../../src/infrastructure/docx_writer.h"

namespace {

/// 剥除行内 markdown 标记：**粗体**、`代码`、__粗体__
QString stripInline(QString s)
{
    s.replace(QStringLiteral("**"), QString());
    s.replace(QStringLiteral("__"), QString());
    s.replace(QStringLiteral("`"), QString());
    return s.trimmed();
}

bool isTableLine(const QString &line)
{
    const QString t = line.trimmed();
    return t.startsWith(QLatin1Char('|')) && t.endsWith(QLatin1Char('|'))
           && t.count(QLatin1Char('|')) >= 2;
}

bool isTableSeparator(const QString &line)
{
    QString t = line.trimmed();
    t.remove(QLatin1Char('|')).remove(QLatin1Char('-'))
     .remove(QLatin1Char(':')).remove(QLatin1Char(' '));
    return t.isEmpty();
}

QStringList splitRow(const QString &line)
{
    QString t = line.trimmed();
    if (t.startsWith(QLatin1Char('|')))
        t = t.mid(1);
    if (t.endsWith(QLatin1Char('|')))
        t.chop(1);
    const auto parts = t.split(QLatin1Char('|'));
    QStringList cells;
    for (const auto &c : parts)
        cells << stripInline(c);
    return cells;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: manual2docx <in.md> <out.docx>\n");
        return 2;
    }
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "open md failed\n");
        return 1;
    }
    const QStringList lines = QString::fromUtf8(f.readAll())
                                  .split(QLatin1Char('\n'));

    DocxWriter w;
    int nHead = 0, nPara = 0, nTable = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const QString raw = lines[i];
        const QString line = raw.trimmed();

        if (line.isEmpty() || line == QLatin1String("---"))
            continue;

        // 表格块：连续 | 行；第二行分隔线跳过
        if (isTableLine(line)) {
            QVector<QVector<QString>> rows;
            while (i < lines.size() && isTableLine(lines[i])) {
                if (!isTableSeparator(lines[i]))
                    rows.append(splitRow(lines[i]).toVector());
                ++i;
            }
            --i;   // for 循环会再 +1
            if (!rows.isEmpty()) {
                w.addTable(rows, true);
                ++nTable;
            }
            continue;
        }

        // 标题：# → 居中大字（封面题），##/###/#### → 1..3 级
        if (line.startsWith(QLatin1Char('#'))) {
            int lv = 0;
            while (lv < line.size() && line[lv] == QLatin1Char('#'))
                ++lv;
            const QString text = stripInline(line.mid(lv));
            if (lv == 1)
                w.addCentered(text, 36, true);        // 书名
            else
                w.addHeading(text, qMin(lv - 1, 3));  // ## → 1 级 …
            ++nHead;
            continue;
        }

        // 引用块：去 "> " 前缀，加竖线示意
        if (line.startsWith(QLatin1String("> "))) {
            w.addParagraph(QStringLiteral("▎ ")
                           + stripInline(line.mid(2)));
            ++nPara;
            continue;
        }

        // 无序列表（含两级缩进）
        if (line.startsWith(QLatin1String("- "))) {
            const bool nested = raw.startsWith(QLatin1String("  "));
            w.addParagraph(QString(nested ? QStringLiteral("　　◦ ")
                                          : QStringLiteral("· "))
                           + stripInline(line.mid(2)));
            ++nPara;
            continue;
        }

        // 其余：有序列表/普通段落原样（数字编号文本自带）
        w.addParagraph(stripInline(line));
        ++nPara;
    }

    if (!w.save(QString::fromLocal8Bit(argv[2]))) {
        std::fprintf(stderr, "save docx failed\n");
        return 1;
    }
    std::printf("headings=%d paras=%d tables=%d -> %s\n", nHead, nPara, nTable,
                argv[2]);
    return 0;
}
