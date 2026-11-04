#include "MainWindow.h"
#include "../../resource.h"
#include "Theme.h"
#include "PopupMenu.h"
#include "QtShell.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cwctype>
#include <iterator>
#include <memory>
#include <string>
#include "NotifyIcon/TrayIcon.h"

// 老版本 SDK 里没有这些 DWMWA_* 的宏定义，这里补一份
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace GameEngine
{
	namespace
	{
		constexpr int kDefaultClientW = 1280;
		constexpr int kDefaultClientH = 720;
		constexpr int kMinWinW = 820;
		constexpr int kMinWinH = 560;

		// 菜单数据：菜单栏自绘，弹出菜单由 PopupMenu 自绘（不用原生 HMENU，避免系统菜单边框白边）
		//
		// 菜单文字和助记符（括号里那个字母）分开存，绘制时拼成 "文件(F)"。
		// 以后换 Qt6 可以直接映射成 QAction 的 "文件(&F)"，菜单数据不用改。
		using MenuEntry = PopupMenuItem;

		const MenuEntry kFileMenu[] = {
			{ L"隐藏到托盘", L"Ctrl+H", IDM_FILE_HIDE_TRAY, false },
			{ nullptr,       nullptr,  0,                  true  },
			{ L"退出",       L"Alt+F4", IDM_FILE_EXIT,      false },
		};

		// 编辑器还没接上，这些命令先置灰（和 Visual Studio 里没有可撤销内容时一致）
		const MenuEntry kEditMenu[] = {
			{ L"撤销", L"Ctrl+Z", IDM_EDIT_UNDO,  false, true },
			{ L"重做", L"Ctrl+Y", IDM_EDIT_REDO,  false, true },
			{ nullptr, nullptr,   0,              true       },
			{ L"剪切", L"Ctrl+X", IDM_EDIT_CUT,   false, true },
			{ L"复制", L"Ctrl+C", IDM_EDIT_COPY,  false, true },
			{ L"粘贴", L"Ctrl+V", IDM_EDIT_PASTE, false, true },
		};

		const MenuEntry kViewMenu[] = {
			{ L"窗口置顶",     L"Ctrl+T", IDM_VIEW_TOPMOST, false },
			{ nullptr,         nullptr,   0,                true  },
			{ L"重置窗口大小", L"",       IDM_VIEW_RESET,   false },
		};

		const MenuEntry kWindowMenu[] = {
			{ L"最小化",      L"",       IDM_WINDOW_MIN,   false },
			{ L"最大化/还原", L"",       IDM_WINDOW_MAX,   false },
			{ nullptr,        nullptr,   0,                true  },
			{ L"关闭窗口",    L"Alt+F4", IDM_WINDOW_CLOSE, false },
		};

		const MenuEntry kHelpMenu[] = {
			{ L"检查更新",        L"", IDM_HELP_UPDATE, false },
			{ nullptr,            nullptr, 0,           true  },
			{ L"关于 GameEngine", L"", IDM_HELP_ABOUT,  false },
		};

		struct MenuBarEntry
		{
			const wchar_t*   text;                 // 不含括号助记符，例如 L"文件"
			wchar_t          mnemonic;             // 助记符字母；Qt6 里写作 "文件(&F)"
			const MenuEntry* items;
			int              count;
			bool             rightAligned = false; // true: 靠右贴边（对齐标题栏关闭按钮下方）
		};

		const MenuBarEntry kMenuBar[] = {
			{ L"文件", L'F', kFileMenu,   static_cast<int>(std::size(kFileMenu))   },
			{ L"编辑", L'E', kEditMenu,   static_cast<int>(std::size(kEditMenu))   },
			{ L"视图", L'V', kViewMenu,   static_cast<int>(std::size(kViewMenu))   },
			{ L"窗口", L'W', kWindowMenu, static_cast<int>(std::size(kWindowMenu)) },
			{ L"帮助", L'H', kHelpMenu,   static_cast<int>(std::size(kHelpMenu)), true },
		};
		constexpr int kMenuBarCount = static_cast<int>(std::size(kMenuBar));
		static_assert(kMenuBarCount <= kMaxMenuBarItems, "菜单栏项数超过数组容量");

		// 显示文字：文字 + "(X)"；Qt6 换成 "文字(&X)" 即可
		std::wstring MenuTitle(const MenuBarEntry& e)
		{
			std::wstring s = e.text;
			s += L"(";
			s += e.mnemonic;
			s += L")";
			return s;
		}

		void FillSolidRect(HDC dc, const RECT& rc, COLORREF color)
		{
			HBRUSH brush = CreateSolidBrush(color);
			::FillRect(dc, &rc, brush);
			DeleteObject(brush);
		}

		int TextWidth(HDC dc, HFONT font, const wchar_t* text)
		{
			if (!dc || !font || !text || !*text)
				return 0;
			HGDIOBJ oldFont = SelectObject(dc, font);
			SIZE sz = {};
			GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &sz);
			SelectObject(dc, oldFont);
			return sz.cx;
		}

		// 取窗口所在显示器的 DPI；窗口还没创建时退回系统 DPI
		UINT DpiOf(HWND hwnd)
		{
			const UINT dpi = hwnd ? ::GetDpiForWindow(hwnd) : ::GetDpiForSystem();
			return dpi ? dpi : 96;
		}

		void CenterOnWorkArea(int winW, int winH, int& x, int& y)
		{
			RECT wa = {};
			SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
			x = wa.left + ((wa.right - wa.left) - winW) / 2;
			y = wa.top + ((wa.bottom - wa.top) - winH) / 2;
		}
	}

	MainWindow::MainWindow()
		: m_trayIcon(std::make_unique<TrayIcon>(GetModuleHandle(nullptr), L"游戏引擎"))
	{
		// 弹出菜单是非模态的：选中项 -> 执行命令；关闭 -> 清状态
		m_popup.SetCallbacks(
			[this](int id) { OnCommand(static_cast<WORD>(id)); },
			[this]()
			{
				m_popupOpen = false;
				m_menuIndex = -1;
				m_hotMenu = -1;
				m_active = (GetForegroundWindow() == m_hwnd);
				if (m_hwnd)
					InvalidateRect(m_hwnd, nullptr, FALSE);
			});
	}

	MainWindow::~MainWindow()
	{
		if (m_fontUi)
		{
			DeleteObject(m_fontUi);
			m_fontUi = nullptr;
		}
		if (m_fontTitle)
		{
			DeleteObject(m_fontTitle);
			m_fontTitle = nullptr;
		}
		if (m_hIconBig)
		{
			DestroyIcon(m_hIconBig);
			m_hIconBig = nullptr;
		}
		if (m_hIconSmall)
		{
			DestroyIcon(m_hIconSmall);
			m_hIconSmall = nullptr;
		}
		if (m_hAccel)
		{
			DestroyAcceleratorTable(m_hAccel);
			m_hAccel = nullptr;
		}
	}

	bool MainWindow::Initialize(const wchar_t* title, int width, int height)
	{
		// 必须在建窗口之前设置，否则原生弹出菜单拿不到正确配色
		m_theme = CurrentTheme();
		ApplyNativeMenuTheme();

		if (!InitWindow(title, width, height))
			return false;

		// Qt 工具条：挂在内容区顶部（需要 QApplication 已存在，见 Main.cpp）
		QtShell::Instance().Initialize(m_hwnd, m_hAccel, m_theme);
		QtShell::Instance().SetContentRect(m_rcContent);

		if (m_trayIcon)
		{
			m_trayIcon->Create(m_hwnd);
			m_trayIcon->ShowBalloon(L"GameEngine starting", L"starting...");
			m_trayIcon->SetWindowGUI(nullptr);
		}

		return true;
	}

	bool MainWindow::InitWindow(const wchar_t* title, int width, int height)
	{
		// 1. 注册窗口类（背景由 WM_PAINT 全权绘制，不用类画刷避免闪烁）
		static const wchar_t CLASS_NAME[] = L"GameEngineWindowClass";
		static bool classRegistered = false;
		HINSTANCE hInst = GetModuleHandle(nullptr);
		if (!classRegistered)
		{
			WNDCLASSEX wc = {};
			wc.cbSize = sizeof(WNDCLASSEX);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = WndProc;
			wc.cbClsExtra = 0;
			wc.cbWndExtra = 0;
			wc.hInstance = hInst;
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hbrBackground = nullptr;
			wc.hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
				GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
			wc.hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
				GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
			wc.lpszClassName = CLASS_NAME;
			if (!RegisterClassEx(&wc))
				return false;
			classRegistered = true;
		}

		// 2. 自绘框架：WS_THICKFRAME 保留缩放边框 / Aero Snap / 窗口阴影，
		//    但 WM_NCCALCSIZE 会把整个窗口变成客户区，标题栏和按钮全部自己画
		const DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
			| WS_SYSMENU | WS_CLIPCHILDREN;
		const DWORD exStyle = WS_EX_APPWINDOW;

		int x = 0, y = 0;
		CenterOnWorkArea(width, height, x, y);

		m_title = title ? title : L"";
		m_hwnd = CreateWindowEx(
			exStyle,
			CLASS_NAME, m_title.c_str(), style,
			x, y, width, height,
			nullptr,
			nullptr,     // 无原生菜单栏：菜单由自绘菜单栏 + 弹出菜单实现
			hInst, this
		);
		if (!m_hwnd)
			return false;

		// 3. 绑定 this 指针到窗口用户数据
		SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

		// 4. DPI / 字体 / 布局 / 主题 / 图标
		m_dpi = static_cast<int>(DpiOf(m_hwnd));
		EnsureFonts();
		RecalcLayout();
		ApplyTheme();
		ApplyIcons();

		// 5. 快捷键表（Ctrl+H 隐藏到托盘）
		ACCEL accels[] = {
			{ FCONTROL | FVIRTKEY, 'H', IDM_FILE_HIDE_TRAY },
			{ FCONTROL | FVIRTKEY, 'T', IDM_VIEW_TOPMOST },
		};
		m_hAccel = CreateAcceleratorTableW(accels, 2);

		// 6. 显示窗口
		ShowWindow(m_hwnd, SW_SHOW);
		UpdateWindow(m_hwnd);
		return true;
	}

	void MainWindow::EnsureFonts()
	{
		if (m_fontUi)
		{
			DeleteObject(m_fontUi);
			m_fontUi = nullptr;
		}
		if (m_fontTitle)
		{
			DeleteObject(m_fontTitle);
			m_fontTitle = nullptr;
		}

		LOGFONTW lf = {};
		lf.lfHeight = -MulDiv(9, m_dpi, 72);
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfQuality = CLEARTYPE_QUALITY;
		lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
		wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
		m_fontUi = CreateFontIndirectW(&lf);

		lf.lfHeight = -MulDiv(9, m_dpi, 72);
		lf.lfWeight = FW_SEMIBOLD;
		m_fontTitle = CreateFontIndirectW(&lf);
	}

	void MainWindow::RecalcLayout()
	{
		RECT rc = {};
		GetClientRect(m_hwnd, &rc);

		// 高度对齐 Visual Studio：标题栏 30dp、菜单栏 24dp（原来 36/30 明显偏高）
		m_titleH = Scale(30);
		m_menuH = Scale(24);
		m_border = Scale(6);

		// 标题栏按钮从右往左排：关闭 / 最大化 / 最小化 / 主题（主题只显示图标，宽度与最小化一致）
		const int btnW = Scale(46);
		m_rcClose = { rc.right - btnW, 0, rc.right, m_titleH };
		m_rcMax = { rc.right - 2 * btnW, 0, rc.right - btnW, m_titleH };
		m_rcMin = { rc.right - 3 * btnW, 0, rc.right - 2 * btnW, m_titleH };
		m_rcTheme = { rc.right - 4 * btnW, 0, rc.right - 3 * btnW, m_titleH };

		HDC dc = GetDC(m_hwnd);

		// 菜单栏：文件/编辑/视图/窗口 靠左，帮助 靠右（落在关闭按钮下方）
		m_rcMenuBar = { 0, m_titleH, rc.right, m_titleH + m_menuH };
		m_menuCount = kMenuBarCount;
		int x = Scale(2);
		for (int i = 0; i < kMenuBarCount; ++i)
		{
			// 显示文字带上助记符："文件" -> "文件(F)"
			m_menuTitles[i] = MenuTitle(kMenuBar[i]);

			// 左右内边距各 7dp，宽度紧贴文字（Visual Studio 的菜单栏就是这么紧凑）
			const int tw = dc ? TextWidth(dc, m_fontUi, m_menuTitles[i].c_str()) : Scale(36);
			const int w = tw + Scale(14);
			if (kMenuBar[i].rightAligned)
			{
				const int right = rc.right - Scale(4);
				m_rcMenuItems[i] = { right - w, m_titleH, right, m_titleH + m_menuH };
			}
			else
			{
				m_rcMenuItems[i] = { x, m_titleH, x + w, m_titleH + m_menuH };
				x += w;
			}
		}

		// 弹出菜单宽度取所有菜单项的最大值，保证各菜单宽度一致
		int maxItemW = Scale(160);
		for (const MenuBarEntry& menu : kMenuBar)
		{
			for (int i = 0; i < menu.count; ++i)
			{
				const MenuEntry& e = menu.items[i];
				if (e.separator)
					continue;
				const int w =
					Scale(28) +
					TextWidth(dc, m_fontUi, e.text) +
					Scale(16) +
					TextWidth(dc, m_fontUi, e.shortcut) +
					Scale(14);
				if (w > maxItemW)
					maxItemW = w;
			}
		}
		if (dc)
			ReleaseDC(m_hwnd, dc);

		m_menuItemW = maxItemW;
		m_menuItemH = Scale(28);

		m_rcContent = { 0, m_titleH + m_menuH, rc.right, rc.bottom };

		// 窗口尺寸 / DPI 变了，Qt 工具条跟着重新贴到内容区顶部
		QtShell::Instance().SetContentRect(m_rcContent);
	}

	void MainWindow::ApplyTheme()
	{
		if (!m_hwnd)
			return;

		// 原生弹出菜单（含托盘右键菜单）跟随主题
		ApplyNativeMenuTheme();

		// DWM：暗色框架 + 边框色 + Win11 圆角
		BOOL dark = (m_theme == ThemeMode::Dark) ? TRUE : FALSE;
		if (FAILED(DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))))
			DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));

		const int corner = IsZoomed(m_hwnd) ? 1 /*DWMWCP_DONOTROUND*/ : 2 /*DWMWCP_ROUND*/;
		DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

		COLORREF borderColor = Palette(m_theme).border;
		DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

		// Qt 工具条也跟着换色
		QtShell::Instance().ApplyTheme(m_theme);

		InvalidateRect(m_hwnd, nullptr, FALSE);
	}

	void MainWindow::ApplyIcons()
	{
		if (!m_hwnd)
			return;

		HINSTANCE hInst = GetModuleHandle(nullptr);
		if (m_hIconBig)
			DestroyIcon(m_hIconBig);
		if (m_hIconSmall)
			DestroyIcon(m_hIconSmall);

		m_hIconBig = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
			GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
		m_hIconSmall = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
			GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

		SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_hIconBig));
		SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_hIconSmall));
	}

	void MainWindow::OnPaint()
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(m_hwnd, &ps);

		RECT rc = {};
		GetClientRect(m_hwnd, &rc);
		if (rc.right <= 0 || rc.bottom <= 0)
		{
			EndPaint(m_hwnd, &ps);
			return;
		}

		// 双缓冲，避免标题栏 / 菜单栏重绘闪烁
		HDC mem = CreateCompatibleDC(hdc);
		HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
		HGDIOBJ oldBmp = SelectObject(mem, bmp);

		const ThemePalette& p = Palette(m_theme);
		FillSolidRect(mem, rc, p.windowBg);

		RECT rcTitle = { 0, 0, rc.right, m_titleH };
		DrawTitleBar(mem, rcTitle);
		DrawMenuBar(mem, m_rcMenuBar);
		DrawFrameBorder(mem, rc);

		BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

		SelectObject(mem, oldBmp);
		DeleteObject(bmp);
		DeleteDC(mem);
		EndPaint(m_hwnd, &ps);
	}

	void MainWindow::DrawFrameBorder(HDC dc, const RECT& rc)
	{
		const COLORREF color = Palette(m_theme).border;

		RECT line = { rc.left, rc.top, rc.right, rc.top + 1 };
		FillSolidRect(dc, line, color);
		line = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
		FillSolidRect(dc, line, color);
		line = { rc.left, rc.top, rc.left + 1, rc.bottom };
		FillSolidRect(dc, line, color);
		line = { rc.right - 1, rc.top, rc.right, rc.bottom };
		FillSolidRect(dc, line, color);
	}

	void MainWindow::DrawTitleBar(HDC dc, const RECT& rcTitle)
	{
		const ThemePalette& p = Palette(m_theme);
		FillSolidRect(dc, rcTitle, p.titleBarBg);

		// 应用图标
		const int iconSz = Scale(16);
		const int iconX = Scale(11);
		const int iconY = rcTitle.top + (m_titleH - iconSz) / 2;
		if (m_hIconSmall)
			DrawIconEx(dc, iconX, iconY, m_hIconSmall, iconSz, iconSz, 0, nullptr, DI_NORMAL);

		// 标题文字
		RECT rcText = { iconX + iconSz + Scale(9), rcTitle.top, m_rcTheme.left - Scale(8), rcTitle.bottom };
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, m_active ? p.titleBarText : p.titleBarInactiveText);
		HGDIOBJ oldFont = SelectObject(dc, m_fontTitle);
		DrawTextW(dc, m_title.c_str(), -1, &rcText,
			DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
		SelectObject(dc, oldFont);

		// 右侧按钮：主题 / 最小化 / 最大化 / 关闭
		DrawCaptionButton(dc, m_rcTheme, CaptionButton::Theme,
			m_hotButton == CaptionButton::Theme, m_pressedButton == CaptionButton::Theme);
		DrawCaptionButton(dc, m_rcMin, CaptionButton::Minimize,
			m_hotButton == CaptionButton::Minimize, m_pressedButton == CaptionButton::Minimize);
		DrawCaptionButton(dc, m_rcMax, CaptionButton::Maximize,
			m_hotButton == CaptionButton::Maximize, m_pressedButton == CaptionButton::Maximize);
		DrawCaptionButton(dc, m_rcClose, CaptionButton::Close,
			m_hotButton == CaptionButton::Close, m_pressedButton == CaptionButton::Close);
	}

	void MainWindow::DrawCaptionButton(HDC dc, const RECT& rc, CaptionButton id, bool hot, bool pressed)
	{
		const ThemePalette& p = Palette(m_theme);
		const bool isClose = (id == CaptionButton::Close);

		COLORREF bg = p.titleBarBg;
		COLORREF fg = m_active ? p.titleBarText : p.titleBarInactiveText;

		if (isClose && (hot || pressed))
		{
			bg = p.closeHover;
			fg = RGB(255, 255, 255);
		}
		else if (pressed)
		{
			bg = p.buttonPressed;
		}
		else if (hot)
		{
			bg = p.buttonHover;
		}

		FillSolidRect(dc, rc, bg);

		const int cx = (rc.left + rc.right) / 2;
		const int cy = (rc.top + rc.bottom) / 2;
		const int s = Scale(5);

		HPEN pen = CreatePen(PS_SOLID, (std::max)(1, Scale(1)), fg);
		HGDIOBJ oldPen = SelectObject(dc, pen);
		HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
		SetBkMode(dc, TRANSPARENT);

		switch (id)
		{
		case CaptionButton::Minimize:
			MoveToEx(dc, cx - s, cy + Scale(1), nullptr);
			LineTo(dc, cx + s + 1, cy + Scale(1));
			break;

		case CaptionButton::Maximize:
			if (IsZoomed(m_hwnd))
			{
				// 已最大化：显示"还原"图标（两个叠放的方框）
				MoveToEx(dc, cx - s + Scale(2), cy - s, nullptr);
				LineTo(dc, cx + s + 1, cy - s);
				LineTo(dc, cx + s + 1, cy + s - Scale(2));
				Rectangle(dc, cx - s, cy - s + Scale(2), cx + s - Scale(2), cy + s + 1);
			}
			else
			{
				Rectangle(dc, cx - s, cy - s, cx + s + 1, cy + s + 1);
			}
			break;

		case CaptionButton::Close:
			MoveToEx(dc, cx - s, cy - s, nullptr);
			LineTo(dc, cx + s + 1, cy + s + 1);
			MoveToEx(dc, cx - s, cy + s + 1, nullptr);
			LineTo(dc, cx + s + 1, cy - s);
			break;

		case CaptionButton::Theme:
		{
			// 只画图标（和最小化等按钮同宽），图标居中
			const int icx = (rc.left + rc.right) / 2;
			if (m_theme == ThemeMode::Dark)
			{
				// 当前暗色 -> 显示太阳（点击切到亮色）
				const int r = Scale(3);
				Ellipse(dc, icx - r, cy - r, icx + r, cy + r);
				static const int dxs[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
				static const int dys[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
				for (int i = 0; i < 8; ++i)
				{
					MoveToEx(dc, icx + dxs[i] * Scale(5), cy + dys[i] * Scale(5), nullptr);
					LineTo(dc, icx + dxs[i] * Scale(7), cy + dys[i] * Scale(7));
				}
			}
			else
			{
				// 当前亮色 -> 显示月亮（点击切到暗色）
				const int r = Scale(5);
				HBRUSH fgBrush = CreateSolidBrush(fg);
				HBRUSH bgBrush = CreateSolidBrush(bg);
				HPEN bgPen = CreatePen(PS_SOLID, (std::max)(1, Scale(1)), bg);

				HGDIOBJ keepBrush = SelectObject(dc, fgBrush);
				HGDIOBJ keepPen = SelectObject(dc, GetStockObject(NULL_PEN));
				Ellipse(dc, icx - r, cy - r, icx + r, cy + r);

				SelectObject(dc, bgBrush);
				SelectObject(dc, bgPen);
				Ellipse(dc, icx - r + Scale(3), cy - r - Scale(2),
					icx + r + Scale(3), cy + r - Scale(2));

				SelectObject(dc, keepPen);
				SelectObject(dc, keepBrush);
				DeleteObject(fgBrush);
				DeleteObject(bgBrush);
				DeleteObject(bgPen);
			}
			break;
		}

		default:
			break;
		}

		SelectObject(dc, oldBrush);
		SelectObject(dc, oldPen);
		DeleteObject(pen);
	}

	void MainWindow::DrawMenuBar(HDC dc, const RECT& rcBar)
	{
		const ThemePalette& p = Palette(m_theme);
		FillSolidRect(dc, rcBar, p.menuBarBg);

		SetBkMode(dc, TRANSPARENT);
		HGDIOBJ oldFont = SelectObject(dc, m_fontUi);

		for (int i = 0; i < m_menuCount; ++i)
		{
			const RECT& rc = m_rcMenuItems[i];
			if (i == m_hotMenu)
				FillSolidRect(dc, rc, p.menuHoverBg);

			const std::wstring& title = m_menuTitles[i];
			SetTextColor(dc, p.menuBarText);
			RECT rcText = rc;
			DrawTextW(dc, title.c_str(), -1, &rcText,
				DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

			// 按住 Alt 时给助记符加下划线（"文件(F)" 里的 F）
			if (m_showMnemonic && title.size() >= 3)
			{
				const std::wstring prefix = title.substr(0, title.size() - 2);
				const std::wstring letter = title.substr(title.size() - 2, 1);

				const int tw = TextWidth(dc, m_fontUi, title.c_str());
				const int pw = TextWidth(dc, m_fontUi, prefix.c_str());
				const int lw = TextWidth(dc, m_fontUi, letter.c_str());

				int x0 = rc.left + ((rc.right - rc.left) - tw) / 2 + pw;

				TEXTMETRICW tm = {};
				GetTextMetricsW(dc, &tm);
				int y = rc.top + ((rc.bottom - rc.top) - tm.tmHeight) / 2 + tm.tmAscent + Scale(2);
				RECT ul = { x0, y, x0 + lw, y + (std::max)(1, Scale(1)) };
				FillSolidRect(dc, ul, p.menuBarText);
			}
		}

		SelectObject(dc, oldFont);

		// 菜单栏底部分隔线
		RECT line = { rcBar.left, rcBar.bottom - 1, rcBar.right, rcBar.bottom };
		FillSolidRect(dc, line, p.border);
	}

	void MainWindow::OpenMenu(int index)
	{
		if (index < 0 || index >= kMenuBarCount || !m_hwnd)
			return;

		const MenuBarEntry& def = kMenuBar[index];

		POINT pt = { m_rcMenuItems[index].left, m_rcMenuItems[index].bottom };
		ClientToScreen(m_hwnd, &pt);

		PopupMenuStyle style;
		style.font = m_fontUi;
		style.dpi = m_dpi;
		style.itemWidth = m_menuItemW;
		style.itemHeight = m_menuItemH;
		style.theme = m_theme;
		style.checkedId = IsTopMost() ? IDM_VIEW_TOPMOST : 0;

		m_popupOpen = true;
		m_menuIndex = index;
		m_hotMenu = index;

		// 已经开着就原地换内容（菜单间切换不重建窗口，不会出现两个菜单叠在一起）
		m_popup.Show(m_hwnd, pt, def.items, def.count, style);

		InvalidateRect(m_hwnd, &m_rcMenuBar, FALSE);
	}

	void MainWindow::CloseMenu()
	{
		if (m_popup.IsOpen())
			m_popup.Close();   // 关闭回调里会清 m_popupOpen / m_menuIndex
	}

	// 菜单间切换：先给菜单栏项画上高亮，再换弹出菜单的内容
	void MainWindow::SwitchMenu(int index)
	{
		if (index < 0 || index >= kMenuBarCount)
			return;
		if (index == m_menuIndex && m_popup.IsOpen())
			return;
		OpenMenu(index);
	}

	// Alt + 字母：展开对应菜单（文件名后面的括号字母）
	bool MainWindow::OpenMenuByMnemonic(wchar_t ch)
	{
		if (!m_hwnd || ch == 0)
			return false;
		const wchar_t upper = static_cast<wchar_t>(towupper(ch));
		for (int i = 0; i < kMenuBarCount; ++i)
		{
			if (static_cast<wchar_t>(towupper(kMenuBar[i].mnemonic)) == upper)
			{
				SetMnemonicVisible(true);
				OpenMenu(i);
				return true;
			}
		}
		return false;
	}

	// 按住 Alt 时菜单字母带下划线，松开就收掉（和资源管理器一致）
	void MainWindow::SetMnemonicVisible(bool visible)
	{
		if (m_showMnemonic == visible)
			return;
		m_showMnemonic = visible;
		if (m_hwnd)
			InvalidateRect(m_hwnd, &m_rcMenuBar, FALSE);
	}

	bool MainWindow::OnCommand(WORD id)
	{
		if (!m_hwnd)
			return false;

		switch (id)
		{
		case IDM_FILE_HIDE_TRAY:
			ShowWindow(m_hwnd, SW_HIDE);
			if (m_trayIcon)
				m_trayIcon->ShowBalloon(L"GameEngine", L"已最小化到托盘，双击托盘图标可恢复窗口。");
			return true;

		case IDM_FILE_EXIT:
			PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
			return true;

		case IDM_VIEW_TOPMOST:
		{
			const bool topmost = !IsTopMost();
			SetWindowPos(m_hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
				0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			return true;
		}

		case IDM_VIEW_RESET:
			ResetWindowSize();
			return true;

		case IDM_WINDOW_MIN:
			ShowWindow(m_hwnd, SW_MINIMIZE);
			return true;

		case IDM_WINDOW_MAX:
			ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
			return true;

		case IDM_WINDOW_CLOSE:
			PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
			return true;

		case IDM_HELP_UPDATE:
			if (m_trayIcon)
				m_trayIcon->CheckUpdate();
			return true;

		case IDM_HELP_ABOUT:
		{
			std::wstring text =
				L"GameEngine v" APP_VERSION L"\n"
				L"基于 Win32 的游戏引擎外壳\n\n"
				L"快捷键：\n"
				L"  Ctrl+H   隐藏到托盘\n"
				L"  Ctrl+T   窗口置顶\n"
				L"  Alt+F4   退出\n\n"
				L"菜单：Alt+F / Alt+E / Alt+V / Alt+W / Alt+H\n";
			MessageBoxW(m_hwnd, text.c_str(), L"关于 GameEngine",
				MB_OK | MB_ICONINFORMATION);
			return true;
		}
		}
		return false;
	}

	void MainWindow::ResetWindowSize()
	{
		if (!m_hwnd)
			return;

		if (IsZoomed(m_hwnd) || IsIconic(m_hwnd))
			ShowWindow(m_hwnd, SW_RESTORE);

		const int winW = Scale(kDefaultClientW);
		const int winH = Scale(kDefaultClientH);
		int x = 0, y = 0;
		CenterOnWorkArea(winW, winH, x, y);

		SetWindowPos(m_hwnd, nullptr, x, y, winW, winH,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}

	bool MainWindow::IsTopMost() const
	{
		if (!m_hwnd)
			return false;
		return (GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	}

	MainWindow::CaptionButton MainWindow::HitTestButton(POINT pt) const
	{
		if (pt.y < 0 || pt.y >= m_titleH)
			return CaptionButton::None;
		if (PtInRect(&m_rcTheme, pt)) return CaptionButton::Theme;
		if (PtInRect(&m_rcMin, pt))   return CaptionButton::Minimize;
		if (PtInRect(&m_rcMax, pt))   return CaptionButton::Maximize;
		if (PtInRect(&m_rcClose, pt)) return CaptionButton::Close;
		return CaptionButton::None;
	}

	int MainWindow::HitTestMenu(POINT pt) const
	{
		if (pt.y < m_titleH || pt.y >= m_titleH + m_menuH)
			return -1;
		for (int i = 0; i < kMenuBarCount; ++i)
		{
			if (PtInRect(&m_rcMenuItems[i], pt))
				return i;
		}
		return -1;
	}

	void MainWindow::UpdateHover(POINT pt)
	{
		const CaptionButton hotButton = HitTestButton(pt);
		int hotMenu = -1;
		if (hotButton == CaptionButton::None && m_pressedButton == CaptionButton::None)
			hotMenu = HitTestMenu(pt);

		// 菜单已经展开时，光标滑到别的菜单栏项上就直接切过去（和原生菜单一致）
		if (m_popup.IsOpen() && hotMenu >= 0 && hotMenu != m_menuIndex)
		{
			SwitchMenu(hotMenu);
			return;
		}

		if (hotButton != m_hotButton || hotMenu != m_hotMenu)
		{
			m_hotButton = hotButton;
			m_hotMenu = hotMenu;
			InvalidateRect(m_hwnd, nullptr, FALSE);
		}
	}

	void MainWindow::DoCaptionButton(CaptionButton id)
	{
		switch (id)
		{
		case CaptionButton::Theme:
			ToggleTheme();
			break;
		case CaptionButton::Minimize:
			ShowWindow(m_hwnd, SW_MINIMIZE);
			break;
		case CaptionButton::Maximize:
			ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
			break;
		case CaptionButton::Close:
			PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
			break;
		default:
			break;
		}
	}

	void MainWindow::ToggleTheme()
	{
		m_theme = (m_theme == ThemeMode::Dark) ? ThemeMode::Light : ThemeMode::Dark;
		SetCurrentTheme(m_theme);

		// 标题栏 / 菜单栏 / 内容区 / 原生弹出菜单一起换色
		ApplyTheme();
	}

	int MainWindow::Run()
	{
		// 主循环交给 Qt：Qt 的事件分发器同样会把本线程里 Win32 窗口的消息
		// DispatchMessage 出去，所以自绘标题栏 / 菜单栏的消息照旧能收到。
		if (QtShell::Instance().IsReady())
			return QtShell::Instance().Exec();

		// 兜底：Qt 没起来（例如缺 Qt6Core.dll）时退回原生消息循环
		MSG msg;
		while (GetMessage(&msg, nullptr, 0, 0) > 0)
		{
			if (m_hAccel && TranslateAcceleratorW(m_hwnd, m_hAccel, &msg))
				continue;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		return 0;
	}

	LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		MainWindow* pThis = nullptr;

		if (msg == WM_CREATE)
		{
			CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
			pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
		}
		else
		{
			pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		}

		if (pThis)
			return pThis->EventProc(hwnd, msg, wParam, lParam);
		else
			return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	LRESULT MainWindow::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_NCCALCSIZE:
		{
			// 关键：整个窗口都变成客户区（自绘标题栏 + 菜单栏）
			if (wParam == TRUE)
			{
				NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
				if (IsZoomed(hwnd))
				{
					// 最大化时系统会把窗口外扩一圈不可见边框，这里直接对齐工作区
					MONITORINFO mi = { sizeof(mi) };
					if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
						params->rgrc[0] = mi.rcWork;
				}
				return 0;
			}
			return 0;
		}

		case WM_NCHITTEST:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			POINT cp = pt;
			ScreenToClient(hwnd, &cp);

			// 缩放边框（最大化时禁用）
			if (!IsZoomed(hwnd))
			{
				RECT wr = {};
				GetWindowRect(hwnd, &wr);
				const int b = m_border;
				const bool left = pt.x < wr.left + b;
				const bool right = pt.x >= wr.right - b;
				const bool top = pt.y < wr.top + b;
				const bool bottom = pt.y >= wr.bottom - b;

				if (top && left)     return HTTOPLEFT;
				if (top && right)    return HTTOPRIGHT;
				if (bottom && left)  return HTBOTTOMLEFT;
				if (bottom && right) return HTBOTTOMRIGHT;
				if (left)            return HTLEFT;
				if (right)           return HTRIGHT;
				if (top)             return HTTOP;
				if (bottom)          return HTBOTTOM;
			}

			// 标题栏按钮走客户区点击，其余空白处用于拖动窗口
			if (cp.y < m_titleH)
			{
				if (HitTestButton(cp) != CaptionButton::None)
					return HTCLIENT;
				return HTCAPTION;
			}
			return HTCLIENT;
		}

		case WM_GETMINMAXINFO:
		{
			MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
			mmi->ptMinTrackSize.x = Scale(kMinWinW);
			mmi->ptMinTrackSize.y = Scale(kMinWinH);
			return 0;
		}

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_ERASEBKGND:
			return 1;   // 全部由 WM_PAINT 双缓冲绘制

		case WM_SIZE:
			// 窗口尺寸变了菜单位置就不对了，直接收掉
			CloseMenu();
			RecalcLayout();
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;

		case WM_DPICHANGED:
		{
			m_dpi = static_cast<int>(HIWORD(wParam));
			CloseMenu();
			EnsureFonts();
			const RECT* prc = reinterpret_cast<const RECT*>(lParam);
			SetWindowPos(hwnd, nullptr, prc->left, prc->top,
				prc->right - prc->left, prc->bottom - prc->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			RecalcLayout();
			if (m_trayIcon)
				m_trayIcon->ReloadIconForDpi();
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}

		case WM_ACTIVATE:
			// 弹出菜单是独立窗口，它激活时主窗口会收到失活通知；
			// 这期间保持标题栏的"活动"外观，避免一开菜单标题就变灰。
			if (m_popupOpen)
				return 0;
			m_active = (LOWORD(wParam) != WA_INACTIVE);
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;

		case WM_NCACTIVATE:
			// 自绘框架：不让系统画非客户区
			InvalidateRect(hwnd, nullptr, FALSE);
			return TRUE;

		case WM_SYSKEYDOWN:
			// Alt 单独按下：显示助记符下划线
			if (wParam == VK_MENU)
			{
				SetMnemonicVisible(true);
				return 0;
			}
			// Alt + 字母：直接展开对应菜单（Alt+F/E/V/W/H）
			if (OpenMenuByMnemonic(static_cast<wchar_t>(wParam)))
				return 0;
			break;   // 其余 Alt 组合（Alt+F4 等）交回系统

		case WM_SYSKEYUP:
			if (wParam == VK_MENU)
			{
				SetMnemonicVisible(false);
				return 0;
			}
			break;

		case WM_KEYUP:
			if (wParam == VK_MENU)
			{
				SetMnemonicVisible(false);
				return 0;
			}
			break;

		case WM_MOUSEMOVE:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			UpdateHover(pt);
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&tme);
			return 0;
		}

		case WM_MOUSELEAVE:
			if (m_pressedButton == CaptionButton::None)
			{
				m_hotButton = CaptionButton::None;
				// 菜单展开时保持菜单栏高亮，不要因为光标移到弹出菜单上就清掉
				m_hotMenu = m_popup.IsOpen() ? m_menuIndex : -1;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;

		case WM_NCLBUTTONDOWN:
		case WM_NCRBUTTONDOWN:
			// 拖标题栏、双击等非客户区操作先把菜单收掉（原生菜单同款行为）
			CloseMenu();
			break;

		case WM_NCMOUSEMOVE:
			// 从按钮移到标题栏空白处（此时是 NC 消息），清掉悬停态
			if (m_hotButton != CaptionButton::None || m_hotMenu != -1)
			{
				m_hotButton = CaptionButton::None;
				m_hotMenu = -1;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;

		case WM_LBUTTONDOWN:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

			// 菜单展开时：点同一个菜单栏项 = 收起，点别的项 = 切换，点其他位置 = 收起
			if (m_popup.IsOpen())
			{
				const int menuIndex = HitTestMenu(pt);
				if (menuIndex >= 0 && menuIndex != m_menuIndex)
					SwitchMenu(menuIndex);
				else
					CloseMenu();
				return 0;
			}

			const CaptionButton btn = HitTestButton(pt);
			if (btn != CaptionButton::None)
			{
				m_pressedButton = btn;
				SetCapture(hwnd);
				InvalidateRect(hwnd, nullptr, FALSE);
				return 0;
			}
			const int menuIndex = HitTestMenu(pt);
			if (menuIndex >= 0)
				OpenMenu(menuIndex);
			return 0;
		}

		case WM_LBUTTONUP:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			const CaptionButton pressed = m_pressedButton;
			m_pressedButton = CaptionButton::None;
			if (GetCapture() == hwnd)
				ReleaseCapture();

			if (pressed != CaptionButton::None && HitTestButton(pt) == pressed)
				DoCaptionButton(pressed);

			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}

		case WM_CAPTURECHANGED:
			if (m_pressedButton != CaptionButton::None)
			{
				m_pressedButton = CaptionButton::None;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;

		case WM_COMMAND:
			// 高位为 0 表示来自快捷键
			if (HIWORD(wParam) == 0 && OnCommand(LOWORD(wParam)))
				return 0;
			break;

		case WM_CLOSE:
			// 关闭按钮 = 真正退出。
			// 想改成"关闭即最小化到托盘"，把这两行换成：
			//   ShowWindow(hwnd, SW_HIDE); return 0;
			DestroyWindow(hwnd);
			return 0;

		case WM_DESTROY:
			CloseMenu();
			QtShell::Instance().Shutdown();   // 先收掉 Qt 工具条窗口
			if (m_trayIcon)
				m_trayIcon->Remove();
			PostQuitMessage(0);            // 否则关窗后进程不退出
			return 0;
		}

		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
}
