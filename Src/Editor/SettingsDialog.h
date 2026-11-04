#pragma once
#include <QDialog>
#include <QVector>
#include <QRect>
#include "Hotkeys.h"

class QTreeWidget;
class QLabel;
class QCheckBox;
class QSpinBox;

namespace GameEditor
{
	// 自绘暗色 Tab 条（原生 QTabWidget 在暗色下样式难控）
	class DarkTabBar : public QWidget
	{
		Q_OBJECT
	public:
		explicit DarkTabBar(QWidget* parent = nullptr);
		void AddTab(const QString& title);
		int  Active() const { return m_active; }
		void SetActive(int i);
	signals:
		void Changed(int index);
	protected:
		void paintEvent(QPaintEvent*) override;
		void mouseMoveEvent(QMouseEvent*) override;
		void mousePressEvent(QMouseEvent*) override;
		void leaveEvent(QEvent*) override;
	private:
		int HitTest(const QPointF& p);
		QStringList m_titles;
		QVector<QRect> m_rects;
		int m_active = 0;
		int m_hot = -1;
	};

	// Maya 风格键盘布局图：高亮当前修饰键组合下已绑定的键，显示命令名
	class KeyboardView : public QWidget
	{
		Q_OBJECT
	public:
		explicit KeyboardView(QWidget* parent = nullptr);
		int  Mods() const { return m_mods; }
		void SetMods(int m) { m_mods = m; update(); }
		int  SelVk() const { return m_selVk; }
		void SetSelVk(int vk) { m_selVk = vk; update(); }
		void ClearSel() { m_selVk = 0; update(); }
	signals:
		void SelectionChanged();
	protected:
		void paintEvent(QPaintEvent*) override;
		void mouseMoveEvent(QMouseEvent*) override;
		void mousePressEvent(QMouseEvent*) override;
		void leaveEvent(QEvent*) override;
	private:
		struct KeyBox { QRect r; int vk; QString cap; };
		void BuildLayout();
		int  HitTest(const QPointF& p);
		QVector<KeyBox> m_keys;
		int m_mods = 0;          // 当前修饰键状态（Shift/Ctrl/Alt 键可点击切换）
		int m_selVk = 0;         // 选中的键（0 = 无）
		int m_hot = -1;
		int m_layW = 0, m_layH = 0;
	};

	// "设置"窗口：快捷键 / 常规 / 视口 三个页（对齐 RAD 版 Setting 窗口）
	class SettingsDialog : public QDialog
	{
		Q_OBJECT
	public:
		explicit SettingsDialog(QWidget* parent = nullptr);

		// 常规 / 视口 设置值（确定时写回）
		bool AutoSave() const { return m_autoSave; }
		int  AutoSaveMinutes() const { return m_autoSaveMinutes; }
		bool ShowGrid() const { return m_showGrid; }
		bool ShowFps() const { return m_showFps; }
		void SetCurrent(bool autoSave, int minutes, bool grid, bool fps);

	signals:
		void SettingsApplied();   // 确定：主窗口刷新视口设置

	private slots:
		void OnTabChanged(int i);
		void OnListSelection();
		void OnAssign();
		void OnClear();
		void OnReset();
		void OnAccept();

	private:
		void RefreshList(int sel);
		void UpdateInfo();

		DarkTabBar*    m_tabs = nullptr;
		QWidget*       m_page[3] = {};
		KeyboardView*  m_kb = nullptr;
		QTreeWidget*   m_list = nullptr;
		QLabel*        m_info = nullptr;
		QCheckBox*     m_cbAutoSave = nullptr;
		QSpinBox*      m_edMinutes = nullptr;
		QCheckBox*     m_cbGrid = nullptr;
		QCheckBox*     m_cbFps = nullptr;

		bool m_autoSave = true;
		int  m_autoSaveMinutes = 10;
		bool m_showGrid = true;
		bool m_showFps = false;
	};
}
