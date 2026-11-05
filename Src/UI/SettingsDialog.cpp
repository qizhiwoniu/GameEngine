#include "SettingsDialog.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSpinBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#define NOMINMAX
#include <windows.h>

namespace GameEditor
{
	// ==================== DarkTabBar ====================
	DarkTabBar::DarkTabBar(QWidget* parent)
		: QWidget(parent)
	{
		setFixedHeight(32);
	}

	void DarkTabBar::AddTab(const QString& title)
	{
		m_titles << title;
		m_rects.clear();   // 触发重排
		update();
	}

	void DarkTabBar::SetActive(int i)
	{
		m_active = i;
		update();
		emit Changed(i);
	}

	int DarkTabBar::HitTest(const QPointF& p)
	{
		QFont f = font();
		f.setPointSize(10);
		QFontMetrics fm(f);
		m_rects.clear();
		int x = 0;
		for (const QString& t : m_titles)
		{
			const int w = fm.horizontalAdvance(t) + 36;
			m_rects.append(QRect(x, 0, w, height()));
			x += w;
		}
		for (int i = 0; i < m_rects.size(); ++i)
			if (m_rects[i].contains(p.toPoint()))
				return i;
		return -1;
	}

	void DarkTabBar::paintEvent(QPaintEvent*)
	{
		QPainter p(this);
		p.fillRect(rect(), QColor(32, 32, 32));
		QFont f = font();
		f.setPointSize(10);
		p.setFont(f);
		const QFontMetrics fm(f);
		int x = 0;
		for (int i = 0; i < m_titles.size(); ++i)
		{
			const int w = fm.horizontalAdvance(m_titles[i]) + 36;
			const QRect r(x, 0, w, height());
			if (m_rects.size() != m_titles.size())
				m_rects.append(r);
			if (i == m_hot && i != m_active)
				p.fillRect(r, QColor(51, 51, 51));
			if (i == m_active)
				p.fillRect(r.left(), r.bottom() - 2, r.width(), 3, QColor(0, 120, 215));
			p.setPen(QColor(255, 255, 255));
			f.setBold(i == m_active);
			p.setFont(f);
			p.drawText(r, Qt::AlignCenter, m_titles[i]);
			f.setBold(false);
			x += w;
		}
		p.setPen(QColor(70, 70, 70));
		p.drawLine(0, height() - 1, width(), height() - 1);
	}

	void DarkTabBar::mouseMoveEvent(QMouseEvent* e)
	{
		const int h = HitTest(e->position());
		if (h != m_hot)
		{
			m_hot = h;
			update();
		}
	}

	void DarkTabBar::mousePressEvent(QMouseEvent* e)
	{
		const int h = HitTest(e->position());
		if (h >= 0 && h != m_active)
			SetActive(h);
	}

	void DarkTabBar::leaveEvent(QEvent*)
	{
		if (m_hot != -1)
		{
			m_hot = -1;
			update();
		}
	}

	// ==================== KeyboardView ====================
	// 视觉参考 Assets/Image/KEYBOARD-82Dark.png（754x211，已编译进 qrc）。
	// 下面的键位矩形是从参考图逐键实测出来的（键距 32px、键面 26x24、
	// 圆角 3px、上亮下暗渐变），运行时把参考图整体缩放铺底，
	// 再按同一缩放比例在键位上叠加"高亮按钮"：
	//   蓝色按钮 = 当前修饰键组合下绑定了快捷键（按钮中央显示命令名）
	//   琥珀色   = 点击 Shift/Ctrl/Alt 切换出的激活修饰键
	//   亮蓝描边 = 当前选中的键
	namespace
	{
		struct KeyDef { int x1, y1, x2, y2; int vk; const wchar_t* cap; };   // 参考图像素坐标（左上/右下）
		const int kImgW = 754, kImgH = 211;
		const KeyDef kKeys82[] = {
			// F 键行
			{ 14,20,39,43,   VK_ESCAPE,  L"Esc" },
			{ 78,20,103,43,  VK_F1,      L"F1" },
			{ 110,20,135,43, VK_F2,      L"F2" },
			{ 142,20,167,43, VK_F3,      L"F3" },
			{ 174,20,199,43, VK_F4,      L"F4" },
			{ 222,20,247,43, VK_F5,      L"F5" },
			{ 254,20,279,43, VK_F6,      L"F6" },
			{ 286,20,311,43, VK_F7,      L"F7" },
			{ 318,20,343,43, VK_F8,      L"F8" },
			{ 366,20,391,43, VK_F9,      L"F9" },
			{ 398,20,423,43, VK_F10,     L"F10" },
			{ 430,20,455,43, VK_F11,     L"F11" },
			{ 462,20,487,43, VK_F12,     L"F12" },
			{ 495,20,520,43, 0,          L"Del" },
			// 数字行
			{ 14,53,39,76,   VK_OEM_3,    L"`" },
			{ 46,53,71,76,   '1',         L"1" },
			{ 78,53,103,76,  '2',         L"2" },
			{ 110,53,135,76, '3',         L"3" },
			{ 142,53,167,76, '4',         L"4" },
			{ 174,53,199,76, '5',         L"5" },
			{ 206,53,231,76, '6',         L"6" },
			{ 238,53,263,76, '7',         L"7" },
			{ 270,53,295,76, '8',         L"8" },
			{ 302,53,327,76, '9',         L"9" },
			{ 334,53,359,76, '0',         L"0" },
			{ 366,53,391,76, VK_OEM_MINUS,L"-" },
			{ 398,53,423,76, VK_OEM_PLUS, L"=" },
			{ 430,53,487,76, VK_BACK,     L"Backspace" },
			{ 525,53,550,76, 0,           L"Home" },
			// Q 行
			{ 14,83,55,106,  VK_TAB,      L"Tab" },
			{ 62,83,87,106,  'Q',         L"Q" },
			{ 94,83,119,106, 'W',         L"W" },
			{ 126,83,151,106,'E',         L"E" },
			{ 158,83,183,106,'R',         L"R" },
			{ 190,83,215,106,'T',         L"T" },
			{ 222,83,247,106,'Y',         L"Y" },
			{ 254,83,279,106,'U',         L"U" },
			{ 286,83,311,106,'I',         L"I" },
			{ 318,83,343,106,'O',         L"O" },
			{ 350,83,375,106,'P',         L"P" },
			{ 382,83,407,106,VK_OEM_4,    L"[" },
			{ 414,83,439,106,VK_OEM_6,    L"]" },
			{ 446,83,487,106,VK_OEM_5,    L"\\" },
			{ 525,83,550,106,0,           L"End" },
			// A 行
			{ 14,113,71,136, VK_CAPITAL,  L"Caps" },
			{ 78,113,103,136,'A',         L"A" },
			{ 110,113,135,136,'S',        L"S" },
			{ 142,113,167,136,'D',        L"D" },
			{ 174,113,199,136,'F',        L"F" },
			{ 206,113,231,136,'G',        L"G" },
			{ 238,113,263,136,'H',        L"H" },
			{ 270,113,295,136,'J',        L"J" },
			{ 302,113,327,136,'K',        L"K" },
			{ 334,113,359,136,'L',        L"L" },
			{ 366,113,391,136,VK_OEM_1,   L";" },
			{ 398,113,423,136,VK_OEM_7,   L"'" },
			{ 430,113,487,136,VK_RETURN,  L"Enter" },
			{ 525,113,550,136,0,          L"PgUp" },
			// Z 行
			{ 14,143,87,166, VK_SHIFT,    L"Shift" },
			{ 94,143,119,166,'Z',         L"Z" },
			{ 126,143,151,166,'X',        L"X" },
			{ 158,143,183,166,'C',        L"C" },
			{ 190,143,215,166,'V',        L"V" },
			{ 222,143,247,166,'B',        L"B" },
			{ 254,143,279,166,'N',        L"N" },
			{ 286,143,311,166,'M',        L"M" },
			{ 318,143,343,166,VK_OEM_COMMA,  L"," },
			{ 350,143,375,166,VK_OEM_PERIOD, L"." },
			{ 382,143,407,166,VK_OEM_2,   L"/" },
			{ 414,143,487,166,VK_SHIFT,   L"Shift" },
			{ 493,143,518,166,0,          L"\x2191" },
			{ 525,143,550,166,0,          L"PgDn" },
			// 底行
			{ 14,173,55,196, VK_CONTROL,  L"Ctrl" },
			{ 62,173,87,196, 0,           L"Win" },
			{ 94,173,119,196,VK_MENU,     L"Alt" },
			{ 126,173,343,196,VK_SPACE,   L"Space" },
			{ 350,173,375,196,VK_MENU,    L"Alt" },
			{ 382,173,407,196,0,          L"Win" },
			{ 414,173,439,196,VK_CONTROL, L"Ctrl" },
			{ 461,173,486,196,0,          L"\x2190" },
			{ 493,173,518,196,0,          L"\x2193" },
			{ 525,173,550,196,0,          L"\x2192" },
		};
		const int kKey82Count = int(std::size(kKeys82));

		// 104 键布局（KEYBOARD-104Dark.png 实测，同一张 754x211 画布）：
		// 在 82 键基础上补 PrtSc/ScrLk/Pause、Ins/Home/PgUp/PgDn、方向键、小键盘。
		const KeyDef kKeys104[] = {
			// F 键行
			{ 14,20,39,43,   VK_ESCAPE,  L"Esc" },
			{ 78,20,103,43,  VK_F1,      L"F1" },
			{ 110,20,135,43, VK_F2,      L"F2" },
			{ 142,20,167,43, VK_F3,      L"F3" },
			{ 174,20,199,43, VK_F4,      L"F4" },
			{ 222,20,247,43, VK_F5,      L"F5" },
			{ 254,20,279,43, VK_F6,      L"F6" },
			{ 286,20,311,43, VK_F7,      L"F7" },
			{ 318,20,343,43, VK_F8,      L"F8" },
			{ 366,20,391,43, VK_F9,      L"F9" },
			{ 398,20,423,43, VK_F10,     L"F10" },
			{ 430,20,455,43, VK_F11,     L"F11" },
			{ 462,20,487,43, VK_F12,     L"F12" },
			{ 508,20,533,43, 0,          L"PrtSc" },
			{ 540,20,565,43, 0,          L"ScrLk" },
			{ 572,20,597,43, 0,          L"Pause" },
			// 数字行
			{ 14,53,39,76,   VK_OEM_3,    L"`" },
			{ 46,53,71,76,   '1',         L"1" },
			{ 78,53,103,76,  '2',         L"2" },
			{ 110,53,135,76, '3',         L"3" },
			{ 142,53,167,76, '4',         L"4" },
			{ 174,53,199,76, '5',         L"5" },
			{ 206,53,231,76, '6',         L"6" },
			{ 238,53,263,76, '7',         L"7" },
			{ 270,53,295,76, '8',         L"8" },
			{ 302,53,327,76, '9',         L"9" },
			{ 334,53,359,76, '0',         L"0" },
			{ 366,53,391,76, VK_OEM_MINUS,L"-" },
			{ 398,53,423,76, VK_OEM_PLUS, L"=" },
			{ 430,53,487,76, VK_BACK,     L"Backspace" },
			{ 508,53,533,76, 0,           L"Ins" },
			{ 540,53,565,76, 0,           L"Home" },
			{ 572,53,597,76, 0,           L"PgUp" },
			{ 618,53,643,76, 0,           L"Num" },
			{ 650,53,675,76, 0,           L"/" },
			{ 682,53,707,76, 0,           L"*" },
			{ 714,53,739,76, 0,           L"-" },
			// Q 行
			{ 14,83,55,106,  VK_TAB,      L"Tab" },
			{ 62,83,87,106,  'Q',         L"Q" },
			{ 94,83,119,106, 'W',         L"W" },
			{ 126,83,151,106,'E',         L"E" },
			{ 158,83,183,106,'R',         L"R" },
			{ 190,83,215,106,'T',         L"T" },
			{ 222,83,247,106,'Y',         L"Y" },
			{ 254,83,279,106,'U',         L"U" },
			{ 286,83,311,106,'I',         L"I" },
			{ 318,83,343,106,'O',         L"O" },
			{ 350,83,375,106,'P',         L"P" },
			{ 382,83,407,106,VK_OEM_4,    L"[" },
			{ 414,83,439,106,VK_OEM_6,    L"]" },
			{ 446,83,487,106,VK_OEM_5,    L"\\" },
			{ 508,83,533,106,0,           L"Del" },
			{ 540,83,565,106,0,           L"End" },
			{ 572,83,597,106,0,           L"PgDn" },
			{ 618,83,643,106,0,           L"7" },
			{ 650,83,675,106,0,           L"8" },
			{ 682,83,707,106,0,           L"9" },
			{ 714,83,739,136,0,           L"+" },
			// A 行
			{ 14,113,71,136, VK_CAPITAL,  L"Caps" },
			{ 78,113,103,136,'A',         L"A" },
			{ 110,113,135,136,'S',        L"S" },
			{ 142,113,167,136,'D',        L"D" },
			{ 174,113,199,136,'F',        L"F" },
			{ 206,113,231,136,'G',        L"G" },
			{ 238,113,263,136,'H',        L"H" },
			{ 270,113,295,136,'J',        L"J" },
			{ 302,113,327,136,'K',        L"K" },
			{ 334,113,359,136,'L',        L"L" },
			{ 366,113,391,136,VK_OEM_1,   L";" },
			{ 398,113,423,136,VK_OEM_7,   L"'" },
			{ 430,113,487,136,VK_RETURN,  L"Enter" },
			{ 618,113,643,136,0,          L"4" },
			{ 650,113,675,136,0,          L"5" },
			{ 682,113,707,136,0,          L"6" },
			// Z 行
			{ 14,143,87,166, VK_SHIFT,    L"Shift" },
			{ 94,143,119,166,'Z',         L"Z" },
			{ 126,143,151,166,'X',        L"X" },
			{ 158,143,183,166,'C',        L"C" },
			{ 190,143,215,166,'V',        L"V" },
			{ 222,143,247,166,'B',        L"B" },
			{ 254,143,279,166,'N',        L"N" },
			{ 286,143,311,166,'M',        L"M" },
			{ 318,143,343,166,VK_OEM_COMMA,  L"," },
			{ 350,143,375,166,VK_OEM_PERIOD, L"." },
			{ 382,143,407,166,VK_OEM_2,   L"/" },
			{ 414,143,487,166,VK_SHIFT,   L"Shift" },
			{ 540,143,565,166,0,          L"\x2191" },
			{ 618,143,643,166,0,          L"1" },
			{ 650,143,675,166,0,          L"2" },
			{ 682,143,707,166,0,          L"3" },
			{ 714,143,739,196,0,          L"Enter" },
			// 底行
			{ 14,173,55,196, VK_CONTROL,  L"Ctrl" },
			{ 62,173,87,196, 0,           L"Win" },
			{ 94,173,119,196,VK_MENU,     L"Alt" },
			{ 126,173,343,196,VK_SPACE,   L"Space" },
			{ 350,173,375,196,VK_MENU,    L"Alt" },
			{ 382,173,407,196,0,          L"Win" },
			{ 414,173,439,196,0,          L"Menu" },
			{ 446,173,487,196,VK_CONTROL, L"Ctrl" },
			{ 508,173,533,196,0,          L"\x2190" },
			{ 540,173,565,196,0,          L"\x2193" },
			{ 572,173,597,196,0,          L"\x2192" },
			{ 618,173,675,196,0,          L"0" },
			{ 682,173,707,196,0,          L"." },
		};
		const int kKey104Count = int(std::size(kKeys104));

		// 选中布局表
		inline const KeyDef* KeyTable(int variant) { return variant == 1 ? kKeys104 : kKeys82; }
		inline int KeyCount(int variant) { return variant == 1 ? kKey104Count : kKey82Count; }

		int ModBitOf(int vk)
		{
			if (vk == VK_SHIFT)   return (int)Qt::ShiftModifier;
			if (vk == VK_CONTROL) return (int)Qt::ControlModifier;
			if (vk == VK_MENU)    return (int)Qt::AltModifier;
			return 0;
		}
	}

	KeyboardView::KeyboardView(QWidget* parent)
		: QWidget(parent)
	{
		setMouseTracking(true);
		setMinimumHeight(240);
		// 排障/验收辅助：GE_KB_104=1 时默认显示 104 键布局
		m_var = qEnvironmentVariableIsSet("GE_KB_104") ? 1 : 0;
		m_bg.load(QStringLiteral(":/images/keyboard82dark.png"));
		m_bg104.load(QStringLiteral(":/images/keyboard104dark.png"));
	}

	void KeyboardView::BuildLayout()
	{
		m_keys.clear();
		// 与参考图等比缩放并水平垂直居中
		const float s = std::min(width() / float(kImgW), height() / float(kImgH));
		const float ox = (width() - kImgW * s) / 2.f, oy = (height() - kImgH * s) / 2.f;
		m_scale = s; m_ox = ox; m_oy = oy;
		const KeyDef* tab = KeyTable(m_var);
		const int n = KeyCount(m_var);
		for (int i = 0; i < n; ++i)
		{
			const KeyDef& d = tab[i];
			KeyBox k;
			k.r = QRect(int(ox + d.x1 * s) + 1, int(oy + d.y1 * s) + 1,
				int((d.x2 - d.x1 + 1) * s) - 2, int((d.y2 - d.y1 + 1) * s) - 2);
			k.vk = d.vk;
			k.cap = QString::fromWCharArray(d.cap);
			m_keys.append(k);
		}
		m_layW = width(); m_layH = height();
	}

	int KeyboardView::HitTest(const QPointF& p)
	{
		if (m_layW != width() || m_layH != height() || m_keys.isEmpty())
			BuildLayout();
		for (int i = m_keys.size() - 1; i >= 0; --i)
			if (m_keys[i].r.contains(p.toPoint()))
				return i;
		return -1;
	}

	void KeyboardView::paintEvent(QPaintEvent*)
	{
		if (m_layW != width() || m_layH != height() || m_keys.isEmpty())
			BuildLayout();
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.fillRect(rect(), QColor(22, 22, 22));   // 参考图底色

		const QImage& bg = (m_var == 1) ? m_bg104 : m_bg;
		const bool hasImg = !bg.isNull();
		if (hasImg)
			p.drawImage(QRectF(m_ox, m_oy, kImgW * m_scale, kImgH * m_scale), bg);

		QFont f = font();
		f.setPointSize(8);
		p.setFont(f);
		const int rad = std::max(2, int(3 * m_scale));

		for (int i = 0; i < m_keys.size(); ++i)
		{
			const KeyBox& k = m_keys[i];
			const int modBit = ModBitOf(k.vk);
			const bool isMod = modBit != 0;
			const bool modOn = isMod && (m_mods & modBit);
			// 当前修饰键组合下绑定到该键的命令
			const HotkeyBinding* bind = nullptr;
			bool otherMod = false;
			if (k.vk > 0 && !isMod)
			{
				for (const auto& b : Hotkeys())
				{
					if (b.vk != k.vk)
						continue;
					if (b.mods == m_mods)
						bind = &b;
					else
						otherMod = true;
				}
			}
			const bool sel = !isMod && k.vk > 0 && k.vk == m_selVk;
			const bool hot = k.vk > 0 && i == m_hot;

			// 没有参考图时退化为代码绘制键帽（保底可读）
			if (!hasImg)
			{
				QColor fill(43, 43, 43), txt(158, 158, 158), edge(56, 56, 56);
				if (k.vk == 0) { fill = QColor(35, 35, 35); txt = QColor(105, 105, 105); }
				QLinearGradient g(k.r.topLeft(), k.r.bottomLeft());
				g.setColorAt(0, fill.lighter(115));
				g.setColorAt(1, fill);
				p.setPen(QPen(edge, 1));
				p.setBrush(g);
				p.drawRoundedRect(k.r, rad, rad);
				p.setPen(txt);
				p.drawText(k.r.adjusted(4, 2, -2, 0), Qt::AlignLeft | Qt::AlignTop, k.cap);
			}

			if (bind)
			{
				// 绑定了快捷键：高亮按钮（蓝色渐变）+ 命令名
				QLinearGradient g(k.r.topLeft(), k.r.bottomLeft());
				g.setColorAt(0, QColor(64, 148, 240));
				g.setColorAt(1, QColor(24, 92, 168));
				p.setPen(QPen(QColor(90, 175, 255), 1));
				p.setBrush(g);
				p.drawRoundedRect(k.r, rad, rad);
				p.setPen(Qt::white);
				p.drawText(k.r.adjusted(3, 1, -1, 0), Qt::AlignLeft | Qt::AlignTop, k.cap);
				// 键面小，命令名用更小字号，尽量多显示几个字
				QFont nf = f;
				nf.setPointSizeF(std::max(6.5, f.pointSizeF() - 1.5));
				p.setFont(nf);
				p.drawText(k.r.adjusted(1, 0, -1, -2), Qt::AlignHCenter | Qt::AlignBottom,
					QFontMetrics(nf).elidedText(bind->name, Qt::ElideRight, k.r.width() - 4));
				p.setFont(f);
			}
			else if (modOn)
			{
				// 激活的修饰键：琥珀色高亮
				QLinearGradient g(k.r.topLeft(), k.r.bottomLeft());
				g.setColorAt(0, QColor(240, 176, 64));
				g.setColorAt(1, QColor(190, 126, 20));
				p.setPen(QPen(QColor(255, 205, 120), 1));
				p.setBrush(g);
				p.drawRoundedRect(k.r, rad, rad);
				p.setPen(QColor(30, 24, 8));
				p.drawText(k.r.adjusted(4, 2, -2, 0), Qt::AlignLeft | Qt::AlignTop, k.cap);
			}
			else if (!hasImg && k.vk == 0)
			{
				// 无图兜底：不可指派键的淡显已在上面画过
			}

			if (hot && k.vk > 0 && !bind && !modOn)
			{
				p.setPen(Qt::NoPen);
				p.setBrush(QColor(255, 255, 255, 36));
				p.drawRoundedRect(k.r, rad, rad);
			}
			if (sel)
			{
				p.setBrush(Qt::NoBrush);
				p.setPen(QPen(QColor(0, 170, 255), 2));
				p.drawRoundedRect(k.r.adjusted(1, 1, -1, -1), rad, rad);
			}
			if (otherMod && !bind)
			{
				// 该键绑定在别的修饰键组合上：右上角小圆点提示
				p.setBrush(QColor(0, 140, 255));
				p.setPen(Qt::NoPen);
				p.drawEllipse(QPoint(k.r.right() - 6, k.r.top() + 6), 3, 3);
			}
		}
	}

	void KeyboardView::mouseMoveEvent(QMouseEvent* e)
	{
		const int h = HitTest(e->position());
		if (h != m_hot)
		{
			m_hot = h;
			update();
		}
	}

	void KeyboardView::mousePressEvent(QMouseEvent* e)
	{
		const int h = HitTest(e->position());
		if (h < 0 || m_keys[h].vk <= 0)
			return;
		const int vk = m_keys[h].vk;
		const int bit = ModBitOf(vk);
		if (bit)
		{
			m_mods ^= bit;   // Shift/Ctrl/Alt 键切换修饰键状态
			update();
		}
		else
		{
			m_selVk = vk;
			update();
			emit SelectionChanged();
		}
	}

	void KeyboardView::leaveEvent(QEvent*)
	{
		if (m_hot != -1)
		{
			m_hot = -1;
			update();
		}
	}

	// ==================== SettingsDialog ====================
	SettingsDialog::SettingsDialog(QWidget* parent)
		: QDialog(parent)
	{
		setWindowTitle(tr("设置"));
		resize(860, 660);

		auto* root = new QVBoxLayout(this);
		root->setContentsMargins(10, 8, 10, 8);
		root->setSpacing(6);

		m_tabs = new DarkTabBar(this);
		m_tabs->AddTab(tr("快捷键"));
		m_tabs->AddTab(tr("常规"));
		m_tabs->AddTab(tr("视口"));
		connect(m_tabs, &DarkTabBar::Changed, this, &SettingsDialog::OnTabChanged);
		root->addWidget(m_tabs);

		// ---- 页 1：快捷键 ----
		m_page[0] = new QWidget(this);
		auto* v1 = new QVBoxLayout(m_page[0]);
		v1->setContentsMargins(0, 0, 0, 0);
		v1->setSpacing(6);

		m_kb = new KeyboardView(m_page[0]);
		m_kb->setMinimumHeight(248);
		connect(m_kb, &KeyboardView::SelectionChanged, this, [this] { UpdateInfo(); });

		// 键盘布局切换：82 键 / 104 键（两种参考图同尺寸，直接换底图与键位表）
		auto* kbRow = new QHBoxLayout;
		kbRow->addWidget(new QLabel(tr("键盘布局"), m_page[0]));
		auto* b82 = new QToolButton(m_page[0]);
		auto* b104 = new QToolButton(m_page[0]);
		b82->setText(tr("82 键"));
		b104->setText(tr("104 键"));
		b82->setCheckable(true);
		b104->setCheckable(true);
		b82->setChecked(m_kb->Variant() == 0);
		b104->setChecked(m_kb->Variant() == 1);
		b82->setStyleSheet("QToolButton:checked{background:#0a5cad;color:white;border:1px solid #2b7bd3;}");
		b104->setStyleSheet("QToolButton:checked{background:#0a5cad;color:white;border:1px solid #2b7bd3;}");
		connect(b82, &QToolButton::clicked, this, [this, b82, b104]
			{ b82->setChecked(true); b104->setChecked(false); m_kb->SetVariant(0); });
		connect(b104, &QToolButton::clicked, this, [this, b82, b104]
			{ b104->setChecked(true); b82->setChecked(false); m_kb->SetVariant(1); });
		kbRow->addWidget(b82);
		kbRow->addWidget(b104);
		kbRow->addStretch();
		v1->addLayout(kbRow);

		v1->addWidget(m_kb);
		m_info = new QLabel(m_page[0]);
		m_info->setStyleSheet("font-weight:bold;");
		v1->addWidget(m_info);

		auto* row = new QHBoxLayout;
		m_list = new QTreeWidget(m_page[0]);
		m_list->setColumnCount(2);
		m_list->setHeaderLabels({ tr("命令"), tr("快捷键") });
		m_list->setRootIsDecorated(false);
		m_list->setUniformRowHeights(true);
		m_list->header()->setStretchLastSection(true);
		m_list->setColumnWidth(0, 300);
		m_list->setFixedHeight(170);
		connect(m_list, &QTreeWidget::itemSelectionChanged, this, &SettingsDialog::OnListSelection);
		row->addWidget(m_list);

		auto* btnCol = new QVBoxLayout;
		auto* bAssign = new QPushButton(tr("指派"), m_page[0]);
		auto* bClear = new QPushButton(tr("清除"), m_page[0]);
		auto* bReset = new QPushButton(tr("恢复默认"), m_page[0]);
		connect(bAssign, &QPushButton::clicked, this, &SettingsDialog::OnAssign);
		connect(bClear, &QPushButton::clicked, this, &SettingsDialog::OnClear);
		connect(bReset, &QPushButton::clicked, this, &SettingsDialog::OnReset);
		btnCol->addWidget(bAssign);
		btnCol->addWidget(bClear);
		btnCol->addWidget(bReset);
		btnCol->addStretch();
		row->addLayout(btnCol);
		v1->addLayout(row);

		auto* hint = new QLabel(tr("提示：在列表中选择命令，点击键盘上的按键，再点“指派”。"
			"点击 Shift / Ctrl / Alt 可切换修饰键。蓝色键 = 已绑定快捷键，琥珀色 = 激活的修饰键。"), m_page[0]);
		hint->setWordWrap(true);
		hint->setStyleSheet("color:#a8a8ac;");
		v1->addWidget(hint);
		root->addWidget(m_page[0]);

		// ---- 页 2：常规 ----
		m_page[1] = new QWidget(this);
		auto* v2 = new QVBoxLayout(m_page[1]);
		m_cbAutoSave = new QCheckBox(tr("自动保存"), m_page[1]);
		v2->addWidget(m_cbAutoSave);
		auto* rowMin = new QHBoxLayout;
		rowMin->addWidget(new QLabel(tr("保存间隔(分钟)"), m_page[1]));
		m_edMinutes = new QSpinBox(m_page[1]);
		m_edMinutes->setRange(1, 999);
		rowMin->addWidget(m_edMinutes);
		rowMin->addStretch();
		v2->addLayout(rowMin);
		v2->addStretch();
		root->addWidget(m_page[1]);

		// ---- 页 3：视口 ----
		m_page[2] = new QWidget(this);
		auto* v3 = new QVBoxLayout(m_page[2]);
		m_cbGrid = new QCheckBox(tr("显示栅格"), m_page[2]);
		m_cbFps = new QCheckBox(tr("显示 FPS"), m_page[2]);
		v3->addWidget(m_cbGrid);
		v3->addWidget(m_cbFps);
		v3->addStretch();
		root->addWidget(m_page[2]);

		// ---- 底部 确定 / 取消 ----
		auto* btnRow = new QHBoxLayout;
		btnRow->addStretch();
		auto* bOk = new QPushButton(tr("确定"), this);
		auto* bCancel = new QPushButton(tr("取消"), this);
		bOk->setDefault(true);
		connect(bOk, &QPushButton::clicked, this, &SettingsDialog::OnAccept);
		connect(bCancel, &QPushButton::clicked, this, &QDialog::reject);
		btnRow->addWidget(bOk);
		btnRow->addWidget(bCancel);
		root->addLayout(btnRow);

		m_page[1]->hide();
		m_page[2]->hide();
		RefreshList(0);
	}

	void SettingsDialog::SetCurrent(bool autoSave, int minutes, bool grid, bool fps)
	{
		m_autoSave = autoSave;
		m_autoSaveMinutes = minutes;
		m_showGrid = grid;
		m_showFps = fps;
		m_cbAutoSave->setChecked(autoSave);
		m_edMinutes->setValue(minutes);
		m_cbGrid->setChecked(grid);
		m_cbFps->setChecked(fps);
	}

	void SettingsDialog::OnTabChanged(int i)
	{
		for (int k = 0; k < 3; ++k)
			m_page[k]->setVisible(k == i);
	}

	void SettingsDialog::RefreshList(int sel)
	{
		m_list->blockSignals(true);
		m_list->clear();
		for (const auto& b : Hotkeys())
		{
			auto* it = new QTreeWidgetItem(m_list);
			it->setText(0, b.name);
			it->setText(1, HotkeyText(b.vk, b.mods));
		}
		m_list->blockSignals(false);
		if (sel >= 0 && sel < m_list->topLevelItemCount())
			m_list->setCurrentItem(m_list->topLevelItem(sel));
		m_kb->update();
	}

	void SettingsDialog::OnListSelection()
	{
		auto* it = m_list->currentItem();
		if (!it)
			return;
		const int i = m_list->indexOfTopLevelItem(it);
		if (i < 0 || i >= int(Hotkeys().size()))
			return;
		const HotkeyBinding& b = Hotkeys()[i];
		if (b.vk > 0)
		{
			m_kb->SetMods(b.mods);
			m_kb->SetSelVk(b.vk);
		}
		UpdateInfo();
	}

	void SettingsDialog::UpdateInfo()
	{
		const int vk = m_kb->SelVk();
		if (vk <= 0)
		{
			m_info->clear();
			return;
		}
		QString who = tr("未指派");
		for (const auto& b : Hotkeys())
			if (b.vk == vk && b.mods == m_kb->Mods())
				who = b.name;
		m_info->setText(QString("%1 : %2").arg(HotkeyText(vk, m_kb->Mods()), who));
	}

	void SettingsDialog::OnAssign()
	{
		const int i = m_list->indexOfTopLevelItem(m_list->currentItem());
		if (i < 0 || i >= int(Hotkeys().size()) || m_kb->SelVk() <= 0)
			return;
		// 同一键组合只能指派给一个命令
		for (auto& b : Hotkeys())
			if (b.vk == m_kb->SelVk() && b.mods == m_kb->Mods())
			{
				b.vk = 0;
				b.mods = 0;
			}
		Hotkeys()[i].vk = m_kb->SelVk();
		Hotkeys()[i].mods = m_kb->Mods();
		RefreshList(i);
		UpdateInfo();
	}

	void SettingsDialog::OnClear()
	{
		const int i = m_list->indexOfTopLevelItem(m_list->currentItem());
		if (i < 0 || i >= int(Hotkeys().size()))
			return;
		Hotkeys()[i].vk = 0;
		Hotkeys()[i].mods = 0;
		RefreshList(i);
		UpdateInfo();
	}

	void SettingsDialog::OnReset()
	{
		ResetHotkeys();
		RefreshList(m_list->indexOfTopLevelItem(m_list->currentItem()));
		UpdateInfo();
	}

	void SettingsDialog::OnAccept()
	{
		m_autoSave = m_cbAutoSave->isChecked();
		m_autoSaveMinutes = m_edMinutes->value();
		m_showGrid = m_cbGrid->isChecked();
		m_showFps = m_cbFps->isChecked();
		SaveHotkeys();
		emit SettingsApplied();
		accept();
	}
}
