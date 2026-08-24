#include "sitemapeditordialog.h"

#include "app/case_manager.h"
#include <QDialogButtonBox>
#include "infrastructure/site_map_render.h"
#include "theme.h"

#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
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
// 导入裁切对话框（拍板：导入底图后给一次裁切机会，确认后固定不再移动）
// ---------------------------------------------------------------------------
class ImageCropDialog : public QDialog
{
public:
    ImageCropDialog(const QImage &img, QWidget *parent = nullptr)
        : QDialog(parent), m_img(img)
    {
        setWindowTitle(tr("裁切底图（拖出保留区域；不裁切直接点「整张使用」）"));
        resize(900, 640);
        auto *lay = new QVBoxLayout(this);
        m_view = new CropView(this);
        m_view->setImage(m_img);
        lay->addWidget(m_view, 1);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("提示：框选监控覆盖的有效区域，去掉无关黑边/桌面边角。"), this));
        row->addStretch();
        auto *wholeBtn = new QPushButton(tr("整张使用"), this);
        auto *cropBtn = new QPushButton(tr("确认裁切"), this);
        cropBtn->setDefault(true);
        cropBtn->setEnabled(false);
        m_view->onSel = [cropBtn](bool has) { cropBtn->setEnabled(has); };
        connect(wholeBtn, &QPushButton::clicked, this, [this]() {
            m_crop = QRect();   // 空=整张
            accept();
        });
        connect(cropBtn, &QPushButton::clicked, this, [this]() {
            m_crop = m_view->selectionImageRect();
            accept();
        });
        auto *cancelBtn = new QPushButton(tr("取消"), this);
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        row->addWidget(wholeBtn);
        row->addWidget(cropBtn);
        row->addWidget(cancelBtn);
        lay->addLayout(row);
    }
    /// 空 rect = 整张使用
    QRect cropRect() const { return m_crop; }

private:
    class CropView : public QWidget
    {
    public:
        std::function<void(bool)> onSel;
        explicit CropView(QWidget *p) : QWidget(p) { setMinimumSize(400, 300); }
        void setImage(const QImage &img) { m_img = img; update(); }
        QRect selectionImageRect() const
        {
            if (m_sel.isNull())
                return {};
            const QRectF r = imgRect();
            const double sx = m_img.width() / r.width();
            const double sy = m_img.height() / r.height();
            const QRectF inImg((m_sel.left() - r.left()) * sx,
                               (m_sel.top() - r.top()) * sy,
                               m_sel.width() * sx, m_sel.height() * sy);
            return inImg.toAlignedRect().intersected(m_img.rect());
        }

    protected:
        QRectF imgRect() const
        {
            const double s = qMin((width() - 20) / double(m_img.width()),
                                  (height() - 20) / double(m_img.height()));
            const double w = m_img.width() * s, h = m_img.height() * s;
            return QRectF((width() - w) / 2, (height() - h) / 2, w, h);
        }
        void paintEvent(QPaintEvent *) override
        {
            QPainter p(this);
            p.fillRect(rect(), QColor(46, 46, 52));
            const QRectF r = imgRect();
            p.drawImage(r, m_img);
            // 选区外压暗
            if (!m_sel.isNull()) {
                QRegion outside(QRect(0, 0, width(), height()));
                outside = outside.subtracted(QRegion(m_sel.toRect()));
                p.setClipRegion(outside);
                p.fillRect(rect(), QColor(0, 0, 0, 150));
                p.setClipping(false);
                p.setPen(QPen(QColor(255, 210, 60), 2, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(m_sel);
            }
        }
        void mousePressEvent(QMouseEvent *e) override
        {
            if (e->button() == Qt::LeftButton && imgRect().contains(e->pos())) {
                m_anchor = e->pos();
                m_sel = QRectF(m_anchor, QSizeF(1, 1));
                m_dragging = true;
            }
        }
        void mouseMoveEvent(QMouseEvent *e) override
        {
            if (m_dragging) {
                m_sel = QRectF(m_anchor, e->pos()).normalized()
                            .intersected(imgRect());
                update();
            }
        }
        void mouseReleaseEvent(QMouseEvent *) override
        {
            if (m_dragging) {
                m_dragging = false;
                if (m_sel.width() < 8 || m_sel.height() < 8)
                    m_sel = QRectF();   // 误点=清选区
                if (onSel)
                    onSel(!m_sel.isNull());
                update();
            }
        }

    private:
        QImage m_img;
        QRectF m_sel;
        QPointF m_anchor;
        bool m_dragging = false;
        friend class ImageCropDialog;
    };

    QImage m_img;
    QRect m_crop;
    CropView *m_view = nullptr;
};

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

    void fitView() { update(); }   // 底图固定适配（拍板：裁切确认后不再移动）

protected:
    // 视图（像素）→ 图像（像素）
    QRectF imageRect() const
    {
        if (base.isNull())
            return {};
        const double s = qMin(width() / double(base.width()),
                              height() / double(base.height()));
        const double w = base.width() * s, h = base.height() * s;
        return QRectF((width() - w) / 2, (height() - h) / 2, w, h);
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
        // 扇面操作提示横幅（拍板：更显眼）——选中点位时画布底部常驻
        if (selected >= 0) {
            const QString hint = tr("🖱 滚轮 = 转朝向     Shift+滚轮 = 调张角     下方属性条可精确输入");
            QFont f = p.font();
            f.setPixelSize(16);
            f.setBold(true);
            p.setFont(f);
            const QFontMetrics fm(f);
            const QRectF tr = fm.boundingRect(hint);
            QRectF band((width() - tr.width()) / 2 - 18, height() - tr.height() - 26,
                        tr.width() + 36, tr.height() + 16);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(30, 30, 34, 215));
            p.drawRoundedRect(band, 8, 8);
            p.setPen(QColor(255, 210, 60));
            p.drawText(band, Qt::AlignCenter, hint);
        }
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
                selected = -1;   // 点空白=取消选中（底图固定不可拖）
                if (onSelected)
                    onSelected(-1);
            }
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
        }
        QWidget::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_dragging = false;
        m_moved = false;
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
                if (e->modifiers() & Qt::AltModifier)
                    data->points[selected].labelScale =
                        qBound(0.5, data->points[selected].labelScale
                               + (step > 0 ? 0.1 : -0.1), 3.0);
                else if (e->modifiers() & Qt::ShiftModifier)
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
        e->ignore();   // 底图固定：未选中扇面时滚轮无操作
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
    bool m_dragging = false;
    bool m_moved = false;
    QPointF m_pressPos;
};

// ---------------------------------------------------------------------------
// 对话框
// ---------------------------------------------------------------------------
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
    auto *renameBtn = mkBtn(tr("机位改名"));
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
    rebuildGroups();
    sideBox->addWidget(m_laneList);
    mid->addLayout(sideBox);

    m_canvas = new SiteMapCanvas(this);
    m_canvas->data = &m_data;
    m_canvas->base = m_base;
    m_canvas->laneColor = m_laneColor;
    // v1.15.3 拍板 C 方案：图上标注 = 机位编号（C01），不显示助记名
    for (const CamGroup &g : m_groups)
        m_canvas->laneLabels[g.key] = g.camNo;
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
    m_fontScale = new QDoubleSpinBox(this);
    m_fontScale->setRange(50, 300);
    m_fontScale->setSingleStep(10);
    m_fontScale->setSuffix(QStringLiteral(" %字号"));
    m_fontScale->setValue(100);
    prop->addWidget(m_heading);
    prop->addWidget(m_spread);
    prop->addWidget(m_radius);
    prop->addWidget(m_fontScale);
    m_hint = new QLabel(tr("💡 选中点位后：滚轮=转朝向　Shift+滚轮=张角　Alt+滚轮=字号　或直接在此精确输入"), this);
    m_hint->setStyleSheet(QStringLiteral(
        "QLabel { color: #8a5a00; background: #fff3d6; border: 1px solid #e0b060;"
        " border-radius: 4px; padding: 4px 8px; font-weight: bold; }"));
    prop->addWidget(m_hint, 1);
    lay->addLayout(prop);

    auto setPropEnabled = [&](bool on) {
        m_heading->setEnabled(on);
        m_spread->setEnabled(on);
        m_radius->setEnabled(on);
        m_fontScale->setEnabled(on);
        m_delBtn->setEnabled(on);
    };
    setPropEnabled(false);

    m_canvas->onSelected = [this, setPropEnabled](int idx) {
        setPropEnabled(idx >= 0);
        if (idx < 0)
            return;
        const SiteMapPoint &pt = m_data.points[idx];
        for (QDoubleSpinBox *sp : {m_heading, m_spread, m_radius, m_fontScale})
            sp->blockSignals(true);
        m_heading->setValue(pt.headingDeg);
        m_spread->setValue(pt.spreadDeg);
        m_radius->setValue(pt.radiusPct);
        m_fontScale->setValue(pt.labelScale * 100.0);
        for (QDoubleSpinBox *sp : {m_heading, m_spread, m_radius, m_fontScale})
            sp->blockSignals(false);
        m_hint->setText(pt.orphan
            ? tr("⚠️ 该机位已不在案件（孤儿点位，可删除）")
            : tr("💡 选中点位后：滚轮=转朝向　Shift+滚轮=张角　Alt+滚轮=字号　或直接在此精确输入"));
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
    connect(m_fontScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, applyTo](double v) {
                applyTo([v](SiteMapPoint &p) {
                    p.labelScale = qBound(0.5, v / 100.0, 3.0); }); });

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
    connect(renameBtn, &QPushButton::clicked, this,
            &SiteMapEditorDialog::renameGroup);
    connect(exportBtn, &QPushButton::clicked, this, &SiteMapEditorDialog::exportFramed);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void SiteMapEditorDialog::syncLaneColors()
{
    m_laneColor.clear();
    int i = 0;
    for (const CamGroup &g : m_groups) {
        m_laneColor[g.key] = Theme::DataPalette[i % Theme::DataPalette.size()];
        ++i;
    }
}

void SiteMapEditorDialog::markOrphans()
{
    for (SiteMapPoint &pt : m_data.points) {
        // 组 id 直接命中
        if (CaseModel::findGroup(m_cm->meta(), pt.laneRef)) {
            pt.orphan = false;
            pt.label = CaseModel::findGroup(m_cm->meta(), pt.laneRef)
                           ->camNo;   // 图上标注=机位编号（C01）
            continue;
        }
        // 旧版引用升格：文件 id → 所属组；机位标签 → 同名组
        QString gid = CaseModel::groupIdOf(m_cm->meta(), pt.laneRef);
        if (gid.isEmpty())
            for (const CaseCameraGroup &g : m_cm->meta().cameraGroups)
                if (!g.name.isEmpty() && g.name == pt.laneRef) {
                    gid = g.groupId;
                    break;
                }
        if (!gid.isEmpty()) {
            pt.laneRef = gid;
            pt.orphan = false;
            pt.label = CaseModel::findGroup(m_cm->meta(), gid)
                           ->camNo;
        } else {
            pt.orphan = true;
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
    // 拍板：导入后给一次裁切机会，确认后固定适配不再移动
    {
        ImageCropDialog cropDlg(img, this);
        if (cropDlg.exec() != QDialog::Accepted)
            return;   // 取消=放弃导入
        const QRect cr = cropDlg.cropRect();
        if (!cr.isNull() && cr.width() > 4 && cr.height() > 4)
            img = img.copy(cr);
    }
    const QString rel = QStringLiteral("reports/assets/sitemap_base.png");
    const QString dst = m_cm->caseDir() + '/' + rel;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (QFile::exists(dst))
        QFile::remove(dst);
    if (!img.save(dst)) {   // 裁切后统一 PNG 存案（无损+格式归一）
        QMessageBox::warning(this, tr("导入底图"), tr("保存入案件失败：%1").arg(dst));
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

void SiteMapEditorDialog::rebuildGroups()
{
    // 正式机位组（2026-08-24 拍板：组键=G### 稳定 id；显示名=组名）
    m_groups.clear();
    for (const CaseCameraGroup &cg : m_cm->meta().cameraGroups) {
        CamGroup g;
        g.key = cg.groupId;
        g.camNo = cg.camNo;
        g.memberIds = cg.memberIds;
        for (const QString &mid : cg.memberIds)
            if (const CaseVideoRef *v = m_cm->videoById(mid))
                g.memberFiles << QFileInfo(m_cm->effectivePathFor(*v)).fileName();
        // 显示名冗余存 memberFiles 首项前——displayName 由 laneLabels 承载
        m_groups << g;
    }
    m_laneList->clear();
    for (const CaseCameraGroup &cg : m_cm->meta().cameraGroups) {
        QString text = CaseModel::groupDisplayName(cg);
        if (cg.memberIds.size() > 1)
            text += QStringLiteral("（%1 个文件）").arg(cg.memberIds.size());
        if (cg.name.isEmpty() && !cg.memberIds.isEmpty())
            if (const CaseVideoRef *v = m_cm->videoById(cg.memberIds.first())) {
                QString fn = QFileInfo(m_cm->effectivePathFor(*v)).fileName();
                if (fn.length() > 22)
                    fn = fn.left(22) + QStringLiteral("…");
                text += QStringLiteral(" ← ") + fn;
            }
        auto *it = new QListWidgetItem(text);
        it->setData(Qt::UserRole, cg.groupId);
        QStringList tip;
        tip << QStringLiteral("组 %1").arg(cg.groupId);
        for (const QString &mid : cg.memberIds)
            if (const CaseVideoRef *v = m_cm->videoById(mid))
                tip << QStringLiteral("%1：%2").arg(mid,
                    QFileInfo(m_cm->effectivePathFor(*v)).fileName());
        it->setToolTip(tip.join(QChar(10)));
        m_laneList->addItem(it);
    }
    syncLaneColors();
    markOrphans();
}

void SiteMapEditorDialog::renameGroup()
{
    QListWidgetItem *it = m_laneList->currentItem();
    if (!it) {
        QMessageBox::information(this, tr("机位改名"),
            tr("请先在左侧机位列表选中一个机位组。"));
        return;
    }
    const QString gid = it->data(Qt::UserRole).toString();
    const CaseCameraGroup *g = CaseModel::findGroup(m_cm->meta(), gid);
    if (!g)
        return;
    bool ok = false;
    const QString prompt = tr("机位名称（建议用位置名，如「东门烟酒店」）：")
        + QChar(10) + tr("组内 %1 个文件同步跟随。").arg(g->memberIds.size());
    const QString name = QInputDialog::getText(this, tr("机位改名"), prompt,
        QLineEdit::Normal, g->name, &ok);
    if (!ok)
        return;
    QString err;
    if (!m_cm->renameGroup(gid, name.trimmed(), &err)) {
        QMessageBox::warning(this, tr("机位改名"), err);
        return;
    }
    m_cm->saveCase(&err);
    // 点位引用稳定组 id——零迁移，仅标签刷新
    rebuildGroups();
    m_canvas->laneColor = m_laneColor;
    m_canvas->laneLabels.clear();
    for (const CaseCameraGroup &cg : m_cm->meta().cameraGroups)
        m_canvas->laneLabels[cg.groupId] = cg.camNo;
    m_canvas->update();
    saveData();
}

