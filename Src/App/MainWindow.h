#pragma once 
#include <windows.h>
#include <memory>  
#include <string>
#include "NotifyIcon/TrayIcon.h"
#include "Theme.h"
#include "PopupMenu.h"
#include "../../version.h"

class TrayIcon;

namespace GameEngine
{
	// 菜单命令 ID（从 40001 起，避开资源编辑器占用的号段）
	enum MenuId : WORD
	{
		IDM_FILE_HIDE_TRAY = 40001, // 文件 - 隐藏到托盘
		IDM_FILE_EXIT,              // 文件 - 退出
		IDM_EDIT_UNDO,              // 编辑 - 撤销
		IDM_EDIT_REDO,              // 编辑 - 重做
		IDM_EDIT_CUT,               // 编辑 - 剪切
		IDM_EDIT_COPY,              // 编辑 - 复制
		IDM_EDIT_PASTE,             // 编辑 - 粘贴
		IDM_VIEW_TOPMOST,           // 视图 - 窗口置顶
		IDM_VIEW_RESET,             // 视图 - 重置窗口大小
		IDM_WINDOW_MIN,             // 窗口 - 最小化
		IDM_WINDOW_MAX,             // 窗口 - 最大化 / 还原
		IDM_WINDOW_CLOSE,           // 窗口 - 关闭
		IDM_HELP_UPDATE,            // 帮助 - 检查更新
		IDM_HELP_ABOUT,             // 帮助 - 关于
	};

	// 菜单栏最多几项（文件/编辑/视图/窗口 + 右侧帮助，留点余量）
	constexpr int kMaxMenuBarItems = 8;

	class MainWindow
	{
	public:
		MainWindow();
		~MainWindow();
		bool Initialize(const wchar_t* title, int width, int height);
		int  Run();
	private:
		// 标题栏右侧的自绘按钮
		enum class CaptionButton
		{
			None,
			Theme,      // 主题切换（位于最小化左侧）
			Minimize,
			Maximize,
			Close,
		};

		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

		bool    InitWindow(const wchar_t* title, int width, int height);
		bool    OnCommand(WORD id);

		// 布局 / 主题 / 字体
		void    RecalcLayout();
		void    EnsureFonts();
		void    ApplyTheme();
		void    ApplyIcons();
		void    ResetWindowSize();

		// 绘制
		void    OnPaint();
		void    DrawTitleBar(HDC dc, const RECT& rcTitle);
		void    DrawMenuBar(HDC dc, const RECT& rcBar);
		void    DrawCaptionButton(HDC dc, const RECT& rc, CaptionButton id, bool hot, bool pressed);
		void    DrawFrameBorder(HDC dc, const RECT& rc);

		// 菜单
		void    OpenMenu(int index);
		void    CloseMenu();
		void    SwitchMenu(int index);
		bool    OpenMenuByMnemonic(wchar_t ch);   // 命中助记符则展开对应菜单，返回是否命中
		void    SetMnemonicVisible(bool visible);

		// 交互
		CaptionButton HitTestButton(POINT pt) const;
		int           HitTestMenu(POINT pt) const;
		void          DoCaptionButton(CaptionButton id);
		void          ToggleTheme();
		void          UpdateHover(POINT pt);

		int     Scale(int value) const { return MulDiv(value, m_dpi, 96); }
		bool    IsTopMost() const;

	private:
		// 窗口
		HWND						  m_hwnd = nullptr;
		HACCEL						  m_hAccel = nullptr;
		HICON						  m_hIconBig = nullptr;
		HICON						  m_hIconSmall = nullptr;
		std::wstring				  m_title;

		// 字体
		HFONT						  m_fontUi = nullptr;     // 菜单 / 按钮
		HFONT						  m_fontTitle = nullptr;  // 窗口标题

		// 主题与交互状态
		ThemeMode					  m_theme = ThemeMode::Dark;
		bool						  m_active = true;
		bool						  m_popupOpen = false;
		int							  m_menuIndex = -1;      // 当前展开的菜单索引，-1 表示没有
		CaptionButton				  m_hotButton = CaptionButton::None;
		CaptionButton				  m_pressedButton = CaptionButton::None;
		int							  m_hotMenu = -1;
		bool						  m_showMnemonic = false;  // 按住 Alt 时给菜单字母加下划线

		// 布局（物理像素）
		int							  m_dpi = 96;
		int							  m_titleH = 36;
		int							  m_menuH = 30;
		int							  m_border = 6;
		RECT						  m_rcTheme{};
		RECT						  m_rcMin{};
		RECT						  m_rcMax{};
		RECT						  m_rcClose{};
		RECT						  m_rcMenuBar{};
		RECT						  m_rcMenuItems[kMaxMenuBarItems]{};
		std::wstring				  m_menuTitles[kMaxMenuBarItems];   // 显示用文字，例如 L"文件(F)"
		RECT						  m_rcContent{};
		int							  m_menuItemW = 180;
		int							  m_menuItemH = 28;
		int							  m_menuCount = 0;

	private:
		std::unique_ptr<TrayIcon> m_trayIcon;
		PopupMenu                 m_popup;
	};
}
