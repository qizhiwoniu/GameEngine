#include "ViewCube.h"
#include <QFont>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace GameEditor
{
	namespace
	{
		struct FaceDef
		{
			QVector3D   n;        // 外法线
			QVector3D   a;        // 从外面看：向右
			QVector3D   b;        // 从外面看：向上
			const char* label;
		};

		// 本引擎 Y 轴朝上、相机默认在 +Z 看向 −Z，所以 +Z 是"前"、+X 是"右"。
		// 每个面都满足 a × b = n（从外面看是右手系），字才不会镜像。
		const FaceDef kFaces[6] = {
			{ { 0,  1,  0 }, {  1, 0,  0 }, { 0, 0, -1 }, "上" },
			{ { 0, -1,  0 }, {  1, 0,  0 }, { 0, 0,  1 }, "下" },
			{ { 0,  0,  1 }, {  1, 0,  0 }, { 0, 1,  0 }, "前" },
			{ { 0,  0, -1 }, { -1, 0,  0 }, { 0, 1,  0 }, "后" },
			{ { 1,  0,  0 }, {  0, 0, -1 }, { 0, 1,  0 }, "右" },
			{ { -1, 0,  0 }, {  0, 0,  1 }, { 0, 1,  0 }, "左" },
		};

		constexpr float kSplit  = 0.6f;    // 3×3×3 的分块界线（立方体为 [−1, 1]³）
		constexpr float kMarginX = 30.f;   // 距视口右边（留宽一点，方块整体更靠左）
		constexpr float kMarginY = 16.f;   // 距视口上边

		float Dot(const QVector3D& a, const QVector3D& b) { return QVector3D::dotProduct(a, b); }
		int   Sign(float v) { return v > 0.25f ? 1 : (v < -0.25f ? -1 : 0); }

		// 分块在一个轴上的范围：−1 → [−1, −0.6]，0 → [−0.6, 0.6]，1 → [0.6, 1]
		void Range(int v, float& lo, float& hi)
		{
			lo = v < 0 ? -1.f : (v > 0 ? kSplit : -kSplit);
			hi = v < 0 ? -kSplit : (v > 0 ? 1.f : kSplit);
		}
	}

	void ViewCube::SetViewportSize(const QSize& s)
	{
		// 尺寸：用户在"ViewCube 设置"里指定了就用指定的（半边长像素），
		// 否则随视口大小自适应；位置贴右上角（右边留白比上边大，观感更居中）
		m_scale  = m_scalePx > 0.f ? m_scalePx
		                           : std::clamp(std::min(s.width(), s.height()) * 0.062f, 18.f, 28.f);
		m_center = QPointF(s.width() - kMarginX - m_scale, kMarginY + m_scale);
	}

	void ViewCube::SetCameraBasis(const QVector3D& right, const QVector3D& up, const QVector3D& eyeDir)
	{
		m_right = right.normalized();
		m_up    = up.normalized();
		m_eye   = eyeDir.normalized();
	}

	QPointF ViewCube::Project(const QVector3D& dir) const
	{
		// 正交投影：世界方向在相机右 / 上方向上的分量就是屏幕偏移（屏幕 y 朝下，取负）
		return { m_center.x() + Dot(dir, m_right) * m_scale,
		         m_center.y() - Dot(dir, m_up) * m_scale };
	}

	float ViewCube::Facing(const QVector3D& n) const
	{
		return Dot(n, m_eye);
	}

	QVector3D ViewCube::BlockToEyeDir(const Block& b)
	{
		return QVector3D(float(b.x), float(b.y), float(b.z)).normalized();
	}

	ViewCube::Block ViewCube::HitTest(const QPointF& pos) const
	{
		if (!m_right.lengthSquared() || m_scale <= 0.f)
			return {};

		// 可见面在屏幕上是平行四边形：pos − 面心 = s·A' + t·B'，解出面坐标 (s, t)
		for (const FaceDef& f : kFaces)
		{
			if (Facing(f.n) < 1e-3f)
				continue;
			const QPointF o  = Project(f.n);
			const QPointF pa = Project(f.n + f.a);
			const QPointF pb = Project(f.n + f.b);
			const double ax = pa.x() - o.x(), ay = pa.y() - o.y();
			const double bx = pb.x() - o.x(), by = pb.y() - o.y();
			const double det = ax * by - ay * bx;
			if (std::abs(det) < 1e-6)
				continue;
			const double px = pos.x() - o.x(), py = pos.y() - o.y();
			const double s = (px * by - py * bx) / det;
			const double t = (ax * py - ay * px) / det;
			if (std::abs(s) > 1.0 || std::abs(t) > 1.0)
				continue;
			// 面上的 3 × 3 分区 → 立方体的一块（面中央 / 棱中间 / 角）
			const int u = s > kSplit ? 1 : (s < -kSplit ? -1 : 0);
			const int v = t > kSplit ? 1 : (t < -kSplit ? -1 : 0);
			const QVector3D d = f.n + f.a * float(u) + f.b * float(v);
			Block hit;
			hit.x = Sign(d.x());
			hit.y = Sign(d.y());
			hit.z = Sign(d.z());
			return hit;
		}
		return {};
	}

	void ViewCube::Paint(QPainter& p) const
	{
		// 视口太小就别画了，免得压住场景
		if (m_center.x() - m_scale < 8.0 || m_center.y() - m_scale < 8.0)
			return;

		p.save();
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setOpacity(std::clamp(m_opacity, 0.f, 1.f));   // 整体透明度（面 / 线 / 字一起淡）

		// 背面先画（正交投影下可见面互不遮挡，顺序只影响边缘抗锯齿）
		int order[6] = { 0, 1, 2, 3, 4, 5 };
		std::sort(order, order + 6, [this](int lhs, int rhs) { return Facing(kFaces[lhs].n) < Facing(kFaces[rhs].n); });
		for (int i : order)
			DrawFace(p, i);

		p.restore();
	}

	void ViewCube::DrawFace(QPainter& p, int index) const
	{
		const FaceDef& f = kFaces[index];
		const float facing = Facing(f.n);
		if (facing < 0.02f)
			return;

		auto onFace = [&](float s, float t) { return Project(f.n + f.a * s + f.b * t); };
		const QPointF quad[4] = { onFace(-1, -1), onFace(1, -1), onFace(1, 1), onFace(-1, 1) };

		// 面的明暗跟着朝向走，才有立体感
		// 注意：facing=1（正对相机）时 150+70+40=260 会超出 255，QColor 变成无效色，
		// 面就画不出来（露出黑底）——所以每通道都要夹到 255 以内
		const int gray = int(150.f + 70.f * std::clamp(facing, 0.f, 1.f));
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(std::min(gray + 32, 255), std::min(gray + 34, 255), std::min(gray + 40, 255)));
		p.drawPolygon(quad, 4);

		// 悬停的那一块与这个面相交（块贴着这个面才画）：角块连带面上的那块区域一起亮
		if (m_hover.Valid() && Dot(QVector3D(float(m_hover.x), float(m_hover.y), float(m_hover.z)), f.n) > 0.5f)
		{
			const QVector3D h(float(m_hover.x), float(m_hover.y), float(m_hover.z));
			float s0 = 0.f, s1 = 0.f, t0 = 0.f, t1 = 0.f;
			Range(Sign(Dot(h, f.a)), s0, s1);
			Range(Sign(Dot(h, f.b)), t0, t1);
			const QPointF zone[4] = { onFace(s0, t0), onFace(s1, t0), onFace(s1, t1), onFace(s0, t1) };
			p.setBrush(QColor(255, 189, 74, 205));
			p.drawPolygon(zone, 4);
		}

		// 分块线：角、棱、面中央各是一块
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(120, 124, 134, 170), 1.0));
		for (float k : { -kSplit, kSplit })
		{
			p.drawLine(onFace(k, -1.f), onFace(k, 1.f));
			p.drawLine(onFace(-1.f, k), onFace(1.f, k));
		}
		p.setPen(QPen(QColor(56, 60, 68), 1.2));
		p.drawPolygon(quad, 4);

		// 面上的字：算出面的 A 轴在屏幕上的旋转角，直接在屏幕空间画，
		// 字就贴着面的朝向转（不再用面平面变换，避免坐标系乘出天文数字）
		if (facing < 0.22f)
			return;
		const QPointF c  = Project(f.n);
		const QPointF pa = Project(f.n + f.a);
		double angle = std::atan2(pa.y() - c.y(), pa.x() - c.x()) * 180.0 / 3.14159265358979323;
		if (std::cos(angle * 3.14159265358979323 / 180.0) < 0.0)
			angle += 180.0;                     // 面朝左时字不倒着

		QColor ink(34, 36, 42);
		ink.setAlphaF(std::clamp((facing - 0.22f) / 0.35f, 0.0f, 1.0f));

		QFont font = p.font();
		font.setFamily(QStringLiteral("Microsoft YaHei"));
		font.setPixelSize(std::max(9, int(m_scale * 0.56f)));
		p.save();
		p.translate(c);
		p.rotate(angle);
		p.setFont(font);
		p.setPen(ink);
		p.drawText(QRectF(-m_scale, -m_scale, m_scale * 2.0, m_scale * 2.0), Qt::AlignCenter,
			QString::fromUtf8(f.label));
		p.restore();
	}
}
