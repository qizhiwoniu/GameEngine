#pragma once
#include <windows.h>
#include <functional>
#include "Theme.h"

namespace GameEngine
{
	// 弹出菜单的一项。菜单数据在编译期就是固定数组，所以这里只存裸指针。
	struct PopupMenuItem
	{
		const wchar_t* text;      // 空指针 + separator=true 表示分隔线
		const wchar_t* shortcut;  // 右侧快捷键提示，可为空
		WORD           id;        // 命令 ID
		bool           separator;
		bool           disabled = false;   // 灰色不可用（和命令是否已实现对应，例如编辑器还没接上时的"撤销/重做"）
	};

	struct PopupMenuStyle
	{
		HFONT     font = nullptr;
		int       dpi = 96;
		int       itemWidth = 180;   // 菜单整体宽度
		int       itemHeight = 28;   // 单个菜单项高度
		ThemeMode theme = ThemeMode::Dark;
		WORD      checkedId = 0;     // 需要显示勾选标记的命令 ID，0 表示不显示
	};

	// 完全自绘、非模态的弹出菜单。
	//
	// 两个设计要点：
	// 1) 不用 HMENU / TrackPopupMenu —— 原生菜单窗口的边框由系统绘制，
	//    Win11 深色菜单天生带一圈浅色描边，没有开关能关掉。这里所有像素（含边框）都自己画。
	// 2) 不走嵌套消息循环 —— 早期版本在 Show() 里自己跑 GetMessage，
	//    结果主窗口的点击也被这个循环派发，点菜单栏会套娃式再开一个菜单、旧的永不关闭。
	//    现在由主窗口统一管理：Show() 只负责创建/更新窗口，选中后回调通知。
	class PopupMenu
	{
	public:
		PopupMenu() = default;
		~PopupMenu();

		PopupMenu(const PopupMenu&) = delete;
		PopupMenu& operator=(const PopupMenu&) = delete;

		// 菜单关闭时先回调 onClose（用来清状态），选中项时再回调 onCommand
		void SetCallbacks(std::function<void(int)> onCommand, std::function<void()> onClose);

		// 打开菜单。若已经打开，则原地换成新内容并重新定位（菜单间切换不重建窗口，不闪）
		bool Show(HWND owner, POINT anchor, const PopupMenuItem* items, int count,
			const PopupMenuStyle& style);

		void Close();
		bool IsOpen() const { return m_hwnd != nullptr; }

	private:
		static bool    EnsureClass();
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT        EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

		bool    Create();
		void    Relayout(POINT anchor);
		void    RequestClose(int command);   // 弹窗内部请求关闭：投递消息，避免在自己的消息处理里销毁自己
		void    DoClose(int command);        // 真正销毁窗口 + 回调

		void    Paint();
		void    DrawItem(HDC dc, int index, const RECT& rc);
		void    DrawCheck(HDC dc, int x, int y, int size, COLORREF color);

		RECT    ItemRect(int index) const;
		int     HitTest(POINT clientPt) const;
		int     NextItem(int from, int step) const;
		void    SetHot(int index);

		int     Scale(int value) const { return MulDiv(value, m_style.dpi, 96); }

	private:
		const PopupMenuItem* m_items = nullptr;
		int                  m_count = 0;
		PopupMenuStyle       m_style{};

		HWND m_hwnd = nullptr;
		HWND m_owner = nullptr;
		int  m_w = 0;
		int  m_h = 0;

		int  m_hot = -1;
		int  m_pressed = -1;

		std::function<void(int)> m_onCommand;
		std::function<void()>    m_onClose;
	};
}
