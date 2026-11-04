#pragma once
#include <QString>
#include <vector>

namespace GameEditor
{
	// 视口命令（对齐 RAD 版 ViewHotkeys 的 TViewCmd）
	enum class VCmd
	{
		Perspective = 0, Ortho, Top, Front, Left, Camera, Stand,
		Wireframe, ResetView, Grid, Focus, ShowFps,
		ToolSelect, ToolMove, ToolRotate, ToolScale,
		SnapToggle, AngleSnap, PercentSnap,
		CmdCount
	};

	struct HotkeyBinding
	{
		QString id;     // 稳定标识（QSettings 存储）
		QString name;   // 显示名（命令列表 / 键盘图）
		VCmd       cmd;
		int        vk = 0;      // Windows VK，0 = 未指派
		int        mods = 0;    // Qt::ShiftModifier | CtrlModifier | AltModifier
		int        defVk = 0;
		int        defMods = 0;
	};

	// 全局快捷键表（启动时从 QSettings 恢复）
	std::vector<HotkeyBinding>& Hotkeys();
	void     LoadHotkeys();                        // QSettings -> 表
	void     SaveHotkeys();                        // 表 -> QSettings
	void     ResetHotkeys();                       // 恢复默认
	// 按键匹配：返回 VCmd，未命中返回 -1。key 为 Qt::Key，mods 为 Qt 修饰符。
	int      MatchHotkey(int key, int mods);

	// 修饰键名 / 键名文本（"Ctrl+P"）
	QString  HotkeyText(int vk, int mods);
}
