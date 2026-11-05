#include "ViewCubeDialog.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace GameEditor
{
	namespace
	{
		const char* kSliderQss =
			"QSlider::groove:horizontal { height:4px; background:#3a3a40; border-radius:2px; }"
			"QSlider::sub-page:horizontal { background:#5b8def; border-radius:2px; }"
			"QSlider::handle:horizontal { width:12px; margin:-5px 0; border-radius:6px;"
			"  background:#d8d8dc; }"
			"QSlider::handle:horizontal:hover { background:#ffffff; }";
	}

	ViewCubeDialog::ViewCubeDialog(QWidget* parent)
		: QDialog(parent)
	{
		setWindowTitle(tr("ViewCube 设置"));
		setMinimumWidth(360);

		auto* v = new QVBoxLayout(this);
		v->setContentsMargins(16, 14, 16, 12);
		v->setSpacing(12);

		auto* title = new QLabel(tr("右上角视图方块"));
		title->setStyleSheet("color:#e6e6ea; font-weight:bold; font-size:13px;");
		v->addWidget(title);

		// ---- 大小 / 透明度 ----
		auto* grid = new QGridLayout;
		grid->setHorizontalSpacing(10);
		grid->setVerticalSpacing(10);

		m_auto = new QCheckBox(tr("大小跟随视口自动"));
		m_auto->setChecked(true);
		m_auto->setStyleSheet("color:#d8d8dc;");
		grid->addWidget(m_auto, 0, 0, 1, 3);

		auto* labSize = new QLabel(tr("大小"));
		labSize->setStyleSheet("color:#c8c8ce;");
		m_size = new QSlider(Qt::Horizontal);
		m_size->setRange(12, 60);
		m_size->setValue(28);
		m_size->setStyleSheet(kSliderQss);
		m_size->setEnabled(false);
		m_sizeVal = new QLabel(tr("自动"));
		m_sizeVal->setMinimumWidth(52);
		m_sizeVal->setStyleSheet("color:#9fd8a0; font-weight:bold;");
		grid->addWidget(labSize, 1, 0);
		grid->addWidget(m_size, 1, 1);
		grid->addWidget(m_sizeVal, 1, 2);

		auto* labAlpha = new QLabel(tr("透明度"));
		labAlpha->setStyleSheet("color:#c8c8ce;");
		m_alpha = new QSlider(Qt::Horizontal);
		m_alpha->setRange(15, 100);
		m_alpha->setValue(100);
		m_alpha->setStyleSheet(kSliderQss);
		m_alphaVal = new QLabel(QStringLiteral("100%"));
		m_alphaVal->setMinimumWidth(52);
		m_alphaVal->setStyleSheet("color:#9fd8a0; font-weight:bold;");
		grid->addWidget(labAlpha, 2, 0);
		grid->addWidget(m_alpha, 2, 1);
		grid->addWidget(m_alphaVal, 2, 2);

		v->addLayout(grid);

		auto* hint = new QLabel(tr("提示：拖动即时生效；点击方块的 面 / 棱 / 角 可切换视角。"));
		hint->setWordWrap(true);
		hint->setStyleSheet("color:#8a8a92;");
		v->addWidget(hint);
		v->addStretch();

		// ---- 按钮 ----
		auto* row = new QHBoxLayout;
		auto* btnReset = new QPushButton(tr("恢复默认"));
		auto* btnClose = new QPushButton(tr("关闭"));
		btnClose->setDefault(true);
		row->addWidget(btnReset);
		row->addStretch();
		row->addWidget(btnClose);
		v->addLayout(row);

		connect(btnClose, &QPushButton::clicked, this, &QDialog::close);
		connect(btnReset, &QPushButton::clicked, this, [this]
			{
				m_syncing = true;
				m_auto->setChecked(true);
				m_size->setValue(28);
				m_alpha->setValue(100);
				m_syncing = false;
				UpdateLabels();
				emit ResetRequested();
				Emit();
			});

		auto onChange = [this] { UpdateLabels(); Emit(); };
		connect(m_auto, &QCheckBox::toggled, this, [this](bool on)
			{
				m_size->setEnabled(!on);
				UpdateLabels();
				Emit();
			});
		connect(m_size, &QSlider::valueChanged, this, onChange);
		connect(m_alpha, &QSlider::valueChanged, this, onChange);
	}

	void ViewCubeDialog::SetParams(int sizePx, int alphaPercent)
	{
		m_syncing = true;
		m_auto->setChecked(sizePx <= 0);
		m_size->setEnabled(sizePx > 0);
		if (sizePx > 0)
			m_size->setValue(sizePx);
		m_alpha->setValue(alphaPercent);
		m_syncing = false;
		UpdateLabels();
	}

	void ViewCubeDialog::UpdateLabels()
	{
		m_sizeVal->setText(m_auto->isChecked() ? tr("自动")
			: QStringLiteral("%1 px").arg(m_size->value()));
		m_alphaVal->setText(QStringLiteral("%1%").arg(m_alpha->value()));
	}

	void ViewCubeDialog::Emit()
	{
		if (m_syncing)
			return;
		emit ParamsChanged(m_auto->isChecked() ? 0 : m_size->value(), m_alpha->value());
	}
}
