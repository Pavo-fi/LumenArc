/**
 * @file region_shape.h
 * @brief 统一区域形状类型：支持矩形和任意多边形
 * @date 2026-06-24
 * @version 0.5
 */
#pragma once

#include <QRect>
#include <QPolygon>
#include <QPoint>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>

/**
 * @brief 统一的ROI区域形状，支持矩形和任意多边形
 */
struct RegionShape
{
    enum Type { Rect, Polygon };

    Type type;
    QRect rect;         // type == Rect 时有效
    QPolygon polygon;   // type == Polygon 时有效

    RegionShape() : type(Rect) {}

    static RegionShape fromRect(const QRect &r) {
        RegionShape s;
        s.type = Rect;
        s.rect = r;
        return s;
    }

    static RegionShape fromPolygon(const QPolygon &p) {
        RegionShape s;
        s.type = Polygon;
        s.polygon = p;
        return s;
    }

    QRect boundingRect() const {
        if (type == Rect) return rect;
        return polygon.boundingRect();
    }

    bool contains(const QPoint &point) const {
        if (type == Rect) return rect.contains(point);
        return polygon.containsPoint(point, Qt::OddEvenFill);
    }

    bool isEmpty() const {
        if (type == Rect) return rect.isEmpty();
        return polygon.size() < 3;
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        if (type == Rect) {
            obj["type"] = "rect";
            obj["x"] = rect.x();
            obj["y"] = rect.y();
            obj["w"] = rect.width();
            obj["h"] = rect.height();
        } else {
            obj["type"] = "polygon";
            QJsonArray pts;
            for (const QPoint &p : polygon) {
                QJsonObject pt;
                pt["x"] = p.x();
                pt["y"] = p.y();
                pts.append(pt);
            }
            obj["points"] = pts;
        }
        return obj;
    }

    static RegionShape fromJson(const QJsonObject &obj) {
        QString typeStr = obj["type"].toString();
        if (typeStr == "polygon") {
            QPolygon poly;
            QJsonArray pts = obj["points"].toArray();
            for (const auto &v : pts) {
                QJsonObject pt = v.toObject();
                poly.append(QPoint(pt["x"].toInt(), pt["y"].toInt()));
            }
            return fromPolygon(poly);
        }
        // 默认矩形
        return fromRect(QRect(obj["x"].toInt(), obj["y"].toInt(),
                              obj["w"].toInt(), obj["h"].toInt()));
    }
};
