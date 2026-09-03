/// @file composeworkbench.cpp — 合成导出工作台（窗口本体 + 片段块时间线控件）
#include "composeworkbench.h"

#include "app/case_manager.h"
#include "app/multicam_sync_service.h"
#include "camtilewidget.h"
#include "domain/case_model.h"
#include "domain/timeline_model.h"
#include "infrastructure/credential_store.h"
#include "infrastructure/ffmpeg_video_engine.h"
#include "theme.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

// ============================================================================
// 片段块时间线控件：块=片段，宽∝输出时长；点选/拖排序/双击精调/右键删/滚轮缩放
// ============================================================================
class ComposeTimelineWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ComposeTimelineWidget(QWidget *parent = nullptr)
        : QWidget(parent) {
        setMinimumHeight(96);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setContextMenuPolicy(Qt::DefaultContextMenu);
    }

    void setSegments(const QVector<SegmentExportEngine::Params::ComposeSeg> &segs,
                     const QStringList &names) {
        m_segs = segs;
        m_names = names;
        if (m_selected >= m_segs.size())
            m_selected = -1;
        update();
    }
    int selected() const { return m_selected; }
    qint64 totalOutMs() const {
        qint64 t = 0;
        for (int i = 0; i < m_segs.size(); ++i)
            t += qint64(segOutMs(i));
        return t;
    }

signals:
    void selectionChanged(int idx);
    void moveRequested(int from, int to);
    void editRequested(int idx);
    void removeRequested(int idx);

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(Theme::BgPanel));
        if (m_segs.isEmpty()) {
            p.setPen(QColor(Theme::TextMuted));
            p.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("时间线为空——预览里 I/O 打点后「+ 加入时间线」"));
            return;
        }
        const int laneH = height() - 34;
        int x = 8;
        for (int i = 0; i < m_segs.size(); ++i) {
            const int w = qMax(36, int(segOutMs(i) * m_pxPerMs));
            const QRect r(x, 22, w - 4, laneH);
            const bool lanes = m_segs[i].isLanes();
            const QColor base = lanes
                ? QColor(Theme::DataPalette[(i + 2) % Theme::DataPalette.size()])
                : QColor(Theme::DataPalette[i % Theme::DataPalette.size()]);
            p.setPen(Qt::NoPen);
            p.setBrush(base.darker(m_selected == i ? 110 : 165));
            p.drawRoundedRect(r, 5, 5);
            if (m_selected == i) {
                p.setPen(QPen(QColor(Theme::Accent), 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 5, 5);
            }
            p.setPen(QColor(Theme::TextPrimary));
            QFont f = p.font();
            f.setPixelSize(12);
            p.setFont(f);
            QString txt = QStringLiteral("%1  %2")
                              .arg(i + 1)
                              .arg(i < m_names.size() ? m_names[i] : QString());
            p.drawText(r.adjusted(8, 4, -6, -laneH / 2), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(txt, Qt::ElideRight, r.width() - 16));
            QString sub = QStringLiteral("%1%2")
                              .arg(ComposeWorkbenchWindow::formatMs(qint64(segOutMs(i))))
                              .arg(qAbs(m_segs[i].rate - 1.0) > 0.01
                                       ? QStringLiteral("  ×%1").arg(m_segs[i].rate, 0, 'g', 3)
                                       : QString());
            if (lanes)
                sub += QStringLiteral("  ▦%1路").arg(m_segs[i].lanes.size());
            p.setPen(QColor(Theme::TextSecond));
            p.drawText(r.adjusted(8, laneH / 2 - 6, -6, -4),
                       Qt::AlignVCenter | Qt::AlignLeft, sub);
            x += w;
        }
        // 末尾总时长
        p.setPen(QColor(Theme::TextMuted));
        p.drawText(QRect(8, 2, width() - 16, 18), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("共 %1 段 · 输出 %2")
                       .arg(m_segs.size())
                       .arg(ComposeWorkbenchWindow::formatMs(totalOutMs())));
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            const int idx = hitBlock(e->pos());
            if (idx != m_selected) {
                m_selected = idx;
                emit selectionChanged(idx);
                update();
            }
            m_dragFrom = idx;
            m_dragStart = e->pos();
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragFrom >= 0 && (e->pos() - m_dragStart).manhattanLength() > 24) {
            const int to = hitBlock(e->pos());
            if (to >= 0 && to != m_dragFrom) {
                emit moveRequested(m_dragFrom, to);
                m_dragFrom = to;
                m_selected = to;
                update();
            }
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *) override { m_dragFrom = -1; }

    void mouseDoubleClickEvent(QMouseEvent *e) override {
        const int idx = hitBlock(e->pos());
        if (idx >= 0)
            emit editRequested(idx);
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        const int idx = hitBlock(e->pos());
        if (idx < 0)
            return;
        m_selected = idx;
        emit selectionChanged(idx);
        update();
        QMenu menu(this);
        QAction *editAct = menu.addAction(QStringLiteral("编辑参数…"));
        QAction *rmAct = menu.addAction(QStringLiteral("删除该段"));
        QAction *sel = menu.exec(e->globalPos());
        if (sel == editAct)
            emit editRequested(idx);
        else if (sel == rmAct)
            emit removeRequested(idx);
    }

    void wheelEvent(QWheelEvent *e) override {
        const double factor = e->angleDelta().y() > 0 ? 1.25 : 0.8;
        m_pxPerMs = qBound(0.004, m_pxPerMs * factor, 2.0);
        update();
        e->accept();
    }

private:
    double segOutMs(int i) const {
        const auto &s = m_segs.at(i);
        return s.outMs > s.inMs ? double(s.outMs - s.inMs) / qMax(0.01, s.rate) : 0.0;
    }
    QRect blockRect(int i) const {
        const int laneH = height() - 34;
        int x = 8;
        for (int k = 0; k < i; ++k)
            x += qMax(36, int(segOutMs(k) * m_pxPerMs));
        return QRect(x, 22, qMax(36, int(segOutMs(i) * m_pxPerMs)) - 4, laneH);
    }
    int hitBlock(const QPoint &pos) const {
        for (int i = 0; i < m_segs.size(); ++i)
            if (blockRect(i).contains(pos))
                return i;
        return -1;
    }

    QVector<SegmentExportEngine::Params::ComposeSeg> m_segs;
    QStringList m_names;
    double m_pxPerMs = 0.05;   // 1s ≈ 50px
    int m_selected = -1;
    int m_dragFrom = -1;
    QPoint m_dragStart;
};

// ============================================================================
// 工作台窗口本体
// ============================================================================
namespace {
/// 引擎工厂：与主窗/多机窗同一 QSettings 硬解口径
FfmpegVideoEngine *makeEngine(QObject *parent) {
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    auto *e = new FfmpegVideoEngine(parent);
    e->setHardwareDecode(s.value(QStringLiteral("hwDecode"), true).toBool());
    e->setHardwareAdapter(s.value(QStringLiteral("hwAdapter"), -1).toInt());
    return e;
}
constexpr int kMaxLanes = 4;
}  // namespace

ComposeWorkbenchWindow::ComposeWorkbenchWindow(CaseManager *cm,
                                               const QString &currentVideo, double fps,
                                               QWidget *parent)
    : QDialog(parent), m_cm(cm), m_currentVideo(currentVideo),
      m_fps(fps > 0 ? fps : 25.0) {
    setWindowTitle(QStringLiteral("合成导出工作台"));
    setModal(false);
    resize(1280, 780);
    setMinimumSize(1024, 640);
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);

    auto *root = new QVBoxLayout(this);
    auto *mid = new QHBoxLayout();

    // ---- 左：素材树 ----
    auto *matBox = new QGroupBox(QStringLiteral("素材"), this);
    auto *matLay = new QVBoxLayout(matBox);
    m_matTree = new QTreeWidget(matBox);
    m_matTree->setHeaderHidden(true);
    m_matTree->setMinimumWidth(230);
    m_matTree->setMaximumWidth(300);
    matLay->addWidget(m_matTree);
    auto *matHint = new QLabel(QStringLiteral("单视频：点击预览\n多通道：勾选 2~4 路机位"), matBox);
    matHint->setStyleSheet(QStringLiteral("color:#888;"));
    matLay->addWidget(matHint);
    mid->addWidget(matBox);

    // ---- 中：预览 + 走带 + 时间线 ----
    auto *center = new QVBoxLayout();
    m_previewStack = new QStackedWidget(this);
    // 单路页
    auto *singlePage = new QWidget(this);
    auto *spLay = new QVBoxLayout(singlePage);
    spLay->setContentsMargins(0, 0, 0, 0);
    m_singleTile = new CamTileWidget(singlePage);
    m_singleTile->setLaneName(QString());
    m_singleTile->setOsdVisible(false);
    spLay->addWidget(m_singleTile);
    m_previewStack->addWidget(singlePage);
    // 多通道页
    m_multiPage = new QWidget(this);
    m_multiGrid = new QGridLayout(m_multiPage);
    m_multiGrid->setContentsMargins(0, 0, 0, 0);
    m_previewStack->addWidget(m_multiPage);
    center->addWidget(m_previewStack, 1);

    // 走带条
    auto *transport = new QHBoxLayout();
    m_playBtn = new QPushButton(QStringLiteral("▶ 播放"), this);
    m_playBtn->setFixedWidth(84);
    m_playBtn->setEnabled(false);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setEnabled(false);
    m_timeLabel = new QLabel(QStringLiteral("--:-- / --:--"), this);
    m_inBtn = new QPushButton(QStringLiteral("I 入点"), this);
    m_outBtn = new QPushButton(QStringLiteral("O 出点"), this);
    m_inBtn->setEnabled(false);
    m_outBtn->setEnabled(false);
    m_markLabel = new QLabel(QString(), this);
    m_markLabel->setStyleSheet(QStringLiteral("color:#d4a017;"));
    m_addBtn = new QPushButton(QStringLiteral("+ 加入时间线"), this);
    m_addBtn->setEnabled(false);
    m_addBtn->setStyleSheet(QStringLiteral("font-weight:bold;"));
    transport->addWidget(m_playBtn);
    transport->addWidget(m_slider, 1);
    transport->addWidget(m_timeLabel);
    transport->addWidget(m_inBtn);
    transport->addWidget(m_outBtn);
    transport->addWidget(m_markLabel);
    transport->addWidget(m_addBtn);
    center->addLayout(transport);

    // 片段块时间线
    m_timeline = new ComposeTimelineWidget(this);
    center->addWidget(m_timeline);
    mid->addLayout(center, 1);
    root->addLayout(mid, 1);

    // ---- 底：导出面板 ----
    auto *outBox = new QGroupBox(QStringLiteral("导出"), this);
    auto *ov = new QVBoxLayout(outBox);
    auto *modeRow = new QHBoxLayout();
    m_demoRadio = new QRadioButton(
        QStringLiteral("分析演示片（重编码：校正时间角标 + 强制「非原始证据」红标）"), outBox);
    m_evidenceRadio = new QRadioButton(
        QStringLiteral("证据片段（无损直拷，像素零改动，附完整性清单）"), outBox);
    m_demoRadio->setChecked(true);
    m_osdCheck = new QCheckBox(QStringLiteral("校正时间角标"), outBox);
    m_osdCheck->setChecked(true);
    m_caseNoCheck = new QCheckBox(QStringLiteral("案件号"), outBox);
    m_caseNoCheck->setChecked(true);
    m_panelsCheck = new QCheckBox(QStringLiteral("图表面板"), outBox);
    m_panelsCheck->setChecked(true);
    m_panelsCheck->setToolTip(QStringLiteral("仅单片段、源为当前视频、原速时可用（走全保真复合导出）"));
    modeRow->addWidget(m_demoRadio);
    modeRow->addWidget(m_evidenceRadio);
    modeRow->addSpacing(16);
    modeRow->addWidget(m_osdCheck);
    modeRow->addWidget(m_caseNoCheck);
    modeRow->addWidget(m_panelsCheck);
    modeRow->addStretch(1);
    ov->addLayout(modeRow);

    auto *pathRow = new QHBoxLayout();
    m_outPath = new QLineEdit(outBox);
    auto *browseBtn = new QPushButton(QStringLiteral("浏览…"), outBox);
    pathRow->addWidget(new QLabel(QStringLiteral("输出："), outBox));
    pathRow->addWidget(m_outPath, 1);
    pathRow->addWidget(browseBtn);
    ov->addLayout(pathRow);

    auto *progRow = new QHBoxLayout();
    m_progress = new QProgressBar(outBox);
    m_progress->setRange(0, 100);
    progRow->addWidget(m_progress, 1);
    m_startBtn = new QPushButton(QStringLiteral("开始导出"), outBox);
    m_startBtn->setDefault(true);
    m_cancelBtn = new QPushButton(QStringLiteral("取消导出"), outBox);
    m_cancelBtn->setEnabled(false);
    m_closeBtn = new QPushButton(QStringLiteral("关 闭"), outBox);
    progRow->addWidget(m_startBtn);
    progRow->addWidget(m_cancelBtn);
    progRow->addWidget(m_closeBtn);
    ov->addLayout(progRow);
    m_status = new QLabel(QString(), outBox);
    m_status->setWordWrap(true);
    ov->addWidget(m_status);
    root->addWidget(outBox);

    // ---- 信号 ----
    connect(m_matTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *, int) { onMaterialChanged(); });
    connect(m_matTree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int) {
                // 多通道勾选变化（勾选框经 itemChanged 而非 itemClicked 报）
                if (item && item->data(0, Qt::UserRole).toString() == QLatin1String("cam")
                    && !m_running)
                    loadMultiPreview();
            });
    connect(m_playBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onPlayPause);
    connect(m_slider, &QSlider::sliderPressed, this, &ComposeWorkbenchWindow::onSliderPressed);
    connect(m_slider, &QSlider::sliderMoved, this, &ComposeWorkbenchWindow::onSliderMoved);
    connect(m_slider, &QSlider::sliderReleased, this, &ComposeWorkbenchWindow::onSliderReleased);
    connect(m_inBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onMarkIn);
    connect(m_outBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onMarkOut);
    connect(m_addBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onAddSegment);
    connect(m_timeline, &ComposeTimelineWidget::moveRequested, this,
            &ComposeWorkbenchWindow::onBlockMove);
    connect(m_timeline, &ComposeTimelineWidget::editRequested, this,
            &ComposeWorkbenchWindow::onBlockEdit);
    connect(m_timeline, &ComposeTimelineWidget::removeRequested, this,
            &ComposeWorkbenchWindow::onBlockRemove);
    connect(m_timeline, &ComposeTimelineWidget::selectionChanged, this,
            [this](int) { updateAddButtonHint(); });
    connect(m_demoRadio, &QRadioButton::toggled, this, &ComposeWorkbenchWindow::onModeChanged);
    connect(browseBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onBrowseOutput);
    connect(m_startBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onStartExport);
    connect(m_cancelBtn, &QPushButton::clicked, this,
            [this]() { emit cancelRequested(); });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_running)
            hide();
    });

    // 多通道预览服务（引擎工厂与主窗同口径）
    m_svc = new MultiCamSyncService(this);
    m_svc->setEngineFactory([](QObject *parent) -> IVideoEngine * {
        return makeEngine(parent);
    });
    connect(m_svc, &MultiCamSyncService::clockChanged, this,
            [this](qint64 wallMs) {
                if (m_multiActive && !m_sliderScrubbing && m_slider->isEnabled())
                    m_slider->setValue(int(wallMs - m_svc->contentStartWallMs()));
                updateTransport();
            });
    connect(m_svc, &MultiCamSyncService::loadFinished, this, [this]() {
        if (m_multiActive) {
            m_slider->setEnabled(true);
            m_slider->setRange(0, int(qMax<qint64>(
                1, m_svc->contentEndWallMs() - m_svc->contentStartWallMs())));
            m_playBtn->setEnabled(true);
            m_inBtn->setEnabled(true);
            m_outBtn->setEnabled(true);
        }
        updateTransport();
    });
    connect(m_svc, &MultiCamSyncService::stateChanged, this,
            [this](MultiCamSyncService::State s) {
                if (m_multiActive)
                    m_playBtn->setText(s == MultiCamSyncService::State::Playing
                                           ? QStringLiteral("⏸ 暂停")
                                           : QStringLiteral("▶ 播放"));
            });

    rebuildMaterials();
    updateSuggestedPath();
    onModeChanged();
}

ComposeWorkbenchWindow::~ComposeWorkbenchWindow() {
    stopPreviews();
}

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------
QString ComposeWorkbenchWindow::formatMs(qint64 ms) {
    const qint64 h = ms / 3600000, m = ms / 60000 % 60, s = ms / 1000 % 60,
                 msec = ms % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(msec, 3, 10, QLatin1Char('0'));
}

qint64 ComposeWorkbenchWindow::parseMs(const QString &text, bool *ok) {
    if (ok) *ok = false;
    const QString t = text.trimmed();
    static const QRegularExpression re(
        QStringLiteral("^(?:(\\d+):)?([0-5]?\\d):([0-5]?\\d)(?:\\.(\\d{1,3}))?$"));
    const auto m = re.match(t);
    if (!m.hasMatch()) {
        bool numOk = false;
        const double sec = t.toDouble(&numOk);
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

bool ComposeWorkbenchWindow::wantPanels() const {
    return m_panelsCheck && m_panelsCheck->isChecked();
}

QString ComposeWorkbenchWindow::effectivePath(const QString &path) const {
    if (m_cm && m_cm->isOpen()) {
        if (const CaseVideoRef *v = m_cm->videoByPath(path))
            return m_cm->effectivePathFor(*v);
    }
    return path;
}

QString ComposeWorkbenchWindow::segDisplayName(
    const SegmentExportEngine::Params::ComposeSeg &seg) const {
    if (!seg.displayName.isEmpty())
        return seg.displayName;
    if (seg.isLanes())
        return QStringLiteral("机位同屏（%1路）").arg(seg.lanes.size());
    return QFileInfo(seg.sourcePath).fileName();
}

void ComposeWorkbenchWindow::hideEvent(QHideEvent *event) {
    stopPreviews();
    QDialog::hideEvent(event);
}

void ComposeWorkbenchWindow::closeEvent(QCloseEvent *event) {
    if (m_running) {
        event->ignore();
        return;
    }
    stopPreviews();
    QDialog::closeEvent(event);
}

// ---------------------------------------------------------------------------
// 素材树：多通道（机位勾选组）+ 单视频（全量含前处理产物）
// ---------------------------------------------------------------------------
void ComposeWorkbenchWindow::rebuildMaterials() {
    m_matTree->blockSignals(true);
    m_matTree->clear();
    if (m_cm && m_cm->isOpen())
        m_inv = buildCamInventory(*m_cm);
    else
        m_inv.clear();
    m_invLoaded = true;

    // 当前视频若不在案内清单 → 伪条目（独立模式）
    QStringList known;
    for (const auto &it : m_inv)
        known << it.path;
    const QString curEff = m_currentVideo.isEmpty() ? QString()
                                                    : effectivePath(m_currentVideo);

    // ---- 顶节点 1：多通道 ----
    auto *multiRoot = new QTreeWidgetItem(m_matTree);
    multiRoot->setText(0, QStringLiteral("▦ 多通道（机位同屏）"));
    multiRoot->setFlags(Qt::ItemIsEnabled);
    int camCount = 0;
    for (int i = 0; i < m_inv.size(); ++i) {
        const auto &it = m_inv[i];
        auto *child = new QTreeWidgetItem(multiRoot);
        QString name = it.displayName;
        if (it.fromPreprocess)
            name += QStringLiteral("（前处理）");
        if (!it.pathExists) {
            child->setText(0, name + QStringLiteral(" ⚠缺失"));
            child->setFlags(Qt::NoItemFlags);
        } else if (!it.calibrated) {
            child->setText(0, name + QStringLiteral("（未校时·临时）"));
            child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            child->setCheckState(0, Qt::Unchecked);
            child->setToolTip(0, QStringLiteral("未校时：按临时路进（墙钟=流内，导出角标显示未校时）"));
        } else {
            child->setText(0, name);
            child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            child->setCheckState(0, Qt::Unchecked);
        }
        child->setData(0, Qt::UserRole, QStringLiteral("cam"));
        child->setData(0, Qt::UserRole + 1, i);   // 清单下标
        ++camCount;
    }
    if (camCount == 0) {
        auto *none = new QTreeWidgetItem(multiRoot);
        none->setText(0, QStringLiteral("（案内无机位素材）"));
        none->setFlags(Qt::NoItemFlags);
    }
    multiRoot->setExpanded(true);

    // ---- 顶节点 2：单视频 ----
    auto *singleRoot = new QTreeWidgetItem(m_matTree);
    singleRoot->setText(0, QStringLiteral("🎞 单视频（含前处理产物）"));
    singleRoot->setFlags(Qt::ItemIsEnabled);
    for (int i = 0; i < m_inv.size(); ++i) {
        const auto &it = m_inv[i];
        auto *child = new QTreeWidgetItem(singleRoot);
        QString name = it.displayName;
        if (it.fromPreprocess)
            name = QStringLiteral("%1 ⚙前处理").arg(QFileInfo(it.path).fileName());
        if (!it.pathExists) {
            child->setText(0, name + QStringLiteral(" ⚠缺失"));
            child->setFlags(Qt::NoItemFlags);
        } else {
            child->setText(0, name);
        }
        child->setData(0, Qt::UserRole, QStringLiteral("single"));
        child->setData(0, Qt::UserRole + 1, i);
        child->setToolTip(0, it.path);
    }
    if (!m_currentVideo.isEmpty()
        && !m_currentVideo.endsWith(".vla", Qt::CaseInsensitive)
        && !known.contains(curEff) && !known.contains(m_currentVideo)) {
        auto *child = new QTreeWidgetItem(singleRoot);
        child->setText(0, QStringLiteral("%1（当前打开）")
                              .arg(QFileInfo(m_currentVideo).fileName()));
        child->setData(0, Qt::UserRole, QStringLiteral("singlefile"));
        child->setData(0, Qt::UserRole + 1, m_currentVideo);
        child->setToolTip(0, m_currentVideo);
    }
    singleRoot->setExpanded(true);
    m_matTree->blockSignals(false);
}

void ComposeWorkbenchWindow::onMaterialChanged() {
    if (m_running)
        return;
    auto *item = m_matTree->currentItem();
    if (!item)
        return;
    const QString kind = item->data(0, Qt::UserRole).toString();
    if (kind == QLatin1String("single")) {
        const int idx = item->data(0, Qt::UserRole + 1).toInt();
        if (idx >= 0 && idx < m_inv.size() && m_inv[idx].pathExists)
            loadSinglePreview(m_inv[idx].path, m_inv[idx].displayName);
    } else if (kind == QLatin1String("singlefile")) {
        const QString path = item->data(0, Qt::UserRole + 1).toString();
        loadSinglePreview(path, QFileInfo(path).fileName());
    }
    // cam 行：勾选变化走 itemChanged → loadMultiPreview
}

// ---------------------------------------------------------------------------
// 预览加载
// ---------------------------------------------------------------------------
void ComposeWorkbenchWindow::stopPreviews() {
    if (m_svc)
        m_svc->closeAll();
    if (m_singleEngine) {
        m_singleEngine->unload();
        m_singleEngine->deleteLater();
        m_singleEngine = nullptr;
    }
    m_singleTile->setEngine(nullptr);
    m_singleTile->clearFrame();
    m_singlePreviewPath.clear();
    m_multiActive = false;
    for (auto *t : m_multiTiles)
        t->deleteLater();
    m_multiTiles.clear();
}

void ComposeWorkbenchWindow::loadSinglePreview(const QString &path,
                                               const QString &displayName) {
    stopPreviews();
    m_previewStack->setCurrentIndex(0);
    m_singleTile->setLaneName(displayName);
    m_singleEngine = makeEngine(this);
    m_singleTile->setEngine(m_singleEngine);
    connect(m_singleEngine, &IVideoEngine::positionChanged, this,
            [this](qint64 ms) {
                if (!m_multiActive && !m_sliderScrubbing && m_slider->isEnabled())
                    m_slider->setValue(int(ms));
                updateTransport();
            });
    connect(m_singleEngine, &IVideoEngine::stateChanged, this,
            [this](PlaybackState st) {
                if (!m_multiActive)
                    m_playBtn->setText(st == PlaybackState::Playing
                                           ? QStringLiteral("⏸ 暂停")
                                           : QStringLiteral("▶ 播放"));
            });
    connect(m_singleEngine, &IVideoEngine::durationChanged, this,
            [this](qint64 dur) {
                if (!m_multiActive)
                    m_slider->setRange(0, int(qMax<qint64>(1, dur)));
                updateTransport();
            });
    m_singlePreviewPath = path;
    if (!m_singleEngine->load(path)) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("预览加载失败：%1").arg(path));
        return;
    }
    m_singleEngine->pause();   // 载入即停帧，播放由用户点
    m_slider->setEnabled(true);
    m_playBtn->setEnabled(true);
    m_inBtn->setEnabled(true);
    m_outBtn->setEnabled(true);
    m_markIn = m_markOut = -1;
    updateAddButtonHint();
}

void ComposeWorkbenchWindow::loadMultiPreview() {
    // 收集勾选机位（清单下标）
    QVector<int> checked;
    auto *multiRoot = m_matTree->topLevelItem(0);
    if (!multiRoot)
        return;
    for (int i = 0; i < multiRoot->childCount(); ++i) {
        auto *c = multiRoot->child(i);
        if (c->data(0, Qt::UserRole).toString() == QLatin1String("cam")
            && (c->flags() & Qt::ItemIsUserCheckable)
            && c->checkState(0) == Qt::Checked)
            checked << c->data(0, Qt::UserRole + 1).toInt();
    }
    if (checked.isEmpty()) {
        if (m_multiActive) {
            stopPreviews();
            m_previewStack->setCurrentIndex(0);
            updateTransport();
        }
        return;
    }
    if (checked.size() > kMaxLanes) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("多通道最多 %1 路，请取消多余勾选").arg(kMaxLanes));
        return;
    }
    // 装配 lanes（与多机播放窗 onStartSync 同口径：校时路升序在前，临时路随后）
    QVector<SyncLaneData> lanes;
    for (int idx : checked) {
        const auto &it = m_inv[idx];
        if (it.calibrated) {
            lanes.append(it.lane);
        } else {
            SyncLaneData l;
            l.id = it.id;
            l.path = it.path;
            l.displayName = it.displayName;
            l.temporary = true;
            lanes.append(l);
        }
    }
    std::stable_sort(lanes.begin(), lanes.end(),
                     [](const SyncLaneData &a, const SyncLaneData &b) {
                         if (a.temporary != b.temporary)
                             return !a.temporary;
                         if (!a.temporary)
                             return syncLaneWallStart(a) < syncLaneWallStart(b);
                         return false;
                     });

    stopPreviews();
    m_previewStack->setCurrentIndex(1);
    m_multiActive = true;
    if (!m_svc->loadLanes(lanes))
        return;
    // 建瓦片
    const int n = lanes.size();
    const int cols = (n <= 2) ? n : 2;
    for (int i = 0; i < n; ++i) {
        auto *tile = new CamTileWidget(m_multiPage);
        tile->setLaneName(lanes[i].displayName);
        tile->setEngine(m_svc->engineAt(i));
        tile->setTemporaryBadge(lanes[i].temporary);
        if (i == 0)
            tile->setAudible(true);
        connect(tile, &CamTileWidget::clicked, this, [this, i]() {
            m_svc->setAudibleLane(i);
            for (int k = 0; k < m_multiTiles.size(); ++k)
                m_multiTiles[k]->setAudible(k == i);
        });
        m_multiGrid->addWidget(tile, i / cols, i % cols);
        m_multiTiles << tile;
    }
    m_svc->setAudibleLane(0);
    m_svc->pause();
    m_markIn = m_markOut = -1;
    updateAddButtonHint();
}

// ---------------------------------------------------------------------------
// 走带
// ---------------------------------------------------------------------------
bool ComposeWorkbenchWindow::previewIsMulti() const { return m_multiActive; }

qint64 ComposeWorkbenchWindow::previewPosMs() const {
    if (m_multiActive)
        return m_svc ? m_svc->clockWallMs() : 0;
    return m_singleEngine ? m_singleEngine->position() : 0;
}

qint64 ComposeWorkbenchWindow::previewDurationMs() const {
    if (m_multiActive)
        return m_svc ? qMax<qint64>(0, m_svc->contentEndWallMs()
                                       - m_svc->contentStartWallMs()) : 0;
    return m_singleEngine ? m_singleEngine->duration() : 0;
}

void ComposeWorkbenchWindow::updateTransport() {
    const qint64 dur = previewDurationMs();
    const qint64 pos = m_multiActive
        ? previewPosMs() - (m_svc ? m_svc->contentStartWallMs() : 0)
        : previewPosMs();
    if (dur > 0)
        m_timeLabel->setText(QStringLiteral("%1 / %2")
                                 .arg(formatMs(qMax<qint64>(0, pos)), formatMs(dur)));
    QString marks;
    if (m_markIn >= 0)
        marks += QStringLiteral("入 %1").arg(formatMs(m_markIn));
    if (m_markOut >= 0) {
        if (!marks.isEmpty())
            marks += QStringLiteral("  ");
        marks += QStringLiteral("出 %1").arg(formatMs(m_markOut));
    }
    m_markLabel->setText(marks);
    updateAddButtonHint();
}

void ComposeWorkbenchWindow::updateAddButtonHint() {
    const bool can = !m_running
        && (m_multiActive ? (m_svc && m_svc->laneCount() >= 2)
                          : !m_singlePreviewPath.isEmpty())
        && previewDurationMs() > 0;
    m_addBtn->setEnabled(can);
}

void ComposeWorkbenchWindow::onPlayPause() {
    if (m_multiActive) {
        if (m_svc)
            m_svc->togglePlay();
    } else if (m_singleEngine) {
        if (m_singleEngine->state() == PlaybackState::Playing)
            m_singleEngine->pause();
        else
            m_singleEngine->play();
    }
}

void ComposeWorkbenchWindow::onSliderPressed() {
    m_sliderScrubbing = true;
    if (m_multiActive && m_svc)
        m_svc->beginScrub();
}

void ComposeWorkbenchWindow::onSliderMoved(int value) {
    if (m_multiActive && m_svc)
        m_svc->scrubTo(m_svc->contentStartWallMs() + value);
    else if (m_singleEngine)
        m_singleEngine->seek(value);
    updateTransport();
}

void ComposeWorkbenchWindow::onSliderReleased() {
    if (m_multiActive && m_svc) {
        m_svc->endScrub();
        m_svc->seekWall(m_svc->contentStartWallMs() + m_slider->value());
    } else if (m_singleEngine) {
        m_singleEngine->seek(m_slider->value());
    }
    m_sliderScrubbing = false;
    updateTransport();
}

void ComposeWorkbenchWindow::onMarkIn() {
    m_markIn = previewPosMs();
    if (m_markOut >= 0 && m_markOut <= m_markIn)
        m_markOut = -1;
    updateTransport();
}

void ComposeWorkbenchWindow::onMarkOut() {
    const qint64 pos = previewPosMs();
    if (m_markIn >= 0 && pos <= m_markIn)
        return;
    m_markOut = pos;
    updateTransport();
}

// ---------------------------------------------------------------------------
// 时间线操作
// ---------------------------------------------------------------------------
void ComposeWorkbenchWindow::onAddSegment() {
    SegmentExportEngine::Params::ComposeSeg seg;
    const qint64 pos = previewPosMs();
    qint64 inMs = m_markIn >= 0 ? m_markIn : qMax<qint64>(0, pos - 10000);
    qint64 outMs = m_markOut >= 0 ? m_markOut : pos;
    if (m_multiActive && m_svc) {
        inMs = m_markIn >= 0 ? m_markIn
                             : qMax<qint64>(m_svc->contentStartWallMs(), pos - 10000);
        outMs = m_markOut >= 0 ? m_markOut : pos;
    }
    if (outMs <= inMs) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("入/出点非法（出点须在入点之后）"));
        return;
    }
    seg.inMs = inMs;
    seg.outMs = outMs;
    if (m_multiActive && m_svc) {
        seg.lanes = m_svc->lanes();
        seg.audioLane = qMax(0, m_svc->audibleLane());
        seg.displayName = QStringLiteral("机位同屏（%1路）").arg(seg.lanes.size());
    } else {
        seg.sourcePath = effectivePath(m_singlePreviewPath);
        seg.displayName = m_singleTile->toolTip().isEmpty()
            ? QFileInfo(seg.sourcePath).fileName() : m_singleTile->toolTip();
    }
    m_segs << seg;
    syncTimeline();
    m_status->clear();
}

void ComposeWorkbenchWindow::syncTimeline() {
    QStringList names;
    for (const auto &s : m_segs)
        names << segDisplayName(s);
    m_timeline->setSegments(m_segs, names);
    // 有宫格段时证据模式不可用
    bool hasLanes = false;
    for (const auto &s : m_segs)
        if (s.isLanes()) hasLanes = true;
    m_evidenceRadio->setEnabled(!hasLanes && !m_running);
    if (hasLanes && m_evidenceRadio->isChecked())
        m_demoRadio->setChecked(true);
    updateSuggestedPath();
}

void ComposeWorkbenchWindow::onBlockMove(int from, int to) {
    if (from < 0 || from >= m_segs.size() || to < 0 || to >= m_segs.size())
        return;
    m_segs.move(from, to);
    syncTimeline();
}

void ComposeWorkbenchWindow::onBlockRemove(int idx) {
    if (idx >= 0 && idx < m_segs.size()) {
        m_segs.removeAt(idx);
        syncTimeline();
    }
}

void ComposeWorkbenchWindow::onBlockEdit(int idx) {
    if (idx < 0 || idx >= m_segs.size() || m_running)
        return;
    auto &seg = m_segs[idx];
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("片段参数（%1）").arg(segDisplayName(seg)));
    auto *lay = new QFormLayout(&dlg);
    auto *inEdit = new QLineEdit(formatMs(seg.inMs), &dlg);
    auto *outEdit = new QLineEdit(formatMs(seg.outMs), &dlg);
    auto *rateSpin = new QDoubleSpinBox(&dlg);
    rateSpin->setRange(0.25, 8.0);
    rateSpin->setSingleStep(0.25);
    rateSpin->setValue(seg.rate);
    lay->addRow(seg.isLanes() ? QStringLiteral("入点（墙钟）") : QStringLiteral("入点（流内）"),
                inEdit);
    lay->addRow(seg.isLanes() ? QStringLiteral("出点（墙钟）") : QStringLiteral("出点（流内）"),
                outEdit);
    lay->addRow(QStringLiteral("倍速"), rateSpin);
    auto *bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addRow(bbox);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    bool ok1 = false, ok2 = false;
    const qint64 inV = parseMs(inEdit->text(), &ok1);
    const qint64 outV = parseMs(outEdit->text(), &ok2);
    if (!ok1 || !ok2 || outV <= inV) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("片段区间非法，未保存"));
        return;
    }
    seg.inMs = inV;
    seg.outMs = outV;
    seg.rate = rateSpin->value();
    syncTimeline();
}

// ---------------------------------------------------------------------------
// 导出
// ---------------------------------------------------------------------------
void ComposeWorkbenchWindow::onModeChanged() {
    const bool demo = m_demoRadio->isChecked();
    m_osdCheck->setEnabled(demo && !m_running);
    m_caseNoCheck->setEnabled(demo && !m_running);
    m_panelsCheck->setEnabled(demo && !m_running);
    updateSuggestedPath();
}

void ComposeWorkbenchWindow::updateSuggestedPath() {
    if (m_running)
        return;
    QString dir;
    if (m_cm && m_cm->isOpen())
        dir = m_cm->caseDir() + QStringLiteral("/exports");
    else if (!m_currentVideo.isEmpty())
        dir = QFileInfo(m_currentVideo).absolutePath();
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("MMdd_HHmmss"));
    const QString prefix = m_evidenceRadio->isChecked()
        ? QStringLiteral("LAEvidence") : QStringLiteral("LACompose");
    m_outPath->setText(QDir(dir).filePath(QStringLiteral("%1_%2.mp4").arg(prefix, stamp)));
}

void ComposeWorkbenchWindow::onBrowseOutput() {
    const QString p = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出到"), m_outPath->text(), QStringLiteral("MP4 (*.mp4)"));
    if (!p.isEmpty())
        m_outPath->setText(p);
}

SegmentExportEngine::Params ComposeWorkbenchWindow::buildParams(QString *err) {
    SegmentExportEngine::Params pp;
    pp.segments = m_segs;
    if (pp.segments.isEmpty()) {
        if (err) *err = QStringLiteral("时间线为空——先预览打点后「+ 加入时间线」");
        return pp;
    }
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
    // 逐文件校正时间表（演示模式 OSD；宫格段各已校时也入表备用）
    if (!pp.evidenceCopy && pp.burnOsd) {
        QStringList seen;
        QStringList paths;
        for (const auto &s : pp.segments) {
            if (s.isLanes())
                for (const auto &l : s.lanes) paths << l.path;
            else
                paths << s.sourcePath;
        }
        for (const QString &sp : paths) {
            if (seen.contains(sp))
                continue;
            seen << sp;
            const QString vlaPath = m_cm
                ? m_cm->vlaPathFor(sp) : sp + QStringLiteral(".vla");
            const TimeCalibration cal = TimelineModel::peekCalibrationFromVla(vlaPath);
            if (cal.isValid())
                pp.calibrationByPath.insert(sp, cal);
        }
    }
    const Credential cred = CredentialStore::load();
    pp.operatorName = cred.name;
    pp.operatorOrg = cred.org;
    return pp;
}

void ComposeWorkbenchWindow::onStartExport() {
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

void ComposeWorkbenchWindow::setExportRunning(bool running, int totalFrames) {
    m_running = running;
    m_startBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);
    m_closeBtn->setEnabled(!running);
    m_matTree->setEnabled(!running);
    m_timeline->setEnabled(!running);
    if (running) {
        m_progress->setValue(0);
        m_progress->setMaximum(qMax(1, totalFrames));
        m_status->setStyleSheet(QStringLiteral("color:#666;"));
        m_status->setText(QStringLiteral("导出进行中…"));
    }
    onModeChanged();
    syncTimeline();
}

void ComposeWorkbenchWindow::setProgress(int done, int total) {
    if (total > 0)
        m_progress->setMaximum(total);
    m_progress->setValue(qMin(done, m_progress->maximum()));
}

void ComposeWorkbenchWindow::setResult(bool ok, const QString &msg) {
    m_running = false;
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_closeBtn->setEnabled(true);
    m_matTree->setEnabled(true);
    m_timeline->setEnabled(true);
    onModeChanged();
    syncTimeline();
    if (ok) {
        m_status->setStyleSheet(QStringLiteral("color:#27ae60;"));
        m_status->setText(QStringLiteral("✔ 导出完成：%1").arg(msg));
    } else {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("✖ %1").arg(msg));
    }
}

void ComposeWorkbenchWindow::refreshContext(const QString &currentVideo, double fps) {
    m_currentVideo = currentVideo;
    if (fps > 0)
        m_fps = fps;
    rebuildMaterials();
    updateSuggestedPath();
}

#include "composeworkbench.moc"
