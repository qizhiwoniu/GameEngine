#include "PopupMenu.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace GameEngine
{
	namespace
	{
		constexpr wchar_t kClassName[] = L"GameEnginePopupMenuClass";


		// 弹窗内部请求关闭用的消息（异步销毁，避免在自己窗口过程里直接 DestroyWindow）
		constexpr UINT kMsgPopupClose = WM_APP + 0x321;
		// 焦点变化后延时复核前台窗口（激活切换期间前台窗口还不稳定）
		constexpr UINT kTimerForegroundId = 1;
		constexpr UINT kTimerForegroundMs = 200;

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
	}

	PopupMenu::~PopupMenu()
	{
		if (m_hwnd)
		{
			HWND hwnd = m_hwnd;
			m_hwnd = nullptr;
			DestroyWindow(hwnd);
		}
	}

	void PopupMenu::SetCallbacks(std::function<void(int)> onCommand, std::function<void()> onClose)
	{
		m_onCommand = std::move(onCommand);
		m_onClose = std::move(onClose);
	}

	bool PopupMenu::EnsureClass()
	{
		static bool registered = false;
		if (registered)
			return true;

		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		// CS_DROPSHADOW 让自绘菜单也有投影；背景全部自绘，所以不给类画刷
		wc.style = CS_DROPSHADOW;
		wc.lpfnWndProc = WndProc;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = nullptr;
		wc.lpszClassName = kClassName;

		if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			return false;

		registered = true;
		return true;
	}

	bool PopupMenu::Create()
	{
		// WS_POPUP + 属主窗口：不进任务栏，也不会跑到主窗口后面
		m_hwnd = CreateWindowExW(
			WS_EX_TOOLWINDOW,
			kClassName, L"", WS_POPUP,
			0, 0, 16, 16,
			m_owner, nullptr, GetModuleHandle(nullptr), this);
		if (!m_hwnd)
			return false;

		// 去掉 DWM 给窗口画的那圈边框（这就是原来白边的另一处来源）
		const COLORREF noBorder = 0xFFFFFFFE;   // DWMWA_COLOR_NONE
		DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));

		const int prefer = 2;   // DWMWCP_ROUND
		DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &prefer, sizeof(prefer));
		return true;
	}

	void PopupMenu::Relayout(POINT anchor)
	{
		const int padY = Scale(5);
		const int sepH = Scale(9);

		m_w = m_style.itemWidth;
		m_h = padY * 2;
		for (int i = 0; i < m_count; ++i)
			m_h += m_items[i].separator ? sepH : m_style.itemHeight;

		// 贴着菜单栏往下弹，超出工作区就往回收
		int x = anchor.x;
		int y = anchor.y;

		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi))
		{
			const RECT& wa = mi.rcWork;
			if (x + m_w > wa.right)  x = wa.right - m_w;
			if (x < wa.left)         x = wa.left;
			if (y + m_h > wa.bottom) y = wa.bottom - m_h;
			if (y < wa.top)          y = wa.top;
		}

		SetWindowPos(m_hwnd, HWND_TOP, x, y, m_w, m_h,
			SWP_NOACTIVATE | SWP_NOZORDER);
	}

	bool PopupMenu::Show(HWND owner, POINT anchor, const PopupMenuItem* items, int count,
		const PopupMenuStyle& style)
	{
		if (!items || count <= 0 || !EnsureClass())
			return false;

		m_owner = owner;
		m_items = items;
		m_count = count;
		m_style = style;
		m_hot = -1;
		m_pressed = -1;

		if (!m_hwnd)
		{
			if (!Create())
				return false;

			// 先摆好位置再显示，避免从 (0,0) 跳一下
			Relayout(anchor);
			ShowWindow(m_hwnd, SW_SHOW);
			SetForegroundWindow(m_hwnd);
			SetFocus(m_hwnd);
			SetTimer(m_hwnd, kTimerForegroundId, kTimerForegroundMs, nullptr);
		}
		else
		{
			Relayout(anchor);
		}
		InvalidateRect(m_hwnd, nullptr, FALSE);
		UpdateWindow(m_hwnd);
		return true;
	}

	void PopupMenu::RequestClose(int command)
	{
		if (!m_hwnd)
			return;
		PostMessageW(m_hwnd, kMsgPopupClose, static_cast<WPARAM>(command), 0);
	}

	void PopupMenu::DoClose(int command)
	{
		if (!m_hwnd)
			return;

		HWND hwnd = m_hwnd;
		m_hwnd = nullptr;
		m_hot = -1;
		m_pressed = -1;
		KillTimer(hwnd, kTimerForegroundId);
		DestroyWindow(hwnd);

		// 先让主窗口清状态，再执行命令（命令可能弹模态框）
		if (m_onClose)
			m_onClose();
		if (command != 0 && m_onCommand)
			m_onCommand(command);
	}

	void PopupMenu::Close()
	{
		DoClose(0);
	}

	RECT PopupMenu::ItemRect(int index) const
	{
		const int padY = Scale(5);
		const int sepH = Scale(9);

		RECT rc = { 0, padY, m_w, padY };
		for (int i = 0; i <= index && i < m_count; ++i)
		{
			rc.top = rc.bottom;
			rc.bottom = rc.top + (m_items[i].separator ? sepH : m_style.itemHeight);
		}
		return rc;
	}

	int PopupMenu::HitTest(POINT cp) const
	{
		for (int i = 0; i < m_count; ++i)
		{
			if (m_items[i].separator || m_items[i].disabled)
				continue;
			RECT rc = ItemRect(i);
			if (PtInRect(&rc, cp))
				return i;
		}
		return -1;
	}

	int PopupMenu::NextItem(int from, int step) const
	{
		// 首尾循环，和系统菜单一致
		int i = from;
		for (int n = 0; n < m_count; ++n)
		{
			i += step;
			if (i < 0)          i = m_count - 1;
			if (i >= m_count)   i = 0;
			if (!m_items[i].separator && !m_items[i].disabled)
				return i;
		}
		return from < 0 ? -1 : from;
	}

	void PopupMenu::SetHot(int index)
	{
		if (index == m_hot)
			return;
		m_hot = index;
		InvalidateRect(m_hwnd, nullptr, FALSE);
	}

	void PopupMenu::DrawCheck(HDC dc, int x, int y, int size, COLORREF color)
	{
		HPEN pen = CreatePen(PS_SOLID, (std::max)(1, Scale(2)), color);
		HGDIOBJ oldPen = SelectObject(dc, pen);
		HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

		POINT pts[3] = {
			{ x, y + size / 2 },
			{ x + size / 2, y + size },
			{ x + size, y },
		};
		Polyline(dc, pts, 3);

		SelectObject(dc, oldBrush);
		SelectObject(dc, oldPen);
		DeleteObject(pen);
	}

	void PopupMenu::DrawItem(HDC dc, int index, const RECT& rc)
	{
		const ThemePalette& p = Palette(m_style.theme);
		const PopupMenuItem& e = m_items[index];

		if (e.separator)
		{
			const int y = (rc.top + rc.bottom) / 2;
			RECT line = { rc.left + Scale(12), y, rc.right - Scale(12), y + 1 };
			FillSolidRect(dc, line, p.border);
			return;
		}

		// 不可用项：不参与悬停高亮，文字和快捷键都用灰色
		const bool disabled = e.disabled;
		if (!disabled && (index == m_hot || index == m_pressed))
			FillSolidRect(dc, rc, p.popupHoverBg);

		HGDIOBJ oldFont = SelectObject(dc, m_style.font);
		SetBkMode(dc, TRANSPARENT);
		const COLORREF fg = disabled ? p.popupDisabled : p.popupText;

		// 勾选标记（窗口置顶）
		if (m_style.checkedId != 0 && e.id == m_style.checkedId)
		{
			const int size = Scale(9);
			DrawCheck(dc, rc.left + Scale(10), rc.top + (m_style.itemHeight - size) / 2,
				size, fg);
		}

		RECT rcText = rc;
		rcText.left += Scale(28);
		rcText.right -= Scale(12);
		SetTextColor(dc, fg);
		DrawTextW(dc, e.text ? e.text : L"", -1, &rcText,
			DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

		// 右侧快捷键
		if (e.shortcut && *e.shortcut)
		{
			const int tw = TextWidth(dc, m_style.font, e.shortcut);
			RECT rcAccel = rc;
			rcAccel.left = rc.right - Scale(12) - tw - Scale(2);
			rcAccel.right = rc.right - Scale(12);
			SetTextColor(dc, p.popupDisabled);
			DrawTextW(dc, e.shortcut, -1, &rcAccel,
				DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
		}

		SelectObject(dc, oldFont);
	}

	void PopupMenu::Paint()
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(m_hwnd, &ps);

		RECT rc = { 0, 0, m_w, m_h };
		HDC mem = CreateCompatibleDC(hdc);
		HBITMAP bmp = CreateCompatibleBitmap(hdc, m_w, m_h);
		HGDIOBJ oldBmp = SelectObject(mem, bmp);

		const ThemePalette& p = Palette(m_style.theme);
		FillSolidRect(mem, rc, p.popupBg);

		// 自绘的 1px 边框：暗色下是深灰，替代系统菜单那圈浅色描边
		RECT line = { 0, 0, m_w, 1 };
		FillSolidRect(mem, line, p.border);
		line = { 0, m_h - 1, m_w, m_h };
		FillSolidRect(mem, line, p.border);
		line = { 0, 0, 1, m_h };
		FillSolidRect(mem, line, p.border);
		line = { m_w - 1, 0, m_w, m_h };
		FillSolidRect(mem, line, p.border);

		for (int i = 0; i < m_count; ++i)
			DrawItem(mem, i, ItemRect(i));

		BitBlt(hdc, 0, 0, m_w, m_h, mem, 0, 0, SRCCOPY);

		SelectObject(mem, oldBmp);
		DeleteObject(bmp);
		DeleteDC(mem);
		EndPaint(m_hwnd, &ps);
	}

	LRESULT CALLBACK PopupMenu::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		PopupMenu* pThis = nullptr;

		if (msg == WM_NCCREATE)
		{
			CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			pThis = reinterpret_cast<PopupMenu*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
			if (pThis)
				pThis->m_hwnd = hwnd;
		}
		else
		{
			pThis = reinterpret_cast<PopupMenu*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		if (pThis)
			return pThis->EventProc(hwnd, msg, wParam, lParam);
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	LRESULT PopupMenu::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
			Paint();
			return 0;

		case kMsgPopupClose:
			DoClose(static_cast<int>(wParam));
			return 0;

		case WM_MOUSEACTIVATE:
			return MA_ACTIVATE;

		case WM_MOUSEMOVE:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			SetHot(HitTest(pt));
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&tme);
			return 0;
		}

		case WM_MOUSELEAVE:
			SetHot(-1);
			return 0;

		case WM_LBUTTONDOWN:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			const int index = HitTest(pt);
			if (index >= 0)
			{
				m_pressed = index;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;
		}

		case WM_LBUTTONUP:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			const int index = HitTest(pt);
			const int pressed = m_pressed;
			m_pressed = -1;

			if (index >= 0 && index == pressed)
				RequestClose(m_items[index].id);
			else
				InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}

		case WM_KEYDOWN:
			switch (wParam)
			{
			case VK_ESCAPE:
				RequestClose(0);
				return 0;
			case VK_RETURN:
			case VK_SPACE:
				RequestClose(m_hot >= 0 ? m_items[m_hot].id : 0);
				return 0;
			case VK_UP:
				SetHot(NextItem(m_hot < 0 ? 0 : m_hot, -1));
				return 0;
			case VK_DOWN:
				SetHot(NextItem(m_hot < 0 ? -1 : m_hot, +1));
				return 0;
			case VK_LEFT:
			case VK_RIGHT:
				RequestClose(0);
				return 0;
			default:
				break;
			}
			return 0;

		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
			RequestClose(0);
			return 0;

		case WM_TIMER:
			// 定时复核前台窗口：用户点到了别的程序 -> 收起。
			// 不依赖 WM_ACTIVATE/WM_KILLFOCUS（激活切换过程中前台状态不稳定，容易误关自己）
			if (wParam == kTimerForegroundId)
			{
				HWND fg = GetForegroundWindow();
				if (fg != m_owner && fg != m_hwnd)
					RequestClose(0);
			}
			return 0;
		}

		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}
