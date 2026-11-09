#include "EditorWindow.h"
#include "AppIcons.h"
#include "Document.h"
#include "RuntimeWindow.h"
#include "Viewport3D.h"
#include "SceneGraph2D.h"
#include "SceneLink.h"
#include "TimelineWidget.h"
#include "SettingsDialog.h"
#include "ViewCubeDialog.h"
#include "Hotkeys.h"
#include "SceneIO.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSettings>
#include <QWidgetAction>
#include <algorithm>
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>

namespace GameEditor
{
	namespace
	{
		// 面板标题栏（带图钉 / 关闭），贴截图样式
		class PanelHeader : public QWidget
		{
		public:
			PanelHeader(const QString& title)
			{
				auto* lay = new QHBoxLayout(this);
				lay->setContentsMargins(8, 4, 4, 4);
				lay->setSpacing(4);
				auto* lab = new QLabel(title, this);
				lab->setStyleSheet("color:#d8d8dc; font-size:12px;");
				lay->addWidget(lab);
				lay->addStretch();
				setFixedHeight(24);
			}
		};

		QDockWidget* MakeDock(const QString& title, QWidget* body)
		{
			auto* dock = new QDockWidget(title);
			dock->setWidget(body);
			// 注意：不加 DockWidgetFloatable——可浮动停靠面板会预建原生浮动
			// 顶层窗口，与视口的原生子窗口混用时初始绘制不可靠（启动后白块）。
			dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
			dock->setTitleBarWidget(new PanelHeader(title));
			return dock;
		}
	}

	EditorWindow::~EditorWindow()
	{
		// 运行窗口是独立顶层窗口，主窗口关闭时要跟着一起退出
		if (m_runtime)
			m_runtime->close();
	}

	EditorWindow::EditorWindow()
	{
		setWindowTitle(tr("3DGameEngine - 未命名"));
		resize(1104, 623);

		ApplyDarkStyle();

		LoadHotkeys();
		m_viewport = new Viewport3D(this);
		m_graph2d = new SceneGraph2D(this);
		// 镜头块「看向」菜单需要 3D 方块列表（uid + 名称）
		m_graph2d->ObjLister = [this]
		{
			std::vector<std::pair<int, QString>> out;
			for (int i = 0; i < m_viewport->ObjectCount(); ++i)
			{
				const Viewport3D::Obj& o = m_viewport->ObjAt(i);
				if (o.kind == 0)
					out.emplace_back(o.uid, QString::fromStdString(o.name));
			}
			return out;
		};
		m_centralStack = new QStackedWidget(this);
		m_centralStack->addWidget(m_viewport);      // index 0 = 3D 场景
		m_centralStack->addWidget(m_graph2d);       // index 1 = 2D 节点图
		setCentralWidget(m_centralStack);

		BuildMenus();
		BuildToolBar();
		BuildDocks();
		LoadViewCubeSettings();   // 工具条的滑块 / 开关按上次的值初始化
		RestoreLayout();      // 上次"保存布局"的结果（没有则保持默认）

		connect(m_viewport, &Viewport3D::SelectionChanged,
			this, &EditorWindow::OnViewportSelection);
		// 拖 gizmo 移动/旋转/缩放：开始真正改动前记一次快照，Ctrl+Z 才能回到拖动前
		connect(m_viewport, &Viewport3D::EditStarted, this, &EditorWindow::PushUndo);

		// 2D 节点图 <-> 3D 场景：双向配对（新建 / 删除 / 改名 / 变换 / 选中），
		// 与 RAD 版 SceneLink 一样用轮询同步（100ms）
		auto* link = new QTimer(this);
		link->setInterval(100);
		connect(link, &QTimer::timeout, this, [this]
			{
				if (SceneLink::Sync(m_viewport, m_graph2d))
					RefreshLayers();
			});
		link->start();
		QTimer::singleShot(0, this, [this] { SceneLink::Sync(m_viewport, m_graph2d); RefreshLayers(); });

		// 播放计时器：按 30fps 推帧
		auto* timer = new QTimer(this);
		timer->setInterval(1000 / m_fps);
		connect(timer, &QTimer::timeout, this, &EditorWindow::OnFrameTick);
		timer->start();   // 常开：m_playing=false 时不推帧，但视口保持刷新

		// 原生标题栏切暗色
		if (HWND hwnd = reinterpret_cast<HWND>(winId()))
		{
			BOOL dark = TRUE;
			DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
		}
	}

	void EditorWindow::showEvent(QShowEvent* e)
	{
		QMainWindow::showEvent(e);
		// 含 D3D 原生子窗口的窗口，停靠面板的首次绘制依赖刷新时机，
		// 不主动推一次会白屏到第一次鼠标交互为止。
		QTimer::singleShot(0, this, [this] { update(); });
	}

	void EditorWindow::ApplyDarkStyle()
	{
		QApplication::setStyle(QStyleFactory::create("Fusion"));

		QPalette pal;
		pal.setColor(QPalette::Window, QColor(43, 43, 46));
		pal.setColor(QPalette::WindowText, QColor(216, 216, 220));
		pal.setColor(QPalette::Base, QColor(30, 30, 34));
		pal.setColor(QPalette::AlternateBase, QColor(43, 43, 46));
		pal.setColor(QPalette::Text, QColor(216, 216, 220));
		pal.setColor(QPalette::Button, QColor(52, 52, 56));
		pal.setColor(QPalette::ButtonText, QColor(216, 216, 220));
		pal.setColor(QPalette::Highlight, QColor(52, 100, 170));
		pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
		pal.setColor(QPalette::ToolTipBase, QColor(60, 60, 64));
		pal.setColor(QPalette::ToolTipText, QColor(216, 216, 220));
		pal.setColor(QPalette::Disabled, QPalette::Text, QColor(110, 110, 114));
		pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 110, 114));
		QApplication::setPalette(pal);

		// QSS：菜单 / 工具条 / 停靠面板 / 按钮的暗色细调
		setStyleSheet(R"(
			QMenuBar { background:#2b2b2e; color:#d8d8dc; spacing:0; padding:1px; }
			QMenuBar::item { padding:3px 7px; background:transparent; }
			QMenuBar::item:selected { background:#3c3c42; }
			QMenu { background:#333338; color:#d8d8dc; border:1px solid #1e1e22; }
			QMenu::item { padding:4px 26px 4px 22px; }
			QMenu::item:selected { background:#3a5a8c; }
			QMenu::separator { height:1px; background:#46464c; margin:3px 6px; }
			QToolBar { background:#333338; border-bottom:1px solid #1e1e22; spacing:2px; padding:2px; }
			QToolButton { background:transparent; border:1px solid transparent; border-radius:3px;
				padding:3px; color:#d8d8dc; }
			QToolButton:hover { background:#3e3e46; }
			QToolButton:pressed { background:#4a4a54; }
			QToolButton:checked { background:#8b2f2f; border-color:#a63c3c; }
			QDockWidget { color:#d8d8dc; titlebar-close-icon: none; }
			QDockWidget > QWidget { background:#2f2f33; }
			QTreeWidget, QTableWidget { background:#2a2a2e; alternate-background-color:#2f2f33;
				color:#d8d8dc; border:none; gridline-color:#3a3a40; }
			QHeaderView::section { background:#333338; color:#c8c8cc;
				border:none; border-right:1px solid #1e1e22; padding:3px 6px; }
			QSpinBox { background:#3a5a8c; color:white; border:none; padding:2px 4px; }
			QPushButton { background:#3a3a40; color:#d8d8dc; border:1px solid #4a4a50;
				border-radius:3px; padding:6px 14px; }
			QPushButton:hover { background:#44444c; }
			QPushButton:pressed { background:#2f2f36; }
		)");
	}

	void EditorWindow::BuildMenus()
	{
		QMenuBar* mb = menuBar();
		mb->setNativeMenuBar(false);   // 自绘样式 + "帮助"才能真正靠右

		// 菜单条目严格对齐 RAD 版 Unit1.dfm 的 MainMenu1 / MainMenu2
		// ---- 文件(F) ----
		QMenu* mFile = mb->addMenu(tr("文件(F)"));
		mFile->addAction(tr("新建(&N)"), QKeySequence::New, this, &EditorWindow::OnFileNew);
		mFile->addAction(tr("打开(&O)"), QKeySequence::Open, this, &EditorWindow::OnFileOpen);
		mFile->addSeparator();
		// Level = Levels 目录下的场景文件（Level01.yu / Level02.yu …）
		mFile->addAction(tr("新建 Level..."), QKeySequence(QStringLiteral("Ctrl+Shift+N")),
			this, &EditorWindow::OnNewLevel);
		m_menuLevels = mFile->addMenu(tr("打开 Level"));
		connect(m_menuLevels, &QMenu::aboutToShow, this, &EditorWindow::FillLevelMenu);
		mFile->addSeparator();
		mFile->addAction(tr("保存(&S)"), QKeySequence::Save, this, &EditorWindow::OnFileSave);
		mFile->addAction(tr("另存为..."), this, &EditorWindow::OnFileSaveAs);
		mFile->addSeparator();
		QMenu* mImport = mFile->addMenu(tr("导入"));
		mImport->addAction(tr("模型 (OBJ)..."), this, &EditorWindow::OnImport);
		mImport->addAction(tr("场景 (JSON)..."), this, &EditorWindow::OnFileOpen);
		QMenu* mExport = mFile->addMenu(tr("导出"));
		mExport->addAction(tr("场景 (JSON)..."), this, &EditorWindow::OnExportJson);
		mExport->addAction(tr("模型 (OBJ)..."), this, &EditorWindow::OnExportObj);
		mExport->addAction(tr("视口截图 (PNG)..."), this, &EditorWindow::OnExportPng);
		mFile->addSeparator();
		mFile->addAction(tr("首选项"), this, &EditorWindow::OnOpenSettings);
		mFile->addSeparator();
		mFile->addAction(tr("退出(&E)"), QKeySequence::Quit, qApp, &QApplication::quit);

		// ---- 编辑(E) ----
		QMenu* mEdit = mb->addMenu(tr("编辑(&E)"));
		mEdit->addAction(tr("Undo"), QKeySequence::Undo, this, &EditorWindow::OnEditUndo);
		mEdit->addAction(tr("Redo"), QKeySequence::Redo, this, &EditorWindow::OnEditRedo);
		mEdit->addSeparator();
		// 剪切 / 复制 / 粘贴 按当前页面分发：2D 页走节点图，3D 页走对象
		mEdit->addAction(tr("剪切"), QKeySequence::Cut, this, &EditorWindow::OnEditCut);
		mEdit->addAction(tr("复制"), QKeySequence::Copy, this, &EditorWindow::OnEditCopy);
		mEdit->addAction(tr("粘贴"), QKeySequence::Paste, this, &EditorWindow::OnEditPaste);
		mEdit->addAction(tr("全选"), QKeySequence::SelectAll, this, &EditorWindow::OnEditSelectAll);
		mEdit->addSeparator();
		// 按当前页面分发：2D 节点图页删节点，3D 页删对象。
		// 之前固定删 3D 选中——在 2D 页按 Delete 时删的是 3D 的选中对象，
		// 纯逻辑节点（时间/运行输出）永远删不掉，还可能删错对象。
		mEdit->addAction(tr("删除"), QKeySequence::Delete, this, [this]
			{
				PushUndo();
				if (m_centralStack->currentWidget() == static_cast<QWidget*>(m_graph2d))
					m_graph2d->DeleteSelected();
				else
					m_viewport->DeleteSelected();
				RefreshLayers();
			});

		// ---- 创建(C) ----
		// 2D / 3D 场景都是彻底空白的新场景：不预置节点、不预置物体、不随机摆放
		QMenu* mCreate = mb->addMenu(tr("创建(&C)"));
		mCreate->addAction(tr("2D场景"), this, &EditorWindow::OnCreate2DScene);
		mCreate->addAction(tr("3D场景"), this, &EditorWindow::OnCreate3DScene);
		mCreate->addSeparator();
		mCreate->addAction(tr("方块"), this, [this] { PushUndo(); m_viewport->AddBox(); RefreshLayers(); });
		mCreate->addAction(tr("平面"), this, [this] { PushUndo(); m_viewport->AddPlane(); RefreshLayers(); });
		mCreate->addAction(tr("跳转精灵"), this, [this] { PushUndo(); m_viewport->AddTrigger(); RefreshLayers(); });
		mCreate->addAction(tr("动画"));

		// ---- 视图(V) ----
		QMenu* mView = mb->addMenu(tr("视图(&V)"));
		mView->addAction(tr("保存布局"), this, &EditorWindow::SaveLayout);
		m_actLock = mView->addAction(tr("锁定布局"));
		m_actLock->setCheckable(true);
		connect(m_actLock, &QAction::toggled, this, [this](bool on)
			{
				// 必须始终保留 DockWidgetClosable：视图▸窗口用的是 toggleViewAction，
				// 它走 close()/show()，面板没有 Closable 特性时 close() 会被静默拒绝，
				// 表现为"锁定/解锁布局后窗口隐藏与显示失灵"。锁定只去掉可移动/可浮动。
				const auto f = on ? QDockWidget::DockWidgetClosable
				                  : QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable;
				for (QDockWidget* d : m_docks)
					d->setFeatures(f);
				for (QToolBar* t : m_bars)
					t->setMovable(!on);
			});
		mView->addSeparator();
		mView->addAction(tr("ViewCube 设置…"), this, &EditorWindow::OpenViewCubeDialog);
		mView->addSeparator();
		// 工具栏 / 窗口的显示隐藏入口（动作在 BuildDocks 末尾统一填充）
		m_menuBars = mView->addMenu(tr("工具栏"));
		m_menuWindows = mView->addMenu(tr("窗口"));

		// ---- Player(P) ----
		// Run Scene   = 运行当前场景（保留网格 / 坐标轴 / 调试信息）
		// Run Project = 运行整个项目（隐藏网格等编辑器参数，只显示 FPS）
		// 快捷键：F8 = Run Scene，F9 = Run Project，F5 = 刷新，F10 = 结束
		// 快捷键必须是 ApplicationShortcut：运行窗口是独立顶层窗口，
		// 它在前台时默认的 WindowShortcut 只认主窗口，按键会全部失效
		QMenu* mPlayer = mb->addMenu(tr("Player(&P)"));
		auto appKey = [](QAction* a) { a->setShortcutContext(Qt::ApplicationShortcut); return a; };
		appKey(mPlayer->addAction(tr("Run Project"), QKeySequence(Qt::Key_F9), this, &EditorWindow::OnRunProject));
		appKey(mPlayer->addAction(tr("Run Scene"), QKeySequence(Qt::Key_F8), this, &EditorWindow::OnRunScene));
		mPlayer->addSeparator();
		m_actRestart = appKey(mPlayer->addAction(tr("Restart"), QKeySequence(QStringLiteral("F6")),
			this, &EditorWindow::OnRunRestart));
		m_actRestart->setToolTip(tr("关掉运行窗口并按上次的模式重新运行"));
		m_actRefresh = appKey(mPlayer->addAction(tr("Refresh"), QKeySequence(Qt::Key_F5),
			this, &EditorWindow::OnRunRefresh));
		m_actRefresh->setToolTip(tr("不关窗口，重新载入编辑器里的场景并重置模拟"));
		m_actShutdown = appKey(mPlayer->addAction(tr("Shutdown"), QKeySequence(Qt::Key_F10),
			this, &EditorWindow::OnRunShutdown));
		m_actShutdown->setToolTip(tr("结束运行，回到编辑器"));
		// 打开菜单时按是否在运行刷新可用状态
		connect(mPlayer, &QMenu::aboutToShow, this, &EditorWindow::SyncRuntimeActions);
		SyncRuntimeActions();

		// ---- 帮助(H)：用 cornerWidget 钉在菜单栏最右侧（对应 RAD 的 MainMenu2）----
		QMenu* mHelp = new QMenu(mb);
		mHelp->addAction(tr("在线(&O)"));
		mHelp->addAction(tr("Developer"));
		mHelp->addAction(tr("关于(&A)"));
		auto* helpBtn = new QToolButton(mb);
		helpBtn->setText(tr("帮助(&H)"));
		helpBtn->setMenu(mHelp);
		helpBtn->setPopupMode(QToolButton::InstantPopup);
		helpBtn->setCursor(Qt::PointingHandCursor);
		helpBtn->setStyleSheet(
			"QToolButton { background:transparent; color:#d8d8dc; border:none; padding:3px 7px; }"
			"QToolButton::menu-indicator { image:none; }"
			"QToolButton:hover { background:#3c3c42; }");
		mb->setCornerWidget(helpBtn, Qt::TopRightCorner);
	}

	QToolBar* EditorWindow::MakeBar(const QString& title)
	{
		auto* tb = new QToolBar(title, this);
		tb->setObjectName(title);            // saveState/restoreState 依赖 objectName
		tb->setWindowTitle(title);
		tb->setMovable(true);                // 可拖动，拖出主窗口即成为浮动工具条
		tb->setFloatable(true);
		tb->setAllowedAreas(Qt::AllToolBarAreas);
		tb->setIconSize(QSize(18, 18));
		tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
		// 空白处右键 -> 自定义的"工具栏/窗口"显示隐藏菜单
		tb->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(tb, &QWidget::customContextMenuRequested, this,
			[this, tb](const QPoint& p) { ShowBarMenu(tb->mapToGlobal(p)); });
		addToolBar(Qt::TopToolBarArea, tb);
		m_bars << tb;
		return tb;
	}

	void EditorWindow::ShowBarMenu(const QPoint& globalPos)
	{
		QMenu menu(this);
		for (QToolBar* tb : m_bars)
			menu.addAction(tb->toggleViewAction());
		if (!m_docks.isEmpty())
		{
			menu.addSeparator();
			QMenu* sub = menu.addMenu(tr("窗口"));
			for (QDockWidget* d : m_docks)
				sub->addAction(d->toggleViewAction());
		}
		menu.exec(globalPos);
	}

	void EditorWindow::BuildToolBar()
	{
		const QColor ic{ 200, 200, 206 };

		// ---- 工具栏1：文件 ----
		QToolBar* b1 = MakeBar(tr("工具栏1"));
		b1->addAction(Make(Icon::NewDoc, ic), tr("新建"), this, &EditorWindow::OnFileNew);
		b1->addAction(Make(Icon::Open, ic), tr("打开"), this, &EditorWindow::OnFileOpen);
		b1->addAction(Make(Icon::Save, ic), tr("保存"), this, &EditorWindow::OnFileSave);

		// ---- 工具栏2：播放控制 ----
		QToolBar* b2 = MakeBar(tr("工具栏2"));
		QAction* aStart = b2->addAction(Make(Icon::SkipStart, ic), tr("跳到开头"));
		QAction* aPrev = b2->addAction(Make(Icon::Prev, ic), tr("上一帧"));
		QAction* aPlay = b2->addAction(Make(Icon::Play, QColor(96, 200, 96)), tr("播放/暂停"));
		aPlay->setCheckable(true);
		connect(aPlay, &QAction::toggled, this, &EditorWindow::OnPlay);
		m_actPlay = aPlay;   // 状态同步用（菜单里不再挂 Run Scene 到播放）
		QAction* aNext = b2->addAction(Make(Icon::Next, ic), tr("下一帧"));
		QAction* aEnd = b2->addAction(Make(Icon::SkipEnd, ic), tr("跳到结尾"));
		b2->addSeparator();
		b2->addAction(Make(Icon::Record, QColor(214, 64, 64)), tr("录制"));
		b2->addAction(Make(Icon::Loop, QColor(214, 150, 64)), tr("循环"));
		connect(aStart, &QAction::triggered, this, [this] { m_frame = 0; SyncPlayhead(); });
		connect(aEnd, &QAction::triggered, this, [this] { m_frame = m_maxFrame; SyncPlayhead(); });
		connect(aPrev, &QAction::triggered, this, [this] { SetFrame(m_frame - 1); });
		connect(aNext, &QAction::triggered, this, [this] { SetFrame(m_frame + 1); });

		// ---- 工具栏3：视图模式 / 吸附 ----
		QToolBar* b3 = MakeBar(tr("工具栏3"));
		QAction* t2d = b3->addAction(tr("2D"));
		QAction* t3d = b3->addAction(tr("3D"));
		m_act2D = t2d;      // 创建菜单切页时同步这两个按钮的勾选状态
		m_act3D = t3d;
		t2d->setCheckable(true);
		t3d->setCheckable(true);
		t3d->setChecked(true);
		t2d->setToolTip(tr("切换 2D 视图"));
		t3d->setToolTip(tr("切换 3D 视图"));
		auto* grp = new QActionGroup(this);
		grp->addAction(t2d);
		grp->addAction(t3d);
		connect(grp, &QActionGroup::triggered, this, [this](QAction* a)
			{
				// 2D = 节点图，3D = 场景视口（两按钮互斥联动）
				m_centralStack->setCurrentWidget(a->text() == tr("2D")
					? static_cast<QWidget*>(m_graph2d)
					: static_cast<QWidget*>(m_viewport));
			});
		b3->addSeparator();
		m_actMagnet = b3->addAction(Make(Icon::Magnet, QColor(214, 64, 64)), tr("吸附"));
		m_actMagnet->setCheckable(true);
		connect(m_actMagnet, &QAction::toggled, this, [this](bool on) { m_viewport->SetSnap(on); });

		// ---- 工具栏4：显示控制 ----
		QToolBar* b4 = MakeBar(tr("工具栏4"));
		QAction* aGrid = b4->addAction(Make(Icon::Grid, ic), tr("显示栅格"));
		aGrid->setCheckable(true);
		aGrid->setChecked(m_viewport->ShowGrid());
		connect(aGrid, &QAction::toggled, this, [this](bool on) { m_viewport->SetShowGrid(on); });
		QAction* aWire = b4->addAction(Make(Icon::Wire, ic), tr("线框"));
		aWire->setCheckable(true);
		connect(aWire, &QAction::toggled, this, [this](bool on) { m_viewport->SetWireFrame(on); });
		QAction* aFps = b4->addAction(Make(Icon::Gauge, ic), tr("显示 FPS"));
		aFps->setCheckable(true);
		aFps->setChecked(m_viewport->ShowFps());
		connect(aFps, &QAction::toggled, this, [this](bool on) { m_viewport->SetShowFps(on); });
		b4->addSeparator();
		b4->addAction(Make(Icon::Home, ic), tr("重置视角"), this, [this] { m_viewport->ResetCamera(); });

		// ---- 工具栏5：ViewCube 外观（大小 / 透明度）----
		QToolBar* b5 = MakeBar(tr("工具栏5"));
		b5->addAction(Make(Icon::Wire, ic), tr("ViewCube 设置…"),
			this, &EditorWindow::OpenViewCubeDialog);

		auto* labSize = new QLabel(tr(" 大小 "));
		labSize->setStyleSheet("color:#c8c8ce;");
		b5->addWidget(labSize);
		m_vcSizeSlider = new QSlider(Qt::Horizontal);
		m_vcSizeSlider->setRange(12, 60);
		m_vcSizeSlider->setValue(28);
		m_vcSizeSlider->setFixedWidth(96);
		m_vcSizeSlider->setToolTip(tr("视图方块的半边长（像素）"));
		b5->addWidget(m_vcSizeSlider);
		m_vcSizeVal = new QLabel;
		m_vcSizeVal->setMinimumWidth(48);
		m_vcSizeVal->setStyleSheet("color:#9fd8a0; font-weight:bold;");
		b5->addWidget(m_vcSizeVal);

		m_vcAuto = b5->addAction(tr("自动"));
		m_vcAuto->setCheckable(true);
		m_vcAuto->setChecked(true);
		m_vcAuto->setToolTip(tr("大小跟随视口自动缩放"));

		auto* labAlpha = new QLabel(tr("  透明 "));
		labAlpha->setStyleSheet("color:#c8c8ce;");
		b5->addWidget(labAlpha);
		m_vcAlphaSlider = new QSlider(Qt::Horizontal);
		m_vcAlphaSlider->setRange(15, 100);
		m_vcAlphaSlider->setValue(100);
		m_vcAlphaSlider->setFixedWidth(96);
		m_vcAlphaSlider->setToolTip(tr("视图方块的透明度"));
		b5->addWidget(m_vcAlphaSlider);
		m_vcAlphaVal = new QLabel;
		m_vcAlphaVal->setMinimumWidth(48);
		m_vcAlphaVal->setStyleSheet("color:#9fd8a0; font-weight:bold;");
		b5->addWidget(m_vcAlphaVal);

		// 滑块的默认样式在暗色主题下几乎看不见，单独调一下
		const QString sliderQss =
			"QSlider::groove:horizontal { height:4px; background:#3a3a40; border-radius:2px; }"
			"QSlider::sub-page:horizontal { background:#5b8def; border-radius:2px; }"
			"QSlider::handle:horizontal { width:12px; margin:-5px 0; border-radius:6px;"
			" background:#d8d8dc; }"
			"QSlider::handle:horizontal:hover { background:#ffffff; }"
			"QSlider::groove:horizontal:disabled { background:#2c2c31; }";
		m_vcSizeSlider->setStyleSheet(sliderQss);
		m_vcAlphaSlider->setStyleSheet(sliderQss);

		// 滑块 / 自动开关 -> 统一入口（ApplyViewCube 内部会回写控件，用信号阻断防成环）
		connect(m_vcSizeSlider, &QSlider::valueChanged, this, [this](int v)
			{
				if (m_vcAuto->isChecked())
				{
					QSignalBlocker autoBlock(m_vcAuto);
					m_vcAuto->setChecked(false);       // 拖动大小即视为手动
				}
				ApplyViewCube(v, m_vcAlpha);
			});
		connect(m_vcAlphaSlider, &QSlider::valueChanged, this, [this](int v)
			{
				ApplyViewCube(m_vcSize, v);
			});
		connect(m_vcAuto, &QAction::toggled, this, [this](bool on)
			{
				ApplyViewCube(on ? 0 : m_vcSizeSlider->value(), m_vcAlpha);
			});
	}

	void EditorWindow::BuildDocks()
	{
		setDockNestingEnabled(true);

		// ---- 右侧：Create ----
		auto* createBody = new QWidget;
		auto* cv = new QVBoxLayout(createBody);
		cv->setContentsMargins(8, 6, 8, 8);
		cv->setSpacing(6);
		auto* labStd = new QLabel(tr("标准基本体"));
		labStd->setStyleSheet("color:#d8d8dc; font-weight:bold;");
		cv->addWidget(labStd);
		auto* grid = new QGridLayout;
		grid->setSpacing(8);
		auto* btnBox = new QPushButton(tr("+ 方体"));
		auto* btnPlane = new QPushButton(tr("+ 平面"));
		auto* btnNum = new QPushButton(tr("+ 数值"));
		btnBox->setMinimumHeight(34);
		btnPlane->setMinimumHeight(34);
		btnNum->setMinimumHeight(34);
		grid->addWidget(btnBox, 0, 0);
		grid->addWidget(btnPlane, 0, 1);
		grid->addWidget(btnNum, 1, 0, 1, 2);
		cv->addLayout(grid);
		cv->addStretch();
		connect(btnBox, &QPushButton::clicked, this, [this] { m_viewport->AddBox(); RefreshLayers(); });
		connect(btnPlane, &QPushButton::clicked, this, [this] { m_viewport->AddPlane(); RefreshLayers(); });
		QDockWidget* dockCreate = MakeDock(tr("Create"), createBody);
		dockCreate->setObjectName("dockCreate");
		addDockWidget(Qt::RightDockWidgetArea, dockCreate);

		// ---- 右侧：属性 ----
		m_propTable = new QTableWidget(1, 2);
		m_propTable->setHorizontalHeaderLabels({ tr("属性"), tr("值") });
		m_propTable->horizontalHeader()->setStretchLastSection(true);
		m_propTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
		m_propTable->setColumnWidth(0, 70);
		m_propTable->verticalHeader()->setVisible(false);
		m_propTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		m_propTable->setAlternatingRowColors(true);
		connect(m_propTable, &QTableWidget::itemChanged, this, &EditorWindow::OnPropItemChanged);
		UpdatePropRow();
		QDockWidget* dockProps = MakeDock(tr("属性"), m_propTable);
		dockProps->setObjectName("dockProps");
		splitDockWidget(dockCreate, dockProps, Qt::Vertical);

		// ---- 底部：动画 ----
		auto* animBody = new QWidget;
		auto* av = new QVBoxLayout(animBody);
		av->setContentsMargins(6, 2, 6, 4);
		av->setSpacing(2);

		auto* row1 = new QHBoxLayout;
		row1->setSpacing(2);
		const QColor ic{ 200, 200, 206 };
		auto mkBtn = [&](Icon icon, const QColor& c, const QString& tip)
		{
			auto* b = new QToolButton;
			b->setIcon(Make(icon, c, 14));
			b->setToolTip(tip);
			b->setAutoRaise(true);
			return b;
		};
		row1->addWidget(mkBtn(Icon::SkipStart, ic, tr("跳到开头")));
		row1->addWidget(mkBtn(Icon::Prev, ic, tr("上一帧")));
		auto* playBtn = mkBtn(Icon::Play, QColor(96, 200, 96), tr("播放/暂停"));
		playBtn->setCheckable(true);
		row1->addWidget(playBtn);
		m_playBtn = playBtn;
		row1->addWidget(mkBtn(Icon::Next, ic, tr("下一帧")));
		row1->addWidget(mkBtn(Icon::SkipEnd, ic, tr("跳到结尾")));
		row1->addWidget(mkBtn(Icon::Loop, QColor(96, 200, 96), tr("循环")));
		row1->addWidget(mkBtn(Icon::Record, QColor(214, 64, 64), tr("录制")));
		row1->addWidget(mkBtn(Icon::Loop, QColor(214, 150, 64), tr("自动关键点")));
		row1->addSpacing(14);
		auto* labFrame = new QLabel(tr("帧"));
		m_frameSpin = new QSpinBox;
		m_frameSpin->setRange(0, m_maxFrame);
		m_frameSpin->setValue(0);
		auto* labTotal = new QLabel(tr("/ 100"));
		row1->addWidget(labFrame);
		row1->addWidget(m_frameSpin);
		row1->addWidget(labTotal);
		row1->addSpacing(18);
		row1->addWidget(new QLabel(tr("频率: 30 fps")));
		row1->addStretch();

		auto* row2 = new QHBoxLayout;
		m_timeLabel = new QLabel(tr("时间: 0.0 s"));
		row2->addWidget(m_timeLabel);
		m_timeline = new TimelineWidget;
		m_timeline->SetRange(m_maxFrame);
		row2->addWidget(m_timeline, 1);
		av->addLayout(row1);
		av->addLayout(row2);

		connect(playBtn, &QToolButton::toggled, this, &EditorWindow::OnPlay);
		connect(m_frameSpin, &QSpinBox::valueChanged, this, [this](int v) { SetFrame(v); });
		connect(m_timeline, &TimelineWidget::FrameChanged, this, [this](int v) { SetFrame(v); });

		QDockWidget* dockAnim = MakeDock(tr("动画"), animBody);
		dockAnim->setObjectName("dockAnim");
		addDockWidget(Qt::BottomDockWidgetArea, dockAnim);

		// ---- 底部左：图层 ----
		m_layerTree = new QTreeWidget;
		m_layerTree->setRootIsDecorated(false);
		m_layerTree->setHeaderHidden(true);
		m_layerTree->setColumnCount(4);
		m_layerTree->setColumnWidth(0, 26);
		m_layerTree->setColumnWidth(1, 26);
		m_layerTree->setColumnWidth(2, 26);
		m_layerTree->setColumnWidth(3, 90);
		RefreshLayers();
		connect(m_layerTree, &QTreeWidget::currentItemChanged,
			this, &EditorWindow::OnLayerCurrentChanged);
		QDockWidget* dockLayer = MakeDock(tr("图层"), m_layerTree);
		dockLayer->setObjectName("dockLayer");
		addDockWidget(Qt::BottomDockWidgetArea, dockLayer);
		// 图层排在动画条下方（贴截图：底部左侧一块，右边留空）
		splitDockWidget(dockAnim, dockLayer, Qt::Vertical);

		// 占位面板：把图层挤到左边窄条，右边保持空白背景
		auto* fillerBody = new QWidget;
		fillerBody->setStyleSheet("background:#2f2f33;");
		auto* dockFiller = new QDockWidget;
		dockFiller->setObjectName("dockFiller");
		dockFiller->setWidget(fillerBody);
		dockFiller->setTitleBarWidget(new QWidget);   // 无标题栏
		dockFiller->setFeatures(QDockWidget::NoDockWidgetFeatures);
		addDockWidget(Qt::BottomDockWidgetArea, dockFiller);
		splitDockWidget(dockLayer, dockFiller, Qt::Horizontal);
		resizeDocks({ dockLayer, dockFiller, dockAnim }, { 180, 900, 0 }, Qt::Horizontal);
		resizeDocks({ dockAnim, dockLayer }, { 100, 130 }, Qt::Vertical);

		// 图层底色浅蓝高亮（对齐截图第二行）
		m_layerTree->setStyleSheet(
			"QTreeWidget::item { height:20px; }"
			"QTreeWidget::item:selected { background:#3a5a8c; }");

		// 记录面板 + 填充 视图▸窗口 / 视图▸工具栏（toggleViewAction 随面板/工具栏显隐自动打勾）
		m_docks << dockCreate << dockProps << dockAnim << dockLayer;
		if (m_menuWindows)
			for (QDockWidget* d : m_docks)
				m_menuWindows->addAction(d->toggleViewAction());
		if (m_menuBars)
			for (QToolBar* t : m_bars)
				m_menuBars->addAction(t->toggleViewAction());

		// 填充面板跟随 动画/图层 显隐：两者都关掉时它也隐藏，
		// 否则底部会残留一块没有标题栏、关不掉也拉不动的空白区域。
		// 注意必须用 visibilityChanged（变更之后触发）而不是 toggleViewAction 的
		// toggled（变更之前触发）：关最后一个面板时 isVisible() 尚未更新，会误判。
		auto syncFiller = [this, dockAnim, dockLayer, dockFiller]
		{
			dockFiller->setVisible(dockAnim->isVisible() || dockLayer->isVisible());
		};
		connect(dockAnim, &QDockWidget::visibilityChanged, this, syncFiller);
		connect(dockLayer, &QDockWidget::visibilityChanged, this, syncFiller);
	}

	void EditorWindow::SaveLayout()
	{
		QSettings s(QStringLiteral("GameEngine"), QStringLiteral("Editor"));
		s.setValue(QStringLiteral("layout/geometry"), saveGeometry());
		s.setValue(QStringLiteral("layout/state"), saveState());
	}

	void EditorWindow::RestoreLayout()
	{
		QSettings s(QStringLiteral("GameEngine"), QStringLiteral("Editor"));
		const QByteArray geo = s.value(QStringLiteral("layout/geometry")).toByteArray();
		const QByteArray st = s.value(QStringLiteral("layout/state")).toByteArray();
		if (!geo.isEmpty())
			restoreGeometry(geo);
		if (!st.isEmpty())
			restoreState(st);
	}

	// ---- 图层列表 ----
	void EditorWindow::RefreshLayers()
	{
		QSignalBlocker blocker(m_layerTree);
		const QString selName = m_viewport->SelectedName();   // 以视口选中项为准
		m_layerTree->clear();
		const QColor ic{ 190, 190, 196 };
		QTreeWidgetItem* selItem = nullptr;
		for (const auto& o : m_viewport->Objects())
		{
			auto* item = new QTreeWidgetItem(m_layerTree);
			item->setIcon(0, Make(Icon::Eye, ic, 12));
			item->setIcon(1, Make(Icon::Sun, ic, 12));
			item->setIcon(2, Make(Icon::Lock, ic, 12));
			// 颜色小块 + 名称
			QPixmap chip(10, 10);
			chip.fill(o.color);
			item->setIcon(3, chip);
			item->setText(3, QString::fromStdString(o.name));
			if (!selName.isEmpty() && selName == item->text(3))
				selItem = item;
		}
		if (selItem)
			m_layerTree->setCurrentItem(selItem);
		else if (m_layerTree->topLevelItemCount() > 0)
			m_layerTree->setCurrentItem(m_layerTree->topLevelItem(0));
		blocker.unblock();
		SyncSelectionUi();
	}

	void EditorWindow::OnLayerCurrentChanged()
	{
		if (!m_layerTree->currentItem())
			return;
		const QString name = m_layerTree->currentItem()->text(3);
		const auto& objs = m_viewport->Objects();
		for (int i = 0; i < static_cast<int>(objs.size()); ++i)
			if (QString::fromStdString(objs[i].name) == name)
			{
				m_viewport->SetSelected(i);
				break;
			}
		SyncSelectionUi();
	}

	void EditorWindow::OnViewportSelection(const QString& name)
	{
		if (!name.isEmpty())
		{
			// 同步图层高亮
			QSignalBlocker blocker(m_layerTree);
			for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i)
			{
				auto* it = m_layerTree->topLevelItem(i);
				if (it->text(3) == name)
				{
					m_layerTree->setCurrentItem(it);
					break;
				}
			}
		}
		SyncSelectionUi();
	}

	void EditorWindow::SyncSelectionUi()
	{
		UpdatePropRow();
	}

	void EditorWindow::UpdatePropRow()
	{
		if (!m_propTable)
			return;
		m_propTable->blockSignals(true);        // 程序填充不触发 itemChanged
		const QString name = m_viewport ? m_viewport->SelectedName() : QString();
		const bool hasSel = m_viewport && m_viewport->SelectedCount() > 0;
		const bool isTrigger = hasSel && m_viewport->SelectedKind() == 2;
		if (m_propTable->rowCount() > 2)
			m_propTable->removeCellWidget(2, 1);
		m_propTable->setRowCount(isTrigger ? 3 : 2);
		m_propTable->setItem(0, 0, new QTableWidgetItem(tr("选择")));
		m_propTable->setItem(0, 1, new QTableWidgetItem(
			name.isEmpty() ? tr("未选择物体") : name));

		// 重力开关（默认开）：运行 Scene 时该物体是否下落
		m_propTable->setItem(1, 0, new QTableWidgetItem(tr("重力")));
		auto* g = new QTableWidgetItem();
		g->setFlags((g->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
		g->setCheckState(hasSel && m_viewport->SelectedGravity() ? Qt::Checked : Qt::Unchecked);
		g->setText(hasSel ? QString() : tr("—"));
		g->setToolTip(tr("勾选后该物体在 Run Scene / Run Project 中受重力下落（新建物体默认勾选）"));
		if (!hasSel)
			g->setFlags(g->flags() & ~Qt::ItemIsEnabled);
		m_propTable->setItem(1, 1, g);

		// 跳转精灵：选目标关卡（空 = 自动下一关）
		if (isTrigger)
		{
			m_propTable->setItem(2, 0, new QTableWidgetItem(tr("跳转关卡")));
			auto* box = new QComboBox(m_propTable);
			box->addItem(tr("自动（下一关）"), QString());
			const QDir d(LevelsDir());
			if (d.exists())
				for (const QString& f : d.entryList(QStringList() << QStringLiteral("*.yu"), QDir::Files, QDir::Name))
					box->addItem(f, f);
			const QString cur = m_viewport->SelectedLevelTarget();
			if (!cur.isEmpty())
			{
				if (box->findData(cur) < 0)      // 目标文件当前不在 Levels 目录里也保留显示
					box->addItem(cur, cur);
				box->setCurrentIndex(box->findData(cur));
			}
			box->setToolTip(tr("方块碰到这个跳转精灵时切到哪个关卡；选「自动」= Levels 目录里的下一关"));
			connect(box, &QComboBox::currentIndexChanged, this, [this, box](int)
				{
					if (m_viewport)
						m_viewport->SetSelectedLevelTarget(box->itemData(box->currentIndex()).toString());
				});
			m_propTable->setCellWidget(2, 1, box);
		}
		m_propTable->blockSignals(false);
	}

	void EditorWindow::OnPropItemChanged(QTableWidgetItem* item)
	{
		if (!item || item->row() != 1 || !m_viewport)
			return;
		m_viewport->SetSelectedGravity(item->checkState() == Qt::Checked);
	}

	// ---- 动画播放 ----
	void EditorWindow::SetFrame(int f)
	{
		m_frame = std::clamp(f, 0, m_maxFrame);
		SyncPlayhead();
	}

	void EditorWindow::SyncPlayhead()
	{
		QSignalBlocker s1(m_frameSpin);
		m_frameSpin->setValue(m_frame);
		m_timeline->SetFrame(m_frame);
		m_timeLabel->setText(tr("时间: %1 s").arg(QString::number(m_frame / double(m_fps), 'f', 1)));
	}

	void EditorWindow::OnPlay(bool play)
	{
		m_playing = play;
		// 工具条 / 动画面板两个播放按钮互相同步（屏蔽信号避免递归）
		if (m_actPlay && m_actPlay->isChecked() != play)
		{
			QSignalBlocker b(m_actPlay);
			m_actPlay->setChecked(play);
		}
		if (m_playBtn && m_playBtn->isChecked() != play)
		{
			QSignalBlocker b(m_playBtn);
			m_playBtn->setChecked(play);
		}
		if (play && m_frame >= m_maxFrame)
			m_frame = 0;
	}

	void EditorWindow::OnStop()
	{
		m_playing = false;
		if (m_actPlay && m_actPlay->isChecked())
		{
			QSignalBlocker b(m_actPlay);
			m_actPlay->setChecked(false);
		}
		m_frame = 0;
		SyncPlayhead();
	}

	void EditorWindow::OnFrameTick()
	{
		if (!m_playing)
			return;
		m_frame = (m_frame + 1) % (m_maxFrame + 1);   // 到 100 回 0 循环
		SyncPlayhead();
	}

	// ---- 设置窗口 ----
	void EditorWindow::SelfTestLock()
	{
		// 验证"锁定布局→解锁布局"后视图▸窗口的显隐开关仍然有效。
		// 历史 bug：锁定曾设 NoDockWidgetFeatures（剥掉 Closable），解锁也只恢复
		// Movable——toggleViewAction 走 close()，没有 Closable 的面板 close 被拒绝。
		if (m_docks.isEmpty())
		{
			qWarning("SelfTest: no docks");
			return;
		}
		auto step = [this](const char* what, bool expectVisible)
		{
			QDockWidget* d = m_docks.first();
			d->toggleViewAction()->trigger();
			const bool ok = d->isVisible() == expectVisible;
			qWarning("SelfTest: %s => %s (visible=%d)", what, ok ? "OK" : "FAIL", (int)d->isVisible());
		};
		m_actLock->setChecked(true);          // 锁定
		step("hide while locked", false);
		step("show while locked", true);
		m_actLock->setChecked(false);         // 解锁
		step("hide after unlock", false);
		step("show after unlock", true);
	}

	void EditorWindow::OnOpenSettings()
	{
		if (!m_settings)
		{
			m_settings = new SettingsDialog(this);
			connect(m_settings, &SettingsDialog::SettingsApplied, this, [this]
				{
					m_viewport->SetShowGrid(m_settings->ShowGrid());
					m_viewport->SetShowFps(m_settings->ShowFps());
					m_autoSave = m_settings->AutoSave();
					m_autoSaveMinutes = m_settings->AutoSaveMinutes();
				});
		}
		m_settings->SetCurrent(m_autoSave, m_autoSaveMinutes,
			m_viewport->ShowGrid(), m_viewport->ShowFps());
		m_settings->show();
		m_settings->raise();
		m_settings->activateWindow();
	}

	// ================= 文件（.yu 文档） =================
	void EditorWindow::UpdateTitle()
	{
		const QString name = m_docPath.isEmpty()
			? tr("未命名")
			: QFileInfo(m_docPath).fileName();
		setWindowTitle(tr("3DGameEngine - %1").arg(name));
	}

	void EditorWindow::OnFileNew()
	{
		m_docPath.clear();
		UpdateTitle();
		SceneLink::Reset();
		m_viewport->LoadState(QJsonObject{});   // 清空对象，相机回默认
		m_graph2d->ResetDocument();             // 清空节点 / 连线 / 组
		RefreshLayers();
	}

	void EditorWindow::OnFileOpen()
	{
		const QString start = m_docPath.isEmpty()
			? QString()
			: QFileInfo(m_docPath).absolutePath();
		const QString path = QFileDialog::getOpenFileName(
			this, tr("打开场景"), start,
			tr("场景文件 (*.yu);;所有文件 (*)"));
		if (path.isEmpty())
			return;
		QString err;
		if (!Document::Load(path, m_viewport, m_graph2d, &err))
		{
			QMessageBox::warning(this, tr("打开"), err);
			return;
		}
		m_docPath = path;
		UpdateTitle();
		RefreshLayers();
	}

	void EditorWindow::OnFileSave()
	{
		if (m_docPath.isEmpty())
		{
			OnFileSaveAs();
			return;
		}
		QString err;
		if (!Document::Save(m_docPath, m_viewport, m_graph2d, &err))
		{
			QMessageBox::warning(this, tr("保存"), err);
			return;
		}
	}

	void EditorWindow::OnFileSaveAs()
	{
		const QString start = m_docPath.isEmpty()
			? QString()
			: QFileInfo(m_docPath).absolutePath();
		const QString path = QFileDialog::getSaveFileName(
			this, tr("保存场景"), start,
			tr("场景文件 (*.yu);;所有文件 (*)"));
		if (path.isEmpty())
			return;
		QString fixed = path;
		if (!fixed.endsWith(QStringLiteral(".yu"), Qt::CaseInsensitive))
			fixed += QStringLiteral(".yu");
		QString err;
		if (!Document::Save(fixed, m_viewport, m_graph2d, &err))
		{
			QMessageBox::warning(this, tr("保存"), err);
			return;
		}
		m_docPath = fixed;
		UpdateTitle();
	}

	// ================= Level（关卡 = Levels 目录下的 .yu 场景文件） =================
	// 目录固定在"工程根目录/Levels"（从 exe 向上找 CMakeLists.txt 定位工程根）：
	// 不会被清理 out/build 时连带删掉，用资源管理器也好找。
	QString EditorWindow::LevelsDir() const
	{
		return Document::LevelsDir();
	}

	// 文件 ▸ 打开 Level 的列表：每次弹出前扫一遍 Levels 目录
	void EditorWindow::FillLevelMenu()
	{
		if (!m_menuLevels)
			return;
		m_menuLevels->clear();

		const QString dir = LevelsDir();
		QDir d(dir);
		const QStringList files = d.exists()
			? d.entryList(QStringList() << QStringLiteral("*.yu"), QDir::Files, QDir::Name)
			: QStringList();

		if (files.isEmpty())
		{
			QAction* a = m_menuLevels->addAction(tr("（Levels 目录里还没有 .yu 场景）"));
			a->setEnabled(false);
		}
		for (const QString& f : files)
		{
			const QString path = d.filePath(f);
			QAction* a = m_menuLevels->addAction(f);
			a->setCheckable(true);
			a->setChecked(!m_docPath.isEmpty() && QFileInfo(path) == QFileInfo(m_docPath));
			connect(a, &QAction::triggered, this, [this, path] { OpenLevelFile(path); });
		}

		m_menuLevels->addSeparator();
		m_menuLevels->addAction(tr("新建 Level..."), this, &EditorWindow::OnNewLevel);
		m_menuLevels->addAction(tr("打开 Level 文件夹"), this, [dir]
			{
				QDir().mkpath(dir);
				QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
			});
		m_menuLevels->addAction(tr("浏览文件..."), this, &EditorWindow::OnOpenLevel);
	}

	bool EditorWindow::OpenLevelFile(const QString& path)
	{
		QString err;
		if (!Document::Load(path, m_viewport, m_graph2d, &err))
		{
			QMessageBox::warning(this, tr("打开 Level"), err);
			return false;
		}
		m_docPath = path;
		UpdateTitle();
		RefreshLayers();
		statusBar()->showMessage(tr("已打开 %1").arg(QFileInfo(path).fileName()), 3000);
		return true;
	}

	void EditorWindow::OnOpenLevel()
	{
		const QString dir = LevelsDir();
		QDir().mkpath(dir);
		const QString path = QFileDialog::getOpenFileName(this, tr("打开 Level"), dir,
			tr("场景文件 (*.yu);;所有文件 (*)"));
		if (path.isEmpty())
			return;
		OpenLevelFile(path);
	}

	void EditorWindow::OnNewLevel()
	{
		const QString dir = LevelsDir();
		QDir().mkpath(dir);

		// 默认名字 = 第一个没被占用的 LevelNN（Level01 / Level02 …）
		QString name = QStringLiteral("Level01");
		for (int n = 1; n < 1000; ++n)
		{
			name = QStringLiteral("Level%1").arg(n, 2, 10, QLatin1Char('0'));
			if (!QFile::exists(QDir(dir).filePath(name + QStringLiteral(".yu"))))
				break;
		}

		bool ok = false;
		const QString in = QInputDialog::getText(this, tr("新建 Level"),
			tr("名称（保存到 %1）").arg(QDir::toNativeSeparators(dir)),
			QLineEdit::Normal, name, &ok);
		if (!ok)
			return;

		QString base = in.trimmed();
		if (base.isEmpty())
			return;
		if (base.endsWith(QStringLiteral(".yu"), Qt::CaseInsensitive))
			base.chop(3);
		const QString path = QDir(dir).filePath(base + QStringLiteral(".yu"));

		if (QFile::exists(path) &&
			QMessageBox::question(this, tr("新建 Level"),
				tr("%1 已存在，要覆盖吗？").arg(QFileInfo(path).fileName()),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
			return;

		// 新 Level = 空白 3D 场景（与 创建 ▸ 3D 场景 一致），先存盘再当作当前文档打开
		PushUndo();
		SceneLink::Reset();
		m_viewport->LoadState(QJsonObject{});
		m_graph2d->ResetDocument();
		m_viewport->SetView3D(true);
		ShowPage(false);
		RefreshLayers();

		QString err;
		if (!Document::Save(path, m_viewport, m_graph2d, &err))
		{
			QMessageBox::warning(this, tr("新建 Level"), err);
			return;
		}
		m_docPath = path;
		UpdateTitle();
		statusBar()->showMessage(tr("已新建 %1").arg(QFileInfo(path).fileName()), 3000);
	}

	// ================= 运行（Run Scene / Run Project） =================
	void EditorWindow::OnRunScene()
	{
		OpenRuntime(false);
	}

	void EditorWindow::OnRunProject()
	{
		OpenRuntime(true);
	}

	void EditorWindow::OnRunRestart()
	{
		OpenRuntime(m_lastRunMode == 2);
	}

	void EditorWindow::OnRunRefresh()
	{
		if (m_runtime)
		{
			QJsonObject g;
			m_graph2d->SaveState(g);
			m_runtime->SetGraph(g);                         // 运行期间可能改过节点图
			m_runtime->Refresh();
		}
	}

	void EditorWindow::OnRunShutdown()
	{
		if (m_runtime)
			m_runtime->close();
	}

	void EditorWindow::SyncRuntimeActions()
	{
		const bool running = !m_runtime.isNull();
		if (m_actRefresh)
			m_actRefresh->setEnabled(running);
		if (m_actShutdown)
			m_actShutdown->setEnabled(running);
		// Restart 保持可用：没在运行时按上次模式直接启动
	}

	void EditorWindow::OpenRuntime(bool project)
	{
		m_lastRunMode = project ? 2 : 1;

		// 同一时间只保留一个运行窗口
		if (m_runtime)
			m_runtime->close();

		const QString name = m_docPath.isEmpty()
			? tr("未命名")
			: QFileInfo(m_docPath).completeBaseName();
		const QString title = (project ? tr("Run Project - %1") : tr("Run Scene - %1")).arg(name);

		// 2D 节点图快照：运行时由它驱动（键盘 / 累加 / 重力 / 镜头）
		QJsonObject g;
		m_graph2d->SaveState(g);

		auto* win = new RuntimeWindow(m_viewport,
			project ? Viewport3D::RuntimeMode::Project : Viewport3D::RuntimeMode::Scene,
			title, g);
		m_runtime = win;
		win->SetLevelPath(m_docPath);              // 跳转精灵没写目标时用它算"下一关"
		connect(win, &QObject::destroyed, this, &EditorWindow::SyncRuntimeActions);
		win->show();
		win->raise();
		win->activateWindow();
		SyncRuntimeActions();
	}

	// ================= 创建菜单 =================
	void EditorWindow::ShowPage(bool twoD)
	{
		if (!m_centralStack)
			return;
		m_centralStack->setCurrentWidget(twoD ? static_cast<QWidget*>(m_graph2d)
		                                     : static_cast<QWidget*>(m_viewport));
		// 同步工具栏 2D / 3D 按钮（避免菜单切页后按钮还勾着旧状态）
		if (m_act2D && m_act2D->isChecked() != twoD)
		{
			QSignalBlocker b(m_act2D);
			m_act2D->setChecked(twoD);
		}
		if (m_act3D && m_act3D->isChecked() == twoD)
		{
			QSignalBlocker b(m_act3D);
			m_act3D->setChecked(!twoD);
		}
		if (twoD)
			m_graph2d->setFocus();
		else
			m_viewport->setFocus();
	}

	void EditorWindow::OnCreate2DScene()
	{
		PushUndo();
		SceneLink::Reset();
		m_viewport->LoadState(QJsonObject{});     // 2D 场景不需要 3D 对象
		m_graph2d->ResetDocument();               // 空白节点图，不预置任何节点
		ShowPage(true);
		RefreshLayers();
		m_docPath.clear();
		UpdateTitle();
	}

	void EditorWindow::OnCreate3DScene()
	{
		PushUndo();
		SceneLink::Reset();
		m_viewport->LoadState(QJsonObject{});     // 清空对象，相机回默认
		m_graph2d->ResetDocument();
		m_viewport->SetView3D(true);
		// 空白场景：不自动加地面 / 方块
		ShowPage(false);
		RefreshLayers();
		m_docPath.clear();
		UpdateTitle();
	}

	// ================= 编辑菜单：撤销 / 重做 =================
	// 做法是整篇文档快照（3D 对象 + 2D 节点图）。只有走菜单/快捷键的编辑操作会记录，
	// 视口里手动拖 gizmo 这类连续操作不单独记步，撤销会回到上一次快照。
	QJsonObject EditorWindow::CaptureState() const
	{
		QJsonObject out;
		QJsonObject vp;
		m_viewport->SaveState(vp);
		QJsonObject g;
		m_graph2d->SaveState(g);
		out["viewport"] = vp;
		out["graph2d"] = g;
		return out;
	}

	void EditorWindow::RestoreState(const QJsonObject& s)
	{
		SceneLink::Reset();
		m_viewport->LoadState(s["viewport"].toObject());
		m_graph2d->LoadState(s["graph2d"].toObject());
		SceneLink::Reset();      // 载入的节点已带配对，不需要历史
		RefreshLayers();
	}

	void EditorWindow::PushUndo()
	{
		m_undo.append(CaptureState());
		while (m_undo.size() > 30)
			m_undo.removeFirst();
		m_redo.clear();
	}

	void EditorWindow::OnEditUndo()
	{
		if (m_undo.isEmpty())
			return;
		m_redo.append(CaptureState());
		RestoreState(m_undo.takeLast());
	}

	void EditorWindow::OnEditRedo()
	{
		if (m_redo.isEmpty())
			return;
		m_undo.append(CaptureState());
		RestoreState(m_redo.takeLast());
	}

	// ================= 编辑菜单：剪切 / 复制 / 粘贴 / 全选 =================
	namespace
	{
		// 3D 剪贴板用 JSON 存对象，字段与 Viewport3D::SaveState 对齐
		QJsonObject ObjToJson(const Viewport3D::Obj& o)
		{
			QJsonObject j;
			j["kind"] = o.kind;
			j["name"] = QString::fromStdString(o.name);
			j["color"] = o.color.name();
			j["pos"] = QJsonArray{ double(o.pos.x()), double(o.pos.y()), double(o.pos.z()) };
			j["rot"] = QJsonArray{ double(o.rot.x()), double(o.rot.y()), double(o.rot.z()) };
			j["size"] = QJsonArray{ double(o.size.x()), double(o.size.y()), double(o.size.z()) };
			return j;
		}

		bool JsonToObj(const QJsonObject& j, Viewport3D::Obj& o)
		{
			const QJsonArray pos = j["pos"].toArray();
			const QJsonArray rot = j["rot"].toArray();
			const QJsonArray siz = j["size"].toArray();
			if (pos.size() != 3 || rot.size() != 3 || siz.size() != 3)
				return false;
			o.kind = j["kind"].toInt();
			o.name = j["name"].toString().toStdString();
			o.color = QColor(j["color"].toString());
			o.pos = QVector3D(float(pos.at(0).toDouble()), float(pos.at(1).toDouble()), float(pos.at(2).toDouble()));
			o.rot = QVector3D(float(rot.at(0).toDouble()), float(rot.at(1).toDouble()), float(rot.at(2).toDouble()));
			o.size = QVector3D(float(siz.at(0).toDouble()), float(siz.at(1).toDouble()), float(siz.at(2).toDouble()));
			o.selected = false;
			return true;
		}
	}

	void EditorWindow::Copy3DSelected()
	{
		m_clipObjs = QJsonArray();
		for (const auto& o : m_viewport->Objects())
			if (o.selected)
				m_clipObjs.append(ObjToJson(o));
	}

	void EditorWindow::Paste3DClipboard()
	{
		if (m_clipObjs.isEmpty())
			return;
		std::vector<int> uids;
		for (const QJsonValue& v : m_clipObjs)
		{
			Viewport3D::Obj o;
			if (!JsonToObj(v.toObject(), o))
				continue;
			o.pos += QVector3D(0.5f, 0.f, 0.5f);   // 错开一点，避免和原件完全重叠
			uids.push_back(m_viewport->AddObjectRaw(o));
		}
		if (!uids.empty())
			m_viewport->SelectByUids(uids);
	}

	void EditorWindow::OnEditCopy()
	{
		if (m_centralStack->currentWidget() == static_cast<QWidget*>(m_graph2d))
			m_graph2d->CopyToClipboard();
		else
			Copy3DSelected();
	}

	void EditorWindow::OnEditCut()
	{
		PushUndo();
		if (m_centralStack->currentWidget() == static_cast<QWidget*>(m_graph2d))
		{
			m_graph2d->CopyToClipboard();
			m_graph2d->DeleteSelected();
		}
		else
		{
			Copy3DSelected();
			m_viewport->DeleteSelected();
		}
		RefreshLayers();
	}

	void EditorWindow::OnEditPaste()
	{
		PushUndo();
		if (m_centralStack->currentWidget() == static_cast<QWidget*>(m_graph2d))
			m_graph2d->PasteAtCenter();
		else
		{
			Paste3DClipboard();
			RefreshLayers();
		}
	}

	void EditorWindow::OnEditSelectAll()
	{
		if (m_centralStack->currentWidget() == static_cast<QWidget*>(m_graph2d))
		{
			m_graph2d->SelectAllNodes();
			return;
		}
		std::vector<int> uids;
		uids.reserve(m_viewport->Objects().size());
		for (const auto& o : m_viewport->Objects())
			uids.push_back(o.uid);
		m_viewport->SelectByUids(uids);
		RefreshLayers();
	}

	// ================= 导入 / 导出 =================
	void EditorWindow::OnImport()
	{
		const QString path = QFileDialog::getOpenFileName(this, tr("导入模型"), QString(),
			tr("OBJ 模型 (*.obj);;所有文件 (*)"));
		if (path.isEmpty())
			return;

		PushUndo();
		QString err;
		const int n = SceneIO::ImportObj(path, m_viewport, &err);
		if (n <= 0)
		{
			m_undo.removeLast();     // 什么都没导进来，别留一步空撤销
			QMessageBox::warning(this, tr("导入"), err.isEmpty() ? tr("没有导入任何对象") : err);
			return;
		}
		RefreshLayers();
		statusBar()->showMessage(tr("已导入 %1 个对象：%2").arg(n).arg(QFileInfo(path).fileName()), 5000);
	}

	void EditorWindow::OnExportJson()
	{
		const QString base = m_docPath.isEmpty()
			? QStringLiteral("scene")
			: QFileInfo(m_docPath).completeBaseName();
		QString path = QFileDialog::getSaveFileName(this, tr("导出场景 JSON"), base + ".json",
			tr("JSON 场景 (*.json);;所有文件 (*)"));
		if (path.isEmpty())
			return;
		if (!path.endsWith(".json", Qt::CaseInsensitive))
			path += ".json";

		QString err;
		if (!SceneIO::ExportJson(path, m_viewport, m_graph2d, &err))
			QMessageBox::warning(this, tr("导出"), err);
		else
			statusBar()->showMessage(tr("已导出场景：%1").arg(path), 5000);
	}

	void EditorWindow::OnExportObj()
	{
		const QString base = m_docPath.isEmpty()
			? QStringLiteral("scene")
			: QFileInfo(m_docPath).completeBaseName();
		QString path = QFileDialog::getSaveFileName(this, tr("导出模型 OBJ"), base + ".obj",
			tr("OBJ 模型 (*.obj);;所有文件 (*)"));
		if (path.isEmpty())
			return;
		if (!path.endsWith(".obj", Qt::CaseInsensitive))
			path += ".obj";

		// 有选中就只导出选中，没有就整场景
		const bool onlySel = m_viewport->SelectedCount() > 0;
		QString err;
		if (!SceneIO::ExportObj(path, m_viewport, onlySel, &err))
			QMessageBox::warning(this, tr("导出"), err);
		else
			statusBar()->showMessage(onlySel ? tr("已导出选中对象：%1").arg(path)
			                                 : tr("已导出全部对象：%1").arg(path), 5000);
	}

	void EditorWindow::OnExportPng()
	{
		const QString base = m_docPath.isEmpty()
			? QStringLiteral("viewport")
			: QFileInfo(m_docPath).completeBaseName();
		QString path = QFileDialog::getSaveFileName(this, tr("导出视口截图"), base + ".png",
			tr("PNG 图片 (*.png);;所有文件 (*)"));
		if (path.isEmpty())
			return;
		if (!path.endsWith(".png", Qt::CaseInsensitive))
			path += ".png";

		QString err;
		if (!SceneIO::ExportPng(path, m_viewport, &err))
			QMessageBox::warning(this, tr("导出"), err);
		else
			statusBar()->showMessage(tr("已保存截图：%1").arg(path), 5000);
	}

	// ================= 右上角视图方块（ViewCube）外观 =================
	void EditorWindow::LoadViewCubeSettings()
	{
		QSettings s(QStringLiteral("GameEngine"), QStringLiteral("Editor"));
		ApplyViewCube(s.value(QStringLiteral("viewcube/size"), 0).toInt(),
			s.value(QStringLiteral("viewcube/alpha"), 100).toInt());
	}

	// 唯一的设置入口：视口 + 工具条 + 设置窗口 + 配置 一起同步
	void EditorWindow::ApplyViewCube(int sizePx, int alphaPct)
	{
		m_vcSize = std::max(0, sizePx);
		m_vcAlpha = std::clamp(alphaPct, 15, 100);
		m_viewport->SetViewCubeSize(float(m_vcSize));          // 0 = 跟随视口自动
		m_viewport->SetViewCubeOpacity(float(m_vcAlpha) / 100.f);

		if (m_vcSizeSlider)
		{
			const bool autoSize = (m_vcSize == 0);
			QSignalBlocker b1(m_vcSizeSlider);
			QSignalBlocker b2(m_vcAlphaSlider);
			QSignalBlocker b3(m_vcAuto);
			m_vcSizeSlider->setEnabled(!autoSize);
			if (!autoSize)
				m_vcSizeSlider->setValue(m_vcSize);
			m_vcAlphaSlider->setValue(m_vcAlpha);
			m_vcAuto->setChecked(autoSize);
			m_vcSizeVal->setText(autoSize ? tr("自动") : QStringLiteral("%1").arg(m_vcSize));
			m_vcAlphaVal->setText(QStringLiteral("%1%").arg(m_vcAlpha));
		}
		if (m_vcDialog)
			m_vcDialog->SetParams(m_vcSize, m_vcAlpha);

		QSettings s(QStringLiteral("GameEngine"), QStringLiteral("Editor"));
		s.setValue(QStringLiteral("viewcube/size"), m_vcSize);
		s.setValue(QStringLiteral("viewcube/alpha"), m_vcAlpha);
	}

	void EditorWindow::OpenViewCubeDialog()
	{
		if (!m_vcDialog)
		{
			m_vcDialog = new ViewCubeDialog(this);
			connect(m_vcDialog, &ViewCubeDialog::ParamsChanged, this, &EditorWindow::ApplyViewCube);
			connect(m_vcDialog, &ViewCubeDialog::ResetRequested, this, [this] { ApplyViewCube(0, 100); });
		}
		m_vcDialog->SetParams(m_vcSize, m_vcAlpha);
		m_vcDialog->show();
		m_vcDialog->raise();
		m_vcDialog->activateWindow();
	}
}
