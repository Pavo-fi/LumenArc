/**
 * @file guide_line.h
 * @brief 辅助线数据结构
 * @date 2026-06-24
 * @version 0.5
 */
#pragma once

#include <QPoint>
#include <QColor>
#include <QJsonObject>
#include <cmath>

/**
 * @brief 辅助线：水平线、垂直线、斜线
 */
struct GuideLine
{
    QPoint start;
    QPoint end;
    QColor color = QColor(255, 255, 0, 180);  // 半透明黄色

    enum Type { Horizontal, Vertical, Diagonal };

    Type type() const {
        if (start.y() == end.y()) return Horizontal;
        if (start.x() == end.x()) return Vertical;
        return Diagonal;
    }

    bool isEmpty() const { return start == end; }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["x1"] = start.x();
        obj["y1"] = start.y();
        obj["x2"] = end.x();
        obj["y2"] = end.y();
        obj["color"] = color.name(QColor::HexArgb);
        return obj;
    }

    static GuideLine fromJson(const QJsonObject &obj) {
        GuideLine line;
        line.start = QPoint(obj["x1"].toInt(), obj["y1"].toInt());
        line.end = QPoint(obj["x2"].toInt(), obj["y2"].toInt());
        line.color = QColor(obj["color"].toString());
        return line;
    }
};
