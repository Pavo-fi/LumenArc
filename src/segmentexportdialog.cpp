#include "segmentexportdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "i18n.h"
#include "theme.h"

SegmentExportDialog::SegmentExportDialog(const speedplan::SpeedPlan &plan,
                                         qint64 cursorMs,
                                         const QString &suggestedPath,
                                         QWidget *parent,
                                         bool wallEpoch)
    : QDialog(parent)
    , m_plan(plan)
    , m_cursorMs(cursorMs)
    , m_wallEpoch(wallEpoch)
{
    m_plan.normalize();
    setWindowTitle(lang("导出选段视频", "Export Segment Clip"));
    setMinimumWidth(640);
    // 非模态浮窗（用户反馈②③）：可最小化、不挡主窗口操作
    setModal(false);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint
                   | Qt::WindowStaysOnTopHint);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(8);

    m_summary = new QLabel(this);
    m_summary->setStyleSheet("color: " + Theme::TextPrimary + ";");
    lay->addWidget(m_summary);

    auto *hint = new QLabel(
        lang("分段变速：不关键的段调快掠过，关键的段常速或慢放。\n"
             "初值已按选段内标签自动分段；本窗口不锁播放——边播边分段。",
             "Per-segment speed: skim unimportant parts, keep key parts normal/slow.\n"
             "Initial splits come from in-selection labels; this panel is modeless."),
        this);
    hint->setStyleSheet("color: " + Theme::TextSecond + ";");
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({lang("段", "Seg"),
                                        m_wallEpoch ? lang("区间（墙钟）", "Range (wall)")
                                                    : lang("区间（流内）", "Range (stream)"),
                                        lang("倍速", "Speed")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setMaximumHeight(220);
    lay->addWidget(m_table);

    auto *splitRow = new QHBoxLayout();
    auto *splitBtn = new QPushButton(lang("在当前游标处分段", "Split at cursor"), this);
    splitBtn->setToolTip(lang("以当前播放位置为边界把所在段一分为二（游标实时跟随播放）",
                              "Split the segment at current play position (live cursor)"));
    connect(splitBtn, &QPushButton::clicked, this, [this]() {
        if (m_cursorMs <= m_plan.aMs || m_cursorMs >= m_plan.bMs) {
            m_resultLabel->setStyleSheet("color: " + Theme::Accent + ";");
            m_resultLabel->setText(lang("游标不在选段内，无法分段。",
                                        "Cursor is outside the selection."));
            return;
        }
        m_plan.splits.append(m_cursorMs);
        m_plan.normalize();
        rebuildTable();
        updateSummary();
    });
    splitRow->addWidget(splitBtn);
    auto *removeBtn = new QPushButton(lang("删除最后一个分段点", "Remove last split"), this);
    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        if (m_plan.splits.isEmpty())
            return;
        m_plan.splits.removeLast();
        m_plan.normalize();
        rebuildTable();
        updateSummary();
    });
    splitRow->addWidget(removeBtn);
    splitRow->addStretch();
    lay->addLayout(splitRow);

    m_osdCheck = new QCheckBox(
        lang("烧录信息角标（演示副本 · 倍速 · 时刻 · 案件号）",
             "Burn info overlay (demo copy · speed · time · case)"), this);
    m_osdCheck->setChecked(true);
    lay->addWidget(m_osdCheck);

    // v1.15.3：沿用上次变速设置的醒目提示条 + 一键恢复原速
    auto *lastRow = new QHBoxLayout();
    m_lastHint = new QLabel(this);
    m_lastHint->setWordWrap(true);
    m_lastHint->setVisible(false);
    m_lastHint->setStyleSheet(QStringLiteral(
        "QLabel { color: #8a5a00; background: #fff3d6; border: 1px solid #e0b060;"
        " border-radius: 4px; padding: 4px 8px; font-weight: bold; }"));
    lastRow->addWidget(m_lastHint, 1);
    m_restoreBtn = new QPushButton(lang("恢复上次变速", "Restore last speeds"), this);
    m_restoreBtn->setVisible(false);
    connect(m_restoreBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastPlan.isValid())
            return;
        m_plan = m_lastPlan;
        m_plan.normalize();
        rebuildTable();
        updateSummary();
        m_lastHint->setVisible(false);
        m_restoreBtn->setVisible(false);
        m_resultLabel->clear();
    });
    lastRow->addWidget(m_restoreBtn);
    lay->addLayout(lastRow);

    auto *pathRow = new QHBoxLayout();
    pathRow->addWidget(new QLabel(lang("输出：", "Output:"), this));
    m_pathEdit = new QLineEdit(suggestedPath, this);
    pathRow->addWidget(m_pathEdit, 1);
    auto *browse = new QPushButton(lang("浏览…", "Browse…"), this);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getSaveFileName(
            this, lang("导出选段视频", "Export Segment Clip"),
            m_pathEdit->text(), lang("MP4 视频 (*.mp4)", "MP4 video (*.mp4)"));
        if (!f.isEmpty())
            m_pathEdit->setText(f.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)
                                    ? f : f + QStringLiteral(".mp4"));
    });
    pathRow->addWidget(browse);
    lay->addLayout(pathRow);

    // ---- 进度区（导出中显示；用户反馈③：进度内嵌不阻塞）----
    m_progressBox = new QWidget(this);
    auto *pgLay = new QHBoxLayout(m_progressBox);
    pgLay->setContentsMargins(0, 0, 0, 0);
    m_progressBar = new QProgressBar(m_progressBox);
    m_progressBar->setTextVisible(true);
    pgLay->addWidget(m_progressBar, 1);
    auto *cancelBtn = new QPushButton(lang("取消导出", "Cancel"), m_progressBox);
    connect(cancelBtn, &QPushButton::clicked, this,
            [this]() { emit cancelRequested(); });
    pgLay->addWidget(cancelBtn);
    m_progressBox->setVisible(false);
    lay->addWidget(m_progressBox);

    m_resultLabel = new QLabel(this);
    m_resultLabel->setWordWrap(true);
    lay->addWidget(m_resultLabel);
    // 成功后显示「打开所在文件夹」（真机反馈：导完想立刻定位产物）
    m_openFolderBtn = new QPushButton(lang("📂 打开所在文件夹",
                                           "📂 Open containing folder"), this);
    m_openFolderBtn->setVisible(false);
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        const QString path = m_openFolderBtn->property("outPath").toString();
        if (path.isEmpty())
            return;
        if (!QProcess::startDetached(QStringLiteral("explorer.exe"),
                {QStringLiteral("/select,"), QDir::toNativeSeparators(path)}))
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                QFileInfo(path).absolutePath()));
    });
    lay->addWidget(m_openFolderBtn);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_exportBtn = new QPushButton(lang("开始导出", "Export"), this);
    m_exportBtn->setStyleSheet(
        "QPushButton { background: " + Theme::Accent + "; color: " + Theme::AccentOnDark
        + "; font-weight: bold; padding: 6px 18px; border-radius: 6px; }"
        "QPushButton:hover { background: " + Theme::AccentHover + "; }"
        "QPushButton:disabled { background: " + Theme::BgPressed + "; color: "
        + Theme::TextMuted + "; }");
    connect(m_exportBtn, &QPushButton::clicked, this, [this]() {
        if (m_running)
            return;
        if (m_pathEdit->text().trimmed().isEmpty()) {
            m_resultLabel->setStyleSheet("color: " + Theme::Accent + ";");
            m_resultLabel->setText(lang("请选择输出路径。",
                                        "Please choose an output path."));
            return;
        }
        emit exportRequested(m_plan, m_osdCheck->isChecked(),
                             m_pathEdit->text().trimmed());
    });
    btnRow->addWidget(m_exportBtn);
    auto *closeBtn = new QPushButton(lang("关闭", "Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::hide);
    btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    rebuildTable();
    updateSummary();
}

QString SegmentExportDialog::formatMs(qint64 ms)
{
    return QStringLiteral("%1:%2:%3.%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString SegmentExportDialog::fmtTime(qint64 ms) const
{
    if (m_wallEpoch)
        return QDateTime::fromMSecsSinceEpoch(ms, Qt::LocalTime)
            .toString(QStringLiteral("MM-dd HH:mm:ss"));
    return formatMs(ms);
}

bool SegmentExportDialog::burnOsd() const { return m_osdCheck->isChecked(); }
QString SegmentExportDialog::outputPath() const { return m_pathEdit->text().trimmed(); }

void SegmentExportDialog::setPlan(const speedplan::SpeedPlan &plan,
                                  const QString &suggestedPath)
{
    m_plan = plan;
    m_plan.normalize();
    m_pathEdit->setText(suggestedPath);
    m_resultLabel->clear();
    // 新选段不沿用上次设置：收起提示条（恢复钮保留，点恢复会重新出现？
    // 不——恢复只作用于本面板当前计划，恢复后即收起，语义见 setLastPlan）
    m_lastHint->setVisible(false);
    m_restoreBtn->setVisible(false);
    rebuildTable();
    updateSummary();
}

void SegmentExportDialog::setLastPlan(const speedplan::SpeedPlan &plan)
{
    m_lastPlan = plan;
    if (!plan.isValid())
        return;
    // 同选段沿用了上次变速（非全 1x）→ 醒目提示，避免“导出的文件不对”
    bool hasNonOne = false;
    for (double r : plan.rates)
        if (qAbs(r - 1.0) > 1e-6) {
            hasNonOne = true;
            break;
        }
    if (!hasNonOne)
        return;
    m_lastHint->setText(lang(
        "⚠ 本次选段与上次导出相同，已默认沿用上次的变速设置"
        "（含非原速段）。如需原速请点「恢复上次变速」右侧按钮，"
        "或在表格中把速率改为 1.00。",
        "Same range as last export: last speed plan reused. "
        "Click Restore or set rates to 1.00 for realtime."));
    m_lastHint->setVisible(true);
    m_restoreBtn->setVisible(true);
}

void SegmentExportDialog::setExportRunning(bool running, int totalFrames)
{
    m_running = running;
    if (running && m_openFolderBtn)
        m_openFolderBtn->setVisible(false);   // 新一轮导出收起旧产物入口
    m_table->setEnabled(!running);
    m_exportBtn->setEnabled(!running);
    m_progressBox->setVisible(running);
    if (running) {
        m_progressBar->setRange(0, qMax(1, totalFrames));
        m_progressBar->setValue(0);
        m_resultLabel->clear();
    }
}

void SegmentExportDialog::setProgress(int done, int total)
{
    m_progressBar->setRange(0, qMax(1, total));
    m_progressBar->setValue(done);
}

void SegmentExportDialog::setResult(bool ok, const QString &msg)
{
    setExportRunning(false);
    if (ok) {
        m_resultLabel->setStyleSheet("color: " + Theme::Success + ";");
        m_resultLabel->setText(lang("✅ 已导出：", "✅ Exported: ") + msg);
        m_openFolderBtn->setProperty("outPath", msg);
        m_openFolderBtn->setVisible(true);
    } else if (msg == QStringLiteral("已取消")) {
        m_resultLabel->setStyleSheet("color: " + Theme::TextSecond + ";");
        m_resultLabel->setText(lang("已取消。", "Cancelled."));
    } else {
        m_resultLabel->setStyleSheet("color: " + Theme::Danger + ";");
        m_resultLabel->setText(lang("❌ 导出失败：", "❌ Failed: ") + msg);
    }
    if (!ok && m_openFolderBtn)
        m_openFolderBtn->setVisible(false);
}

void SegmentExportDialog::rebuildTable()
{
    const int n = m_plan.segmentCount();
    m_table->setRowCount(n);
    const auto rates = speedplan::allowedRates();
    for (int i = 0; i < n; ++i) {
        qint64 s, e;
        m_plan.segmentBounds(i, &s, &e);
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_table->setItem(i, 1, new QTableWidgetItem(
            fmtTime(s) + QStringLiteral(" → ") + fmtTime(e)));
        auto *combo = new QComboBox(m_table);
        for (double r : rates) {
            combo->addItem(QStringLiteral("%1x").arg(r, 0, 'g', 3), r);
            if (qAbs(r - m_plan.segmentRate(i)) < 1e-9)
                combo->setCurrentIndex(combo->count() - 1);
        }
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, i, combo](int) {
                    m_plan.rates[i] = combo->currentData().toDouble();
                    updateSummary();
                });
        m_table->setCellWidget(i, 2, combo);
    }
}

void SegmentExportDialog::updateSummary()
{
    const double outSec = m_plan.outputDurationMs() / 1000.0;
    const double srcSec = (m_plan.bMs - m_plan.aMs) / 1000.0;
    m_summary->setText(lang("选段 %1 → %2（源 %3 秒，导出时长约 %4 秒）",
                            "Selection %1 → %2 (source %3 s, export ~%4 s)")
                           .arg(fmtTime(m_plan.aMs), fmtTime(m_plan.bMs))
                           .arg(srcSec, 0, 'f', 1)
                           .arg(outSec, 0, 'f', 1));
}
