/**
 * @file sortablefiletable.h
 * @brief 行拖拽插入语义表格（现场反馈：QTableWidget 默认 InternalMove 是
 *        "覆盖/替换"目标行，用户期望"插入到目标位置、其余顺移"）。
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 无 Q_OBJECT（纯虚函数重写 + std::function 回调，不依赖 moc），
 * moveRowTo() 公开以便 headless 单测。
 */
#pragma once

#include <QTableWidget>
#include <QDropEvent>
#include <functional>

class SortableFileTable : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;
    std::function<void()> orderChanged;   // 拖拽落定后回调（同步文件顺序）

    /// 将 srcRow 整行移动到"插到 targetRow 之前"的位置；insertAfter=true 时
    /// 插到 targetRow 之后。返回是否发生移动。位置不变时返回 false。
    bool moveRowTo(int srcRow, int targetRow, bool insertAfter)
    {
        if (srcRow < 0 || srcRow >= rowCount() || targetRow < 0
            || targetRow >= rowCount())
            return false;
        int toRow = targetRow + (insertAfter ? 1 : 0);
        if (srcRow < toRow)
            toRow -= 1;                   // 移除源行后目标位置前移
        if (toRow == srcRow)
            return false;                 // 位置未变
        // QTableModel 不支持 moveRows：逐列 take（保住所有权）→ removeRow
        // 移除空行 → insertRow → 逐列回填（行数守恒）
        QList<QTableWidgetItem *> rowItems;
        for (int c = 0; c < columnCount(); ++c)
            rowItems.append(takeItem(srcRow, c));
        removeRow(srcRow);
        insertRow(qBound(0, toRow, rowCount()));
        for (int c = 0; c < rowItems.size(); ++c)
            setItem(qBound(0, toRow, rowCount() - 1), c, rowItems[c]);
        return true;
    }

protected:
    void dropEvent(QDropEvent *event) override
    {
        if (event->source() == this
            && (event->dropAction() == Qt::MoveAction
                || event->dropAction() == Qt::CopyAction)) {
            const QModelIndex target = indexAt(event->position().toPoint());
            // 拖拽行推断：selectedIndexes 兼容任意点击列（selectedRows() 默认
            // 只查第 0 列，点击非首列拖拽会返回空 → 拦截失效被覆盖）
            const auto selIdx = selectionModel()
                                    ? selectionModel()->selectedIndexes()
                                    : QModelIndexList();
            if (!target.isValid() || selIdx.isEmpty()) {
                QTableWidget::dropEvent(event);   // 异常路径兜底：交给默认
                return;
            }
            const int srcRow = selIdx.first().row();
            const bool after = dropIndicatorPosition()
                == QAbstractItemView::BelowItem;   // 落点下缘 → 插到目标行之后
            event->accept();
            if (moveRowTo(srcRow, target.row(), after) && orderChanged)
                orderChanged();
            return;
        }
        QTableWidget::dropEvent(event);
    }
};
