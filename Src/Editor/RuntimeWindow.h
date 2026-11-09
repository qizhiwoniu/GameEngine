#pragma once
#include <QWidget>
#include <QJsonObject>

#include "Viewport3D.h"

class QShowEvent;
class QKeyEvent;

namespace GameEditor
{
	// 运行窗口：把编辑器里的场景复制一份，在自己的视口里跑起来。
	//   Scene   = 保留网格 / 坐标轴 / 调试信息（objects、time）
	//   Project = 隐藏网格等编辑器参数，只显示 FPS
	// 场景若在 2D 里搭了节点图，运行时由图驱动（键盘/累加/重力/镜头）；
	// 没有图就只跑纯重力。ESC 或关窗即退出。
	class RuntimeWindow : public QWidget
	{
		Q_OBJECT
	public:
		RuntimeWindow(Viewport3D* source, Viewport3D::RuntimeMode mode,
			const QString& title, const QJsonObject& graph, QWidget* parent = nullptr);

		Viewport3D::RuntimeMode Mode() const { return m_mode; }

		// Player ▸ Refresh 前更新节点图（运行期间可能改过 2D 图）
		void SetGraph(const QJsonObject& graph) { m_graph = graph; }
		void Refresh();

		// 当前关卡文件（.yu）：跳转精灵没写目标时用它算"下一关"
		void SetLevelPath(const QString& path) { m_levelPath = path; }

	protected:
		void showEvent(QShowEvent* e) override;
		void keyPressEvent(QKeyEvent* e) override;

	private:
		// 切关卡：target 空 = 自动下一关
		void SwitchLevel(const QString& target);
		QString ResolveLevel(const QString& target) const;

		Viewport3D*             m_source = nullptr;   // 编辑器里的源视口（复制场景用）
		Viewport3D*             m_view = nullptr;
		Viewport3D::RuntimeMode m_mode = Viewport3D::RuntimeMode::None;
		QJsonObject             m_graph;              // 运行时执行的 2D 节点图
		QString                 m_levelPath;          // 当前关卡文件（切关卡用）
	};
}
