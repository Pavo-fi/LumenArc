# ============================================================================
# 工作记录（2026-08-22，第六十二批）——放大镜布局重构（用户标注拍板）v1.13.1
# ============================================================================

## 71. 放大镜 QDockWidget→QWidget 内嵌顶行 + 案件列表常驻手动折叠条

### 拍板来源

用户两次实测纠偏：①v1.12.9「右 dock 半屏」方向错误——dock 天生贯通整窗高度，
挤压图表区且放大画面上下大黑边；②本批初案「裁切取景填满 dock」（源区域纵横比
随视口）被用户否掉（纵向取景收窄不可接受），代码+测试干净回退后，用户以
**亲手标注截图**拍板目标布局：

```
┌────────────────────────────────────────────┐
│ 菜单栏/工具栏                                │
├──┬───────────────────┬─────────────────────┤
│视│                   │                     │
│频│  原视频（左半）    │   放大镜（右半）     │  ← 同一行等大同高
│列│                   │                     │
│表│                   │                     │
│案├───────────────────┴─────────────────────┤
│件│  图表（量化分析）——右区全宽              │
│列├─────────────────────────────────────────┤
│表│  语谱图——右区全宽                        │
└──┴─────────────────────────────────────────┘
```

### 施工

1. **MagnifierWidget 基类 QDockWidget→QWidget**：ctor 改 QVBoxLayout 承载
   ContentWidget（左缘 1px 分隔线）；所有 dock API（addDockWidget/
   setWindowTitle/setFeatures/resizeDocks 50%）从 MainWindow 清除。
2. **中央布局**：顶行新增水平 QSplitter m_topRow [m_videoWidget | m_magnifier]，
   垂直 m_splitter 三行 [m_topRow | 图表 | 语谱图] 不变；左列 dock 天然全高。
   createMagnifier → m_topRow->addWidget + 首次均分；用户拖过分割条的比例在
   removeMagnifier 存 m_topRowSavedSizes、下次呼出恢复。
3. **关闭回排排雷**：QSplitter 子项 deleteLater/摘除后**不自动拉伸剩余子项**
   （offscreen 截图实证右半空白）——removeMagnifier 显式
   `setParent(nullptr)` + `setSizes({width()})` 让视频立即吃满整行。
4. **案件列表常驻手动折叠条**：复刻视频列表模式（28px 竖排细条+◀/▶钮），
   重排 CaseDock 内容 [细条|内容]；放大镜自动折叠（占位细条）逻辑保留且改为
   记录真实手动状态——用户本已手动收起的，关放大镜后仍保持收起。
5. **回归网**：mw_test 新增 testMagnifierLayout（合成 2s 彩条素材 → 开窗 →
   invoke onMagnifierWheelZoom → 几何断言：同行顶对齐/同高/等宽/图表全宽/
   左列全高/案件折叠钮存在/关闭后视频吃满整行）+ 开关态截图留档
   （build/Release/maglayout_shot_open.png 人工目检）。mw 84→96。

### 排雷记（offscreen 平台）

- **offscreen 下 isVisible() 恒 false**（裸 QWidget show() 探针实证），
  无头可见性断言无效，一律改几何断言；窗口截图用 grab()（强制渲染不受
  可见性影响）；
- offscreen 无字体目录 → 截图文字全 tofu，仅看布局不看文字。

### 测试

- mw 96 绿（新增 12 断言）；ui_chain 97（mag.widget()→mag.grab() 适配基类
  变更）；全回归 15 套绿。
- 版本 → **v1.13.1**（CMakeLists/app.rc/aboutdialog/mainwindow 四同步）。
