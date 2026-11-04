#include "Hotkeys.h"

#include <QSettings>
#include <QKeyEvent>
#include <windows.h>

namespace GameEditor
{
	namespace
	{
		struct Def { const char* id; const char* name; VCmd cmd; int vk; int mods; };
		// 默认键位（与 RAD 版 Setting.cpp 一致）
		const Def kDefaults[] = {
			{ "persp",     "透视图",          VCmd::Perspective, 'P', 0 },
			{ "ortho",     "正交图",          VCmd::Ortho,       'O', 0 },
			{ "top",       "顶视图",          VCmd::Top,         'T', 0 },
			{ "front",     "前视图",          VCmd::Front,       'F', 0 },
			{ "left",      "左视图",          VCmd::Left,        'L', 0 },
			{ "camera",    "相机",            VCmd::Camera,      'C', 0 },
			{ "stand",     "默认照明",        VCmd::Stand,       'D', 0 },
			{ "wire",      "线框图",          VCmd::Wireframe,    0,  0 },
			{ "reset",     "重置视图",        VCmd::ResetView,    0,  0 },
			{ "grid",      "切换栅格",        VCmd::Grid,        'G', 0 },
			{ "focus",     "聚焦选定物体",    VCmd::Focus,       'Z', 0 },
			{ "fps",       "显示 FPS",        VCmd::ShowFps,     '7', 0 },
			{ "toolsel",   "选择工具",        VCmd::ToolSelect,  'Q', 0 },
			{ "toolmove",  "移动工具",        VCmd::ToolMove,    'W', 0 },
			{ "toolrot",   "旋转工具",        VCmd::ToolRotate,  'E', 0 },
			{ "toolscale", "缩放工具",        VCmd::ToolScale,   'R', 0 },
			{ "snap",      "启用捕捉",        VCmd::SnapToggle,  'S', 0 },
			{ "anglesnap", "角度捕捉",        VCmd::AngleSnap,   'A', 0 },
			{ "pctsnap",   "百分比捕捉",      VCmd::PercentSnap, 'P', (int)Qt::ShiftModifier },
		};

		int QtMods(int mods) { return mods & ((int)Qt::ShiftModifier | (int)Qt::ControlModifier | (int)Qt::AltModifier); }

		// Qt::Key -> Windows VK（覆盖本表用到的键 + 字母数字）
		int KeyToVk(int key)
		{
			if (key >= Qt::Key_A && key <= Qt::Key_Z)   return 'A' + (key - Qt::Key_A);
			if (key >= Qt::Key_0 && key <= Qt::Key_9)   return '0' + (key - Qt::Key_0);
			if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return VK_F1 + (key - Qt::Key_F1);
			switch (key)
			{
				case Qt::Key_Escape: return VK_ESCAPE;
				case Qt::Key_Return: case Qt::Key_Enter: return VK_RETURN;
				case Qt::Key_Tab:    return VK_TAB;
				case Qt::Key_Backspace: return VK_BACK;
				case Qt::Key_Space:  return VK_SPACE;
				case Qt::Key_Minus:  return VK_OEM_MINUS;
				case Qt::Key_Equal:  return VK_OEM_PLUS;
				case Qt::Key_BracketLeft:  return VK_OEM_4;
				case Qt::Key_BracketRight: return VK_OEM_6;
				case Qt::Key_Backslash:    return VK_OEM_5;
				case Qt::Key_Semicolon:    return VK_OEM_1;
				case Qt::Key_Apostrophe:   return VK_OEM_7;
				case Qt::Key_Comma:  return VK_OEM_COMMA;
				case Qt::Key_Period: return VK_OEM_PERIOD;
				case Qt::Key_Slash:  return VK_OEM_2;
				case Qt::Key_QuoteLeft: return VK_OEM_3;
				default: return 0;
			}
		}
	}

	std::vector<HotkeyBinding>& Hotkeys()
	{
		static std::vector<HotkeyBinding> v = [] {
			std::vector<HotkeyBinding> r;
			for (const Def& d : kDefaults)
			{
				HotkeyBinding b;
				b.id = QString(d.id);
				b.name = QString(d.name);
				b.cmd = d.cmd;
				b.vk = b.defVk = d.vk;
				b.mods = b.defMods = d.mods;
				r.push_back(b);
			}
			return r;
		}();
		return v;
	}

	void LoadHotkeys()
	{
		QSettings st("GameEngine", "GameEngine");
		for (auto& b : Hotkeys())
		{
			const int vk = st.value("hk/" + b.id + "/vk", b.defVk).toInt();
			const int md = st.value("hk/" + b.id + "/mods", b.defMods).toInt();
			b.vk = vk;
			b.mods = QtMods(md);
		}
	}

	void SaveHotkeys()
	{
		QSettings st("GameEngine", "GameEngine");
		for (const auto& b : Hotkeys())
		{
			st.setValue("hk/" + b.id + "/vk", b.vk);
			st.setValue("hk/" + b.id + "/mods", b.mods);
		}
		st.setValue("ui/grid", st.value("ui/grid", true));
	}

	void ResetHotkeys()
	{
		for (auto& b : Hotkeys())
		{
			b.vk = b.defVk;
			b.mods = b.defMods;
		}
	}

	int MatchHotkey(int key, int mods)
	{
		const int vk = KeyToVk(key);
		if (!vk)
			return -1;
		const int m = QtMods(mods);
		for (const auto& b : Hotkeys())
			if (b.vk == vk && b.mods == m)
				return static_cast<int>(b.cmd);
		return -1;
	}

	QString HotkeyText(int vk, int mods)
	{
		if (vk <= 0)
			return {};
		QString s;
		if (mods & (int)Qt::ControlModifier) s += "Ctrl+";
		if (mods & (int)Qt::AltModifier)     s += "Alt+";
		if (mods & (int)Qt::ShiftModifier)   s += "Shift+";
		// 键名
		if (vk >= '0' && vk <= '9') { s += QChar('0' + (vk - '0')); return s; }
		if (vk >= 'A' && vk <= 'Z') { s += QChar('A' + (vk - 'A')); return s; }
		if (vk >= VK_F1 && vk <= VK_F12) { s += QString("F%1").arg(vk - VK_F1 + 1); return s; }
		switch (vk)
		{
			case VK_ESCAPE: s += "Esc"; break;
			case VK_RETURN: s += "Enter"; break;
			case VK_TAB:    s += "Tab"; break;
			case VK_BACK:   s += "Backspace"; break;
			case VK_SPACE:  s += "Space"; break;
			case VK_OEM_3:  s += "`"; break;
			case VK_OEM_MINUS: s += "-"; break;
			case VK_OEM_PLUS:  s += "="; break;
			case VK_OEM_4:  s += "["; break;
			case VK_OEM_6:  s += "]"; break;
			case VK_OEM_5:  s += "\\"; break;
			case VK_OEM_1:  s += ";"; break;
			case VK_OEM_7:  s += "'"; break;
			case VK_OEM_COMMA:  s += ","; break;
			case VK_OEM_PERIOD: s += "."; break;
			case VK_OEM_2:  s += "/"; break;
			default: s += "?"; break;
		}
		return s;
	}
}
