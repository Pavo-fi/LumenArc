// manual2pdf.cpp — 一次性工具：MANUAL.md → 随包 PDF
// Qt QTextDocument 原生 markdown（含 GFM 表格）+ QPdfWriter 直出，零第三方依赖
#include <QGuiApplication>
#include <QFile>
#include <QTextDocument>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QFont>
#include <QTextOption>
#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: manual2pdf <in.md> <out.pdf>\n");
        return 2;
    }
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "open md failed\n");
        return 1;
    }
    const QString md = QString::fromUtf8(f.readAll());

    QTextDocument doc;
    // 中文正文 + emoji 回落（📷✓⚠ 等）
    QFont font;
    font.setFamilies({QStringLiteral("Microsoft YaHei"),
                      QStringLiteral("Segoe UI Emoji")});
    font.setPointSize(10);
    doc.setDefaultFont(font);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    doc.setDefaultTextOption(opt);
    doc.setMarkdown(md);   // Qt6 默认 GitHub 方言（含表格）

    QPdfWriter w(QString::fromLocal8Bit(argv[2]));
    w.setPageSize(QPageSize(QPageSize::A4));
    w.setPageMargins(QMarginsF(16, 16, 16, 16), QPageLayout::Millimeter);
    w.setResolution(150);
    w.setTitle(QStringLiteral("追光者 Lumen Arc — 操作手册"));
    doc.print(&w);

    // 自检：表格是否真渲染（toHtml 应含 <table>）
    const bool hasTable = doc.toHtml().contains(QStringLiteral("<table"));
    std::printf("pages~%d blocks=%d tables=%s\n",
                int(doc.pageCount()), doc.blockCount(),
                hasTable ? "yes" : "NO");
    return 0;
}
