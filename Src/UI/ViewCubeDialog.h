#pragma once
#include <QDialog>

class QCheckBox;
class QLabel;
class QSlider;

namespace GameEditor
{
	// "ViewCube 设置"窗口：调右上角视图方块的大小与透明度，改动立即生效（无确定/取消）
	class ViewCubeDialog : public QDialog
	{
		Q_OBJECT
	public:
		explicit ViewCubeDialog(QWidget* parent = nullptr);

		// 外部（工具条 / 启动时读配置）同步到窗口，不触发 ParamsChanged
		void SetParams(int sizePx, int alphaPercent);

	signals:
		void ParamsChanged(int sizePx, int alphaPercent);   // sizePx = 0 表示跟随视口自动
		void ResetRequested();

	private:
		void Emit();
		void UpdateLabels();

		QCheckBox* m_auto = nullptr;
		QSlider*   m_size = nullptr;
		QSlider*   m_alpha = nullptr;
		QLabel*    m_sizeVal = nullptr;
		QLabel*    m_alphaVal = nullptr;
		bool       m_syncing = false;                       // 防止回写时信号成环
	};
}
