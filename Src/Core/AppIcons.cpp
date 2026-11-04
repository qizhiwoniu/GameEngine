#include "AppIcons.h"
#include <QPainter>
#include <QPen>

namespace GameEditor
{
	QPixmap Make(Icon kind, const QColor& color, int logicalSize)
	{
		const int scale = 2;
		QPixmap pm(logicalSize * scale, logicalSize * scale);
		pm.setDevicePixelRatio(scale);
		pm.fill(Qt::transparent);

		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing);
		const double s = logicalSize / 16.0;
		p.scale(s, s);

		QPen pen(color, 1.5);
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);

		// 16x16 逻辑坐标
		switch (kind)
		{
		case Icon::NewDoc:
			p.drawRect(QRectF(3.5, 2.5, 8, 11));
			p.drawLine(QPointF(5.8, 5.5), QPointF(9.2, 5.5));
			p.drawLine(QPointF(5.8, 8), QPointF(9.2, 8));
			p.drawLine(QPointF(5.8, 10.5), QPointF(9.2, 10.5));
			break;
		case Icon::Open:
			p.drawPolyline(QPolygonF() << QPointF(2.5, 12.5) << QPointF(2.5, 4.5)
				<< QPointF(6, 4.5) << QPointF(7.5, 6) << QPointF(13.5, 6)
				<< QPointF(13.5, 12.5) << QPointF(2.5, 12.5));
			p.drawLine(QPointF(2.8, 12.5), QPointF(5.2, 8.5));
			p.drawLine(QPointF(5.2, 8.5), QPointF(14, 8.5));
			p.drawLine(QPointF(14, 8.5), QPointF(11.5, 12.5));
			break;
		case Icon::Save:
			p.drawPolygon(QPolygonF() << QPointF(3, 3) << QPointF(11, 3) << QPointF(13, 5)
				<< QPointF(13, 13) << QPointF(3, 13) << QPointF(3, 3));
			p.drawRect(QRectF(5.5, 3, 5, 3.5));
			p.drawRect(QRectF(5, 9, 6, 4));
			break;
		case Icon::Play:
			p.setBrush(color);
			p.drawPolygon(QPolygonF() << QPointF(5, 3.5) << QPointF(13, 8) << QPointF(5, 12.5));
			break;
		case Icon::Pause:
			p.setBrush(color);
			p.drawRect(QRectF(4.5, 3.5, 2.6, 9));
			p.drawRect(QRectF(8.9, 3.5, 2.6, 9));
			break;
		case Icon::Stop:
			p.setBrush(color);
			p.drawRect(QRectF(4.5, 4.5, 7, 7));
			break;
		case Icon::SkipStart:
			p.drawLine(QPointF(4, 3.5), QPointF(4, 12.5));
			p.setBrush(color);
			p.drawPolygon(QPolygonF() << QPointF(12, 3.5) << QPointF(5.5, 8) << QPointF(12, 12.5));
			break;
		case Icon::SkipEnd:
			p.drawLine(QPointF(12, 3.5), QPointF(12, 12.5));
			p.setBrush(color);
			p.drawPolygon(QPolygonF() << QPointF(4, 3.5) << QPointF(10.5, 8) << QPointF(4, 12.5));
			break;
		case Icon::Prev:
			p.setBrush(color);
			p.drawPolygon(QPolygonF() << QPointF(9, 4) << QPointF(4, 8) << QPointF(9, 12));
			p.drawLine(QPointF(11.5, 4), QPointF(11.5, 12));
			break;
		case Icon::Next:
			p.setBrush(color);
			p.drawPolygon(QPolygonF() << QPointF(7, 4) << QPointF(12, 8) << QPointF(7, 12));
			p.drawLine(QPointF(4.5, 4), QPointF(4.5, 12));
			break;
		case Icon::Record:
			p.setBrush(color);
			p.drawEllipse(QRectF(4, 4, 8, 8));
			break;
		case Icon::Loop:
			p.setBrush(color);
			p.drawPolygon(QPolygonF() << QPointF(8, 2.5) << QPointF(13.5, 8) << QPointF(8, 13.5) << QPointF(2.5, 8));
			break;
		case Icon::Magnet:
			p.drawArc(QRectF(4, 4, 8, 8), 0, 180 * 16);
			p.drawLine(QPointF(4, 8), QPointF(4, 12.5));
			p.drawLine(QPointF(12, 8), QPointF(12, 12.5));
			p.drawLine(QPointF(2.8, 10.5), QPointF(5.2, 10.5));
			p.drawLine(QPointF(10.8, 10.5), QPointF(13.2, 10.5));
			break;
		case Icon::Eye:
			p.drawEllipse(QRectF(2.5, 5, 11, 6));
			p.setBrush(color);
			p.drawEllipse(QRectF(6.5, 7, 3, 2.6));
			break;
		case Icon::Sun:
			p.drawEllipse(QRectF(5.5, 5.5, 5, 5));
			for (int i = 0; i < 8; ++i)
			{
				const double a = i * 3.14159265 / 4.0;
				p.drawLine(QPointF(8 + 4.2 * cos(a), 8 + 4.2 * sin(a)),
					QPointF(8 + 5.8 * cos(a), 8 + 5.8 * sin(a)));
			}
			break;
		case Icon::Lock:
			p.drawArc(QRectF(5, 3.5, 6, 6), 0, 180 * 16);
			p.drawLine(QPointF(5, 6.5), QPointF(5, 9));
			p.drawLine(QPointF(11, 6.5), QPointF(11, 9));
			p.drawRect(QRectF(4, 8.5, 8, 5.5));
			break;
		case Icon::Pin:
			p.drawLine(QPointF(8, 8), QPointF(8, 14));
			p.setBrush(color);
			p.drawEllipse(QRectF(5, 2.5, 6, 6));
			break;
		case Icon::Close:
			p.drawLine(QPointF(4.5, 4.5), QPointF(11.5, 11.5));
			p.drawLine(QPointF(11.5, 4.5), QPointF(4.5, 11.5));
			break;
		case Icon::Grid:
			p.drawRect(QRectF(2.5, 2.5, 11, 11));
			p.drawLine(QPointF(6.2, 2.5), QPointF(6.2, 13.5));
			p.drawLine(QPointF(9.8, 2.5), QPointF(9.8, 13.5));
			p.drawLine(QPointF(2.5, 6.2), QPointF(13.5, 6.2));
			p.drawLine(QPointF(2.5, 9.8), QPointF(13.5, 9.8));
			break;
		case Icon::Wire:
			p.drawPolygon(QPolygonF() << QPointF(8, 2.5) << QPointF(13.5, 5.5)
				<< QPointF(13.5, 10.5) << QPointF(8, 13.5)
				<< QPointF(2.5, 10.5) << QPointF(2.5, 5.5));
			p.drawLine(QPointF(8, 2.5), QPointF(8, 8));
			p.drawLine(QPointF(8, 8), QPointF(13.5, 10.5));
			p.drawLine(QPointF(8, 8), QPointF(2.5, 10.5));
			break;
		case Icon::Gauge:
			p.drawArc(QRectF(2.5, 4.5, 11, 11), 0, 180 * 16);
			p.drawLine(QPointF(8, 10), QPointF(11, 6.5));
			break;
		case Icon::Home:
			p.drawPolyline(QPolygonF() << QPointF(2.5, 8) << QPointF(8, 3)
				<< QPointF(13.5, 8));
			p.drawRect(QRectF(4.5, 8, 7, 5.5));
			break;
		}
		return pm;
	}
}
