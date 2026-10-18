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
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(3);

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

    // 工具（与原型一致的 chip 行）
    addSection(lay, lang(QStringLiteral("工具"), QStringLiteral("Tool")));
    {
        auto *row = new QGridLayout();
        row->setSpacing(3);
        const char *zh[] = { "选择", "矩形", "多边形", "辅助线" };
        const char *en[] = { "Select", "Rect", "Polygon", "Guide" };
        const QString css = QStringLiteral(
            "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:4px;"
            "font-size:11px;padding:3px 0;}"
            "QPushButton:hover{border-color:%4;color:%5;}"
            "QPushButton:checked{background:%4;color:%6;border-color:%4;font-weight:600;}")
            .arg(Theme::BgCard, Theme::TextSecond, Theme::Border,
                 Theme::Accent, Theme::TextPrimary, Theme::BgApp);
        for (int i = 0; i < 4; ++i) {
            auto *b = new QPushButton(lang(QString::fromUtf8(zh[i]),
                                           QString::fromUtf8(en[i])), this);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);
            b->setStyleSheet(css);
            row->addWidget(b, 0, i);
            m_toolBtns.append(b);
            connect(b, &QPushButton::clicked, this, [this, i]() {
                setTool(i);
                emit toolRequested(i);
            });
        }
        lay->addLayout(row);
    }

    // 显示开关
    addSection(lay, lang(QStringLiteral("显示"), QStringLiteral("Display")));
    {
        const QString css = QStringLiteral(
            "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:4px;"
            "font-size:11px;padding:4px 0;text-align:left;padding-left:10px;}"
            "QPushButton:hover{border-color:%4;}"
            "QPushButton:checked{color:%5;border-color:%5;}")
            .arg(Theme::BgCard, Theme::TextSecond, Theme::Border,
                 Theme::Accent, Theme::Accent);
        m_magBtn = new QPushButton(lang(QStringLiteral("放大镜"), QStringLiteral("Magnifier")), this);
        m_ovlBtn = new QPushButton(lang(QStringLiteral("截图叠加"), QStringLiteral("Overlay")), this);
        for (QPushButton *b : { m_magBtn, m_ovlBtn }) {
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);
            b->setStyleSheet(css);
            lay->addWidget(b);
        }
        connect(m_magBtn, &QPushButton::toggled, this, &InspectorPanel::magnifierToggled);
        connect(m_ovlBtn, &QPushButton::toggled, this, &InspectorPanel::overlayToggled);
    }

    lay->addStretch(1);

    // 初始空态
    setSource(QString(), 0, 0, 0.0, 0);
    setCalibration(lang(QStringLiteral("未校时"), QStringLiteral("None")), false);
    setCursor(0);
    setRoi(0, 0);
    setTool(0);
}

QLabel *InspectorPanel::addSection(QVBoxLayout *lay, const QString &title)
{
    auto *l = new QLabel(title, this);
    l->setStyleSheet(QStringLiteral("color:%1;font-size:10px;letter-spacing:.6px;"
                                    "background:transparent;padding-top:8px;")
                         .arg(Theme::TextMuted));
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
    k->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;")
                         .arg(Theme::TextSecond));
    auto *v = new QLabel(QStringLiteral("—"), row);
    v->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;")
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
    m_cal->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;")
                             .arg(ok ? Theme::Success : Theme::TextMuted));
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

void InspectorPanel::setTool(int tool)
{
    for (int i = 0; i < m_toolBtns.size(); ++i)
        m_toolBtns[i]->setChecked(i == tool);
}

void InspectorPanel::setMagnifier(bool on)
{
    if (!m_magBtn)
        return;
    QSignalBlocker b(m_magBtn);
    m_magBtn->setChecked(on);
}

void InspectorPanel::setOverlay(bool on)
{
    if (!m_ovlBtn)
        return;
    QSignalBlocker b(m_ovlBtn);
    m_ovlBtn->setChecked(on);
}

} // namespace ui
