#pragma once
#include <QWidget>

#include "Viewport3D.h"

class QShowEvent;
class QKeyEvent;

namespace GameEditor
{
	// 运行窗口：把编辑器里的场景复制一份，在自己的视口里跑起来。
	//   Scene   = 保留网格 / 坐标轴 / 调试信息（objects、time）
	//   Project = 隐藏网格等编辑器参数，只显示 FPS
	// 运行内容由 Viewport3D::EnterRuntime 切换；ESC 或关窗即退出。
	class RuntimeWindow : public QWidget
	{
		Q_OBJECT
	public:
		RuntimeWindow(Viewport3D* source, Viewport3D::RuntimeMode mode,
			const QString& title, QWidget* parent = nullptr);

		Viewport3D::RuntimeMode Mode() const { return m_mode; }

		// Player ▸ Refresh：不关窗口，重新从编辑器场景载入并重置模拟
		void Refresh();

	protected:
		void showEvent(QShowEvent* e) override;
		void keyPressEvent(QKeyEvent* e) override;

	private:
		Viewport3D*             m_source = nullptr;   // 编辑器里的源视口（复制场景用）
		Viewport3D*             m_view = nullptr;
		Viewport3D::RuntimeMode m_mode = Viewport3D::RuntimeMode::None;
	};
}
