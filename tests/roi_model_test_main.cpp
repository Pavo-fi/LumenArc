/**
 * @file roi_model_test_main.cpp
 * @brief RoiModel 统一序列单测（v1.5.0 首批 Q-18：矩形+多边形合并）
 *
 * 核心断言：roiId 统一递增（矩形/多边形共享序列）、clear 不重置序列、
 * restore 取两表 max+1、双槽位独立信号。
 */
#include "domain/roi_model.h"
#include <QCoreApplication>
#include <cstdio>
#include <cmath>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg);  \
        }                                                                   \
    } while (0)

static void testUnifiedSequence()
{
    RoiModel m;
    m.addRegion(QRect(0, 0, 10, 10));       // id=1
    m.addPolygon(QPolygon() << QPoint(0, 0) << QPoint(5, 0) << QPoint(5, 5)); // id=2
    m.addRegion(QRect(1, 1, 3, 3));         // id=3（不再回到 1 —— 统一序列）
    CHECK(m.roiIdAt(0) == 1, "unified: rect0 id=1");
    CHECK(m.polygonRoiIdAt(0) == 2, "unified: poly0 id=2");
    CHECK(m.roiIdAt(1) == 3, "unified: rect1 id=3 (shared counter)");
    CHECK(m.roiIds() != m.polygonRoiIds(), "unified: rect/poly id lists differ");
}

static void testClearDoesNotResetCounter()
{
    RoiModel m;
    m.addPolygon(QPolygon() << QPoint(0, 0) << QPoint(5, 0) << QPoint(5, 5)); // id=1
    m.addRegion(QRect(0, 0, 4, 4));          // id=2
    m.clearRegions();                        // 只清矩形，序列不重置
    CHECK(m.regionCount() == 0, "clear: rects emptied");
    CHECK(m.polygonCount() == 1, "clear: polygons untouched");
    m.addRegion(QRect(2, 2, 6, 6));          // id=3（若重置会与多边形 id=1 冲突）
    CHECK(m.roiIdAt(0) == 3, "clear: rect id continues sequence (no clash)");
    CHECK(m.polygonRoiIdAt(0) == 1, "clear: poly id unchanged");

    m.clearPolygons();
    CHECK(m.polygonCount() == 0, "clear: polys emptied");
    m.addPolygon(QPolygon() << QPoint(0, 0) << QPoint(4, 0) << QPoint(4, 4));
    CHECK(m.polygonRoiIdAt(0) == 4, "clear: poly id continues sequence");
}

static void testRestoreTakesMaxOfBothTables()
{
    RoiModel m;
    // 恢复：矩形 id 5, 多边形 id 9 → 下一个新增必须是 10（不撞任何一张表）
    m.restoreRegions({QRect(0, 0, 2, 2)}, {5});
    m.restorePolygons({QPolygon() << QPoint(0, 0) << QPoint(2, 0) << QPoint(2, 2)}, {9});
    m.addRegion(QRect(1, 1, 2, 2));
    CHECK(m.roiIdAt(1) == 10, "restore: next rect id = max(both)+1");
    m.addPolygon(QPolygon() << QPoint(0, 0) << QPoint(1, 0) << QPoint(1, 1));
    CHECK(m.polygonRoiIdAt(1) == 11, "restore: next poly id continues");

    // 反向：矩形 max 更高时
    RoiModel m2;
    m2.restoreRegions({QRect(0, 0, 2, 2)}, {9});
    m2.restorePolygons({QPolygon() << QPoint(0, 0) << QPoint(2, 0) << QPoint(2, 2)}, {5});
    m2.addPolygon(QPolygon() << QPoint(0, 0) << QPoint(1, 0) << QPoint(1, 1));
    CHECK(m2.polygonRoiIdAt(1) == 10, "restore: poly id after rect-max");
}

static void testFindAndRemove()
{
    RoiModel m;
    m.restoreRegions({QRect(0, 0, 2, 2), QRect(5, 5, 3, 3)}, {7, 8});
    m.restorePolygons({QPolygon() << QPoint(0, 0) << QPoint(2, 0) << QPoint(2, 2)},
                      {3});
    CHECK(m.findIndexByRoiId(8) == 1, "find: rect 8 at index 1");
    CHECK(m.findIndexByRoiId(3) == -1, "find: rect table misses poly id 3");
    CHECK(m.findPolygonIndexByRoiId(3) == 0, "find: poly 3 at index 0");
    CHECK(m.findPolygonIndexByRoiId(8) == -1, "find: poly table misses rect id 8");

    bool rmSig = false, rmRemoved = false;
    bool pmSig = false, pmRemoved = false;
    QObject::connect(&m, &RoiModel::regionsChanged, [&]() { rmSig = true; });
    QObject::connect(&m, &RoiModel::polygonsChanged, [&]() { pmSig = true; });
    QObject::connect(&m, &RoiModel::regionRemoved,
                     [&](int idx, int id) { rmRemoved = (idx == 0 && id == 7); });
    QObject::connect(&m, &RoiModel::polygonRemoved,
                     [&](int idx, int id) { pmRemoved = (idx == 0 && id == 3); });

    m.removeRegion(0);
    CHECK(rmSig && rmRemoved && !pmSig, "remove: region signals fire, poly silent");
    rmSig = false;
    m.removePolygon(0);
    CHECK(pmSig && pmRemoved && !rmSig, "remove: polygon signals fire, rect silent");
    m.removeRegion(99);
    CHECK(m.regionCount() == 1, "remove: out-of-range ignored");
}

static void testColors()
{
    CHECK(RoiModel::regionColor(0) == RoiModel::regionColor(7),
          "color: 7-cycle for rect palette");
    CHECK(RoiModel::polygonColor(0) == RoiModel::polygonColor(7),
          "color: 7-cycle for poly palette");
    CHECK(RoiModel::regionColor(0) != RoiModel::polygonColor(0),
          "color: rect/poly palettes differ");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testUnifiedSequence();
    testClearDoesNotResetCounter();
    testRestoreTakesMaxOfBothTables();
    testFindAndRemove();
    testColors();
    fprintf(stderr, "roi_model_test: %d checks, %d failures\n",
            g_checks, g_failures);
    return g_failures ? 1 : 0;
}
