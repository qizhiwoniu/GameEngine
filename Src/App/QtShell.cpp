#include "QtShell.h"
#include "MainWindow.h"     // MenuId 命令号，工具条按钮直接复用

#include <QAbstractNativeEventFilter>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSize>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace GameEngine
{
	namespace
	{
		// 工具条高度（逻辑像素，Qt 自己按 DPI 放大）
		constexpr int kToolBarHeight = 34;

		enum class IconKind { TopMost, Reload, Tray, About };

		const IconKind kIconOrder[] = {
			IconKind::TopMost, IconKind::Reload, IconKind::Tray, IconKind::About
		};

		// 图标自己画：不依赖系统图标主题，暗色 / 亮色能跟着换颜色
		QPixmap MakeIcon(IconKind kind, const QColor& color)
		{
			constexpr int kLogical = 16;
			constexpr int kScale = 2;                 // 2x 图，高分屏不糊
			QPixmap pm(kLogical * kScale, kLogical * kScale);
			pm.setDevicePixelRatio(kScale);
			pm.fill(Qt::transparent);

			QPainter p(&pm);
			p.setRenderHint(QPainter::Antialiasing);
			p.scale(kScale, kScale);

			QPen pen(color, 1.4);
			pen.setCapStyle(Qt::RoundCap);
			pen.setJoinStyle(Qt::RoundJoin);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);

			switch (kind)
			{
			case IconKind::TopMost:      // 向上箭头 + 底横线
				p.drawLine(QPointF(8, 12.5), QPointF(8, 3.5));
				p.drawLine(QPointF(8, 3.5), QPointF(4.5, 7));
				p.drawLine(QPointF(8, 3.5), QPointF(11.5, 7));
				p.drawLine(QPointF(3.5, 13.5), QPointF(12.5, 13.5));
				break;

			case IconKind::Reload:       // 圆弧 + 箭头
			{
				p.drawArc(QRectF(2.5, 2.5, 11, 11), 40 * 16, 280 * 16);
				p.drawLine(QPointF(10.6, 1.6), QPointF(12.4, 4.6));
				p.drawLine(QPointF(12.4, 4.6), QPointF(9.0, 5.3));
				break;
			}
			case IconKind::Tray:         // 向下箭头 + 托盘
				p.drawLine(QPointF(8, 2.5), QPointF(8, 10.5));
				p.drawLine(QPointF(8, 10.5), QPointF(4.8, 7.3));
				p.drawLine(QPointF(8, 10.5), QPointF(11.2, 7.3));
				p.drawLine(QPointF(3.0, 13.5), QPointF(13.0, 13.5));
				break;

			case IconKind::About:        // 圆形 + i
				p.drawEllipse(QRectF(2.5, 2.5, 11, 11));
				p.drawLine(QPointF(8, 7.2), QPointF(8, 11.2));
				p.drawPoint(QPointF(8, 4.9));
				break;
			}
			return pm;
		}

		// Qt 的事件循环不会调 TranslateAccelerator，这里补上（Ctrl+H / Ctrl+T 才继续有效）
		class AccelFilter : public QAbstractNativeEventFilter
		{
		public:
			AccelFilter(HWND hwnd, HACCEL accel) : m_hwnd(hwnd), m_accel(accel) {}

			bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
			{
				MSG* msg = static_cast<MSG*>(message);
				if (!msg || !m_accel || msg->hwnd != m_hwnd)
					return false;
				return TranslateAcceleratorW(m_hwnd, m_accel, msg) != 0;
			}

		private:
			HWND   m_hwnd = nullptr;
			HACCEL m_accel = nullptr;
		};
	}

	struct QtShell::Impl
	{
		HWND         owner = nullptr;
		HWND         hwndHost = nullptr;
		QWidget*     host = nullptr;
		QToolBar*    bar = nullptr;
		AccelFilter* accelFilter = nullptr;
		ThemeMode    theme = ThemeMode::Dark;

		void BuildActions();
		void ApplyStyle();
	};

	QtShell& QtShell::Instance()
	{
		static QtShell instance;
		return instance;
	}

	QtShell::~QtShell()
	{
		Shutdown();
	}

	bool QtShell::IsReady() const
	{
		return m_impl && m_impl->host && m_impl->hwndHost;
	}

	bool QtShell::Initialize(HWND owner, HACCEL accel, ThemeMode theme)
	{
		if (!owner || !QApplication::instance())
			return false;
		if (IsReady())
		{
			m_impl->owner = owner;
			return true;
		}

		m_impl = new Impl();
		m_impl->owner = owner;
		m_impl->theme = theme;

		// 1) 先建 Qt 顶层窗口（Qt 里只有顶层窗口有自己的 HWND），
		//    再把它改成主窗口的子窗口 —— 这是 Qt 控件嵌进 Win32 窗口的标准做法
		m_impl->host = new QWidget(nullptr,
			Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
		m_impl->host->setObjectName("qtToolBarHost");
		m_impl->host->setAttribute(Qt::WA_NativeWindow);

		HWND hwndHost = reinterpret_cast<HWND>(m_impl->host->winId());
		if (!hwndHost)
		{
			delete m_impl->host;
			delete m_impl;
			m_impl = nullptr;
			return false;
		}
		m_impl->hwndHost = hwndHost;

		LONG_PTR style = GetWindowLongPtrW(hwndHost, GWL_STYLE);
		style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
		style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
		SetWindowLongPtrW(hwndHost, GWL_STYLE, style);
		SetParent(hwndHost, owner);
		SetWindowPos(hwndHost, nullptr, 0, 0, 0, 0,
			SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

		// 2) 工具条 + 布局
		m_impl->bar = new QToolBar(m_impl->host);
		m_impl->bar->setObjectName("qtToolBar");
		m_impl->bar->setMovable(false);
		m_impl->bar->setFloatable(false);
		m_impl->bar->setIconSize(QSize(16, 16));
		m_impl->bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
		m_impl->bar->setContextMenuPolicy(Qt::PreventContextMenu);

		auto* layout = new QVBoxLayout(m_impl->host);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);
		layout->addWidget(m_impl->bar);

		m_impl->BuildActions();
		m_impl->ApplyStyle();
		m_impl->host->show();

		// 3) 快捷键接回 Win32
		m_impl->accelFilter = new AccelFilter(owner, accel);
		QCoreApplication::instance()->installNativeEventFilter(m_impl->accelFilter);
		return true;
	}

	void QtShell::Impl::BuildActions()
	{
		struct ActionDef
		{
			IconKind       kind;
			const wchar_t* text;
			WORD           cmd;
			bool           separatorBefore;
		};
		const ActionDef actions[] = {
			{ IconKind::TopMost, L"窗口置顶",        IDM_VIEW_TOPMOST,   false },
			{ IconKind::Reload,  L"重置窗口大小",    IDM_VIEW_RESET,     false },
			{ IconKind::Tray,    L"隐藏到托盘",      IDM_FILE_HIDE_TRAY, true  },
			{ IconKind::About,   L"关于 GameEngine", IDM_HELP_ABOUT,     true  },
		};

		const HWND owner = this->owner;
		const QColor iconColor = (theme == ThemeMode::Dark)
			? QColor(0xF0, 0xF0, 0xF2) : QColor(0x1A, 0x1A, 0x1E);

		for (const ActionDef& def : actions)
		{
			if (def.separatorBefore)
				bar->addSeparator();

			QAction* act = bar->addAction(QString::fromWCharArray(def.text));
			act->setToolTip(QString::fromWCharArray(def.text));
			act->setIcon(MakeIcon(def.kind, iconColor));

			// 按钮 -> 直接发 WM_COMMAND，复用主窗口已有的命令分发，不必另写一套
			const WORD cmd = def.cmd;
			QObject::connect(act, &QAction::triggered, [owner, cmd]()
				{
					PostMessageW(owner, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
				});
		}
	}

	void QtShell::Impl::ApplyStyle()
	{
		const bool dark = (theme == ThemeMode::Dark);
		const QString bg = dark ? "#202024" : "#f3f3f5";
		const QString border = dark ? "#3a3a40" : "#d6d6dc";
		const QString hover = dark ? "#38383e" : "#e4e4ea";
		const QString pressed = dark ? "#2c2c32" : "#d4d4dc";

		host->setStyleSheet(QString(
			"#qtToolBarHost { background: %1; }"
			"QToolBar { background: %1; border: none; border-bottom: 1px solid %2;"
			"           spacing: 2px; padding: 0px 6px; }"
			"QToolBar::separator { background: %2; width: 1px; margin: 6px 4px; }"
			"QToolButton { background: transparent; border: 1px solid transparent;"
			"              border-radius: 4px; padding: 4px; }"
			"QToolButton:hover { background: %3; border-color: %2; }"
			"QToolButton:pressed { background: %4; }")
			.arg(bg, border, hover, pressed));

		// 图标颜色跟着主题走（顺序和 BuildActions 里一致，分隔线跳过）
		const QColor iconColor = dark ? QColor(0xF0, 0xF0, 0xF2) : QColor(0x1A, 0x1A, 0x1E);
		int i = 0;
		for (QAction* act : bar->actions())
		{
			if (act->isSeparator())
				continue;
			if (i >= static_cast<int>(sizeof(kIconOrder) / sizeof(kIconOrder[0])))
				break;
			act->setIcon(MakeIcon(kIconOrder[i], iconColor));
			++i;
		}
	}

	void QtShell::ApplyTheme(ThemeMode theme)
	{
		if (!m_impl)
			return;
		m_impl->theme = theme;
		m_impl->ApplyStyle();
	}

	void QtShell::SetContentRect(const RECT& rcContent)
	{
		if (!IsReady())
			return;

		const int width = rcContent.right - rcContent.left;
		if (width <= 0)
			return;

		qreal dpr = m_impl->host->devicePixelRatioF();
		if (dpr <= 0.0)
			dpr = 1.0;

		const int barH = qRound(kToolBarHeight * dpr);
		MoveWindow(m_impl->hwndHost, rcContent.left, rcContent.top, width, barH, TRUE);
	}

	int QtShell::Exec()
	{
		return QApplication::exec();
	}

	void QtShell::Shutdown()
	{
		if (!m_impl)
			return;

		if (m_impl->accelFilter)
		{
			if (QCoreApplication::instance())
				QCoreApplication::instance()->removeNativeEventFilter(m_impl->accelFilter);
			delete m_impl->accelFilter;
			m_impl->accelFilter = nullptr;
		}
		if (m_impl->host)
		{
			delete m_impl->host;    // 连带 toolbar / layout
			m_impl->host = nullptr;
		}
		m_impl->hwndHost = nullptr;
		delete m_impl;
		m_impl = nullptr;
	}
}
