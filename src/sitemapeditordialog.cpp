#include "sitemapeditordialog.h"

#include "app/case_manager.h"
#include "infrastructure/site_map_render.h"
#include "theme.h"

#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

// ---------------------------------------------------------------------------
// 机位侧栏（拖拽源：mime = 机位 id）
// ---------------------------------------------------------------------------
class LaneListWidget : public QListWidget
{
public:
    using QListWidget::QListWidget;

protected:
    void startDrag(Qt::DropActions supported) override   // 视图级拖拽注入机位 id
    {
        Q_UNUSED(supported);
        QListWidgetItem *item = currentItem();
        if (!item)
            return;
        auto *mime = new QMimeData;
        mime->setText(item->data(Qt::UserRole).toString());
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};

// ---------------------------------------------------------------------------
// 画布：底图平移缩放 + 点位拖放/移动/选中；滚轮在选中点位上转朝向
// ---------------------------------------------------------------------------
class SiteMapCanvas : public QWidget
{
public:
    SiteMapData *data = nullptr;
    QImage base;
    QHash<QString, QColor> laneColor;
    int selected = -1;

    std::function<void(int)> onSelected;
    std::function<void()> onChanged;   // 点位几何被编辑（存盘提示）

    explicit SiteMapCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAcceptDrops(true);
        setMinimumSize(400, 300);
        setMouseTracking(false);
    }

    void fitView()
    {
        m_zoom = 1.0;
        m_pan = QPointF(0, 0);
        update();
    }

protected:
    // 视图（像素）→ 图像（像素）
    QRectF imageRect() const
    {
        if (base.isNull())
            return {};
        const double s = qMin(width() / double(base.width()),
                              height() / double(base.height())) * m_zoom;
        const double w = base.width() * s, h = base.height() * s;
        return QRectF((width() - w) / 2 + m_pan.x(),
                      (height() - h) / 2 + m_pan.y(), w, h);
    }
    QPointF viewToNorm(const QPointF &v) const
    {
        const QRectF r = imageRect();
        if (r.isEmpty())
            return QPointF(0.5, 0.5);
        return QPointF(qBound(0.0, (v.x() - r.left()) / r.width(), 1.0),
                       qBound(0.0, (v.y() - r.top()) / r.height(), 1.0));
    }
    QPointF normToView(double x, double y) const
    {
        const QRectF r = imageRect();
        return QPointF(r.left() + x * r.width(), r.top() + y * r.height());
    }
    int hitPoint(const QPointF &v) const
    {
        if (!data)
            return -1;
        const double tol = 14.0;   // 像素命中半径
        for (int i = data->points.size() - 1; i >= 0; --i) {
            const QPointF c = normToView(data->points[i].x, data->points[i].y);
            if (QLineF(c, v).length() <= tol)
                return i;
        }
        return -1;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(46, 46, 52));
        const QRectF r = imageRect();
        if (base.isNull()) {
            p.setPen(QColor(200, 200, 200));
            p.drawText(rect(), Qt::AlignCenter,
                       tr("尚未导入底图——工具行「导入底图」选择现场平面图/地图截图"));
            return;
        }
        p.drawImage(r, base);
        if (data)
            sitemaprender::drawPoints(p, *data, r, laneColor, selected);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            const int hit = hitPoint(e->pos());
            if (hit >= 0) {
                selected = hit;
                m_dragging = true;
                m_moved = false;
                m_pressPos = e->pos();
                if (onSelected)
                    onSelected(hit);
            } else {
                selected = -1;
                m_panning = true;
                m_pressPos = e->pos();
                if (onSelected)
                    onSelected(-1);
            }
            setCursor(m_panning ? Qt::ClosedHandCursor : Qt::ArrowCursor);
            update();
        }
        QWidget::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragging && selected >= 0 && data) {
            const QPointF n = viewToNorm(e->pos());
            data->points[selected].x = n.x();
            data->points[selected].y = n.y();
            m_moved = true;
            if (onChanged)
                onChanged();
            update();
        } else if (m_panning) {
            m_pan += e->pos() - m_pressPos;
            m_pressPos = e->pos();
            update();
        }
        QWidget::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_dragging = m_panning = false;
        m_moved = false;
        setCursor(Qt::ArrowCursor);
    }
    void wheelEvent(QWheelEvent *e) override
    {
        // 选中点位 + 光标在其附近：滚轮转朝向；Shift+滚轮调张角；否则缩放
        if (selected >= 0 && data) {
            const QPointF c = normToView(data->points[selected].x,
                                         data->points[selected].y);
            const QRectF r = imageRect();
            const double radiusPx = data->points[selected].radiusPct / 100.0
                * qMin(r.width(), r.height());
            if (QLineF(c, e->position()).length() <= radiusPx + 16) {
                const double step = e->angleDelta().y() > 0 ? 5.0 : -5.0;
                if (e->modifiers() & Qt::ShiftModifier)
                    data->points[selected].spreadDeg =
                        qBound(10.0, data->points[selected].spreadDeg + step, 180.0);
                else
                    data->points[selected].headingDeg =
                        std::fmod(data->points[selected].headingDeg + step + 360.0, 360.0);
                if (onChanged)
                    onChanged();
                if (onSelected)
                    onSelected(selected);   // 属性条回填
                update();
                e->accept();
                return;
            }
        }
        const double factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        m_zoom = qBound(0.2, m_zoom * factor, 8.0);
        update();
        e->accept();
    }

    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasText())
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        if (!data || base.isNull() || !e->mimeData()->hasText())
            return;
        const QString laneId = e->mimeData()->text();
        // 同机位已布点 → 挪位而非重复（一案一点语义由用户掌控）
        for (int i = 0; i < data->points.size(); ++i)
            if (data->points[i].laneRef == laneId) {
                const QPointF n = viewToNorm(e->position().toPoint());
                data->points[i].x = n.x();
                data->points[i].y = n.y();
                selected = i;
                if (onSelected) onSelected(i);
                if (onChanged) onChanged();
                update();
                e->acceptProposedAction();
                return;
            }
        SiteMapPoint pt;
        pt.laneRef = laneId;
        pt.label = laneLabel(laneId);
        const QPointF n = viewToNorm(e->position().toPoint());
        pt.x = n.x();
        pt.y = n.y();
        data->points.append(pt);
        selected = data->points.size() - 1;
        if (onSelected) onSelected(selected);
        if (onChanged) onChanged();
        update();
        e->acceptProposedAction();
    }

private:
    QString laneLabel(const QString &id) const
    {
        return laneLabels.value(id, id);
    }

public:
    QHash<QString, QString> laneLabels;

private:
    double m_zoom = 1.0;
    QPointF m_pan{0, 0};
    bool m_dragging = false;
    bool m_panning = false;
    bool m_moved = false;
    QPointF m_pressPos;
};

// ---------------------------------------------------------------------------
// 对话框
// ---------------------------------------------------------------------------
SiteMapEditorDialog::SiteMapEditorDialog(CaseManager *cm, QWidget *parent)
    : QDialog(parent), m_cm(cm)
{
    setWindowTitle(tr("监控点位图编辑器"));
    resize(1100, 760);

    m_data = SiteMapData::load(m_cm->caseDir());
    if (!m_data.baseImageRel.isEmpty()) {
        const QString abs = m_cm->caseDir() + '/' + m_data.baseImageRel;
        m_base.load(abs);
    }
    syncLaneColors();
    markOrphans();

    auto *lay = new QVBoxLayout(this);

    // ---- 工具行 ----
    auto *bar = new QHBoxLayout;
    auto mkBtn = [&](const QString &t) -> QPushButton * {
        auto *b = new QPushButton(t, this);
        bar->addWidget(b);
        return b;
    };
    auto *importBtn = mkBtn(tr("导入底图"));
    auto *fitBtn = mkBtn(tr("适应窗口"));
    m_delBtn = mkBtn(tr("删除选中"));
    auto *exportBtn = mkBtn(tr("出图保存（图框版）"));
    bar->addStretch();
    auto *closeBtn = mkBtn(tr("关闭"));
    lay->addLayout(bar);

    // ---- 主区：侧栏 + 画布 ----
    auto *mid = new QHBoxLayout;
    auto *sideBox = new QVBoxLayout;
    sideBox->addWidget(new QLabel(tr("机位（拖到画布布点）："), this));
    m_laneList = new LaneListWidget(this);
    m_laneList->setDragEnabled(true);
    m_laneList->setMaximumWidth(200);
    for (const CaseVideoRef &v : m_cm->meta().videos) {
        const QString label = v.cameraLabel.isEmpty() ? v.id : v.cameraLabel;
        auto *it = new QListWidgetItem(label + QStringLiteral("（") + v.id
                                       + QStringLiteral("）"));
        it->setData(Qt::UserRole, v.id);
        m_laneList->addItem(it);
    }
    sideBox->addWidget(m_laneList);
    mid->addLayout(sideBox);

    m_canvas = new SiteMapCanvas(this);
    m_canvas->data = &m_data;
    m_canvas->base = m_base;
    m_canvas->laneColor = m_laneColor;
    for (const CaseVideoRef &v : m_cm->meta().videos)
        m_canvas->laneLabels[v.id] = v.cameraLabel.isEmpty() ? v.id : v.cameraLabel;
    mid->addWidget(m_canvas, 1);
    lay->addLayout(mid, 1);

    // ---- 属性条 ----
    auto *prop = new QHBoxLayout;
    prop->addWidget(new QLabel(tr("选中点位："), this));
    m_heading = new QDoubleSpinBox(this);
    m_heading->setRange(0, 359);
    m_heading->setSuffix(QStringLiteral(" °朝向"));
    m_spread = new QDoubleSpinBox(this);
    m_spread->setRange(10, 180);
    m_spread->setSuffix(QStringLiteral(" °张角"));
    m_radius = new QDoubleSpinBox(this);
    m_radius->setRange(3, 50);
    m_radius->setSuffix(QStringLiteral(" %半径"));
    prop->addWidget(m_heading);
    prop->addWidget(m_spread);
    prop->addWidget(m_radius);
    m_hint = new QLabel(tr("滚轮在选中扇面上=转朝向；Shift+滚轮=调张角"), this);
    prop->addWidget(m_hint, 1);
    lay->addLayout(prop);

    auto setPropEnabled = [&](bool on) {
        m_heading->setEnabled(on);
        m_spread->setEnabled(on);
        m_radius->setEnabled(on);
        m_delBtn->setEnabled(on);
    };
    setPropEnabled(false);

    m_canvas->onSelected = [this, setPropEnabled](int idx) {
        setPropEnabled(idx >= 0);
        if (idx < 0)
            return;
        const SiteMapPoint &pt = m_data.points[idx];
        for (QDoubleSpinBox *sp : {m_heading, m_spread, m_radius})
            sp->blockSignals(true);
        m_heading->setValue(pt.headingDeg);
        m_spread->setValue(pt.spreadDeg);
        m_radius->setValue(pt.radiusPct);
        for (QDoubleSpinBox *sp : {m_heading, m_spread, m_radius})
            sp->blockSignals(false);
        m_hint->setText(pt.orphan
            ? tr("⚠️ 该机位已不在案件（孤儿点位，可删除）")
            : tr("滚轮在选中扇面上=转朝向；Shift+滚轮=调张角"));
    };
    m_canvas->onChanged = [this]() { saveData(); };

    auto applyTo = [this](std::function<void(SiteMapPoint &)> setter) {
        const int idx = m_canvas->selected;
        if (idx >= 0 && idx < m_data.points.size()) {
            setter(m_data.points[idx]);
            m_canvas->update();
            saveData();
        }
    };
    connect(m_heading, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, applyTo](double v) {
                applyTo([v](SiteMapPoint &p) { p.headingDeg = v; }); });
    connect(m_spread, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, applyTo](double v) {
                applyTo([v](SiteMapPoint &p) { p.spreadDeg = v; }); });
    connect(m_radius, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, applyTo](double v) {
                applyTo([v](SiteMapPoint &p) { p.radiusPct = v; }); });

    connect(importBtn, &QPushButton::clicked, this, &SiteMapEditorDialog::importBase);
    connect(fitBtn, &QPushButton::clicked, this, [this]() { m_canvas->fitView(); });
    connect(m_delBtn, &QPushButton::clicked, this, [this]() {
        const int idx = m_canvas->selected;
        if (idx < 0 || idx >= m_data.points.size())
            return;
        const auto ret = QMessageBox::question(this, tr("删除点位"),
            tr("删除点位「%1」？").arg(m_data.points[idx].label));
        if (ret != QMessageBox::Yes)
            return;
        m_data.points.removeAt(idx);
        m_canvas->selected = -1;
        m_canvas->update();
        saveData();
    });
    connect(exportBtn, &QPushButton::clicked, this, &SiteMapEditorDialog::exportFramed);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void SiteMapEditorDialog::syncLaneColors()
{
    m_laneColor.clear();
    int i = 0;
    for (const CaseVideoRef &v : m_cm->meta().videos) {
        m_laneColor[v.id] = Theme::DataPalette[i % Theme::DataPalette.size()];
        ++i;
    }
}

void SiteMapEditorDialog::markOrphans()
{
    for (SiteMapPoint &pt : m_data.points) {
        pt.orphan = !m_cm->videoById(pt.laneRef);
        if (!pt.orphan) {
            // 机位改名跟随（图上标签同步案内最新）
            const QString label = m_cm->videoById(pt.laneRef)->cameraLabel;
            if (!label.isEmpty())
                pt.label = label;
        }
    }
}

void SiteMapEditorDialog::saveData()
{
    if (!m_data.save(m_cm->caseDir()))
        qWarning() << "sitemap: save failed";
}

void SiteMapEditorDialog::importBase()
{
    const QString src = QFileDialog::getOpenFileName(this, tr("导入点位图底图"),
        QString(), tr("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if (src.isEmpty())
        return;
    QImage img(src);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("导入底图"), tr("图片无法读取：%1").arg(src));
        return;
    }
    const QString rel = QStringLiteral("reports/assets/sitemap_base.")
                        + QFileInfo(src).suffix().toLower();
    const QString dst = m_cm->caseDir() + '/' + rel;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (QFile::exists(dst))
        QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        QMessageBox::warning(this, tr("导入底图"), tr("复制入案件失败：%1").arg(dst));
        return;
    }
    m_data.baseImageRel = rel;
    m_base = img;
    m_canvas->base = m_base;
    m_canvas->fitView();
    m_canvas->update();
    saveData();
    qInfo() << "sitemap: base imported" << dst;
}

void SiteMapEditorDialog::exportFramed()
{
    if (m_base.isNull()) {
        QMessageBox::warning(this, tr("出图保存"), tr("请先导入底图。"));
        return;
    }
    markOrphans();
    const CaseMeta &meta = m_cm->meta();
    const QImage out = sitemaprender::renderFramed(
        m_data, m_base, m_laneColor, meta.caseNo, meta.investigator,
        meta.extraFields.value(QStringLiteral("report/reviewer")),
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd")));
    const QString dst = m_cm->caseDir()
                        + QStringLiteral("/reports/assets/sitemap.png");
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (!out.save(dst)) {
        QMessageBox::critical(this, tr("出图保存"), tr("写出失败：%1").arg(dst));
        return;
    }
    saveData();
    QMessageBox::information(this, tr("出图保存"),
        tr("成品图已保存：%1\n\n生成分析报告时将自动嵌入「二（三）监控点位图」。").arg(dst));
    qInfo() << "sitemap: framed exported" << dst;
}
