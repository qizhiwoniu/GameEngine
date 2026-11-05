#pragma once
#include <QMainWindow>
#include <QList>

class QLabel;
class QTableWidget;
class QTreeWidget;
class QToolBar;
class QAction;
class QSpinBox;
class QMenu;
class QDockWidget;
class QStackedWidget;

namespace GameEditor
{
	class Viewport3D;
	class SceneGraph2D;
	class TimelineWidget;
	class SettingsDialog;

	// 3DMax 风格主编辑器窗口（纯 Qt6 外壳 + SDL3 中央视口）
	class EditorWindow : public QMainWindow
	{
		Q_OBJECT
	public:
		EditorWindow();
		~EditorWindow() override;

		void OpenSettings() { OnOpenSettings(); }   // 供自动验收（GE_SHOW_SETTINGS=1）等外部调用

	private slots:
		void OnPlay(bool play);
		void OnStop();
		void OnFrameTick();
		void OnViewportSelection(const QString& name);
		void OnLayerCurrentChanged();

	private:
		void BuildMenus();
		void BuildToolBar();
		void BuildDocks();
		void ApplyDarkStyle();
		void showEvent(QShowEvent* e) override;
		void SyncPlayhead();            // 帧 -> 时间标签 / spinbox / 时间轴
		void RefreshLayers();           // 视口场景 -> 图层列表
		void SyncSelectionUi();
		void UpdatePropRow();
		void SetFrame(int frame);
		void OnOpenSettings();
		void UpdateTitle();             // 依据 m_docPath 刷新窗口标题
		void OnFileNew();
		void OnFileOpen();
		void OnFileSave();
		void OnFileSaveAs();
		QToolBar* MakeBar(const QString& title);      // 建一个独立工具栏
		void ShowBarMenu(const QPoint& globalPos);    // 工具栏空白处右键菜单
		void SaveLayout();
		void RestoreLayout();

		// 菜单 / 工具条动作
		QAction* m_actPlay = nullptr;
		QAction* m_actMagnet = nullptr;
		QAction* m_actLock = nullptr;
		QList<QToolBar*>    m_bars;         // 工具栏1..4
		QList<QDockWidget*> m_docks;        // Create / 属性 / 动画 / 图层
		QMenu* m_menuBars = nullptr;        // 视图 ▸ 工具栏
		QMenu* m_menuWindows = nullptr;     // 视图 ▸ 窗口

		// 中央视口（3D 场景 / 2D 节点图 二选一显示）
		Viewport3D*     m_viewport = nullptr;
		SceneGraph2D*   m_graph2d = nullptr;
		QStackedWidget* m_centralStack = nullptr;

		// 设置窗口（懒创建）
		SettingsDialog* m_settings = nullptr;
		bool m_autoSave = true;
		int  m_autoSaveMinutes = 10;

		// 当前文档（.yu）；空 = 未命名
		QString m_docPath;

		// Create / 属性 / 图层 / 动画面板
		QTableWidget*  m_propTable = nullptr;
		QTreeWidget*   m_layerTree = nullptr;
		TimelineWidget* m_timeline = nullptr;
		QSpinBox*      m_frameSpin = nullptr;
		QLabel*        m_timeLabel = nullptr;

		// 动画状态
		int  m_frame = 0;
		int  m_maxFrame = 100;
		int  m_fps = 30;
		bool m_playing = false;
	};
}
