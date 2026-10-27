/**
 * @file ui/inspector_panel.cpp
 * @brief 前端 V2 骨架：检视器实现
 */
#include "ui/inspector_panel.h"

#include "i18n.h"
#include "theme.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QVBoxLayout>

namespace ui {

namespace {

QString fmtMs(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 h = ms / 3600000;
    const qint64 m = (ms / 60000) % 60;
    const qint64 s = (ms / 1000) % 60;
    const qint64 f = ms % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(f, 3, 10, QLatin1Char('0'));
}

QString fmtLen(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("--");
    const qint64 m = ms / 60000;
    const qint64 s = (ms / 1000) % 60;
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

InspectorPanel::InspectorPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("inspectorPanel"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- 上半：事实（可滚动）----
    // v2.2：工具栏与检视器分开 —— 事实随内容滚，工具常驻底部不跟着滚走
    auto *factsScroll = new QScrollArea(this);
    factsScroll->setWidgetResizable(true);
    factsScroll->setFrameShape(QFrame::NoFrame);
    factsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *facts = new QWidget(factsScroll);
    auto *lay = new QVBoxLayout(facts);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(5);

    addSection(lay, lang(QStringLiteral("素材"), QStringLiteral("Source")));
    m_name = addRow(lay, lang(QStringLiteral("名称"), QStringLiteral("Name")));
    m_size = addRow(lay, lang(QStringLiteral("分辨率"), QStringLiteral("Size")));
    m_fps = addRow(lay, lang(QStringLiteral("帧率"), QStringLiteral("FPS")));
    m_dur = addRow(lay, lang(QStringLiteral("时长"), QStringLiteral("Length")));

    addSection(lay, lang(QStringLiteral("时间基准"), QStringLiteral("Time base")));
    m_cal = addRow(lay, lang(QStringLiteral("校时"), QStringLiteral("Calibration")));
    m_cursor = addRow(lay, lang(QStringLiteral("游标"), QStringLiteral("Cursor")));

    addSection(lay, lang(QStringLiteral("区域"), QStringLiteral("Regions")));
    m_roi = addRow(lay, lang(QStringLiteral("ROI"), QStringLiteral("ROI")));
    lay->addStretch(1);
    factsScroll->setWidget(facts);
    outer->addWidget(factsScroll, 1);

    // ---- 分隔线 ----
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QStringLiteral("background:%1;border:0;").arg(Theme::Border));
    outer->addWidget(sep, 0);

    // ---- 下半：工具（固定，不随滚动）----
    auto *toolsHost = new QWidget(this);
    auto *tlay = new QVBoxLayout(toolsHost);
    tlay->setContentsMargins(10, 6, 10, 8);
    tlay->setSpacing(4);

    // 工具：容器留空，由 MainWindow 把「分析与标注」整行的真控件收进来
    // —— 同一批指针，不重建，所以启用态/勾选态与工具栏时代完全一致。
    addSection(tlay, lang(QStringLiteral("工具"), QStringLiteral("Tool")), 4);
    m_toolLay = new QVBoxLayout();
    m_toolLay->setContentsMargins(0, 0, 0, 0);
    m_toolLay->setSpacing(4);
    tlay->addLayout(m_toolLay);

    // 截图叠加：这一栏只是标题（本身不是开关），下面是 截取 / 编辑 / 放置
    addSection(tlay, lang(QStringLiteral("截图叠加"), QStringLiteral("Overlay")), 8);
    m_captureLay = new QVBoxLayout();
    m_captureLay->setContentsMargins(0, 0, 0, 0);
    m_captureLay->setSpacing(3);
    tlay->addLayout(m_captureLay);

    // 显示栏：快照/画面调节 由 MainWindow 追加（自动排在放大镜之前）
    addSection(tlay, lang(QStringLiteral("显示"), QStringLiteral("Display")), 8);
    m_displayLay = new QVBoxLayout();
    m_displayLay->setContentsMargins(0, 0, 0, 0);
    m_displayLay->setSpacing(3);
    tlay->addLayout(m_displayLay);
    {
        const QString css = QStringLiteral(
            "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:4px;"
            "font-size:11px;padding:3px 0;}"
            "QPushButton:hover{border-color:%4;}"
            "QPushButton:checked{color:%5;border-color:%5;}")
            .arg(Theme::BgCard, Theme::TextSecond, Theme::Border,
                 Theme::Accent, Theme::Accent);
        m_magBtn = new QPushButton(lang(QStringLiteral("放大镜"), QStringLiteral("Magnifier")), this);
        m_magBtn->setCheckable(true);
        m_magBtn->setCursor(Qt::PointingHandCursor);
        m_magBtn->setFocusPolicy(Qt::NoFocus);
        m_magBtn->setStyleSheet(css);
        m_displayLay->addWidget(m_magBtn);
        connect(m_magBtn, &QPushButton::toggled, this, &InspectorPanel::magnifierToggled);
    }
    outer->addWidget(toolsHost, 0);

    // 初始空态
    setSource(QString(), 0, 0, 0.0, 0);
    setCalibration(lang(QStringLiteral("未校时"), QStringLiteral("None")), false);
    setCursor(0);
    setRoi(0, 0);
}

QLabel *InspectorPanel::addSection(QVBoxLayout *lay, const QString &title, int topPad)
{
    auto *l = new QLabel(title, this);
    // L1 分区标题：小、暗、宽字距 + 粗体 —— 只做结构，不与数值抢视线
    l->setStyleSheet(QStringLiteral("color:%1;font-size:10px;letter-spacing:1.4px;"
                                    "font-weight:700;background:transparent;"
                                    "padding-top:%2px;")
                         .arg(Theme::TextMuted).arg(topPad));
    lay->addWidget(l);
    return l;
}

QLabel *InspectorPanel::addRow(QVBoxLayout *lay, const QString &key)
{
    auto *row = new QWidget(this);
    auto *h = new QGridLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);

    auto *k = new QLabel(key, row);
    // L2 行键：中性灰、常规字重
    k->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;")
                         .arg(Theme::TextSecond));
    auto *v = new QLabel(QStringLiteral("—"), row);
    // L3 行值：最亮、最大、半粗 —— 真正要读的那一层
    v->setStyleSheet(QStringLiteral("color:%1;font-size:13px;font-weight:600;"
                                    "background:transparent;")
                         .arg(Theme::TextPrimary));
    v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    h->addWidget(k, 0, 0);
    h->addWidget(v, 0, 1);
    h->setColumnStretch(1, 1);
    lay->addWidget(row);
    return v;
}

void InspectorPanel::setSource(const QString &name, int w, int h, double fps, qint64 durationMs)
{
    if (m_name) m_name->setText(name.isEmpty()
                                    ? lang(QStringLiteral("未打开"), QStringLiteral("None"))
                                    : name);
    if (m_size) m_size->setText(w > 0 && h > 0 ? QStringLiteral("%1×%2").arg(w).arg(h)
                                               : QStringLiteral("—"));
    if (m_fps)  m_fps->setText(fps > 0.0 ? QStringLiteral("%1").arg(fps, 0, 'f', 2)
                                         : QStringLiteral("—"));
    if (m_dur)  m_dur->setText(fmtLen(durationMs));
}

void InspectorPanel::setCalibration(const QString &state, bool ok)
{
    if (!m_cal)
        return;
    m_cal->setText(state);
    m_cal->setStyleSheet(QStringLiteral("color:%1;font-size:13px;font-weight:600;"
                                        "background:transparent;")
                             .arg(ok ? Theme::Success : Theme::TextSecond));
}

void InspectorPanel::setCursor(qint64 ms)
{
    if (m_cursor)
        m_cursor->setText(fmtMs(ms));
}

void InspectorPanel::setRoi(int rects, int polys)
{
    if (m_roi)
        m_roi->setText(QStringLiteral("%1 / %2").arg(rects).arg(polys));
}

QGridLayout *InspectorPanel::makeChipGrid(const QList<QWidget *> &widgets, bool primary)
{
    // 紧凑 chip 样式：宽度交给网格拉伸，字号降一号，高度可控
    const QString chip = QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:4px;"
        "font-size:11px;padding:2px 0;min-width:0;}"
        "QPushButton:hover{border-color:%4;color:%5;}"
        "QPushButton:pressed{background:%6;}"
        "QPushButton:checked{background:%4;color:%7;border-color:%4;font-weight:600;}"
        "QPushButton:disabled{color:%8;border-color:%1;}")
        .arg(Theme::BgCard, Theme::TextSecond, Theme::Border, Theme::Accent,
             Theme::TextPrimary, Theme::BgPressed, Theme::AccentOnDark, Theme::TextMuted);
    const QString chipPrimary = QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %1;border-radius:4px;"
        "font-size:11px;font-weight:600;padding:2px 0;min-width:0;}"
        "QPushButton:hover{background:%3;}"
        "QPushButton:pressed{background:%4;}"
        "QPushButton:disabled{background:%5;color:%6;border-color:%5;}")
        .arg(Theme::Accent, Theme::AccentOnDark, Theme::AccentHover,
             Theme::AccentPress, Theme::BgPressed, Theme::TextMuted);

    // 3 个控件就排一行（模式三连），其余一律两列
    const int cols = (widgets.size() >= 3) ? 3 : 2;
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);

    for (int i = 0; i < widgets.size(); ++i) {
        QWidget *w = widgets.at(i);
        if (!w)
            continue;
        w->setParent(this);                     // 从工具栏/隐藏壳接管
        w->setCursor(Qt::PointingHandCursor);
        w->setFocusPolicy(Qt::NoFocus);         // 不抢全局快捷键
        w->setStyleSheet((primary && i == 0) ? chipPrimary : chip);
        w->setMinimumHeight(20);
        w->setMaximumHeight(24);
        const int row = i / cols;
        const int col = i % cols;
        const int span = (cols > 1 && i == widgets.size() - 1 && col == 0) ? cols : 1;
        grid->addWidget(w, row, col, 1, span);
        w->show();                              // setParent 会隐藏，必须显式 show
    }
    for (int c = 0; c < cols; ++c)
        grid->setColumnStretch(c, 1);
    return grid;
}

void InspectorPanel::addToolGroup(const QString &label, const QList<QWidget *> &widgets,
                                  bool primary)
{
    if (!m_toolLay || widgets.isEmpty())
        return;

    // 分组小标签（比分区标题小一号，不抢眼）
    if (!label.isEmpty()) {
        auto *sub = new QLabel(label, this);
        sub->setStyleSheet(QStringLiteral("color:%1;font-size:10px;letter-spacing:.6px;"
                                          "font-weight:600;background:transparent;")
                               .arg(Theme::TextMuted));
        m_toolLay->addWidget(sub);
    }
    m_toolLay->addLayout(makeChipGrid(widgets, primary));
}

void InspectorPanel::setCaptureWidgets(const QList<QWidget *> &widgets)
{
    if (!m_captureLay || widgets.isEmpty())
        return;
    m_captureLay->addLayout(makeChipGrid(widgets, false));
}

void InspectorPanel::setDisplayWidgets(const QList<QWidget *> &widgets)
{
    if (!m_displayLay || widgets.isEmpty())
        return;
    m_displayLay->insertLayout(0, makeChipGrid(widgets, false));   // 排在放大镜之前
}

void InspectorPanel::setMagnifier(bool on)
{
    if (!m_magBtn)
        return;
    QSignalBlocker b(m_magBtn);
    m_magBtn->setChecked(on);
}

} // namespace ui
