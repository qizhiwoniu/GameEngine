#pragma once
#include <QColor>
#include <QPixmap>

namespace GameEditor
{
	// 工具条 / 面板图标全部用 QPainter 画：不依赖图标文件，颜色可跟随主题
	enum class Icon
	{
		NewDoc,     // 文档 +
		Open,       // 文件夹
		Save,       // 软盘
		Play,       // ▶
		Pause,      // ‖
		Stop,       // ■
		SkipStart,  // |◀
		SkipEnd,    // ▶|
		Prev,       // ◀|
		Next,       // |▶
		Record,     // ●（红）
		Loop,       // ◆（橙）
		Magnet,     // 磁铁
		Eye,        // 眼睛（图层可见）
		Sun,        // 太阳（光照）
		Lock,       // 锁
		Pin,        // 图钉（面板）
		Close,      // ×
		Grid,       // 栅格
		Wire,       // 线框立方体
		Gauge,      // 仪表（FPS）
		Home,       // 房子（重置视角）
	};

	QPixmap Make(Icon kind, const QColor& color, int logicalSize = 16);
}
