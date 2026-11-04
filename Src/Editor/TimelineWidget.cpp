#include "TimelineWidget.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace GameEditor
{
	namespace
	{
		const QColor kBg     = { 36, 36, 40 };
		const QColor kTick   = { 120, 120, 126 };
		const QColor kText   = { 170, 170, 176 };
		const QColor kLine   = { 226, 64, 64 };
		const int    kHeadH  = 14;   // 播放头三角高
	}

	TimelineWidget::TimelineWidget(QWidget* parent)
		: QWidget(parent)
	{
		setMouseTracking(true);
		setMinimumHeight(34);
	}

	void TimelineWidget::SetFrame(int frame)
	{
		m_frame = std::clamp(frame, 0, m_maxFrame);
		update();
	}

	void TimelineWidget::paintEvent(QPaintEvent*)
	{
		QPainter p(this);
		p.fillRect(rect(), kBg);

		const int left = 6, right = width() - 6;
		const int span = std::max(1, right - left);
		const int rulerY = height() - 1;

		// 刻度：每 10 帧一个长刻度 + 数字
		p.setPen(kTick);
		p.setFont(font());
		for (int f = 0; f <= m_maxFrame; f += 10)
		{
			const int x = left + span * f / m_maxFrame;
			p.drawLine(x, rulerY - 6, x, rulerY);
			if (f % 10 == 0)
			{
				p.setPen(kText);
				p.drawText(QRect(x - 14, rulerY - 20, 28, 13), Qt::AlignCenter,
					QString::number(f));
				p.setPen(kTick);
			}
		}

		// 红色播放头
		const int hx = left + span * m_frame / m_maxFrame;
		p.setPen(kLine);
		p.drawLine(hx, 2, hx, rulerY);
		QPolygonF head;
		head << QPointF(hx - 5, 2) << QPointF(hx + 5, 2) << QPointF(hx, 2 + kHeadH);
		p.setBrush(kLine);
		p.drawPolygon(head);
	}

	void TimelineWidget::mousePressEvent(QMouseEvent* e)
	{
		m_dragging = true;
		UpdateFromPos(e->pos().x());
	}

	void TimelineWidget::mouseMoveEvent(QMouseEvent* e)
	{
		if (m_dragging)
			UpdateFromPos(e->pos().x());
	}

	void TimelineWidget::UpdateFromPos(int x)
	{
		const int left = 6, right = width() - 6;
		const int span = std::max(1, right - left);
		const int f = std::clamp((x - left) * m_maxFrame / span, 0, m_maxFrame);
		if (f != m_frame)
		{
			m_frame = f;
			update();
			emit FrameChanged(f);
		}
	}
}
