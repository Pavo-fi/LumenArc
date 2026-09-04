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
#include <QDesktopServices>
#include <QMouseEvent>
#include <QUrl>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QKeySequence>
#include <QShortcut>
#include <QToolButton>
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
// 标注框选覆盖层：聚光灯/箭头在预览画面上拖框（归一化内容坐标输出）
// ============================================================================
class ComposeWorkbenchWindow::AnnoPickOverlay : public QWidget
{
public:
    explicit AnnoPickOverlay(QWidget *parent, CamTileWidget *tile,
                             std::function<void(QRectF)> onPick,
                             std::function<void()> onCancel)
        : QWidget(parent), m_tile(tile), m_onPick(std::move(onPick)),
          m_onCancel(std::move(onCancel)) {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setCursor(Qt::CrossCursor);
        hide();
    }
    void beginPick() { m_dragging = false; m_rect = QRectF(); show(); raise(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 40));
        p.setPen(QColor(255, 225, 77));
        p.drawText(rect().adjusted(12, 8, -12, 0), Qt::AlignTop | Qt::AlignLeft,
                   QStringLiteral("在画面上拖出一个框（右键/Esc 取消）"));
        if (!m_rect.isNull()) {
            p.setPen(QPen(QColor(255, 225, 77), 2, Qt::DashLine));
            p.setBrush(QColor(255, 225, 77, 40));
            p.drawRect(m_rect);
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::RightButton) { cancel(); return; }
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_start = e->pos();
            m_rect = QRectF();
            update();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging) {
            m_rect = QRectF(m_start, e->pos()).normalized();
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (!m_dragging || e->button() != Qt::LeftButton)
            return;
        m_dragging = false;
        m_rect = QRectF(m_start, e->pos()).normalized();
        if (m_rect.width() < 12 || m_rect.height() < 12) {   // 太小=误触
            m_rect = QRectF();
            update();
            return;
        }
        // 控件坐标 → 归一化内容坐标（经瓦片适配矩形）
        const QRectF fit = m_tile->videoFitRect();
        if (fit.isEmpty()) { cancel(); return; }
        QRectF n((m_rect.left() - fit.x()) / fit.width(),
                 (m_rect.top() - fit.y()) / fit.height(),
                 m_rect.width() / fit.width(), m_rect.height() / fit.height());
        n = n.intersected(QRectF(0, 0, 1, 1));
        hide();
        if (n.width() > 0.01 && n.height() > 0.01)
            m_onPick(n);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) cancel();
    }
private:
    void cancel() { hide(); m_onCancel(); }
    CamTileWidget *m_tile;
    std::function<void(QRectF)> m_onPick;
    std::function<void()> m_onCancel;
    bool m_dragging = false;
    QPointF m_start;
    QRectF m_rect;
};

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
    void setPlaySeg(int idx) {          // P2.6 播放头联动：预览位置落在哪段
        if (m_playSeg != idx) { m_playSeg = idx; update(); }
    }
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
    void rateRequested(int idx, double rate);      // P2.7 右键倍速预设
    void annoRemoveRequested(int segIdx, int annoIdx);

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(Theme::BgPanel));
        if (m_segs.isEmpty()) {
            p.setPen(QColor(Theme::TextMuted));
            p.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("还没有片段——在上方画面按 I 键（或点红色圆钮）截取第一段"));
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
            if (m_playSeg == i) {   // 预览正在放这段：顶部▼标记
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(Theme::Accent));
                const int cx0 = r.center().x();
                const QPoint tri[3] = {QPoint(cx0 - 5, r.top() - 12),
                                       QPoint(cx0 + 5, r.top() - 12),
                                       QPoint(cx0, r.top() - 3)};
                p.drawPolygon(tri, 3);
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
                sub += QStringLiteral("  ▦%1路%2").arg(m_segs[i].lanes.size())
                           .arg(m_segs[i].gridLayout == 1 ? QStringLiteral("·主路大窗")
                                                          : QString());
            p.setPen(QColor(Theme::TextSecond));
            p.drawText(r.adjusted(8, laneH / 2 - 6, -6, -4),
                       Qt::AlignVCenter | Qt::AlignLeft, sub);
            x += w;
        }
        // 末尾总时长（右对齐，左区留给标注 chips）
        p.setPen(QColor(Theme::TextMuted));
        p.drawText(QRect(8, 2, width() - 16, 18), Qt::AlignVCenter | Qt::AlignRight,
                   QStringLiteral("共 %1 段 · 输出 %2")
                       .arg(m_segs.size())
                       .arg(ComposeWorkbenchWindow::formatMs(totalOutMs())));
        // P2.7 标注 chips 行（块上方）：🎯聚光灯 ↗箭头 💬字幕
        QFont cf = p.font();
        cf.setPixelSize(11);
        p.setFont(cf);
        for (int i = 0; i < m_segs.size(); ++i) {
            const auto &sg = m_segs[i];
            if (sg.annos.isEmpty())
                continue;
            const QRect br = blockRect(i);
            const double span = double(qMax<qint64>(1, sg.outMs - sg.inMs));
            for (int j = 0; j < sg.annos.size(); ++j) {
                const auto &an = sg.annos[j];
                const double f0 = double(an.inMs - sg.inMs) / span;
                const double f1 = double(an.outMs - sg.inMs) / span;
                const int cx = br.x() + int(f0 * br.width());
                const int cw = qMax(16, int((f1 - f0) * br.width()));
                const QRect chip(cx, 3, cw, 16);
                QString icon;
                switch (an.type) {
                case SegmentExportEngine::Params::ComposeAnno::Spotlight: icon = QStringLiteral("🎯"); break;
                case SegmentExportEngine::Params::ComposeAnno::Arrow:     icon = QStringLiteral("↗"); break;
                default:                                                  icon = QStringLiteral("💬"); break;
                }
                p.setPen(Qt::NoPen);
                p.setBrush(QColor::fromRgb(an.colorRgb).darker(150));
                p.drawRoundedRect(chip, 3, 3);
                p.setPen(Qt::white);
                p.drawText(chip, Qt::AlignCenter,
                           icon + (an.type == SegmentExportEngine::Params::ComposeAnno::Caption
                                       ? an.text.left(6) : QString()));
            }
        }
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
        // 标注 chip 优先
        const auto chip = hitChip(e->pos());
        if (chip.first >= 0) {
            QMenu menu(this);
            const auto &an = m_segs[chip.first].annos[chip.second];
            QAction *rmAnno = menu.addAction(QStringLiteral("删除该标注"));
            menu.addAction(QStringLiteral("（双击块可改时间段）"))->setEnabled(false);
            if (menu.exec(e->globalPos()) == rmAnno)
                emit annoRemoveRequested(chip.first, chip.second);
            Q_UNUSED(an);
            return;
        }
        const int idx = hitBlock(e->pos());
        if (idx < 0)
            return;
        m_selected = idx;
        emit selectionChanged(idx);
        update();
        QMenu menu(this);
        QAction *editAct = menu.addAction(QStringLiteral("编辑参数（双击同效）…"));
        // P2.7 倍速预设子菜单（简便调速）
        QMenu *rateMenu = menu.addMenu(QStringLiteral("倍速"));
        const double curRate = m_segs[idx].rate;
        for (double r : {0.5, 1.0, 1.25, 1.5, 2.0, 4.0}) {
            QAction *a = rateMenu->addAction(QStringLiteral("×%1").arg(r, 0, 'g', 3));
            a->setCheckable(true);
            a->setChecked(qAbs(curRate - r) < 0.01);
            a->setData(r);
        }
        QAction *rmAct = menu.addAction(QStringLiteral("删除该段"));
        QAction *sel = menu.exec(e->globalPos());
        if (sel == editAct)
            emit editRequested(idx);
        else if (sel == rmAct)
            emit removeRequested(idx);
        else if (sel && rateMenu->actions().contains(sel))
            emit rateRequested(idx, sel->data().toDouble());
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
    /// 命中标注 chip（chips 行 y 3..19）
    QPair<int, int> hitChip(const QPoint &pos) const {
        if (pos.y() < 3 || pos.y() > 19)
            return {-1, -1};
        for (int i = 0; i < m_segs.size(); ++i) {
            const auto &sg = m_segs[i];
            if (sg.annos.isEmpty())
                continue;
            const QRect br = blockRect(i);
            const double span = double(qMax<qint64>(1, sg.outMs - sg.inMs));
            for (int j = 0; j < sg.annos.size(); ++j) {
                const auto &an = sg.annos[j];
                const int cx = br.x() + int(double(an.inMs - sg.inMs) / span * br.width());
                const int cw = qMax(16, int(double(an.outMs - an.inMs) / span * br.width()));
                if (QRect(cx, 3, cw, 16).contains(pos))
                    return {i, j};
            }
        }
        return {-1, -1};
    }

    QVector<SegmentExportEngine::Params::ComposeSeg> m_segs;
    QStringList m_names;
    int m_playSeg = -1;
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

    // ---- 顶部：四步引导条（当前步高亮/完成变绿）+ 一句白话动态提示 ----
    auto *guideBar = new QFrame(this);
    guideBar->setObjectName(QStringLiteral("wbGuideBar"));
    guideBar->setStyleSheet(QStringLiteral(
        "QFrame#wbGuideBar{background:#23272e;border:1px solid #3a3f47;border-radius:6px;}"));
    auto *gh = new QHBoxLayout(guideBar);
    gh->setContentsMargins(10, 6, 10, 6);
    const QStringList stepTexts = {
        QStringLiteral("① 选素材"), QStringLiteral("② 截片段"),
        QStringLiteral("③ 排顺序"), QStringLiteral("④ 导出")};
    for (int i = 0; i < 4; ++i) {
        m_stepLabels[i] = new QLabel(stepTexts[i], guideBar);
        gh->addWidget(m_stepLabels[i]);
        if (i < 3) {
            auto *arrow = new QLabel(QStringLiteral("→"), guideBar);
            arrow->setStyleSheet(QStringLiteral("color:#555;"));
            gh->addWidget(arrow);
        }
    }
    gh->addSpacing(20);
    m_guideHint = new QLabel(guideBar);
    m_guideHint->setStyleSheet(QStringLiteral("color:#d4a017;"));
    gh->addWidget(m_guideHint, 1);
    root->addWidget(guideBar);

    auto *mid = new QHBoxLayout();

    // ---- 左：素材树 ----
    auto *matBox = new QGroupBox(QStringLiteral("素材"), this);
    auto *matLay = new QVBoxLayout(matBox);
    m_matTree = new QTreeWidget(matBox);
    m_matTree->setHeaderHidden(true);
    m_matTree->setMinimumWidth(230);
    m_matTree->setMaximumWidth(300);
    matLay->addWidget(m_matTree);
    auto *matHint = new QLabel(QStringLiteral("单视频：点选即预览\n勾 2~4 个机位 = 同屏"), matBox);
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

    // 走带条（键位对齐剪映/PR：空格=播放暂停，←/→=逐帧，Shift+←/→=±1秒，J/L=±5秒）
    auto *transport = new QHBoxLayout();
    m_playBtn = new QPushButton(QStringLiteral("▶ 播放（空格）"), this);
    m_playBtn->setFixedWidth(116);
    m_playBtn->setEnabled(false);
    m_playBtn->setFocusPolicy(Qt::NoFocus);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setEnabled(false);
    m_slider->setFocusPolicy(Qt::NoFocus);
    m_timeLabel = new QLabel(QStringLiteral("--:-- / --:--"), this);
    transport->addWidget(m_playBtn);
    transport->addWidget(m_slider, 1);
    transport->addWidget(m_timeLabel);
    center->addLayout(transport);

    // 截取条：红点一键记录（像录音笔，主操作）+ I/O 精确打点（剪映/PR 同款键位）
    auto *cutRow = new QHBoxLayout();
    m_recordBtn = new QPushButton(QStringLiteral("⏺ 从这里开始（I）"), this);
    m_recordBtn->setObjectName(QStringLiteral("wbRecordBtn"));
    m_recordBtn->setEnabled(false);
    m_recordBtn->setMinimumHeight(34);
    m_recordBtn->setFocusPolicy(Qt::NoFocus);
    m_recordBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#8c2f2f;color:#fff;font-weight:bold;border-radius:5px;padding:0 14px;}"
        "QPushButton:disabled{background:#4a3a3a;color:#998;}"));
    m_recordBtn->setToolTip(QStringLiteral(
        "用法像录音笔：到关键画面按一下（或按 I 键）开始记录；继续播放/拖动到结束画面，再按一下（或按 O 键）→ 这段自动进下方清单"));
    m_inBtn = new QPushButton(QStringLiteral("设为起点（I）"), this);
    m_outBtn = new QPushButton(QStringLiteral("设为终点并加入（O）"), this);
    m_inBtn->setEnabled(false);
    m_outBtn->setEnabled(false);
    m_inBtn->setFocusPolicy(Qt::NoFocus);
    m_outBtn->setFocusPolicy(Qt::NoFocus);
    m_inBtn->setToolTip(QStringLiteral("快捷键 I——与剪映/PR 的入点一致"));
    m_outBtn->setToolTip(QStringLiteral("快捷键 O——设终点并立即把这段加入下方清单"));
    m_markLabel = new QLabel(QString(), this);
    m_markLabel->setStyleSheet(QStringLiteral("color:#d4a017;"));
    cutRow->addWidget(m_recordBtn);
    cutRow->addSpacing(12);
    cutRow->addWidget(m_inBtn);
    cutRow->addWidget(m_outBtn);
    m_splitBtn = new QPushButton(QStringLiteral("✂ 在此切开（Ctrl+B）"), this);
    m_splitBtn->setObjectName(QStringLiteral("wbSplitBtn"));
    m_splitBtn->setEnabled(false);
    m_splitBtn->setFocusPolicy(Qt::NoFocus);
    m_splitBtn->setToolTip(QStringLiteral(
        "把预览当前位置所在的片段一切为二（对齐剪映 Ctrl+B 分割）；两半各自可调倍速"));
    cutRow->addWidget(m_splitBtn);
    cutRow->addWidget(m_markLabel);
    cutRow->addStretch(1);
    auto *keysHint = new QLabel(
        QStringLiteral("←/→ 逐帧 · Shift+←/→ ±1秒 · J/L ±5秒"), this);
    keysHint->setStyleSheet(QStringLiteral("color:#777;"));
    cutRow->addWidget(keysHint);
    center->addLayout(cutRow);

    // 标注条（P2.7 标注轨：挂到预览位置所在的单视频片段块上）
    auto *annoRow = new QHBoxLayout();
    auto *annoCap = new QLabel(QStringLiteral("标注："), this);
    annoCap->setStyleSheet(QStringLiteral("color:#999;"));
    annoRow->addWidget(annoCap);
    m_annoSpotBtn = new QPushButton(QStringLiteral("🎯 聚光灯"), this);
    m_annoSpotBtn->setToolTip(QStringLiteral(
        "在画面上拖框 → 该区域提亮、其余变暗，并平滑放大至满屏（突出重点区域）"));
    m_annoArrowBtn = new QPushButton(QStringLiteral("↗ 箭头"), this);
    m_annoArrowBtn->setToolTip(QStringLiteral("在画面上从起点拖到终点 → 烧录指示箭头"));
    m_annoCapBtn = new QPushButton(QStringLiteral("💬 字幕"), this);
    m_annoCapBtn->setToolTip(QStringLiteral("底部黑带白字解说词，指定起止时间"));
    for (auto *b : {m_annoSpotBtn, m_annoArrowBtn, m_annoCapBtn}) {
        b->setEnabled(false);
        b->setFocusPolicy(Qt::NoFocus);
        annoRow->addWidget(b);
    }
    auto *annoHint = new QLabel(
        QStringLiteral("标注挂在「预览位置所在」的片段块上；时间线块上方出小标，右键可删"), this);
    annoHint->setStyleSheet(QStringLiteral("color:#777;"));
    annoRow->addWidget(annoHint);
    annoRow->addStretch(1);
    center->addLayout(annoRow);

    // 片段块时间线
    auto *tlCap = new QLabel(
        QStringLiteral("片段清单（拖动排序 · 双击微调 · Delete 删除选中）"), this);
    tlCap->setStyleSheet(QStringLiteral("color:#999;"));
    center->addWidget(tlCap);
    m_timeline = new ComposeTimelineWidget(this);
    m_timeline->setObjectName(QStringLiteral("wbTimeline"));
    center->addWidget(m_timeline);
    mid->addLayout(center, 1);
    root->addLayout(mid, 1);

    // ---- 底：导出面板（白话二选一；高级项默认折叠）----
    auto *outBox = new QGroupBox(QStringLiteral("导出"), this);
    auto *ov = new QVBoxLayout(outBox);
    auto *modeRow = new QHBoxLayout();
    m_demoRadio = new QRadioButton(
        QStringLiteral("演示片 —— 带时间角标/红标，可加曲线和 ROI，用于汇报讲解"), outBox);
    m_evidenceRadio = new QRadioButton(
        QStringLiteral("证据原始片段 —— 画面零改动，附完整性清单，用于存档送检"), outBox);
    m_demoRadio->setChecked(true);
    m_demoRadio->setToolTip(QStringLiteral("重编码合成：多段拼接、宫格、变速、水印都用它"));
    m_evidenceRadio->setToolTip(QStringLiteral(
        "无损直拷原始码流（仅限纯单视频片段；含多机位同屏段时不可用）"));
    modeRow->addWidget(m_demoRadio);
    modeRow->addWidget(m_evidenceRadio);
    modeRow->addStretch(1);
    m_advToggle = new QToolButton(outBox);
    m_advToggle->setText(QStringLiteral("更多选项 ▸"));
    m_advToggle->setCheckable(true);
    m_advToggle->setChecked(false);
    m_advToggle->setFocusPolicy(Qt::NoFocus);
    modeRow->addWidget(m_advToggle);
    ov->addLayout(modeRow);

    // 折叠区：烧录/图表面板等高级开关
    m_advPanel = new QWidget(outBox);
    auto *advRow = new QHBoxLayout(m_advPanel);
    advRow->setContentsMargins(20, 0, 0, 0);
    m_osdCheck = new QCheckBox(QStringLiteral("校正时间角标"), m_advPanel);
    m_osdCheck->setChecked(true);
    m_caseNoCheck = new QCheckBox(QStringLiteral("案件号"), m_advPanel);
    m_caseNoCheck->setChecked(true);
    m_panelsCheck = new QCheckBox(QStringLiteral("图表面板"), m_advPanel);
    m_panelsCheck->setChecked(true);
    m_panelsCheck->setToolTip(QStringLiteral("仅单片段、源为当前视频、原速时可用（走全保真复合导出）"));
    m_roiCheck = new QCheckBox(QStringLiteral("ROI 烧录"), m_advPanel);
    m_roiCheck->setChecked(true);
    m_roiCheck->setToolTip(QStringLiteral("单视频段：叠加 .vla 中的矩形/多边形 ROI（无数据自动缺席）"));
    m_chartCheck = new QCheckBox(QStringLiteral("曲线滚动条"), m_advPanel);
    m_chartCheck->setChecked(true);
    m_chartCheck->setToolTip(QStringLiteral("单视频段：底部烧录亮度/音量曲线滚动条（游标跟随，无数据自动缺席）"));
    advRow->addWidget(m_osdCheck);
    advRow->addWidget(m_caseNoCheck);
    advRow->addWidget(m_panelsCheck);
    advRow->addWidget(m_roiCheck);
    advRow->addWidget(m_chartCheck);
    advRow->addStretch(1);
    m_advPanel->setVisible(false);
    ov->addWidget(m_advPanel);

    auto *pathRow = new QHBoxLayout();
    m_outPath = new QLineEdit(outBox);
    auto *browseBtn = new QPushButton(QStringLiteral("更改…"), outBox);
    browseBtn->setFocusPolicy(Qt::NoFocus);
    pathRow->addWidget(new QLabel(QStringLiteral("保存到："), outBox));
    pathRow->addWidget(m_outPath, 1);
    pathRow->addWidget(browseBtn);
    ov->addLayout(pathRow);

    auto *progRow = new QHBoxLayout();
    m_progress = new QProgressBar(outBox);
    m_progress->setRange(0, 100);
    progRow->addWidget(m_progress, 1);
    m_etaLabel = new QLabel(QString(), outBox);
    m_etaLabel->setStyleSheet(QStringLiteral("color:#999;"));
    m_etaLabel->setMinimumWidth(190);
    progRow->addWidget(m_etaLabel);
    m_startBtn = new QPushButton(QStringLiteral("开始导出（Ctrl+E）"), outBox);
    m_startBtn->setObjectName(QStringLiteral("wbStartBtn"));
    m_startBtn->setMinimumHeight(34);
    m_startBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#2f5d8c;color:#fff;font-weight:bold;border-radius:5px;padding:0 18px;}"
        "QPushButton:disabled{background:#3a4048;color:#889;}"));
    m_cancelBtn = new QPushButton(QStringLiteral("取消导出"), outBox);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setFocusPolicy(Qt::NoFocus);
    m_closeBtn = new QPushButton(QStringLiteral("关 闭"), outBox);
    m_closeBtn->setFocusPolicy(Qt::NoFocus);
    m_openOutBtn = new QPushButton(QStringLiteral("📂 打开输出文件夹"), outBox);
    m_openOutBtn->setFocusPolicy(Qt::NoFocus);
    m_openOutBtn->setVisible(false);
    progRow->addWidget(m_openOutBtn);
    progRow->addWidget(m_startBtn);
    progRow->addWidget(m_cancelBtn);
    progRow->addWidget(m_closeBtn);
    ov->addLayout(progRow);
    m_status = new QLabel(QString(), outBox);
    m_status->setObjectName(QStringLiteral("wbStatus"));
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
    connect(m_recordBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onRecordToggle);
    connect(m_advToggle, &QToolButton::toggled, this, [this](bool on) {
        m_advPanel->setVisible(on);
        m_advToggle->setText(on ? QStringLiteral("更多选项 ▾")
                                : QStringLiteral("更多选项 ▸"));
    });
    connect(m_timeline, &ComposeTimelineWidget::moveRequested, this,
            &ComposeWorkbenchWindow::onBlockMove);
    connect(m_timeline, &ComposeTimelineWidget::editRequested, this,
            &ComposeWorkbenchWindow::onBlockEdit);
    connect(m_timeline, &ComposeTimelineWidget::removeRequested, this,
            &ComposeWorkbenchWindow::onBlockRemove);
    connect(m_timeline, &ComposeTimelineWidget::selectionChanged, this,
            [this](int) { updateGuide(); });
    connect(m_timeline, &ComposeTimelineWidget::rateRequested, this,
            &ComposeWorkbenchWindow::onBlockRate);
    connect(m_timeline, &ComposeTimelineWidget::annoRemoveRequested, this,
            &ComposeWorkbenchWindow::onAnnoRemove);
    connect(m_splitBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onSplitBlock);
    connect(m_annoSpotBtn, &QPushButton::clicked, this,
            &ComposeWorkbenchWindow::onAnnoToolSpotlight);
    connect(m_annoArrowBtn, &QPushButton::clicked, this,
            &ComposeWorkbenchWindow::onAnnoToolArrow);
    connect(m_annoCapBtn, &QPushButton::clicked, this,
            &ComposeWorkbenchWindow::onAnnoToolCaption);
    connect(m_demoRadio, &QRadioButton::toggled, this, &ComposeWorkbenchWindow::onModeChanged);
    connect(browseBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onBrowseOutput);
    connect(m_startBtn, &QPushButton::clicked, this, &ComposeWorkbenchWindow::onStartExport);
    connect(m_cancelBtn, &QPushButton::clicked, this,
            [this]() { emit cancelRequested(); });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_running)
            hide();
    });
    connect(m_openOutBtn, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(m_outPath->text()).absolutePath()));
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
            m_recordBtn->setEnabled(true);
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
    installShortcuts();
    updateGuide();
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
    multiRoot->setText(0, QStringLiteral("▦ 多机位同屏（勾 2~4 个）"));
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
    singleRoot->setText(0, QStringLiteral("🎞 单个视频（案内全部，含前处理产物）"));
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
    m_singlePreviewName.clear();
    m_multiActive = false;
    for (auto *t : m_multiTiles)
        t->deleteLater();
    m_multiTiles.clear();
    m_markIn = m_markOut = -1;
    if (m_recordBtn)
        m_recordBtn->setEnabled(false);
    updateGuide();
}

void ComposeWorkbenchWindow::loadSinglePreview(const QString &path,
                                               const QString &displayName) {
    stopPreviews();
    m_previewStack->setCurrentIndex(0);
    m_singleTile->setLaneName(displayName);
    m_singlePreviewName = displayName;
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
    m_recordBtn->setEnabled(true);
    updateGuide();
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
    m_recordBtn->setEnabled(true);
    updateGuide();
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
    // P2.6 播放头联动：预览位置落在哪个片段块 → 顶部▼标记
    int playIdx = -1;
    if (!m_segs.isEmpty()) {
        const qint64 pos = previewPosMs();
        if (m_multiActive) {
            for (int i = 0; i < m_segs.size(); ++i)
                if (m_segs[i].isLanes() && m_segs[i].inMs <= pos && pos < m_segs[i].outMs) {
                    playIdx = i; break;
                }
        } else if (!m_singlePreviewPath.isEmpty()) {
            const QString eff = effectivePath(m_singlePreviewPath);
            for (int i = 0; i < m_segs.size(); ++i)
                if (!m_segs[i].isLanes() && m_segs[i].sourcePath == eff
                    && m_segs[i].inMs <= pos && pos < m_segs[i].outMs) {
                    playIdx = i; break;
                }
        }
    }
    m_timeline->setPlaySeg(playIdx);
    m_playSegIdx = playIdx;
    // 切割钮：预览位置严格落在某段内部（两端留 200ms）才可用
    bool canSplit = false;
    if (playIdx >= 0 && !m_running) {
        const auto &sg = m_segs[playIdx];
        const qint64 pos = previewPosMs();
        canSplit = (pos - sg.inMs > 200) && (sg.outMs - pos > 200);
    }
    if (m_splitBtn)
        m_splitBtn->setEnabled(canSplit);
    // 标注钮：预览位置落在单视频段内（v1 宫格段不支持标注）
    const bool canAnno = canSplit || (playIdx >= 0 && !m_running
                                      && !m_segs[playIdx].isLanes());
    const bool annoSingle = playIdx >= 0 && !m_running && !m_segs[playIdx].isLanes();
    if (m_annoSpotBtn) {
        m_annoSpotBtn->setEnabled(annoSingle);
        m_annoArrowBtn->setEnabled(annoSingle);
        m_annoCapBtn->setEnabled(annoSingle);
    }
    Q_UNUSED(canAnno);
    updateGuide();
}

void ComposeWorkbenchWindow::updateGuide() {
    const bool hasMaterial = m_multiActive
        ? (m_svc && m_svc->laneCount() >= 2)
        : !m_singlePreviewPath.isEmpty();
    const bool hasSegs = !m_segs.isEmpty();
    int cur = 0;
    if (hasMaterial && !hasSegs) cur = 1;
    else if (hasSegs) cur = 3;
    static const char *kOn =
        "color:#fff;background:#3d6b8e;padding:2px 8px;border-radius:3px;font-weight:bold;";
    static const char *kDone = "color:#7ec97e;padding:2px 8px;font-weight:bold;";
    static const char *kOff  = "color:#888;padding:2px 8px;font-weight:bold;";
    const bool done[4] = {hasMaterial, hasSegs, hasSegs, false};
    for (int i = 0; i < 4; ++i)
        m_stepLabels[i]->setStyleSheet(QString::fromLatin1(
            i == cur ? kOn : (done[i] ? kDone : kOff)));
    if (cur == 0)
        m_guideHint->setText(QStringLiteral(
            "先在左边点一个视频；要多机位同屏就勾 2~4 个机位"));
    else if (cur == 1)
        m_guideHint->setText(QStringLiteral(
            "空格播放/暂停，拖到关键画面 → 按 I（或红点）开始，按 O 结束并加入清单；←/→ 逐帧微调"));
    else
        m_guideHint->setText(QStringLiteral(
            "下方清单可拖动排序、双击微调、Delete 删除选中；确认类型后点『开始导出』（Ctrl+E）"));
    if (m_startBtn)
        m_startBtn->setEnabled(hasSegs && !m_running);
    // 记录钮可视状态（armed=已设起点待收点）
    if (m_recordBtn) {
        const bool armed = m_markIn >= 0;
        m_recordBtn->setText(armed ? QStringLiteral("⏹ 到这里，加入清单（O）")
                                   : QStringLiteral("⏺ 从这里开始（I）"));
        m_recordBtn->setStyleSheet(
            armed ? QStringLiteral(
                "QPushButton{background:#c0392b;color:#fff;font-weight:bold;border-radius:5px;padding:0 14px;}"
                "QPushButton:disabled{background:#4a3a3a;color:#998;}")
                  : QStringLiteral(
                "QPushButton{background:#8c2f2f;color:#fff;font-weight:bold;border-radius:5px;padding:0 14px;}"
                "QPushButton:disabled{background:#4a3a3a;color:#998;}"));
    }
}

void ComposeWorkbenchWindow::onRecordToggle() {
    // 红点 = 录音笔式单键流：第一下=起点（等价 I），第二下=终点并加入（等价 O）
    if (m_markIn < 0)
        onMarkIn();
    else
        onMarkOut();
}

void ComposeWorkbenchWindow::seekPreviewRelative(qint64 deltaMs) {
    if (!m_slider->isEnabled())
        return;
    if (m_multiActive && m_svc) {
        const qint64 lo = m_svc->contentStartWallMs(), hi = m_svc->contentEndWallMs();
        m_svc->seekWall(qBound(lo, m_svc->clockWallMs() + deltaMs, hi));
    } else if (m_singleEngine) {
        m_singleEngine->seek(qBound<qint64>(0, m_singleEngine->position() + deltaMs,
                                            m_singleEngine->duration()));
    }
    updateTransport();
}

void ComposeWorkbenchWindow::installShortcuts() {
    // 键位口径对齐剪映/PR；输入框/下拉聚焦时不抢键（ShortcutOverride 已挡大半，双保险）
    auto mk = [this](const QKeySequence &ks, std::function<void()> fn) {
        auto *sc = new QShortcut(ks, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this, fn]() {
            if (m_running)
                return;
            if (QWidget *f = focusWidget())
                if (qobject_cast<QLineEdit *>(f) || qobject_cast<QAbstractSpinBox *>(f)
                    || qobject_cast<QComboBox *>(f))
                    return;
            fn();
        });
    };
    mk(QKeySequence(Qt::Key_Space), [this] { if (m_playBtn->isEnabled()) onPlayPause(); });
    mk(QKeySequence(Qt::Key_K),     [this] { if (m_playBtn->isEnabled()) onPlayPause(); });
    mk(QKeySequence(Qt::Key_I),     [this] { if (m_inBtn->isEnabled()) onMarkIn(); });
    mk(QKeySequence(Qt::Key_O),     [this] { if (m_outBtn->isEnabled()) onMarkOut(); });
    mk(QKeySequence(Qt::Key_Return),[this] { if (m_markIn >= 0) onMarkOut(); });
    mk(QKeySequence(Qt::Key_Enter), [this] { if (m_markIn >= 0) onMarkOut(); });
    const qint64 frame = qint64(1000.0 / qMax(1.0, m_fps));
    mk(QKeySequence(Qt::Key_Left),  [this, frame] { seekPreviewRelative(-frame); });
    mk(QKeySequence(Qt::Key_Right), [this, frame] { seekPreviewRelative(frame); });
    mk(QKeySequence(Qt::SHIFT | Qt::Key_Left),  [this] { seekPreviewRelative(-1000); });
    mk(QKeySequence(Qt::SHIFT | Qt::Key_Right), [this] { seekPreviewRelative(1000); });
    mk(QKeySequence(Qt::Key_J), [this] { seekPreviewRelative(-5000); });
    mk(QKeySequence(Qt::Key_L), [this] { seekPreviewRelative(5000); });
    mk(QKeySequence(Qt::Key_Home), [this] {
        if (m_multiActive && m_svc) m_svc->seekWall(m_svc->contentStartWallMs());
        else if (m_singleEngine) m_singleEngine->seek(0);
    });
    mk(QKeySequence(Qt::Key_End), [this] {
        if (m_multiActive && m_svc) m_svc->seekWall(m_svc->contentEndWallMs());
        else if (m_singleEngine && m_singleEngine->duration() > 0)
            m_singleEngine->seek(m_singleEngine->duration() - 50);
    });
    mk(QKeySequence(Qt::Key_Delete), [this] {
        const int idx = m_timeline->selected();
        if (idx >= 0)
            onBlockRemove(idx);
    });
    mk(QKeySequence(QStringLiteral("Ctrl+B")), [this] {   // 剪映同款分割
        if (m_splitBtn->isEnabled())
            onSplitBlock();
    });
    mk(QKeySequence(QStringLiteral("Ctrl+E")), [this] {
        if (m_startBtn->isEnabled())
            onStartExport();
    });
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
    if (m_markIn >= 0 && pos <= m_markIn) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("终点在起点之前——请拖到起点后面的画面再按 O"));
        return;
    }
    m_markOut = pos;
    updateTransport();
    if (m_markIn >= 0)
        onAddSegment();          // O = 设终点并立即加入清单（一拍成片）
}

// ---------------------------------------------------------------------------
// 时间线操作
// ---------------------------------------------------------------------------
void ComposeWorkbenchWindow::onAddSegment() {
    if (m_markIn < 0) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("先按 I（或点红色圆钮）在画面上设起点"));
        return;
    }
    SegmentExportEngine::Params::ComposeSeg seg;
    const qint64 inMs = m_markIn;
    const qint64 outMs = m_markOut >= 0 ? m_markOut : previewPosMs();
    if (outMs <= inMs) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("终点须在起点之后"));
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
        seg.displayName = m_singlePreviewName.isEmpty()
            ? QFileInfo(seg.sourcePath).fileName() : m_singlePreviewName;
    }
    m_segs << seg;
    m_markIn = m_markOut = -1;   // 提交后清打点，记录钮回到待开始
    syncTimeline();
    m_status->clear();
    updateGuide();
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

int ComposeWorkbenchWindow::segIndexAtPreviewPos() const {
    return m_playSegIdx;
}

void ComposeWorkbenchWindow::onSplitBlock() {
    const int idx = segIndexAtPreviewPos();
    if (idx < 0 || idx >= m_segs.size())
        return;
    const qint64 cut = previewPosMs();
    auto &sg = m_segs[idx];
    if (cut - sg.inMs <= 200 || sg.outMs - cut <= 200) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("切点太靠近片段边缘（两端至少留 0.2 秒）"));
        return;
    }
    auto right = sg;                 // 右半：同素材/同倍速/同宫格
    sg.outMs = cut;                  // 左半收尾
    right.inMs = cut;                // 右半起刀
    // 标注按切点分家：整段在左→左；整段在右→右；跨界→两边各留夹取副本
    QVector<SegmentExportEngine::Params::ComposeAnno> leftAn, rightAn;
    for (const auto &an : sg.annos) {
        if (an.outMs <= cut) leftAn << an;
        else if (an.inMs >= cut) rightAn << an;
        else {
            auto a1 = an; a1.outMs = cut; leftAn << a1;
            auto a2 = an; a2.inMs = cut; rightAn << a2;
        }
    }
    sg.annos = leftAn;
    right.annos = rightAn;
    m_segs.insert(idx + 1, right);
    syncTimeline();
    m_status->setStyleSheet(QStringLiteral("color:#7ec97e;"));
    m_status->setText(QStringLiteral("已在此切成 %1 段（两半可各自右键调倍速）")
                          .arg(m_segs.size()));
}

void ComposeWorkbenchWindow::onBlockRate(int idx, double rate) {
    if (idx >= 0 && idx < m_segs.size()) {
        m_segs[idx].rate = rate;
        syncTimeline();
    }
}

void ComposeWorkbenchWindow::onAnnoRemove(int segIdx, int annoIdx) {
    if (segIdx >= 0 && segIdx < m_segs.size()
        && annoIdx >= 0 && annoIdx < m_segs[segIdx].annos.size()) {
        m_segs[segIdx].annos.removeAt(annoIdx);
        syncTimeline();
    }
}

// ---------------------------------------------------------------------------
// 标注轨（P2.7）
// ---------------------------------------------------------------------------
void ComposeWorkbenchWindow::appendAnnoFromDialog(int type, const QRectF &normRect) {
    const int idx = segIndexAtPreviewPos();
    if (idx < 0 || m_segs[idx].isLanes())
        return;
    auto &sg = m_segs[idx];
    const qint64 pos = qBound(sg.inMs, previewPosMs(), sg.outMs);

    QDialog dlg(this);
    dlg.setWindowTitle(type == 2 ? QStringLiteral("添加字幕")
                                 : (type == 0 ? QStringLiteral("添加聚光灯")
                                              : QStringLiteral("添加箭头")));
    auto *lay = new QFormLayout(&dlg);
    auto *inEdit = new QLineEdit(formatMs(qMax(sg.inMs, pos - 1500)), &dlg);
    auto *outEdit = new QLineEdit(formatMs(qMin(sg.outMs, pos + 1500)), &dlg);
    lay->addRow(QStringLiteral("开始（该视频时刻）"), inEdit);
    lay->addRow(QStringLiteral("结束"), outEdit);
    QLineEdit *textEdit = nullptr;
    if (type == 2) {
        textEdit = new QLineEdit(&dlg);
        textEdit->setPlaceholderText(QStringLiteral("解说词，如：起火点位于画面中央"));
        lay->addRow(QStringLiteral("字幕文本"), textEdit);
    }
    auto *colorCombo = new QComboBox(&dlg);
    colorCombo->addItem(QStringLiteral("黄"), 0xffe14d);
    colorCombo->addItem(QStringLiteral("红"), 0xff6060);
    colorCombo->addItem(QStringLiteral("蓝"), 0x64b4ff);
    colorCombo->addItem(QStringLiteral("绿"), 0x7ec97e);
    if (type == 1) colorCombo->setCurrentIndex(1);
    if (type != 2)
        lay->addRow(QStringLiteral("颜色"), colorCombo);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                      &dlg);
    lay->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;

    bool ok1 = false, ok2 = false;
    const qint64 a = parseMs(inEdit->text(), &ok1);
    const qint64 b = parseMs(outEdit->text(), &ok2);
    if (!ok1 || !ok2 || b - a < 200) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("标注起止非法（至少 0.2 秒）"));
        return;
    }
    SegmentExportEngine::Params::ComposeAnno an;
    an.type = type == 0 ? SegmentExportEngine::Params::ComposeAnno::Spotlight
                        : (type == 1 ? SegmentExportEngine::Params::ComposeAnno::Arrow
                                     : SegmentExportEngine::Params::ComposeAnno::Caption);
    an.inMs = qBound(sg.inMs, a, sg.outMs);
    an.outMs = qBound(sg.inMs, b, sg.outMs);
    an.rect = normRect;
    an.colorRgb = quint32(colorCombo->currentData().toInt());
    if (textEdit) {
        an.text = textEdit->text().trimmed();
        if (an.text.isEmpty())
            return;
    }
    sg.annos << an;
    syncTimeline();
    m_status->setStyleSheet(QStringLiteral("color:#7ec97e;"));
    m_status->setText(QStringLiteral("标注已加入（仅演示片烧录；证据片段画面零改动不携带）"));
}

void ComposeWorkbenchWindow::onAnnoToolSpotlight() {
    if (m_singleTile->zoom() > 1.01) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("先中键复位画面缩放再框选标注"));
        return;
    }
    if (!m_pickOverlay) {
        auto *page = m_previewStack->widget(0);
        m_pickOverlay = new AnnoPickOverlay(
            page, m_singleTile,
            [this](const QRectF &n) { appendAnnoFromDialog(m_annoPickMode, n); },
            [this]() {});
        m_pickOverlay->setGeometry(m_singleTile->geometry());
    }
    m_annoPickMode = 0;
    m_pickOverlay->setGeometry(m_singleTile->geometry());
    m_pickOverlay->beginPick();
}

void ComposeWorkbenchWindow::onAnnoToolArrow() {
    onAnnoToolSpotlight();          // 同一框选流（起点→终点=框对角线）
    m_annoPickMode = 1;
}

void ComposeWorkbenchWindow::onAnnoToolCaption() {
    appendAnnoFromDialog(2, QRectF(0, 0, 1, 1));   // 字幕无需框选
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
    QComboBox *layoutCombo = nullptr;
    if (seg.isLanes()) {
        layoutCombo = new QComboBox(&dlg);
        layoutCombo->addItem(QStringLiteral("均分宫格"));
        layoutCombo->addItem(QStringLiteral("主听路大窗（左侧 2/3）"));
        layoutCombo->setCurrentIndex(seg.gridLayout == 1 ? 1 : 0);
        lay->addRow(QStringLiteral("宫格布局"), layoutCombo);
    }
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
    if (layoutCombo)
        seg.gridLayout = layoutCombo->currentIndex() == 1 ? 1 : 0;
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
    m_roiCheck->setEnabled(demo && !m_running);
    m_chartCheck->setEnabled(demo && !m_running);
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
    // + P2：vlaPathByPath（ROI/曲线数据源）
    if (!pp.evidenceCopy) {
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
            pp.vlaPathByPath.insert(sp, vlaPath);
            if (pp.burnOsd) {
                const TimeCalibration cal = TimelineModel::peekCalibrationFromVla(vlaPath);
                if (cal.isValid())
                    pp.calibrationByPath.insert(sp, cal);
            }
        }
    }
    const Credential cred = CredentialStore::load();
    pp.operatorName = cred.name;
    pp.operatorOrg = cred.org;
    // P2：单视频段烧录开关（全局勾选 → 逐段生效；无数据段自动缺席）
    if (!pp.evidenceCopy) {
        for (auto &s : pp.segments)
            if (!s.isLanes()) {
                s.burnRoi = m_roiCheck->isChecked();
                s.burnChart = m_chartCheck->isChecked();
            }
    }
    return pp;
}

void ComposeWorkbenchWindow::onStartExport() {
    if (m_evidenceRadio->isChecked()) {
        int annoCount = 0;
        for (const auto &sg : m_segs) annoCount += sg.annos.size();
        if (annoCount > 0) {
            m_status->setStyleSheet(QStringLiteral("color:#d4a017;"));
            m_status->setText(QStringLiteral(
                "提示：%1 条标注只在演示片烧录，证据片段画面零改动不会携带。").arg(annoCount));
        }
    }
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
        m_elapsed.start();
        if (m_openOutBtn)
            m_openOutBtn->setVisible(false);
        if (m_etaLabel)
            m_etaLabel->setText(QStringLiteral("已用 0:00 · 估算中…"));
        m_status->setStyleSheet(QStringLiteral("color:#666;"));
        m_status->setText(QStringLiteral("导出进行中…"));
    }
    onModeChanged();
    syncTimeline();
    updateGuide();
}

void ComposeWorkbenchWindow::setProgress(int done, int total) {
    if (m_etaLabel && m_elapsed.isValid() && total > 0) {
        const qint64 el = m_elapsed.elapsed();
        auto mmss = [](qint64 ms) {
            return QStringLiteral("%1:%2").arg(ms / 60000, 2, 10, QLatin1Char('0'))
                                          .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
        };
        if (done > 24 && done < total) {
            const qint64 eta = qint64(double(total - done) * double(el) / double(done));
            m_etaLabel->setText(QStringLiteral("已用 %1 · 预计剩余 %2")
                                    .arg(mmss(el), mmss(eta)));
        } else if (done >= total) {
            m_etaLabel->setText(QStringLiteral("共用时 %1").arg(mmss(el)));
        }
    }
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
        m_openOutBtn->setVisible(true);
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
