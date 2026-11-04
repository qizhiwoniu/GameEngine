#include "EditorWindow.h"
#include "AppIcons.h"
#include "Document.h"
#include "Viewport3D.h"
#include "SceneGraph2D.h"
#include "SceneLink.h"
#include "TimelineWidget.h"
#include "SettingsDialog.h"
#include "Hotkeys.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QJsonObject>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
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

	EditorWindow::~EditorWindow() = default;

	EditorWindow::EditorWindow()
	{
		setWindowTitle(tr("3DGameEngine - 未命名"));
		resize(1104, 623);

		ApplyDarkStyle();

		LoadHotkeys();
		m_viewport = new Viewport3D(this);
		m_graph2d = new SceneGraph2D(this);
		m_centralStack = new QStackedWidget(this);
		m_centralStack->addWidget(m_viewport);      // index 0 = 3D 场景
		m_centralStack->addWidget(m_graph2d);       // index 1 = 2D 节点图
		setCentralWidget(m_centralStack);

		BuildMenus();
		BuildToolBar();
		BuildDocks();
		RestoreLayout();      // 上次"保存布局"的结果（没有则保持默认）

		connect(m_viewport, &Viewport3D::SelectionChanged,
			this, &EditorWindow::OnViewportSelection);

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
		mFile->addAction(tr("保存(&S)"), QKeySequence::Save, this, &EditorWindow::OnFileSave);
		mFile->addAction(tr("另存为..."), this, &EditorWindow::OnFileSaveAs);
		mFile->addSeparator();
		mFile->addAction(tr("导入"));
		mFile->addAction(tr("导出"));
		mFile->addSeparator();
		mFile->addAction(tr("首选项"), this, &EditorWindow::OnOpenSettings);
		mFile->addSeparator();
		mFile->addAction(tr("退出(&E)"), QKeySequence::Quit, qApp, &QApplication::quit);

		// ---- 编辑(E) ----
		QMenu* mEdit = mb->addMenu(tr("编辑(&E)"));
		mEdit->addAction(tr("Undo"), QKeySequence::Undo);
		mEdit->addAction(tr("Redo"), QKeySequence::Redo);
		mEdit->addSeparator();
		mEdit->addAction(tr("剪切"), QKeySequence::Cut);
		mEdit->addAction(tr("复制"), QKeySequence::Copy);
		mEdit->addAction(tr("粘贴"), QKeySequence::Paste);
		mEdit->addSeparator();
		// 按当前页面分发：2D 节点图页删节点，3D 页删对象。
		// 之前固定删 3D 选中——在 2D 页按 Delete 时删的是 3D 的选中对象，
		// 纯逻辑节点（时间/运行输出）永远删不掉，还可能删错对象。
		mEdit->addAction(tr("删除"), QKeySequence::Delete, this, [this]
			{
				if (m_centralStack->currentWidget() == static_cast<QWidget*>(m_graph2d))
					m_graph2d->DeleteSelected();
				else
					m_viewport->DeleteSelected();
				RefreshLayers();
			});

		// ---- 创建(C) ----
		QMenu* mCreate = mb->addMenu(tr("创建(&C)"));
		mCreate->addAction(tr("2D场景"));
		mCreate->addAction(tr("3D场景"));
		mCreate->addAction(tr("方块"), this, [this] { m_viewport->AddBox(); RefreshLayers(); });
		mCreate->addAction(tr("平面"), this, [this] { m_viewport->AddPlane(); RefreshLayers(); });
		mCreate->addAction(tr("动画"));

		// ---- 视图(V) ----
		QMenu* mView = mb->addMenu(tr("视图(&V)"));
		mView->addAction(tr("保存布局"), this, &EditorWindow::SaveLayout);
		m_actLock = mView->addAction(tr("锁定布局"));
		m_actLock->setCheckable(true);
		connect(m_actLock, &QAction::toggled, this, [this](bool on)
			{
				const auto f = on ? QDockWidget::NoDockWidgetFeatures : QDockWidget::DockWidgetMovable;
				for (QDockWidget* d : m_docks)
					d->setFeatures(f);
				for (QToolBar* t : m_bars)
					t->setMovable(!on);
			});
		mView->addSeparator();
		// 工具栏 / 窗口的显示隐藏入口（动作在 BuildDocks 末尾统一填充）
		m_menuBars = mView->addMenu(tr("工具栏"));
		m_menuWindows = mView->addMenu(tr("窗口"));

		// ---- Player(P) ----
		QMenu* mPlayer = mb->addMenu(tr("Player(&P)"));
		mPlayer->addAction(tr("Run Project"));
		m_actPlay = mPlayer->addAction(tr("Run Scene"), QKeySequence(Qt::Key_Space));
		m_actPlay->setCheckable(true);
		connect(m_actPlay, &QAction::toggled, this, &EditorWindow::OnPlay);
		mPlayer->addSeparator();
		mPlayer->addAction(tr("Restart"));
		mPlayer->addAction(tr("Refresh"));
		mPlayer->addAction(tr("Shutdown"), qApp, &QApplication::quit);

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
		const QString name = m_viewport ? m_viewport->SelectedName() : QString();
		m_propTable->setItem(0, 0, new QTableWidgetItem(tr("选择")));
		m_propTable->setItem(0, 1, new QTableWidgetItem(
			name.isEmpty() ? tr("未选择物体") : name));
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
		if (m_actPlay && m_actPlay->isChecked() != play)
		{
			QSignalBlocker b(m_actPlay);
			m_actPlay->setChecked(play);
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
}
