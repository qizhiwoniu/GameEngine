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
	namespace
	{
		// 键盘布局：宽单位 1 = 标准键；vk -1 = 空位；vk 0 = 不可指派键
		struct KeyDef { const wchar_t* cap; int vk; float w; };
		#define ROW_END { nullptr, -2, 0 }
		const KeyDef kLayout[] = {
			// 功能键行
			{ L"Esc", VK_ESCAPE, 1 }, { L"", -1, 1 },
			{ L"F1", VK_F1, 1 }, { L"F2", VK_F2, 1 }, { L"F3", VK_F3, 1 }, { L"F4", VK_F4, 1 }, { L"", -1, 0.5f },
			{ L"F5", VK_F5, 1 }, { L"F6", VK_F6, 1 }, { L"F7", VK_F7, 1 }, { L"F8", VK_F8, 1 }, { L"", -1, 0.5f },
			{ L"F9", VK_F9, 1 }, { L"F10", VK_F10, 1 }, { L"F11", VK_F11, 1 }, { L"F12", VK_F12, 1 },
			ROW_END,
			// 数字行
			{ L"`", VK_OEM_3, 1 }, { L"1", '1', 1 }, { L"2", '2', 1 }, { L"3", '3', 1 }, { L"4", '4', 1 },
			{ L"5", '5', 1 }, { L"6", '6', 1 }, { L"7", '7', 1 }, { L"8", '8', 1 }, { L"9", '9', 1 },
			{ L"0", '0', 1 }, { L"-", VK_OEM_MINUS, 1 }, { L"=", VK_OEM_PLUS, 1 }, { L"Backspace", VK_BACK, 2 },
			ROW_END,
			// Q 行
			{ L"Tab", VK_TAB, 1.5f },
			{ L"Q", 'Q', 1 }, { L"W", 'W', 1 }, { L"E", 'E', 1 }, { L"R", 'R', 1 }, { L"T", 'T', 1 },
			{ L"Y", 'Y', 1 }, { L"U", 'U', 1 }, { L"I", 'I', 1 }, { L"O", 'O', 1 }, { L"P", 'P', 1 },
			{ L"[", VK_OEM_4, 1 }, { L"]", VK_OEM_6, 1 }, { L"\\", VK_OEM_5, 1.5f },
			ROW_END,
			// A 行
			{ L"Caps", VK_CAPITAL, 1.75f },
			{ L"A", 'A', 1 }, { L"S", 'S', 1 }, { L"D", 'D', 1 }, { L"F", 'F', 1 }, { L"G", 'G', 1 },
			{ L"H", 'H', 1 }, { L"J", 'J', 1 }, { L"K", 'K', 1 }, { L"L", 'L', 1 },
			{ L";", VK_OEM_1, 1 }, { L"'", VK_OEM_7, 1 }, { L"Enter", VK_RETURN, 2.25f },
			ROW_END,
			// Z 行
			{ L"Shift", VK_SHIFT, 2.25f },
			{ L"Z", 'Z', 1 }, { L"X", 'X', 1 }, { L"C", 'C', 1 }, { L"V", 'V', 1 }, { L"B", 'B', 1 },
			{ L"N", 'N', 1 }, { L"M", 'M', 1 },
			{ L",", VK_OEM_COMMA, 1 }, { L".", VK_OEM_PERIOD, 1 }, { L"/", VK_OEM_2, 1 }, { L"Shift", VK_SHIFT, 2.75f },
			ROW_END,
			// 底行
			{ L"Ctrl", VK_CONTROL, 1.5f }, { L"Win", 0, 1.5f }, { L"Alt", VK_MENU, 1.5f },
			{ L"Space", VK_SPACE, 5.5f },
			{ L"Alt", VK_MENU, 1.5f }, { L"Win", 0, 1.5f }, { L"Ctrl", VK_CONTROL, 1.5f },
			ROW_END,
		};
		const int kKeyCount = int(std::size(kLayout));

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
		setMinimumHeight(300);
	}

	void KeyboardView::BuildLayout()
	{
		m_keys.clear();
		const float U = std::min(width() / 15.f, height() / 6.5f);
		const float ox = (width() - U * 15.f) / 2.f, oy = 2.f;
		float x = 0, y = 0;
		int row = 0;
		for (int i = 0; i < kKeyCount; ++i)
		{
			const KeyDef& d = kLayout[i];
			if (!d.cap)          // 行结束
			{
				x = 0;
				y += 1.f;
				if (row == 0) y += 0.5f;   // 功能键行下多留半格
				++row;
				continue;
			}
			if (d.vk != -1)
			{
				KeyBox k;
				k.r = QRect(int(ox + x * U + 1.5f), int(oy + y * U + 1.5f),
					int(d.w * U - 3.f), int(U - 3.f));
				k.vk = d.vk;
				k.cap = QString::fromWCharArray(d.cap);
				m_keys.append(k);
			}
			x += d.w;
		}
		m_layW = width();
		m_layH = height();
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
		p.setRenderHint(QPainter::Antialiasing, false);
		p.fillRect(rect(), QColor(32, 32, 32));

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

			QColor fill(62, 62, 62), txt(255, 255, 255), edge(90, 90, 90);
			if (k.vk == 0) { fill = QColor(42, 42, 42); txt = QColor(140, 140, 140); }
			else if (bind) fill = QColor(28, 92, 150);
			else if (modOn) { fill = QColor(0, 120, 215); txt = Qt::white; }
			if (k.vk > 0 && i == m_hot)
				fill = fill.lighter(130);

			p.setBrush(fill);
			p.setPen(sel ? QPen(QColor(0, 160, 255), 2) : QPen(edge, 1));
			p.drawRoundedRect(k.r, 4, 4);
			p.setPen(txt);
			QFont f = font();
			f.setPointSize(8);
			p.setFont(f);
			p.drawText(k.r.adjusted(4, 2, -2, 0), Qt::AlignLeft | Qt::AlignTop, k.cap);
			if (bind)
			{
				p.drawText(k.r.adjusted(2, 0, -2, -3), Qt::AlignHCenter | Qt::AlignBottom,
					QFontMetrics(f).elidedText(bind->name, Qt::ElideRight, k.r.width() - 6));
			}
			if (otherMod && !bind)
			{
				p.setBrush(QColor(0, 120, 215));
				p.setPen(Qt::NoPen);
				p.drawEllipse(QPoint(k.r.right() - 8, k.r.top() + 8), 3, 3);
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
		resize(760, 620);

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
		m_kb->setMinimumHeight(310);
		connect(m_kb, &KeyboardView::SelectionChanged, this, [this] { UpdateInfo(); });
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
			"点击 Shift / Ctrl / Alt 可切换修饰键。"), m_page[0]);
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
