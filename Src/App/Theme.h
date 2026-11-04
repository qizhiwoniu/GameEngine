#pragma once
#include <windows.h>

namespace GameEngine
{
	enum class ThemeMode
	{
		Dark,
		Light,
	};

	// 一套主题的配色表。暗色：深底白字；亮色：浅底黑字（完全反色）
	struct ThemePalette
	{
		COLORREF windowBg;            // 内容区背景
		COLORREF border;              // 窗口外边框 / 分隔线
		COLORREF titleBarBg;          // 标题栏背景
		COLORREF titleBarText;        // 标题栏文字
		COLORREF titleBarInactiveText;// 窗口失焦时的标题文字
		COLORREF buttonHover;         // 标题栏按钮悬停
		COLORREF buttonPressed;       // 标题栏按钮按下
		COLORREF closeHover;          // 关闭按钮悬停（红）
		COLORREF menuBarBg;           // 菜单栏背景
		COLORREF menuBarText;         // 菜单栏文字
		COLORREF menuHoverBg;         // 菜单项悬停
		COLORREF popupBg;             // 弹出菜单背景
		COLORREF popupText;           // 弹出菜单文字
		COLORREF popupHoverBg;        // 弹出菜单悬停
		COLORREF popupDisabled;       // 快捷键/次要文字
	};

	const ThemePalette& Palette(ThemeMode mode);

	ThemeMode CurrentTheme();
	void      SetCurrentTheme(ThemeMode mode);

	// 用 uxtheme 未公开导出（序号 135/136）切换本进程原生菜单/框架的明暗配色
	void SetNativeAppMode(bool dark);
	// 按当前主题重新应用原生菜单配色（原生弹出菜单、托盘菜单用）
	void ApplyNativeMenuTheme();
}
