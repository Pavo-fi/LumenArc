#include "composeexportdialog.h"

#include "app/case_manager.h"
#include "domain/case_model.h"
#include "domain/timeline_model.h"
#include "infrastructure/credential_store.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString timeToText(qint64 ms) { return ComposeExportDialog::formatMs(ms); }
}  // namespace

bool ComposeExportDialog::wantPanels() const {
    return m_panelsCheck && m_panelsCheck->isChecked();
}

ComposeExportDialog::ComposeExportDialog(CaseManager *cm, const QString &currentVideo,
                                         qint64 aMs, qint64 bMs, qint64 cursorMs,
                                         double fps, QWidget *parent)
    : QDialog(parent), m_cm(cm), m_currentVideo(currentVideo),
      m_aMs(aMs), m_bMs(bMs), m_cursorMs(cursorMs), m_fps(fps > 0 ? fps : 25.0) {
    setWindowTitle(QStringLiteral("合成导出"));
    setModal(false);
    setMinimumWidth(760);
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);

    auto *root = new QVBoxLayout(this);

    // ---- 片段序列 ----
    auto *segBox = new QGroupBox(QStringLiteral("片段序列（按顺序拼接）"), this);
    auto *sv = new QVBoxLayout(segBox);
    m_table = new QTableWidget(0, 5, segBox);
    m_table->setHorizontalHeaderLabels({QStringLiteral("源视频"), QStringLiteral("入点"),
                                        QStringLiteral("出点"), QStringLiteral("速率"),
                                        QStringLiteral("时长")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    sv->addWidget(m_table);
    auto *btnRow = new QHBoxLayout();
    auto *addBtn = new QPushButton(QStringLiteral("添加当前选段"), segBox);
    auto *addCursorBtn = new QPushButton(QStringLiteral("添加游标起 10s"), segBox);
    auto *rmBtn = new QPushButton(QStringLiteral("删除"), segBox);
    auto *upBtn = new QPushButton(QStringLiteral("上移"), segBox);
    auto *dnBtn = new QPushButton(QStringLiteral("下移"), segBox);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(addCursorBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(upBtn);
    btnRow->addWidget(dnBtn);
    btnRow->addWidget(rmBtn);
    sv->addLayout(btnRow);
    root->addWidget(segBox);
    connect(addBtn, &QPushButton::clicked, this, &ComposeExportDialog::onAddSegment);
    connect(addCursorBtn, &QPushButton::clicked, this, [this]() {
        Row r;
        r.sourcePath = m_currentVideo;
        r.inMs = m_cursorMs;
        r.outMs = m_cursorMs + 10000;
        m_rows << r;
        rebuildTable();
    });
    connect(rmBtn, &QPushButton::clicked, this, &ComposeExportDialog::onRemoveSegment);
    connect(upBtn, &QPushButton::clicked, this, [this]() { onMoveSegment(-1); });
    connect(dnBtn, &QPushButton::clicked, this, [this]() { onMoveSegment(1); });
    connect(m_table, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *item) {
                if (!item || m_running)
                    return;
                const int row = item->row();
                if (row < 0 || row >= m_rows.size())
                    return;
                bool ok = false;
                if (item->column() == 1 || item->column() == 2) {
                    const qint64 v = parseMs(item->text(), &ok);
                    if (ok) {
                        if (item->column() == 1) m_rows[row].inMs = v;
                        else                     m_rows[row].outMs = v;
                    }
                } else if (item->column() == 3) {
                    const double r = item->text().toDouble(&ok);
                    if (ok && r >= 0.24 && r <= 8.01) m_rows[row].rate = r;
                }
                refreshRowDurations();
            });

    // ---- 导出模式 ----
    auto *modeBox = new QGroupBox(QStringLiteral("导出模式"), this);
    auto *mv = new QVBoxLayout(modeBox);
    m_evidenceRadio = new QRadioButton(
        QStringLiteral("证据片段（无损直拷，像素零改动，附完整性清单 JSON）"), modeBox);
    m_demoRadio = new QRadioButton(
        QStringLiteral("分析演示片（重编码烧录：校正时间角标 / 案件号 / 图表面板）"), modeBox);
    m_demoRadio->setChecked(true);
    mv->addWidget(m_evidenceRadio);
    mv->addWidget(m_demoRadio);
    auto *optRow = new QHBoxLayout();
    m_osdCheck = new QCheckBox(QStringLiteral("校正时间角标"), modeBox);
    m_osdCheck->setChecked(true);
    m_caseNoCheck = new QCheckBox(QStringLiteral("案件号"), modeBox);
    m_caseNoCheck->setChecked(true);
    m_panelsCheck = new QCheckBox(QStringLiteral("图表面板（曲线/语谱/放大镜）"), modeBox);
    m_panelsCheck->setChecked(true);
    optRow->addWidget(m_osdCheck);
    optRow->addWidget(m_caseNoCheck);
    optRow->addWidget(m_panelsCheck);
    optRow->addStretch(1);
    mv->addLayout(optRow);
    auto *wmNote = new QLabel(
        QStringLiteral("※ 分析演示片将强制叠加「分析演示材料 · 非原始证据」角标；"
                       "图表面板仅在单片段、源为当前视频且速率 1x 时可用。"), modeBox);
    wmNote->setWordWrap(true);
    wmNote->setStyleSheet(QStringLiteral("color:#888;"));
    mv->addWidget(wmNote);
    root->addWidget(modeBox);
    connect(m_evidenceRadio, &QRadioButton::toggled, this,
            &ComposeExportDialog::onModeChanged);

    // ---- 输出 ----
    auto *outBox = new QGroupBox(QStringLiteral("输出"), this);
    auto *ov = new QVBoxLayout(outBox);
    auto *pathRow = new QHBoxLayout();
    m_outPath = new QLineEdit(outBox);
    auto *browseBtn = new QPushButton(QStringLiteral("浏览…"), outBox);
    pathRow->addWidget(m_outPath, 1);
    pathRow->addWidget(browseBtn);
    ov->addLayout(pathRow);
    m_progress = new QProgressBar(outBox);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    ov->addWidget(m_progress);
    m_status = new QLabel(QString(), outBox);
    m_status->setWordWrap(true);
    ov->addWidget(m_status);
    root->addWidget(outBox);
    connect(browseBtn, &QPushButton::clicked, this,
            &ComposeExportDialog::onBrowseOutput);

    auto *foot = new QHBoxLayout();
    m_startBtn = new QPushButton(QStringLiteral("开始导出"), this);
    m_startBtn->setDefault(true);
    m_cancelBtn = new QPushButton(QStringLiteral("取消导出"), this);
    m_cancelBtn->setEnabled(false);
    m_closeBtn = new QPushButton(QStringLiteral("关 闭"), this);
    foot->addStretch(1);
    foot->addWidget(m_startBtn);
    foot->addWidget(m_cancelBtn);
    foot->addWidget(m_closeBtn);
    root->addLayout(foot);
    connect(m_startBtn, &QPushButton::clicked, this,
            &ComposeExportDialog::onStartExport);
    connect(m_cancelBtn, &QPushButton::clicked, this,
            [this]() { emit cancelRequested(); });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_running)
            hide();
    });

    // 初值：当前选段作为第一条片段
    if (!m_currentVideo.isEmpty() && m_bMs > m_aMs) {
        Row r;
        r.sourcePath = m_currentVideo;
        r.inMs = m_aMs;
        r.outMs = m_bMs;
        m_rows << r;
    }
    rebuildTable();
    updateSuggestedPath();
    onModeChanged();
}

void ComposeExportDialog::refreshContext(const QString &currentVideo, qint64 aMs,
                                         qint64 bMs, qint64 cursorMs, double fps) {
    m_currentVideo = currentVideo;
    m_aMs = aMs;
    m_bMs = bMs;
    m_cursorMs = cursorMs;
    if (fps > 0) m_fps = fps;
    if (m_rows.isEmpty() && !m_currentVideo.isEmpty() && m_bMs > m_aMs) {
        Row r;
        r.sourcePath = m_currentVideo;
        r.inMs = m_aMs;
        r.outMs = m_bMs;
        m_rows << r;
        rebuildTable();
    }
    updateSuggestedPath();
}

QString ComposeExportDialog::formatMs(qint64 ms) {
    const qint64 h = ms / 3600000, m = ms / 60000 % 60, s = ms / 1000 % 60,
                 msec = ms % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(msec, 3, 10, QLatin1Char('0'));
}

qint64 ComposeExportDialog::parseMs(const QString &text, bool *ok) {
    if (ok) *ok = false;
    const QString t = text.trimmed();
    static const QRegularExpression re(
        QStringLiteral("^(?:(\\d+):)?([0-5]?\\d):([0-5]?\\d)(?:\\.(\\d{1,3}))?$"));
    const auto m = re.match(t);
    if (!m.hasMatch()) {
        bool numOk = false;
        const double sec = t.toDouble(&numOk);   // 裸秒数兜底
        if (numOk && sec >= 0.0) {
            if (ok) *ok = true;
            return qint64(sec * 1000.0);
        }
        return 0;
    }
    const qint64 h = m.captured(1).isEmpty() ? 0 : m.captured(1).toLongLong();
    const qint64 mm = m.captured(2).toLongLong();
    const qint64 ss = m.captured(3).toLongLong();
    QString frac = m.captured(4);
    while (frac.length() < 3) frac += QLatin1Char('0');
    if (ok) *ok = true;
    return h * 3600000 + mm * 60000 + ss * 1000 + frac.toLongLong();
}

QString ComposeExportDialog::videoDisplayName(const QString &path) const {
    if (m_cm && m_cm->isOpen()) {
        if (const CaseVideoRef *v = m_cm->videoByPath(path)) {
            if (!v->cameraLabel.isEmpty())
                return QStringLiteral("%1（%2）").arg(v->cameraLabel,
                                                     QFileInfo(path).fileName());
        }
    }
    return QFileInfo(path).fileName();
}

QString ComposeExportDialog::effectivePath(const QString &path) const {
    if (m_cm && m_cm->isOpen()) {
        if (const CaseVideoRef *v = m_cm->videoByPath(path))
            return m_cm->effectivePathFor(*v);
    }
    return path;
}

void ComposeExportDialog::rebuildTable() {
    m_table->blockSignals(true);
    m_table->setRowCount(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        // 源视频列：案内视频下拉（含当前视频），独立模式只读文本
        auto *combo = new QComboBox(m_table);
        QStringList paths;
        if (m_cm && m_cm->isOpen())
            for (const auto &v : m_cm->meta().videos)
                paths << v.originalPath;
        if (!m_currentVideo.isEmpty() && !paths.contains(m_currentVideo))
            paths << m_currentVideo;
        if (paths.isEmpty() && !r.sourcePath.isEmpty())
            paths << r.sourcePath;
        for (const QString &p : paths)
            combo->addItem(videoDisplayName(p), p);
        const int idx = paths.indexOf(r.sourcePath);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, i](int ci) {
                    if (i < m_rows.size()) {
                        const QString p = static_cast<QComboBox *>(
                            m_table->cellWidget(i, 0))->currentData().toString();
                        if (!p.isEmpty()) m_rows[i].sourcePath = p;
                        Q_UNUSED(ci);
                    }
                });
        m_table->setCellWidget(i, 0, combo);

        auto *inItem = new QTableWidgetItem(timeToText(r.inMs));
        auto *outItem = new QTableWidgetItem(timeToText(r.outMs));
        auto *rateItem = new QTableWidgetItem(QString::number(r.rate, 'g', 3));
        auto *durItem = new QTableWidgetItem();
        durItem->setFlags(durItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, inItem);
        m_table->setItem(i, 2, outItem);
        m_table->setItem(i, 3, rateItem);
        m_table->setItem(i, 4, durItem);
    }
    m_table->blockSignals(false);
    refreshRowDurations();
    updateSuggestedPath();
}

void ComposeExportDialog::refreshRowDurations() {
    qint64 totalOut = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        const double outMs = (r.outMs > r.inMs)
            ? double(r.outMs - r.inMs) / qMax(0.01, r.rate) : 0.0;
        totalOut += qint64(outMs);
        if (auto *it = m_table->item(i, 4))
            it->setText(outMs > 0 ? timeToText(qint64(outMs))
                                  : QStringLiteral("（区间非法）"));
    }
    setWindowTitle(QStringLiteral("合成导出（共 %1 段，输出时长 %2）")
                       .arg(m_rows.size())
                       .arg(totalOut > 0 ? timeToText(totalOut)
                                         : QStringLiteral("--")));
}

void ComposeExportDialog::updateSuggestedPath() {
    if (m_running)
        return;
    QString dir;
    if (m_cm && m_cm->isOpen()) {
        dir = m_cm->caseDir() + QStringLiteral("/exports");
    } else if (!m_currentVideo.isEmpty()) {
        dir = QFileInfo(m_currentVideo).absolutePath();
    }
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("MMdd_HHmmss"));
    const QString prefix = m_evidenceRadio && m_evidenceRadio->isChecked()
        ? QStringLiteral("LAEvidence") : QStringLiteral("LACompose");
    m_outPath->setText(QDir(dir).filePath(
        QStringLiteral("%1_%2.mp4").arg(prefix, stamp)));
}

void ComposeExportDialog::onModeChanged() {
    const bool demo = m_demoRadio->isChecked();
    m_osdCheck->setEnabled(demo);
    m_caseNoCheck->setEnabled(demo);
    m_panelsCheck->setEnabled(demo);
    updateSuggestedPath();
}

void ComposeExportDialog::onAddSegment() {
    if (m_currentVideo.isEmpty())
        return;
    Row r;
    r.sourcePath = m_currentVideo;
    r.inMs = m_aMs;
    r.outMs = m_bMs > m_aMs ? m_bMs : m_aMs + 10000;
    m_rows << r;
    rebuildTable();
}

void ComposeExportDialog::onRemoveSegment() {
    const int row = m_table->currentRow();
    if (row >= 0 && row < m_rows.size()) {
        m_rows.removeAt(row);
        rebuildTable();
    }
}

void ComposeExportDialog::onMoveSegment(int delta) {
    const int row = m_table->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || row >= m_rows.size() || to >= m_rows.size())
        return;
    m_rows.swapItemsAt(row, to);
    rebuildTable();
    m_table->setCurrentCell(to, 0);
}

void ComposeExportDialog::onBrowseOutput() {
    const QString p = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出到"), m_outPath->text(),
        QStringLiteral("MP4 (*.mp4)"));
    if (!p.isEmpty())
        m_outPath->setText(p);
}

bool ComposeExportDialog::collectSegments(
    QVector<SegmentExportEngine::Params::ComposeSeg> *out, QString *err) {
    out->clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        if (r.sourcePath.isEmpty() || r.outMs <= r.inMs) {
            if (err) *err = QStringLiteral("第 %1 段区间非法").arg(i + 1);
            return false;
        }
        SegmentExportEngine::Params::ComposeSeg seg;
        seg.sourcePath = effectivePath(r.sourcePath);
        seg.inMs = r.inMs;
        seg.outMs = r.outMs;
        seg.rate = r.rate;
        out->append(seg);
    }
    if (out->isEmpty()) {
        if (err) *err = QStringLiteral("片段序列为空，请先「添加当前选段」");
        return false;
    }
    return true;
}

SegmentExportEngine::Params ComposeExportDialog::buildParams(QString *err) {
    SegmentExportEngine::Params pp;
    if (!collectSegments(&pp.segments, err))
        return pp;
    pp.outputPath = m_outPath->text().trimmed();
    if (pp.outputPath.isEmpty()) {
        if (err) *err = QStringLiteral("请选择输出路径");
        pp.segments.clear();
        return pp;
    }
    pp.outFps = m_fps;
    pp.canvas = QSize(1920, 1080);
    pp.evidenceCopy = m_evidenceRadio->isChecked();
    pp.demoWatermark = !pp.evidenceCopy;
    pp.burnOsd = !pp.evidenceCopy && m_osdCheck->isChecked();
    if (m_cm && m_cm->isOpen())
        pp.caseLabel = m_caseNoCheck->isChecked() ? m_cm->meta().caseNo : QString();
    // 逐文件校正时间表（演示模式 OSD 用）
    if (!pp.evidenceCopy && pp.burnOsd) {
        QStringList seen;
        for (const auto &s : pp.segments) {
            if (seen.contains(s.sourcePath))
                continue;
            seen << s.sourcePath;
            // 校正存在 .vla 侧车（vlaPathFor 对案内/独立路径都有分流）
            const QString vlaPath = m_cm
                ? m_cm->vlaPathFor(s.sourcePath)
                : s.sourcePath + QStringLiteral(".vla");
            const TimeCalibration cal = TimelineModel::peekCalibrationFromVla(vlaPath);
            if (cal.isValid())
                pp.calibrationByPath.insert(s.sourcePath, cal);
        }
    }
    // 证据清单签署人（账号档案，署名写死策略 v1.4）
    const Credential cred = CredentialStore::load();
    pp.operatorName = cred.name;
    pp.operatorOrg = cred.org;
    return pp;
}

void ComposeExportDialog::onStartExport() {
    if (m_running)
        return;
    QString err;
    SegmentExportEngine::Params pp = buildParams(&err);
    if (pp.segments.isEmpty()) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(err.isEmpty() ? QStringLiteral("参数非法") : err);
        return;
    }
    m_status->clear();
    emit exportRequested(pp);
}

void ComposeExportDialog::setExportRunning(bool running, int totalFrames) {
    m_running = running;
    m_startBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);
    m_table->setEnabled(!running);
    m_evidenceRadio->setEnabled(!running);
    m_demoRadio->setEnabled(!running);
    m_osdCheck->setEnabled(!running);
    m_caseNoCheck->setEnabled(!running);
    m_panelsCheck->setEnabled(!running);
    m_closeBtn->setEnabled(!running);
    if (running) {
        m_progress->setValue(0);
        m_progress->setMaximum(qMax(1, totalFrames));
        m_status->setStyleSheet(QStringLiteral("color:#666;"));
        m_status->setText(QStringLiteral("导出进行中…"));
    }
}

void ComposeExportDialog::setProgress(int done, int total) {
    if (total > 0)
        m_progress->setMaximum(total);
    m_progress->setValue(qMin(done, m_progress->maximum()));
}

void ComposeExportDialog::setResult(bool ok, const QString &msg) {
    m_running = false;
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_table->setEnabled(true);
    m_evidenceRadio->setEnabled(true);
    m_demoRadio->setEnabled(true);
    onModeChanged();
    m_closeBtn->setEnabled(true);
    if (ok) {
        m_status->setStyleSheet(QStringLiteral("color:#27ae60;"));
        m_status->setText(QStringLiteral("✔ 导出完成：%1").arg(msg));
    } else {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("✖ %1").arg(msg));
    }
}
