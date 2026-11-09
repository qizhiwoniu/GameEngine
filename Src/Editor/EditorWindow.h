#pragma once
#include <QMainWindow>
#include <QList>
#include <QPointer>
#include <QJsonArray>
#include <QJsonObject>

class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QTreeWidget;
class QToolBar;
class QAction;
class QToolButton;
class QSpinBox;
class QSlider;
class QMenu;
class QDockWidget;
class QStackedWidget;

namespace GameEditor
{
	class Viewport3D;
	class SceneGraph2D;
	class TimelineWidget;
	class SettingsDialog;
	class RuntimeWindow;
	class ViewCubeDialog;

	// 3DMax 风格主编辑器窗口（纯 Qt6 外壳 + SDL3 中央视口）
	class EditorWindow : public QMainWindow
	{
		Q_OBJECT
	public:
		EditorWindow();
		~EditorWindow() override;

		void OpenSettings() { OnOpenSettings(); }   // 供自动验收（GE_SHOW_SETTINGS=1）等外部调用
		void SelfTestLock();                        // 锁定/解锁后面板显隐自测（GE_TEST_LOCK=1）

	private slots:
		void OnPlay(bool play);
		void OnStop();
		void OnFrameTick();
		void OnViewportSelection(const QString& name);
		void OnLayerCurrentChanged();
		void OnPropItemChanged(QTableWidgetItem* item);   // 属性面板勾选（重力）

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
		// ---- Level（关卡 = Levels 目录下的 .yu 场景文件，如 Level01.yu）----
		void OnNewLevel();                              // 文件 ▸ 新建 Level...
		void OnOpenLevel();                             // 文件 ▸ 打开 Level ▸ 浏览文件...
		void FillLevelMenu();                            // 打开"打开 Level"子菜单前填充列表
		bool OpenLevelFile(const QString& path);         // 载入某个 Level 文件
		QString LevelsDir() const;                       // Levels 目录（工程根/Levels）
		void OnRunScene();              // Player ▸ Run Scene（带网格/调试信息）
		void OnRunProject();            // Player ▸ Run Project（只留 FPS）
		void OnRunRestart();            // Player ▸ Restart（关掉重开）
		void OnRunRefresh();            // Player ▸ Refresh（不关窗重载场景）
		void OnRunShutdown();           // Player ▸ Shutdown（结束运行）
		void OpenRuntime(bool project); // 打开运行窗口（同一时间只保留一个）
		void SyncRuntimeActions();      // 按"是否正在运行"启用/禁用 Restart/Refresh/Shutdown
		QToolBar* MakeBar(const QString& title);      // 建一个独立工具栏
		void ShowBarMenu(const QPoint& globalPos);    // 工具栏空白处右键菜单
		void SaveLayout();
		void RestoreLayout();

		// ---- 创建菜单 ----
		void OnCreate2DScene();
		void OnCreate3DScene();
		void ShowPage(bool twoD);       // 切换中央页（0=3D 视口，1=2D 节点图）并同步工具栏按钮

		// ---- 编辑菜单 ----
		QJsonObject CaptureState() const;              // 当前文档快照（viewport + graph2d）
		void RestoreState(const QJsonObject& s);
		void PushUndo();                               // 操作前记录快照
		void OnEditUndo();
		void OnEditRedo();
		void OnEditCut();
		void OnEditCopy();
		void OnEditPaste();
		void OnEditSelectAll();
		void Copy3DSelected();
		void Paste3DClipboard();

		// ---- 导入 / 导出 ----
		void OnImport();
		void OnExportJson();
		void OnExportObj();
		void OnExportPng();

		// ---- 右上角视图方块（ViewCube）外观 ----
		void LoadViewCubeSettings();                    // 启动时读配置
		void ApplyViewCube(int sizePx, int alphaPct);   // sizePx=0 表示自动；同步工具条 + 窗口 + 写配置
		void OpenViewCubeDialog();

		// 菜单 / 工具条动作
		QAction* m_actPlay = nullptr;    // 指向工具条的"播放/暂停"（与菜单状态互相同步）
		QAction* m_actMagnet = nullptr;
		QAction* m_actLock = nullptr;
		QToolButton* m_playBtn = nullptr;   // 动画面板上的播放按钮（与工具条同步）
		QList<QToolBar*>    m_bars;         // 工具栏1..4
		QList<QDockWidget*> m_docks;        // Create / 属性 / 动画 / 图层
		QMenu* m_menuBars = nullptr;        // 视图 ▸ 工具栏
		QMenu* m_menuWindows = nullptr;     // 视图 ▸ 窗口

		// 运行窗口（Run Scene / Run Project）
		QPointer<RuntimeWindow> m_runtime;
		int  m_lastRunMode = 2;             // 1 = Scene, 2 = Project（Restart 用）
		QAction* m_actRestart = nullptr;
		QAction* m_actRefresh = nullptr;
		QAction* m_actShutdown = nullptr;

		// 中央视口（3D 场景 / 2D 节点图 二选一显示）
		Viewport3D*     m_viewport = nullptr;
		SceneGraph2D*   m_graph2d = nullptr;
		QStackedWidget* m_centralStack = nullptr;
		QAction*        m_act2D = nullptr;   // 工具栏 2D / 3D 视图模式（菜单切页时同步勾选）
		QAction*        m_act3D = nullptr;

		// 撤销 / 重做（整篇文档快照，仅由菜单类编辑操作记录）与 3D 剪贴板
		QList<QJsonObject> m_undo;
		QList<QJsonObject> m_redo;
		QJsonArray         m_clipObjs;

		// 设置窗口（懒创建）
		SettingsDialog* m_settings = nullptr;
		bool m_autoSave = true;
		int  m_autoSaveMinutes = 10;

		// 右上角视图方块外观（sizePx=0 自动）
		int            m_vcSize = 0;
		int            m_vcAlpha = 100;
		QSlider*       m_vcSizeSlider = nullptr;    // 工具条上的两个滑块
		QSlider*       m_vcAlphaSlider = nullptr;
		QLabel*        m_vcSizeVal = nullptr;
		QLabel*        m_vcAlphaVal = nullptr;
		QAction*       m_vcAuto = nullptr;          // 工具条上的"自动"开关
		QPointer<ViewCubeDialog> m_vcDialog;

		// 当前文档（.yu）；空 = 未命名
		QString m_docPath;

		// 文件 ▸ 打开 Level 的子菜单（每次弹出前按 Levels 目录刷新）
		QMenu*  m_menuLevels = nullptr;

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
