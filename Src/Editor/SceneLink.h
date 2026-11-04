#pragma once
// SceneLink - 把 3D 视口对象和 2D 节点图的「方块 / 平面」节点配成对（移植自 RAD 版 SceneLink.h）
//
//   * 3D 里新建对象   -> 2D 出现对应节点
//   * 2D 里新建方块/平面节点 -> 3D 出现对应对象
//   * 任一侧删除 -> 另一侧同步删除
//   * 任一侧改名 -> 另一侧同步改名
//   * 3D 里移动/旋转/缩放对象 -> 节点上的 5 个数值跟着变
//   * 3D 里选中对象 -> 2D 里同名节点被选中
//
// 坐标约定（本引擎 Y 轴向上）：节点数值 = (x, z, y, 绕 Y 旋转角度, 缩放)
// 方块：缩放 = 边长；平面：缩放 = 边长 / 5
// 由 EditorWindow 的 100ms 定时器调用 Sync。

namespace GameEditor
{
	class Viewport3D;
	class SceneGraph2D;

	namespace SceneLink
	{
		// 返回 true 表示这一轮新建了 3D 对象或 2D 节点
		bool Sync(Viewport3D* v3d, SceneGraph2D* v2d);
	}
}
