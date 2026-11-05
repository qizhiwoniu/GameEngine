#pragma once
#include <QWidget>

namespace GameEditor
{
	// 动画时间轴：刻度标尺 + 红色播放头；点击 / 拖动跳帧
	class TimelineWidget : public QWidget
	{
		Q_OBJECT
	public:
		explicit TimelineWidget(QWidget* parent = nullptr);

		int  Frame() const { return m_frame; }
		void SetFrame(int frame);       // 不发 frameChanged（程序设值）
		void SetRange(int maxFrame) { m_maxFrame = maxFrame; update(); }

	signals:
		void FrameChanged(int frame);   // 用户点击 / 拖动时发出

	protected:
		void paintEvent(QPaintEvent*) override;
		void mousePressEvent(QMouseEvent*) override;
		void mouseMoveEvent(QMouseEvent*) override;

	private:
		void UpdateFromPos(int x);

		int m_frame = 0;
		int m_maxFrame = 100;
		bool m_dragging = false;
	};
}
