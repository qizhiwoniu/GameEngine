#include "Theme.h"

namespace GameEngine
{
	namespace
	{
		const ThemePalette kDarkPalette = {
			/* windowBg              */ RGB(24, 24, 26),
			/* border                */ RGB(58, 58, 64),
			/* titleBarBg            */ RGB(32, 32, 36),
			/* titleBarText          */ RGB(240, 240, 242),
			/* titleBarInactiveText  */ RGB(140, 140, 148),
			/* buttonHover           */ RGB(56, 56, 62),
			/* buttonPressed         */ RGB(44, 44, 50),
			/* closeHover            */ RGB(232, 17, 35),
			/* menuBarBg             */ RGB(32, 32, 36),
			/* menuBarText           */ RGB(240, 240, 242),
			/* menuHoverBg           */ RGB(58, 58, 66),
			/* popupBg               */ RGB(43, 43, 48),
			/* popupText             */ RGB(240, 240, 242),
			/* popupHoverBg          */ RGB(62, 62, 72),
			/* popupDisabled         */ RGB(136, 136, 144),
		};

		const ThemePalette kLightPalette = {
			/* windowBg              */ RGB(245, 245, 247),
			/* border                */ RGB(214, 214, 220),
			/* titleBarBg            */ RGB(243, 243, 245),
			/* titleBarText          */ RGB(26, 26, 30),
			/* titleBarInactiveText  */ RGB(130, 130, 138),
			/* buttonHover           */ RGB(228, 228, 234),
			/* buttonPressed         */ RGB(212, 212, 220),
			/* closeHover            */ RGB(232, 17, 35),
			/* menuBarBg             */ RGB(243, 243, 245),
			/* menuBarText           */ RGB(26, 26, 30),
			/* menuHoverBg           */ RGB(224, 224, 232),
			/* popupBg               */ RGB(255, 255, 255),
			/* popupText             */ RGB(26, 26, 30),
			/* popupHoverBg          */ RGB(235, 235, 242),
			/* popupDisabled         */ RGB(150, 150, 158),
		};

		ThemeMode g_theme = ThemeMode::Dark;
	}

	const ThemePalette& Palette(ThemeMode mode)
	{
		return mode == ThemeMode::Dark ? kDarkPalette : kLightPalette;
	}

	ThemeMode CurrentTheme()
	{
		return g_theme;
	}

	void SetCurrentTheme(ThemeMode mode)
	{
		g_theme = mode;
	}

	void SetNativeAppMode(bool dark)
	{
		// uxtheme 必须常驻：卸载后进程的菜单主题状态可能失效，
		// 所以这里加载一次后一直保留（进程生命周期内也就几 MB）
		static HMODULE hUxTheme = LoadLibraryExW(
			L"uxtheme.dll",
			nullptr,
			LOAD_LIBRARY_SEARCH_SYSTEM32
		);
		if (!hUxTheme)
			return;

		enum PreferredAppMode
		{
			Default = 0,
			AllowDark = 1,
			ForceDark = 2,
			ForceLight = 3,
			Max = 4
		};

		using SetPreferredAppModeFunc = PreferredAppMode(WINAPI*)(PreferredAppMode);
		using FlushMenuThemesFunc = void(WINAPI*)();

		auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFunc>(
			GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
		auto flushMenuThemes = reinterpret_cast<FlushMenuThemesFunc>(
			GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136)));

		if (setPreferredAppMode)
			setPreferredAppMode(dark ? ForceDark : ForceLight);

		if (flushMenuThemes)
			flushMenuThemes();
	}

	void ApplyNativeMenuTheme()
	{
		SetNativeAppMode(CurrentTheme() == ThemeMode::Dark);
	}
}
