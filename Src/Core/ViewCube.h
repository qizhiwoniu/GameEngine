#pragma once
#include <QPointF>
#include <QSize>
#include <QVector3D>

class QPainter;

namespace GameEditor
{
	// 视口右上角的视图方块（同 3ds Max 的 ViewCube / AutoCAD 的 ViewCube）。
	//   - 立方体按 3×3×3 分块：6 个面中央、12 条棱中间、8 个角各是一块，点哪一块就从那个
	//     方向看（面 → 正视，棱 → 两面之间 45°，角 → 等轴测，共 26 个方向）
	//   - 悬停：整块高亮（角块同时亮三个面、棱块亮两个面）
	//   - 面上的字（上 / 下 / 前 / 后 / 左 / 右）贴在面上，跟着面一起转
	//   - 视口侧按住拖动 = 环绕相机（与右键拖动一致）
	//
	// 这个类只管"画"和"命中测试"，不碰相机：视口每帧把相机基向量喂进来，
	// 鼠标事件由视口转发，点中后由视口按 BlockToEyeDir() 的结果改 yaw / pitch。
	class ViewCube
	{
	public:
		// 一块：X / Y / Z 各取 −1、0、1（不能全 0）
		struct Block
		{
			int x = 0, y = 0, z = 0;
			bool Valid() const { return x || y || z; }
			bool operator==(const Block& o) const { return x == o.x && y == o.y && z == o.z; }
			bool operator!=(const Block& o) const { return !(*this == o); }
		};

		void SetViewportSize(const QSize& s);                                    // 算方块大小与右上角位置
		void SetCameraBasis(const QVector3D& right, const QVector3D& up,
			const QVector3D& eyeDir);                                            // 相机右 / 上 / 指向眼睛

		// 由"视图 ▸ ViewCube 设置"与工具条控制：尺寸固定像素（<=0 表示跟随视口自动），
		// 透明度 0..1
		void  SetScalePx(float px) { m_scalePx = px; }
		float ScalePx() const { return m_scalePx; }
		void  SetOpacity(float a) { m_opacity = a; }
		float Opacity() const { return m_opacity; }
		float CurrentScale() const { return m_scale; }                            // 当前实际半边长（像素）

		void  Paint(QPainter& p) const;
		Block HitTest(const QPointF& pos) const;                                 // 无效 = 没点到
		void  SetHover(const Block& b) { m_hover = b; }
		Block Hover() const { return m_hover; }

		// 某块对应的视线方向（世界空间，从目标指向眼睛，单位向量）
		static QVector3D BlockToEyeDir(const Block& b);

	private:
		QPointF Project(const QVector3D& dir) const;                             // 世界方向 -> 屏幕点
		float   Facing(const QVector3D& n) const;                                // 面的外法线朝向相机的程度
		void    DrawFace(QPainter& p, int index) const;

		QVector3D m_right{ 1, 0, 0 };
		QVector3D m_up{ 0, 1, 0 };
		QVector3D m_eye{ 0, 0, 1 };
		QPointF   m_center{ 0, 0 };
		float     m_scale = 34.f;                                                // 立方体半边长（像素）
		float     m_scalePx = 0.f;                                               // 用户指定半边长（0 = 自动）
		float     m_opacity = 1.f;                                               // 整体透明度 0..1
		Block     m_hover;
	};
}
