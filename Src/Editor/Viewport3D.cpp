#include "Viewport3D.h"
#include "D3D12Renderer.h"
#include "GraphRuntime.h"

#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QDateTime>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <algorithm>
#include <cmath>
#define NOMINMAX
#include <windows.h>

namespace GameEditor
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		// 拖拽中单帧位移超过这个像素数就当成"坐标跳变"（虫洞 / 边缘夹持 / 切窗口回来），
		// 这一帧不产生位移。正常鼠标一帧最多也就一两百像素。
		constexpr float kJumpPx = 300.f;

		QColor ScaleColor(const QColor& c, float f)
		{
			return QColor(int(std::clamp(c.red() * f, 0.f, 255.f)),
				int(std::clamp(c.green() * f, 0.f, 255.f)),
				int(std::clamp(c.blue() * f, 0.f, 255.f)));
		}

		QMatrix4x4 ObjRotation(const QVector3D& rotRad)
		{
			QMatrix4x4 m;
			m.rotate(float(qRadiansToDegrees(rotRad.z())), 0, 0, 1);
			m.rotate(float(qRadiansToDegrees(rotRad.y())), 0, 1, 0);
			m.rotate(float(qRadiansToDegrees(rotRad.x())), 1, 0, 0);
			return m;
		}

		void ObjCorners(const Viewport3D::Obj& o, QVector3D out[8])
		{
			const QVector3D h = o.size * 0.5f;
			const QMatrix4x4 R = ObjRotation(o.rot);
			const QVector3D loc[8] = {
				{ -h.x(), -h.y(), -h.z() }, { +h.x(), -h.y(), -h.z() },
				{ +h.x(), +h.y(), -h.z() }, { -h.x(), +h.y(), -h.z() },
				{ -h.x(), -h.y(), +h.z() }, { +h.x(), -h.y(), +h.z() },
				{ +h.x(), +h.y(), +h.z() }, { -h.x(), +h.y(), +h.z() },
			};
			for (int i = 0; i < 8; ++i)
				out[i] = o.pos + R * loc[i];
		}
	}

	// ================= 生命周期 =================
	Viewport3D::Viewport3D(QWidget* parent)
		: QWidget(parent)
	{
		setAttribute(Qt::WA_NativeWindow);
		// 注意：不能设 WA_PaintOnScreen！它会让整个顶层窗口退回"直接上屏"模式，
		// 兄弟控件（停靠面板等）将永远得不到绘制（白块，且 RedrawWindow 也救不回来）。
		//
		// 渲染路径：D3D12 渲染到离屏纹理 -> 回读成 QImage -> paintEvent 里
		// 用 QPainter 整片贴上去。全程不碰窗口 HWND、不建交换链，
		// 窗口合成完全交给 Qt，菜单栏/停靠面板/最小化还原都正常。
		// 因为 paintEvent 会把整个客户区都涂满，可以安全开 OpaquePaintEvent
		// 省掉一次背景擦除。
		setAttribute(Qt::WA_OpaquePaintEvent);
		setFocusPolicy(Qt::StrongFocus);
		setMouseTracking(true);

		m_r = new D3D12Renderer;

		// FPS 打开时才持续渲染（平时按需渲染）；页面切到 2D 节点图时暂停
		auto* timer = new QTimer(this);
		timer->setInterval(33);
		connect(timer, &QTimer::timeout, this, [this]
			{
				// 运行模式：固定步长推进物理并持续出帧
				if (m_runtime != RuntimeMode::None)
				{
					StepPhysics(1.f / 30.f);
					RenderNow();
					return;
				}
				if (m_showFps && isVisible())
					RenderNow();
			});
		timer->start();

		// 初始场景：Box001(红) / Box002(黄,选中) / Box003(蓝)
		Obj b1; b1.name = "Box001"; b1.color = QColor(196, 78, 122);
		b1.pos = { -1.5f, 0.45f, 0.2f }; b1.size = { 0.5f, 0.9f, 0.5f };
		Obj b2; b2.name = "Box002"; b2.color = QColor(206, 178, 70);
		Obj b3; b3.name = "Box003"; b3.color = QColor(96, 158, 224);
		b3.pos = { 1.5f, 0.3f, 0.5f }; b3.size = { 0.5f, 0.6f, 0.5f };
		m_objects = { b1, b2, b3 };
		for (Obj& o : m_objects)
		{
			o.uid = m_nextUid++;
			o.kind = 0;
		}
		SetSelected(1);
	}

	Viewport3D::~Viewport3D()
	{
		delete m_graph;
		delete m_r;
	}

	void Viewport3D::showEvent(QShowEvent*)
	{
		if (!m_initialized)
		{
			// D3D 初始化耗时数秒（设备/PSO/着色器编译）。同步做会把 UI 线程
			// 卡死在 showEvent 里，导致其余控件（停靠面板等）迟迟画不出来。
			// 先让事件循环把整个窗口画完，再初始化。
			QTimer::singleShot(0, this, [this]
				{
					if (m_initialized)
						return;
					// 排障开关：GAMEENGINE_NO_D3D=1 完全不初始化 D3D（只剩 Qt 绘制），
					// 用来区分"Qt 自身问题"和"D3D 渲染问题"。
					static const bool noD3D = qEnvironmentVariableIsSet("GAMEENGINE_NO_D3D");
					m_initialized = noD3D ? false : m_r->Init(reinterpret_cast<HWND>(winId()));
					if (m_initialized)
						RenderNow();
				});
		}
	}

	void Viewport3D::resizeEvent(QResizeEvent*)
	{
		if (m_initialized)
		{
			m_r->Resize(std::max(1, width()), std::max(1, height()));
			RenderNow();
		}
	}

	void Viewport3D::paintEvent(QPaintEvent*)
	{
		QPainter p(this);
		const QImage& f = m_r->Frame();
		if (!f.isNull())
		{
			if (f.size() == size())
				p.drawImage(0, 0, f);
			else
				p.drawImage(rect(), f, f.rect());   // resize 后一帧的兜底拉伸
		}
		else
		{
			p.fillRect(rect(), QColor(30, 30, 33));
		}
		DrawOverlay(p);
	}

	// ================= 相机 / 投影 =================
	QVector3D Viewport3D::EyePos() const
	{
		const float cp = std::cos(m_pitch), sp = std::sin(m_pitch);
		QVector3D dir(cp * std::sin(m_yaw), sp, cp * std::cos(m_yaw));
		return m_target + dir * m_dist;
	}

	void Viewport3D::UpdateMatrices()
	{
		// 正交俯视时 lookAt 的 up(0,1,0) 与视线几乎平行 → 矩阵退化，
		// 线框/网格会歪斜"对不上"。俯仰角很大时改用随 yaw 旋转的水平 up。
		QVector3D up(0, 1, 0);
		if (m_ortho && std::abs(m_pitch) > 1.35f)
			up = QVector3D(-std::sin(m_yaw), 0.f, -std::cos(m_yaw));

		m_view.setToIdentity();
		m_view.lookAt(EyePos(), m_target, up);

		m_proj.setToIdentity();
		const float aspect = std::max(1.f, float(width())) / std::max(1.f, float(height()));
		if (m_ortho)
		{
			const float halfH = m_dist * 0.55f;
			const float halfW = halfH * aspect;
			m_proj.ortho(-halfW, halfW, -halfH, halfH, 0.05f, 500.f);
		}
		else
		{
			m_proj.perspective(45.f, aspect, 0.05f, 500.f);
		}
		// GL 的 NDC z∈[-1,1] 映射到 D3D 的 [0,1]
		QMatrix4x4 fix;
		fix(2, 2) = 0.5f; fix(2, 3) = 0.5f;
		m_proj = fix * m_proj;

		m_vp = m_proj * m_view;
		m_invVp = m_vp.inverted();
	}

	bool Viewport3D::ProjectPoint(const QVector3D& w, QPointF& out, float* depth) const
	{
		const QVector4D c = m_vp * QVector4D(w, 1.f);
		if (c.w() <= 1e-5f)
			return false;
		out.setX((c.x() / c.w() * 0.5f + 0.5f) * width());
		out.setY((0.5f - c.y() / c.w() * 0.5f) * height());
		if (depth) *depth = c.z() / c.w();
		return true;
	}

	float Viewport3D::WorldPerPixel(float depth) const
	{
		const float h = std::max(1.f, float(height()));
		if (m_ortho)
			return 2.f * m_dist * 0.55f / h;
		return 2.f * std::max(0.05f, depth) * std::tan(22.5f * kPi / 180.f) / h;
	}

	QVector3D Viewport3D::MouseRayDir(const QPointF& p) const
	{
		const float nx = float(p.x()) / width() * 2.f - 1.f;
		const float ny = 1.f - float(p.y()) / height() * 2.f;
		const QVector4D p0 = m_invVp * QVector4D(nx, ny, 0.f, 1.f);
		const QVector4D p1 = m_invVp * QVector4D(nx, ny, 1.f, 1.f);
		QVector3D a(p0.x() / p0.w(), p0.y() / p0.w(), p0.z() / p0.w());
		QVector3D b(p1.x() / p1.w(), p1.y() / p1.w(), p1.z() / p1.w());
		return (b - a).normalized();
	}

	bool Viewport3D::RayObject(int index, const QVector3D& o, const QVector3D& d, float* t) const
	{
		const Obj& ob = m_objects[index];
		// 射线转到物体局部空间（旋转用转置逆变换），做 AABB slab 检测
		const QMatrix4x4 R = ObjRotation(ob.rot);
		const QVector3D ol = R.transposed() * (o - ob.pos);
		const QVector3D dl = R.transposed() * d;
		const QVector3D h = ob.size * 0.5f + QVector3D(0.02f, 0.02f, 0.02f);
		float tmin = -1e9f, tmax = 1e9f;
		for (int a = 0; a < 3; ++a)
		{
			if (std::abs(dl[a]) < 1e-8f)
			{
				if (std::abs(ol[a]) > h[a]) return false;
				continue;
			}
			float t1 = (-h[a] - ol[a]) / dl[a];
			float t2 = (h[a] - ol[a]) / dl[a];
			if (t1 > t2) std::swap(t1, t2);
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax) return false;
		}
		if (tmax < 0.f) return false;
		if (t) *t = tmin >= 0.f ? tmin : tmax;
		return true;
	}

	// ================= 几何收集 =================
	void Viewport3D::PushTri(const QVector3D& a, const QVector3D& b, const QVector3D& c, const QColor& col)
	{
		const float r = col.redF(), g = col.greenF(), bl = col.blueF(), al = col.alphaF();
		GpuVertex v[3] = { {a.x(),a.y(),a.z(), r,g,bl,al}, {b.x(),b.y(),b.z(), r,g,bl,al},
			{c.x(),c.y(),c.z(), r,g,bl,al} };
		m_r->DrawTris(v, 3);
	}

	void Viewport3D::PushLine(const QVector3D& a, const QVector3D& b, const QColor& col)
	{
		// 沿"相机视线方向"做统一偏移，避免与共面三角形深度打架。
		// 注意：不能用每端点各自"指向眼睛"的方向——正交模式下各点方向不同，
		// 屏幕投影偏移量不一致，共线的边会错开几个像素（T/L/O 视图线对不上）。
		// 沿视线轴平移在正交投影下屏幕位移恰好为 0，边线严格对齐。
		const QVector3D fwd = (m_target - EyePos()).normalized();
		const float eps = std::max(0.006f, m_dist * 0.0012f);
		const QVector3D off = fwd * eps;
		const float r = col.redF(), g = col.greenF(), bl = col.blueF(), al = col.alphaF();
		GpuVertex v[2] = { {a.x()+off.x(),a.y()+off.y(),a.z()+off.z(), r,g,bl,al},
			{b.x()+off.x(),b.y()+off.y(),b.z()+off.z(), r,g,bl,al} };
		m_r->DrawLines(v, 2);
	}

	void Viewport3D::PushLineNoOffset(const QVector3D& a, const QVector3D& b, const QColor& col)
	{
		// 不做视线偏移的连线：框选矩形在近裁剪面上绘制时要求投影位置分毫不差
		const float r = col.redF(), g = col.greenF(), bl = col.blueF(), al = col.alphaF();
		GpuVertex v[2] = { {a.x(),a.y(),a.z(), r,g,bl,al},
			{b.x(),b.y(),b.z(), r,g,bl,al} };
		m_r->DrawLines(v, 2);
	}

	void Viewport3D::PushBoxWire(const QVector3D corners[8], const QColor& col)
	{
		static const int e[12][2] = {
			{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
		for (auto& ed : e)
			PushLine(corners[ed[0]], corners[ed[1]], col);
	}

	void Viewport3D::PushBoxFaces(const QVector3D corners[8], const QColor& col)
	{
		static const int quads[6][4] = {
			{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{4,5,1,0},{3,2,6,7} };
		static const QVector3D normals[6] = {
			{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0} };
		for (int f = 0; f < 6; ++f)
		{
			// 视图空间法线决定明暗（简单方向光）
			QVector3D n = (m_view * QVector4D(normals[f], 0.f)).toVector3D().normalized();
			const float lum = 0.5f + 0.5f * std::max(0.f, n.y() * 0.7f - n.z() * 0.6f);
			const QColor c = ScaleColor(col, lum);
			PushTri(corners[quads[f][0]], corners[quads[f][1]], corners[quads[f][2]], c);
			PushTri(corners[quads[f][0]], corners[quads[f][2]], corners[quads[f][3]], c);
		}
	}

	void Viewport3D::PushGrid()
	{
		const QColor major(58, 58, 64), minor(44, 44, 50);

		// 正视 / 侧视（相机水平且正对坐标轴）时，地面网格侧棱对着相机只剩一条线，
		// 画面几乎全黑。与 3ds Max 一致：把网格立起来画在与视线垂直的坐标面上。
		const QVector3D fwd(std::sin(m_yaw), 0.f, std::cos(m_yaw));
		if (std::abs(m_pitch) < 1e-3f && std::min(std::abs(fwd.x()), std::abs(fwd.z())) < 1e-3f)
		{
			const QVector3D u(std::cos(m_yaw), 0.f, -std::sin(m_yaw));   // 与视线垂直的水平轴
			for (int i = -8; i <= 8; ++i)
			{
				const QColor& c = (i % 4 == 0) ? major : minor;
				PushLine(u * float(i) + QVector3D(0, -8, 0), u * float(i) + QVector3D(0, 8, 0), c);
				PushLine(QVector3D(0, float(i), 0) - u * 8.f, QVector3D(0, float(i), 0) + u * 8.f, c);
			}
			return;
		}

		for (int i = -8; i <= 8; ++i)
		{
			const QColor& c = (i % 4 == 0) ? major : minor;
			PushLine({ float(i), 0, -8 }, { float(i), 0, 8 }, c);
			PushLine({ -8, 0, float(i) }, { 8, 0, float(i) }, c);
		}
	}

	// 世界原点的坐标轴改成视口左下角的方向标（见 DrawAxisGizmo），这里不再画
	void Viewport3D::PushGizmo()
	{
		if (m_tool == Tool::Select || m_selection.empty())
			return;
		const QVector3D o = SelectionCenter();
		const QColor ax[3] = { QColor(224,64,64), QColor(96,200,96), QColor(80,140,230) };
		const float len = 1.35f;
		const QVector3D axes[3] = { {1,0,0},{0,1,0},{0,0,1} };

		if (m_tool == Tool::Move || m_tool == Tool::Scale)
		{
			for (int a = 0; a < 3; ++a)
			{
				const QVector3D tip = o + axes[a] * len;
				PushLine(o, tip, ax[a]);
				if (m_tool == Tool::Move)
				{
					// 圆锥箭头（10 段）
					const QVector3D u = std::abs(axes[a].y()) < 0.9f ? QVector3D(0,1,0) : QVector3D(1,0,0);
					const QVector3D v1 = QVector3D::crossProduct(axes[a], u).normalized();
					const QVector3D v2 = QVector3D::crossProduct(axes[a], v1).normalized();
					const QVector3D base = tip - axes[a] * 0.2f;
					for (int s = 0; s < 10; ++s)
					{
						const float a1 = s / 10.f * 2.f * kPi, a2 = (s + 1) / 10.f * 2.f * kPi;
						const QVector3D p1 = base + (v1 * std::cos(a1) + v2 * std::sin(a1)) * 0.07f;
						const QVector3D p2 = base + (v1 * std::cos(a2) + v2 * std::sin(a2)) * 0.07f;
						PushTri(tip, p1, p2, ax[a]);
					}
				}
				else
				{
					// 轴端小方块
					const QVector3D u = std::abs(axes[a].y()) < 0.9f ? QVector3D(0,1,0) : QVector3D(1,0,0);
					const QVector3D v1 = QVector3D::crossProduct(axes[a], u).normalized();
					const QVector3D v2 = QVector3D::crossProduct(axes[a], v1).normalized();
					const float r = 0.06f;
					QVector3D c[8];
					for (int k = 0; k < 8; ++k)
					{
						const float sx = (k & 1) ? r : -r, sy = (k & 2) ? r : -r, sz = (k & 4) ? r : -r;
						c[k] = tip + v1 * sx + v2 * sy + axes[a] * sz;
					}
					static const int qf[6][4] = {
						{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{4,5,1,0},{3,2,6,7} };
					for (auto& f : qf)
					{
						PushTri(c[f[0]], c[f[1]], c[f[2]], ax[a]);
						PushTri(c[f[0]], c[f[2]], c[f[3]], ax[a]);
					}
				}
			}
			if (m_tool == Tool::Scale)
			{
				// 中心手柄（均匀缩放）：小白色方块
				const float r = 0.08f;
				QVector3D c[8];
				for (int k = 0; k < 8; ++k)
				{
					const float sx = (k & 1) ? r : -r, sy = (k & 2) ? r : -r, sz = (k & 4) ? r : -r;
					c[k] = o + QVector3D(sx, sy, sz);
				}
				static const int qf[6][4] = {
					{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{4,5,1,0},{3,2,6,7} };
				for (auto& f : qf)
				{
					PushTri(c[f[0]], c[f[1]], c[f[2]], QColor(230, 230, 230));
					PushTri(c[f[0]], c[f[2]], c[f[3]], QColor(230, 230, 230));
				}
			}
		}
		else if (m_tool == Tool::Rotate)
		{
			const float R = 1.05f;
			for (int a = 0; a < 3; ++a)
			{
				const QVector3D u = std::abs(axes[a].y()) < 0.9f ? QVector3D(0,1,0) : QVector3D(1,0,0);
				const QVector3D v1 = QVector3D::crossProduct(axes[a], u).normalized();
				const QVector3D v2 = QVector3D::crossProduct(axes[a], v1).normalized();
				QVector3D prev = o + (v1 * R);
				for (int s = 1; s <= 48; ++s)
				{
					const float ang = s / 48.f * 2.f * kPi;
					const QVector3D p = o + (v1 * std::cos(ang) + v2 * std::sin(ang)) * R;
					PushLine(prev, p, ax[a]);
					prev = p;
				}
			}
		}
	}

	void Viewport3D::BuildFrame()
	{
		UpdateMatrices();
		if (!m_r->Ok())
			return;

		// 必须先 Begin（内部会清空顶点收集缓冲），再推入本帧几何
		m_r->Begin(m_view.constData(), m_proj.constData(), 0.118f, 0.118f, 0.133f);

		// 网格（Run Project 隐藏一切编辑器辅助线）
		if (m_showGrid)
			PushGrid();

		// 物体
		for (const Obj& o : m_objects)
		{
			if (!o.active)                  // 「运行输出」没接住的物体：不进运行画面
				continue;
			QVector3D corners[8];
			ObjCorners(o, corners);
			// 跳转精灵：编辑器里画成青色线框体积，运行时完全不可见（只是触发区）
			if (o.kind == 2)
			{
				if (m_runtime == RuntimeMode::None)
					PushBoxWire(corners, o.selected ? QColor(255, 214, 64) : QColor(80, 210, 200));
				continue;
			}
			if (m_wireFrame)
			{
				PushBoxWire(corners, o.color.lightness() > 0 ? o.color : QColor(200, 200, 200));
			}
			else
			{
				PushBoxFaces(corners, o.color);
				if (o.selected && m_runtime == RuntimeMode::None)
					PushBoxWire(corners, QColor(255, 214, 64));
			}
		}

		if (m_runtime == RuntimeMode::None)
			PushGizmo();

		// 框选矩形：把 4 个屏幕角点反投影到近裁剪面上连线（不偏移）。
		// 这些世界点的投影恰好回到原屏幕位置，透视/正交都精确；
		// 深度在所有物体之前，不会被交换链或几何盖住。
		// 从左向右（window 全包含选择）= 虚线；从右向左（crossing 碰到就选）= 实线。
		if (m_drag == DragMode::BoxSelect && m_bandActive)
		{
			const QPointF sc[4] = {
				{ m_bandStart.x(), m_bandStart.y() },
				{ m_bandEnd.x(),   m_bandStart.y() },
				{ m_bandEnd.x(),   m_bandEnd.y() },
				{ m_bandStart.x(), m_bandEnd.y() },
			};
			const bool dashed = m_bandEnd.x() >= m_bandStart.x();   // 左→右 = 虚线
			auto unproject = [this](const QPointF& s)
			{
				const float nx = float(s.x() * 2.f / std::max(1, width()) - 1.f);
				const float ny = float(1.f - s.y() * 2.f / std::max(1, height()));
				const QVector4D h = m_invVp * QVector4D(nx, ny, 0.001f, 1.f);
				return (h.w() > 1e-5f) ? (h.toVector3D() / h.w()) : QVector3D();
			};
			const QColor bandCol(235, 235, 240);
			for (int i = 0; i < 4; ++i)
			{
				const QPointF a = sc[i], b = sc[(i + 1) % 4];
				if (!dashed)
				{
					PushLineNoOffset(unproject(a), unproject(b), bandCol);
					continue;
				}
				// 屏幕空间按 6px 画 / 5px 空切成短段，每段各自反投影
				const float len = float(std::hypot(b.x() - a.x(), b.y() - a.y()));
				const int n = std::max(1, int(len / 11.f));
				for (int k = 0; k <= n; ++k)
				{
					const float t0 = std::clamp(k * 11.f, 0.f, len);
					const float t1 = std::clamp(k * 11.f + 6.f, 0.f, len);
					if (t1 - t0 < 0.5f)
						continue;
					const QPointF pa = a + (b - a) * (t0 / len);
					const QPointF pb = a + (b - a) * (t1 / len);
					PushLineNoOffset(unproject(pa), unproject(pb), bandCol);
				}
			}
		}

		// QMatrix4x4 为列主序存储；直接取 .data() 顺序传给着色器
		m_r->End();
	}

	void Viewport3D::RenderNow()
	{
		if (!m_initialized)
			return;
		// 排障开关：GAMEENGINE_NO_RENDER=1 时跳过实际渲染（区分初始化/渲染问题）
		static const bool noRender = qEnvironmentVariableIsSet("GAMEENGINE_NO_RENDER");
		if (noRender)
			return;
		BuildFrame();

		// FPS 统计（左上角状态文字用）
		++m_frames;
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (now - m_fpsClock >= 500)
		{
			m_fpsValue = int(m_frames * 1000.0 / (now - m_fpsClock));
			m_frames = 0;
			m_fpsClock = now;
		}

		// 帧已经在 m_r->Frame() 里了，请 Qt 把它画出来
		update();
	}

	// ================= 左上角状态文字 =================
	// 直接画在帧图上（paintEvent 里 QPainter 画完 QImage 之后接着画文字）。
	// 早期版本用过一个独立的 Qt::Tool 半透明顶层窗口，是因为 flip-model
	// 交换链会把窗口 DC 上的 GDI 绘制全盖掉；现在不呈现到窗口了，没必要多开
	// 一个 HWND —— 少一个窗口就少一处显隐不同步。
	void Viewport3D::DrawOverlay(QPainter& p)
	{
		if (!m_initialized)
			return;
		QFont f = p.font();
		f.setPointSizeF(9.5f);
		p.setFont(f);

		// 运行模式：Run Project 只留 FPS，Run Scene 额外显示调试信息
		if (m_runtime != RuntimeMode::None)
		{
			if (m_showFps)
			{
				p.setPen(QColor(159, 216, 160));
				p.drawText(QPoint(8, 20), QString("FPS: %1").arg(m_fpsValue));
			}
			if (m_runtime == RuntimeMode::Scene)
			{
				int shown = 0;
				for (const Obj& o : m_objects)
					if (o.active && o.kind != 2)
						++shown;
				p.setPen(QColor(198, 198, 204));
				p.drawText(QPoint(8, 40), QString("objects: %1%2").arg(shown)
					.arg(m_outputFiltered ? QStringLiteral("  (运行输出)") : QString()));
				p.drawText(QPoint(8, 58), QString("time: %1 s").arg(QString::number(double(m_simTime), 'f', 1)));
			}
			p.setPen(QColor(112, 112, 120));
			if (m_graph && m_graph->IsValid())
			{
				const QString cam = m_graph->HasCamera()
					? QStringLiteral("   相机跟随被驱动的方块") : QString();
				p.drawText(QPoint(8, std::max(20, height() - 10)),
					QStringLiteral("由 2D 节点图驱动   ESC 退出") + cam);
			}
			else if (m_runtime == RuntimeMode::Scene)
				p.drawText(QPoint(8, std::max(20, height() - 10)),
					QStringLiteral("右键旋转   中键平移   滚轮缩放   ESC 退出"));
			return;
		}

		const QColor normal(198, 198, 204), on(159, 216, 160), off(112, 112, 120);
		p.setPen(normal);
		p.drawText(QPoint(8, 20), QString("%1 + %2")
			.arg(m_ortho ? "orthographic" : "perspective")
			.arg(m_wireFrame ? "wireframe" : "stand"));

		const char* toolName = "select";
		switch (m_tool)
		{
			case Tool::Move:   toolName = "move";   break;
			case Tool::Rotate: toolName = "rotate"; break;
			case Tool::Scale:  toolName = "scale";  break;
			default: break;
		}
		p.setPen(normal);
		p.drawText(QPoint(8, 40), QLatin1String(toolName));

		const QFontMetrics fm(f);
		const int tw = fm.horizontalAdvance(QLatin1String(toolName));
		p.setPen(m_snap ? on : off);
		p.drawText(QPoint(8 + tw + 14, 40), m_snap ? "on" : "off");
		p.setPen(m_angleSnap ? on : off);
		p.drawText(QPoint(8 + tw + 14 + 58, 40), m_angleSnap ? "on" : "off");

		if (m_showFps)
		{
			p.setPen(on);
			p.drawText(QPoint(8, 60), QString("FPS: %1").arg(m_fpsValue));
		}

		DrawAxisGizmo(p);   // 左下角方向标
		DrawViewCube(p);    // 右上角视图方块
	}

	// ================= 右上角视图方块 =================
	// 用视图矩阵的前两行取相机的右 / 上方向，方块朝向就和视口里的场景完全一致
	// （正交俯视那种 up 退化的情形也一致，因为直接读的是矩阵而不是 yaw/pitch）。
	void Viewport3D::UpdateViewCubeBasis()
	{
		UpdateMatrices();
		const QVector3D right(m_view(0, 0), m_view(0, 1), m_view(0, 2));
		const QVector3D up(m_view(1, 0), m_view(1, 1), m_view(1, 2));
		const QVector3D eye(m_view(2, 0), m_view(2, 1), m_view(2, 2));   // 第三行 = −视线 = 指向眼睛
		m_viewCube.SetViewportSize(size());
		m_viewCube.SetCameraBasis(right, up, eye);
	}

	void Viewport3D::DrawViewCube(QPainter& p)
	{
		if (m_runtime != RuntimeMode::None)     // 运行时保持干净画面
			return;
		UpdateViewCubeBasis();
		m_viewCube.Paint(p);
	}

	void Viewport3D::ApplyViewCubePick(const ViewCube::Block& b)
	{
		const QVector3D d = ViewCube::BlockToEyeDir(b);
		// 世界方向 -> 轨道相机的 yaw / pitch（正上正下时方位角无意义，取 0）
		const float horiz = std::hypot(d.x(), d.z());
		m_yaw = horiz < 1e-4f ? 0.f : std::atan2(d.x(), d.z());
		m_pitch = std::clamp(std::asin(std::clamp(d.y(), -1.f, 1.f)), -1.5f, 1.5f);
		RenderNow();
	}

	// 左下角方向标：把世界 X/Y/Z 方向按当前相机旋转投到屏幕上，长度固定像素，
	// 不随平移/缩放变化，也不占用场景中心。
	void Viewport3D::DrawAxisGizmo(QPainter& p)
	{
		const float cx = 46.f;
		const float cy = float(height()) - 54.f;
		const float r = 24.f;
		if (cy < 24.f)          // 视口太矮就别画了，避免压住状态文字
			return;

		struct Ax { QVector3D dir; QColor col; const char* name; };
		const Ax axes[3] = {
			{ { 1, 0, 0 }, QColor(214, 64, 64),  "X" },
			{ { 0, 1, 0 }, QColor(96, 200, 96),  "Y" },
			{ { 0, 0, 1 }, QColor(80, 130, 230), "Z" },
		};

		// 视图空间里 z 越小越远，远的先画
		QVector3D view[3];
		for (int i = 0; i < 3; ++i)
			view[i] = m_view.mapVector(axes[i].dir);
		int order[3] = { 0, 1, 2 };
		std::sort(order, order + 3, [&](int a, int b) { return view[a].z() < view[b].z(); });

		QFont f = p.font();
		f.setPointSizeF(8.f);
		p.setFont(f);

		for (int k = 0; k < 3; ++k)
		{
			const int i = order[k];
			const QVector3D v = view[i];
			const QPointF tip(cx + v.x() * r, cy - v.y() * r);

			// 正对相机（几乎退化成点）的轴淡一点，免得三条糊在一起
			QColor c = axes[i].col;
			c.setAlphaF(0.30f + 0.70f * std::clamp(qAbs(v.z()), 0.f, 1.f));
			p.setPen(QPen(c, 2.f, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(QPointF(cx, cy), tip);

			p.setPen(c);
			p.drawText(QPointF(tip.x() + (v.x() >= 0.f ? 3.0 : -10.0),
				tip.y() + (v.y() >= 0.f ? -3.0 : 11.0)), QLatin1String(axes[i].name));
		}

		p.setPen(QColor(150, 150, 158));
		p.drawEllipse(QPointF(cx, cy), 1.8, 1.8);
	}


	// ================= 选中 / 工具 =================
	void Viewport3D::SyncSelectionFlags()
	{
		for (size_t i = 0; i < m_objects.size(); ++i)
			m_objects[i].selected = std::find(m_selection.begin(), m_selection.end(), int(i)) != m_selection.end();
	}

	void Viewport3D::EmitSelection()
	{
		SyncSelectionFlags();
		emit SelectionChanged(SelectedName());
		RenderNow();
	}

	void Viewport3D::SetSelected(int index)
	{
		m_selection.clear();
		if (index >= 0 && index < int(m_objects.size()))
			m_selection.push_back(index);
		EmitSelection();
	}

	QString Viewport3D::SelectedName() const
	{
		if (m_selection.empty())
			return {};
		const int i = m_selection.back();
		if (i < 0 || i >= int(m_objects.size()))
			return {};
		return QString::fromStdString(m_objects[i].name);
	}

	bool Viewport3D::SelectedGravity() const
	{
		if (m_selection.empty())
			return true;
		const int i = m_selection.back();
		if (i < 0 || i >= int(m_objects.size()))
			return true;
		return m_objects[i].gravity;
	}

	void Viewport3D::SetSelectedGravity(bool on)
	{
		for (int i : m_selection)
			if (i >= 0 && i < int(m_objects.size()))
				m_objects[i].gravity = on;
	}

	int Viewport3D::SelectedKind() const
	{
		if (m_selection.empty())
			return -1;
		const int i = m_selection.back();
		if (i < 0 || i >= int(m_objects.size()))
			return -1;
		return m_objects[i].kind;
	}

	QString Viewport3D::SelectedLevelTarget() const
	{
		if (m_selection.empty())
			return {};
		const int i = m_selection.back();
		if (i < 0 || i >= int(m_objects.size()) || m_objects[i].kind != 2)
			return {};
		return QString::fromStdString(m_objects[i].target);
	}

	void Viewport3D::SetSelectedLevelTarget(const QString& target)
	{
		for (int i : m_selection)
			if (i >= 0 && i < int(m_objects.size()) && m_objects[i].kind == 2)
				m_objects[i].target = target.toStdString();
	}

	// ================= 2D 节点配对用的对象 API =================
	int Viewport3D::IndexOfUid(int uid) const
	{
		for (int i = 0; i < int(m_objects.size()); ++i)
			if (m_objects[i].uid == uid)
				return i;
		return -1;
	}

	int Viewport3D::AddObjectRaw(const Obj& o)
	{
		Obj n = o;
		n.uid = m_nextUid++;
		m_objects.push_back(n);
		RenderNow();
		return n.uid;
	}

	bool Viewport3D::RemoveByUid(int uid)
	{
		const int idx = IndexOfUid(uid);
		if (idx < 0)
			return false;
		// 选中集合里大于该下标的索引要左移
		for (int k = int(m_selection.size()) - 1; k >= 0; --k)
		{
			if (m_selection[k] == idx)
				m_selection.erase(m_selection.begin() + k);
			else if (m_selection[k] > idx)
				--m_selection[k];
		}
		m_objects.erase(m_objects.begin() + idx);
		EmitSelection();
		return true;
	}

	void Viewport3D::SetObjectName(int uid, const std::string& name)
	{
		const int idx = IndexOfUid(uid);
		if (idx < 0)
			return;
		m_objects[idx].name = name;
		emit SelectionChanged(SelectedName());
	}

	void Viewport3D::SetObjectTarget(int uid, const std::string& target)
	{
		const int idx = IndexOfUid(uid);
		if (idx < 0)
			return;
		m_objects[idx].target = target;
	}

	void Viewport3D::SetObjectTransform(int uid, const QVector3D& pos, const QVector3D& size, float rotZRad)
	{
		const int idx = IndexOfUid(uid);
		if (idx < 0)
			return;
		Obj& o = m_objects[idx];
		o.pos = pos;
		o.size = size;
		o.rot.setZ(rotZRad);
		RenderNow();
	}

	std::vector<int> Viewport3D::SelectedUids() const
	{
		std::vector<int> out;
		for (int i : m_selection)
			if (i >= 0 && i < int(m_objects.size()))
				out.push_back(m_objects[i].uid);
		return out;
	}

	void Viewport3D::SelectByUids(const std::vector<int>& uids)
	{
		std::vector<int> sel;
		for (int uid : uids)
		{
			const int idx = IndexOfUid(uid);
			if (idx >= 0)
				sel.push_back(idx);
		}
		if (sel == m_selection)
			return;
		m_selection = sel;
		EmitSelection();
	}

	// ================= 文档存取（.yu） =================
	void Viewport3D::SaveState(QJsonObject& out) const
	{
		QJsonArray objs;
		for (const Obj& o : m_objects)
		{
			QJsonObject j;
			j["uid"] = o.uid;
			j["kind"] = o.kind;
			j["name"] = QString::fromStdString(o.name);
			j["color"] = o.color.name();
			QJsonArray pos = { double(o.pos.x()), double(o.pos.y()), double(o.pos.z()) };
			QJsonArray rot = { double(o.rot.x()), double(o.rot.y()), double(o.rot.z()) };
			QJsonArray siz = { double(o.size.x()), double(o.size.y()), double(o.size.z()) };
			j["pos"] = pos;
			j["rot"] = rot;
			j["size"] = siz;
			j["solid"] = o.solid;
			j["gravity"] = o.gravity;
			j["target"] = QString::fromStdString(o.target);   // 跳转精灵的目标关卡（其它物体为空）
			objs.append(j);
		}
		out["objects"] = objs;
		out["nextUid"] = m_nextUid;

		QJsonObject cam;
		cam["tx"] = double(m_target.x());
		cam["ty"] = double(m_target.y());
		cam["tz"] = double(m_target.z());
		cam["yaw"] = double(m_yaw);
		cam["pitch"] = double(m_pitch);
		cam["dist"] = double(m_dist);
		cam["ortho"] = m_ortho;
		cam["wire"] = m_wireFrame;
		out["camera"] = cam;
	}

	void Viewport3D::LoadState(const QJsonObject& in)
	{
		m_objects.clear();
		m_selection.clear();
		int maxUid = 0;
		const QJsonArray objs = in["objects"].toArray();
		for (const QJsonValue& v : objs)
		{
			const QJsonObject j = v.toObject();
			Obj o;
			o.uid = j["uid"].toInt();
			o.kind = j["kind"].toInt();
			o.name = j["name"].toString().toStdString();
			o.color = QColor(j["color"].toString());
			const QJsonArray pos = j["pos"].toArray();
			const QJsonArray rot = j["rot"].toArray();
			const QJsonArray siz = j["size"].toArray();
			if (pos.size() == 3)
				o.pos = QVector3D(float(pos.at(0).toDouble()), float(pos.at(1).toDouble()), float(pos.at(2).toDouble()));
			if (rot.size() == 3)
				o.rot = QVector3D(float(rot.at(0).toDouble()), float(rot.at(1).toDouble()), float(rot.at(2).toDouble()));
			if (siz.size() == 3)
				o.size = QVector3D(float(siz.at(0).toDouble()), float(siz.at(1).toDouble()), float(siz.at(2).toDouble()));
			o.solid = j["solid"].toBool(true);      // 旧文件没有该字段 = 默认参与碰撞
			o.gravity = j["gravity"].toBool(true);  // 旧文件没有该字段 = 默认受重力
			o.target = j["target"].toString().toStdString();
			maxUid = std::max(maxUid, o.uid);
			m_objects.push_back(o);
		}
		m_nextUid = std::max(in["nextUid"].toInt(maxUid + 1), maxUid + 1);

		const QJsonObject cam = in["camera"].toObject();
		if (!cam.isEmpty())
		{
			m_target = QVector3D(float(cam["tx"].toDouble(0)), float(cam["ty"].toDouble(0.4)), float(cam["tz"].toDouble(0)));
			m_yaw = float(cam["yaw"].toDouble(-0.55));
			m_pitch = float(cam["pitch"].toDouble(0.45));
			m_dist = float(cam["dist"].toDouble(8.0));
			m_ortho = cam["ortho"].toBool(false);
			m_wireFrame = cam["wire"].toBool(false);
		}
		UpdateMatrices();
		RenderNow();
	}

	QVector3D Viewport3D::SelectionCenter() const
	{
		if (m_selection.empty())
			return {};
		QVector3D c;
		for (int i : m_selection)
			if (i >= 0 && i < int(m_objects.size()))
				c += m_objects[i].pos;
		return c / float(m_selection.size());
	}

	void Viewport3D::AddBox()
	{
		Obj b;
		const int next = int(m_objects.size()) + 1;
		b.name = "Box" + std::string(3 - std::to_string(next).size(), '0') + std::to_string(next);
		static const QColor palette[] = { QColor(120,190,120), QColor(190,140,90),
			QColor(150,120,190), QColor(90,180,180), QColor(200,130,110) };
		b.color = palette[m_objects.size() % 5];
		auto& rng = *QRandomGenerator::global();
		b.pos.setX(float(rng.bounded(60) - 30) / 10.f);
		b.pos.setZ(float(rng.bounded(60) - 30) / 10.f);
		const float sy = 0.4f + float(rng.bounded(8)) / 10.f;
		b.size = { 0.8f, sy, 0.8f };
		b.pos.setY(sy * 0.5f);
		b.uid = m_nextUid++;
		b.kind = 0;
		m_objects.push_back(b);
		SetSelected(int(m_objects.size()) - 1);
	}

	void Viewport3D::AddPlane()
	{
		Obj p;
		p.name = "Plane" + std::to_string(m_objects.size() + 1);
		p.color = QColor(90, 100, 110);
		p.pos = { 0, 0.01f, 0 };
		p.size = { 4.f, 0.02f, 4.f };
		p.uid = m_nextUid++;
		p.kind = 1;
		m_objects.push_back(p);
		SetSelected(int(m_objects.size()) - 1);
	}

	// 跳转精灵：运行时不可见的触发体，任何方块碰到就切到目标关卡
	void Viewport3D::AddTrigger()
	{
		int n = 1;
		for (const Obj& o : m_objects)
			if (o.kind == 2)
				++n;
		Obj t;
		t.name = QStringLiteral("跳转精灵%1").arg(n).toStdString();
		t.color = QColor(80, 210, 200);
		t.pos = { 0, 0.75f, 0 };
		t.size = { 1.5f, 1.5f, 1.5f };
		t.uid = m_nextUid++;
		t.kind = 2;
		t.solid = false;                  // 不挡路、不参与碰撞
		t.gravity = false;
		m_objects.push_back(t);
		SetSelected(int(m_objects.size()) - 1);
		RenderNow();
	}

	void Viewport3D::DeleteSelected()
	{
		if (m_selection.empty())
			return;
		// 从大到小删除避免索引失效
		std::vector<int> del = m_selection;
		std::sort(del.rbegin(), del.rend());
		for (int i : del)
			if (i >= 0 && i < int(m_objects.size()))
				m_objects.erase(m_objects.begin() + i);
		m_selection.clear();
		EmitSelection();
	}

	void Viewport3D::SetView3D(bool on)
	{
		m_ortho = !on;
		m_pitch = on ? 0.45f : kPi * 0.5f;   // 正交俯视必须精确 90°，否则上下边错开
		if (!on)
			m_yaw = 0.f;
		RenderNow();
	}

	void Viewport3D::ResetCamera()
	{
		m_yaw = -0.55f; m_pitch = 0.45f; m_dist = 8.f;
		m_target = { 0, 0.4f, 0 };
		m_ortho = false;
		RenderNow();
	}

	void Viewport3D::SetShowFps(bool on)
	{
		m_showFps = on;
		if (on)
		{
			m_frames = 0;
			m_fpsClock = QDateTime::currentMSecsSinceEpoch();
		}
		RenderNow();
	}

	// ================= 运行模式 =================
	void Viewport3D::SetRuntimeGraph(const QJsonObject& graph)
	{
		if (!m_graph)
			m_graph = new GraphRuntime;
		m_graph->Load(graph);
	}

	void Viewport3D::EnterRuntime(RuntimeMode mode)
	{
		m_runtime = mode;
		m_selection.clear();
		for (Obj& o : m_objects)
			o.selected = false;
		m_tool = Tool::Select;
		m_snap = false;
		m_angleSnap = false;
		m_wireFrame = false;
		m_drag = DragMode::None;
		m_bandActive = false;

		m_showGrid = (mode == RuntimeMode::Scene);  // 整项目运行不显示网格
		m_showFps = true;

		m_vel.assign(m_objects.size(), QVector3D());
		m_keysDown.clear();
		m_simTime = 0.f;
		m_frames = 0;
		m_fpsClock = QDateTime::currentMSecsSinceEpoch();
		m_camFollowInit = false;                 // 镜头块第一帧直接对准注视点
		m_levelFired = false;                    // 本次运行还没触发过关
		if (m_graph && m_graph->IsValid())
		{
			m_graph->SnapshotSizes(m_objects);      // 缩放基准 = 进入运行时的尺寸
			m_graph->SeedFromObjects(m_objects);    // 起始位置 = 编辑器里摆的位置（不种就回世界原点）
		}
		ApplyOutputFilter();                     // 只显示接到「运行输出」的物体
		RenderNow();
	}

	// 运行输出白名单：有运行输出块接了线 -> 只有它的物体进运行场景；
	// 一个都没接（老场景）-> 全部照旧，不做过滤。跳转精灵始终保留（它本来就是触发区，不显示）。
	void Viewport3D::ApplyOutputFilter()
	{
		for (Obj& o : m_objects)
			o.active = true;

		m_outputFiltered = (m_graph && m_graph->IsValid() && m_graph->HasOutputFilter());
		if (!m_outputFiltered)
			return;
		const QSet<int> vis = m_graph->OutputUids();
		for (Obj& o : m_objects)
			if (o.kind != 2)
				o.active = vis.contains(o.uid);
	}

	// AABB 穿透深度：返回最小穿透轴 0=x 1=y 2=z（pen>0），不重叠返回 -1
	static int AabbPenetration(const QVector3D& pa, const QVector3D& sa,
		const QVector3D& pb, const QVector3D& sb, float* pen)
	{
		const float dx = pb.x() - pa.x(), dy = pb.y() - pa.y(), dz = pb.z() - pa.z();
		const float px = (sa.x() + sb.x()) * 0.5f - std::abs(dx);
		const float py = (sa.y() + sb.y()) * 0.5f - std::abs(dy);
		const float pz = (sa.z() + sb.z()) * 0.5f - std::abs(dz);
		if (px <= 0.f || py <= 0.f || pz <= 0.f)
			return -1;
		if (py <= px && py <= pz) { *pen = py; return 1; }
		if (px <= pz)             { *pen = px; return 0; }
		*pen = pz;  return 2;
	}

	// 沿单轴移动 amt，遇到 solid 方块 / 平面就贴面停住：
	// 只挡"迎面撞上"的情况（另一轴必须真的重叠），所以站在顶面上可以自由平移，
	// 也不会像"最小穿透轴"那样被从侧面弹飞。返回 true = 该轴被挡住。
	// hitDown：Y 轴向下被挡住（= 落地 / 踩到东西）
	static bool SweepAxis(QVector3D& pos, const QVector3D& size, int axis, float amt,
		const std::vector<Viewport3D::Obj>& objs, size_t self, bool* hitDown)
	{
		if (std::abs(amt) < 1e-6f)
			return false;

		const float half = (axis == 0 ? size.x() : axis == 1 ? size.y() : size.z()) * 0.5f;
		const float cur = (axis == 0 ? pos.x() : axis == 1 ? pos.y() : pos.z());
		float best = cur + amt;
		bool blocked = false;

		for (size_t k = 0; k < objs.size(); ++k)
		{
			if (k == self)
				continue;
			const Viewport3D::Obj& p = objs[k];
			if (!p.active)
				continue;
			if (p.kind == 1)
			{
				if (axis != 1 || amt >= 0.f)     // 平面只当"地面"，只挡向下
					continue;
			}
			else if (p.kind == 0)
			{
				if (!p.solid)
					continue;
			}
			else
				continue;

			// 另外两轴必须重叠，才可能被它挡住
			bool other = true;
			for (int a = 0; a < 3 && other; ++a)
			{
				if (a == axis)
					continue;
				const float sa = (a == 0 ? size.x() : a == 1 ? size.y() : size.z()) * 0.5f;
				const float sb = (a == 0 ? p.size.x() : a == 1 ? p.size.y() : p.size.z()) * 0.5f;
				const float pa = (a == 0 ? pos.x() : a == 1 ? pos.y() : pos.z());
				const float pb = (a == 0 ? p.pos.x() : a == 1 ? p.pos.y() : p.pos.z());
				if (std::abs(pa - pb) >= sa + sb - 1e-4f)
					other = false;
			}
			if (!other)
				continue;

			const float hp = (axis == 0 ? p.size.x() : axis == 1 ? p.size.y() : p.size.z()) * 0.5f;
			const float pc = (axis == 0 ? p.pos.x() : axis == 1 ? p.pos.y() : p.pos.z());
			if (amt > 0.f)
			{
				const float face = (pc - hp) - half;      // 贴到它的负侧面
				if (cur <= face + 0.05f && face < best)
				{
					best = face;
					blocked = true;
				}
			}
			else
			{
				const float face = (pc + hp) + half;      // 贴到它的正侧面
				if (cur >= face - 0.05f && face > best)
				{
					best = face;
					blocked = true;
					if (axis == 1 && hitDown)
						*hitDown = true;
				}
			}
		}

		if (axis == 0)      pos.setX(best);
		else if (axis == 1) pos.setY(best);
		else                pos.setZ(best);
		return blocked;
	}

	// 已经重叠（物件被摆进方块里 / 图强行写进墙里）时按最小穿透轴顶出来一次，
	// 之后靠分轴扫掠就不会再穿。
	static void SeparateOverlap(Viewport3D::Obj& o, const std::vector<Viewport3D::Obj>& objs, size_t self)
	{
		for (size_t k = 0; k < objs.size(); ++k)
		{
			if (k == self)
				continue;
			const Viewport3D::Obj& p = objs[k];
			if (!p.active)
				continue;
			if (p.kind != 0 || !p.solid)
				continue;
			float pen = 0.f;
			const int axis = AabbPenetration(o.pos, o.size, p.pos, p.size, &pen);
			if (axis < 0)
				continue;
			if (axis == 0)      o.pos.setX(o.pos.x() + (o.pos.x() >= p.pos.x() ? pen : -pen));
			else if (axis == 1) o.pos.setY(o.pos.y() + (o.pos.y() >= p.pos.y() ? pen : -pen));
			else                o.pos.setZ(o.pos.z() + (o.pos.z() >= p.pos.z() ? pen : -pen));
		}
	}

	// 脚下有没有东西顶着：世界地面 / 平面顶面 / 别的 solid 方块顶面
	static bool StandingOn(const QVector3D& pos, const QVector3D& size,
		const std::vector<Viewport3D::Obj>& objs, size_t self, bool worldFloor)
	{
		const float bottom = pos.y() - size.y() * 0.5f;
		if (worldFloor && bottom <= 0.02f)
			return true;
		for (size_t k = 0; k < objs.size(); ++k)
		{
			if (k == self)
				continue;
			const Viewport3D::Obj& p = objs[k];
			if (!p.active)
				continue;
			if (p.kind == 0 && !p.solid)
				continue;
			if (p.kind != 0 && p.kind != 1)
				continue;
			const float top = p.pos.y() + p.size.y() * 0.5f;
			if (std::abs(top - bottom) > 0.06f)
				continue;
			if (std::abs(pos.x() - p.pos.x()) >= (size.x() + p.size.x()) * 0.5f - 1e-3f)
				continue;
			if (std::abs(pos.z() - p.pos.z()) >= (size.z() + p.size.z()) * 0.5f - 1e-3f)
				continue;
			return true;
		}
		return false;
	}

	// 运行时物理：
	//   1) 2D 节点图驱动（场景载入了图才有）：键盘 -> 逻辑 -> 写回对象 / 相机；
	//      被方块/平面 block 接管的对象不再吃视口重力（高度由"重力"block 管），但仍参与碰撞
	//   2) 自由方块：重力 + 地面接触（反弹）
	//   3) 所有 solid 方块之间 AABB 碰撞（分轴推出，可站上其它方块顶面）
	void Viewport3D::StepPhysics(float dt)
	{
		dt = std::clamp(dt, 0.f, 0.05f);
		m_simTime += dt;

		// 本帧开始时的位置 = 上一帧碰撞解算后的合法位置
		// （图驱动对象也从这里出发做分轴扫掠，这样图写进去的"穿墙位移"会被挡在墙面外）
		std::vector<QVector3D> prevPos(m_objects.size());
		for (size_t i = 0; i < m_objects.size(); ++i)
			prevPos[i] = m_objects[i].pos;

		// ---- 节点图 ----
		bool hasCam = false;
		QVector3D camOff;
		int camUid = 0;
		const bool graphOn = (m_graph && m_graph->IsValid());
		if (graphOn)
			m_graph->Step(dt, m_objects, m_keysDown, hasCam, camOff, camUid);

		std::vector<char> driven(m_objects.size(), 0);
		if (graphOn)
		{
			for (int uid : m_graph->DrivenUids())
			{
				const int idx = IndexOfUid(uid);
				if (idx >= 0 && idx < int(driven.size()))
					driven[idx] = 1;
			}
		}

		// 镜头块：注视点锁定要看的方块（绑定的 uid 优先，否则第一个被驱动的），
		// 平滑跟随（~0.1s 收敛）不甩头；yaw/pitch/dist 仍归鼠标（第三人称跟随）
		if (hasCam)
		{
			QVector3D look;
			bool found = false;
			if (camUid > 0)
			{
				const int idx = IndexOfUid(camUid);
				if (idx >= 0 && idx < int(m_objects.size()) && m_objects[idx].kind == 0)
				{
					look = m_objects[idx].pos;
					found = true;
				}
			}
			if (!found)
				for (size_t i = 0; i < m_objects.size(); ++i)
					if (driven[i] && m_objects[i].kind == 0) { look = m_objects[i].pos; break; }
			look += camOff;
			if (!m_camFollowInit)
			{
				m_target = look;             // 刚进运行时：直接对准，不从编辑器机位飘过去
				m_camFollowInit = true;
			}
			else
				m_target += (look - m_target) * std::clamp(dt * 10.f, 0.f, 1.f);
		}

		// ---- 物理 ----
		// 每个方块都从"上一帧的合法位置"出发，按 Y -> X -> Z 逐轴扫掠：
		//   竖直先走 => 落地 / 踩到方块顶面准确；水平后走 => 撞墙就贴面停住，不会被挤飞。
		// 图驱动的方块算完后，把被挡住的那段位移回馈给节点图（累加 / 重力块的状态），
		// 否则图每帧仍然把方块按回墙里，看起来就是"穿墙 / 很滑 / 站不住"。
		for (size_t i = 0; i < m_objects.size() && i < m_vel.size(); ++i)
		{
			Obj& o = m_objects[i];
			if (o.kind != 0 || !o.active)
				continue;
			QVector3D& v = m_vel[i];
			if (driven[i])
				v = QVector3D();                   // 图驱动：位置由图给，速度只是占位

			// 目标位置：图驱动 = 节点图刚写进来的值；否则 = 上一帧位置 + 速度积分
			QVector3D target;
			if (driven[i])
			{
				target = o.pos;
			}
			else
			{
				if (o.gravity)
					v.setY(v.y() + m_gravity * dt);
				else
					v = QVector3D();
				target = prevPos[i] + v * dt;
			}

			o.pos = prevPos[i];                    // 回到合法起点，重新走一遍
			SeparateOverlap(o, m_objects, i);      // 起步就嵌在别人身体里 -> 先顶出来

			bool hitDown = false;
			if (SweepAxis(o.pos, o.size, 1, target.y() - o.pos.y(), m_objects, i, &hitDown))
			{
				if (v.y() < 0.f)
				{
					if (v.y() < -1.5f)             // 落地反弹，速度太小则停住
						v.setY(-v.y() * 0.3f);
					else
						v.setY(0.f);
					v.setX(v.x() * 0.8f);
					v.setZ(v.z() * 0.8f);
				}
				else if (v.y() > 0.f)              // 顶到天花板
					v.setY(0.f);
			}
			if (SweepAxis(o.pos, o.size, 0, target.x() - o.pos.x(), m_objects, i, nullptr))
				v.setX(0.f);
			if (SweepAxis(o.pos, o.size, 2, target.z() - o.pos.z(), m_objects, i, nullptr))
				v.setZ(0.f);

			// 场景里没有平面垫底时兜一个世界地面，别让自由方块无限下落
			if (!driven[i] && o.pos.y() - o.size.y() * 0.5f < 0.f)
			{
				o.pos.setY(o.size.y() * 0.5f);
				if (v.y() < 0.f) v.setY(0.f);
			}

			// 脚下有东西顶着（地面 / 平面 / 别的方块顶面）
			const bool grounded = StandingOn(o.pos, o.size, m_objects, i, !driven[i]);

			// 贴面摩擦：站住时水平速度快速衰减，不"打滑"
			if (!driven[i] && grounded)
			{
				const float damp = std::clamp(1.f - 10.f * dt, 0.f, 1.f);
				v.setX(v.x() * damp);
				v.setZ(v.z() * damp);
				if (std::abs(v.x()) < 0.05f) v.setX(0.f);
				if (std::abs(v.z()) < 0.05f) v.setZ(0.f);
			}

			// 图驱动对象：碰撞挡下的位移写回节点图，并把"脚下支撑面"告诉重力块
			// （踩在别的方块上也能起跳；不写回的话累加块会一路涨，转身要等半天）
			if (driven[i] && graphOn)
				m_graph->FeedBack(o.uid, o.pos, o.pos - target, grounded);
		}

		// ---- 跳转精灵：任何可见方块碰到就切到目标关卡（一次） ----
		if (m_runtime != RuntimeMode::None && !m_levelFired)
		{
			for (const Obj& t : m_objects)
			{
				if (t.kind != 2 || !t.active)
					continue;
				for (const Obj& b : m_objects)
				{
					if (b.kind != 0 || !b.active)
						continue;
					if (std::abs(t.pos.x() - b.pos.x()) >= (t.size.x() + b.size.x()) * 0.5f)
						continue;
					if (std::abs(t.pos.y() - b.pos.y()) >= (t.size.y() + b.size.y()) * 0.5f)
						continue;
					if (std::abs(t.pos.z() - b.pos.z()) >= (t.size.z() + b.size.z()) * 0.5f)
						continue;
					m_levelFired = true;
					emit LevelSwitchRequested(QString::fromStdString(t.target));
					return;
				}
			}
		}
	}

	// ================= 键盘 =================
	void Viewport3D::keyPressEvent(QKeyEvent* e)
	{
		// 运行时：ESC 退出；WASD/方向键/空格记入按键表交给物理步进，其余屏蔽
		if (m_runtime != RuntimeMode::None)
		{
			if (e->key() == Qt::Key_Escape)
				emit RuntimeExitRequested();
			else
				m_keysDown.insert(e->key());
			e->accept();
			return;
		}

		const int cmd = MatchHotkey(e->key(), int(e->modifiers()));
		if (cmd < 0)
		{
			QWidget::keyPressEvent(e);
			return;
		}
		switch (VCmd(cmd))
		{
			case VCmd::ToolSelect:  m_tool = Tool::Select; break;
			case VCmd::ToolMove:    m_tool = Tool::Move;   break;
			case VCmd::ToolRotate:  m_tool = Tool::Rotate; break;
			case VCmd::ToolScale:   m_tool = Tool::Scale;  break;
			case VCmd::Grid:        m_showGrid = !m_showGrid; break;
			case VCmd::ShowFps:     SetShowFps(!m_showFps); return;
			case VCmd::Focus:
				if (!m_selection.empty())
				{
					m_target = SelectionCenter();
					m_dist = 4.f;
				}
				break;
			case VCmd::ResetView:   ResetCamera(); return;
			case VCmd::Perspective: m_ortho = false; m_pitch = 0.45f; m_yaw = -0.55f; m_dist = 8.f; break;
			case VCmd::Ortho:       m_ortho = true; m_pitch = 1.45f; m_yaw = 0.f; break;
			// 预设视图必须用精确角度：差一点点，盒子的顶/底面就会投成
			// 两条错开的线（"侧面的线不在一条线上"）。
			case VCmd::Top:         m_ortho = true; m_pitch = kPi * 0.5f; m_yaw = 0.f; break;
			case VCmd::Front:       m_ortho = true; m_pitch = 0.f; m_yaw = 0.f; break;
			case VCmd::Left:        m_ortho = true; m_pitch = 0.f; m_yaw = -kPi * 0.5f; break;
			case VCmd::Wireframe:   m_wireFrame = !m_wireFrame; break;
			case VCmd::SnapToggle:  m_snap = !m_snap; break;
			case VCmd::AngleSnap:   m_angleSnap = !m_angleSnap; break;
			default: return;   // Camera / Stand / AngleSnap / PercentSnap 暂未接
		}
		emit ToolChanged(int(m_tool));
		RenderNow();
	}

	void Viewport3D::keyReleaseEvent(QKeyEvent* e)
	{
		m_keysDown.remove(e->key());
		QWidget::keyReleaseEvent(e);
	}

	void Viewport3D::focusOutEvent(QFocusEvent*)
	{
		m_keysDown.clear();   // 切走焦点后松键收不到，清掉防止"按键卡住"
		if (m_mouseGrabbed)
		{
			releaseMouse();   // 别把鼠标扣在视口上，切走就放手
			m_mouseGrabbed = false;
		}
	}

	// ================= Gizmo 命中与拖拽 =================
	static float DistToSeg(const QPointF& p, const QPointF& a, const QPointF& b)
	{
		const float dx = float(b.x() - a.x()), dy = float(b.y() - a.y());
		const float l2 = dx * dx + dy * dy;
		if (l2 < 1e-6f)
			return float(std::hypot(p.x() - a.x(), p.y() - a.y()));
		float t = (float(p.x() - a.x()) * dx + float(p.y() - a.y()) * dy) / l2;
		t = std::clamp(t, 0.f, 1.f);
		return float(std::hypot(p.x() - (a.x() + t * dx), p.y() - (a.y() + t * dy)));
	}

	int Viewport3D::GizmoHitTest(const QPointF& p, int* sub) const
	{
		*sub = 0;
		if (m_tool == Tool::Select || m_selection.empty())
			return -1;
		const QVector3D o = SelectionCenter();
		QPointF op;
		float od = 0.f;
		if (!ProjectPoint(o, op, &od))
			return -1;
		const float wpp = WorldPerPixel(od);
		const float len = (m_tool == Tool::Rotate) ? 1.05f : 1.35f;
		const QVector3D axes[3] = { {1,0,0},{0,1,0},{0,0,1} };
		const float pickR = 9.f;

		if (m_tool == Tool::Scale)
		{
			// 中心手柄 = 均匀缩放（axis = -2）
			if (float(std::hypot(p.x() - op.x(), p.y() - op.y())) < pickR)
			{
				*sub = 0;
				return -2;
			}
		}

		int best = -1;
		// 环是细目标，容差放宽一点；箭头 / 轴是长线段，9px 足够
		float bestDist = (m_tool == Tool::Rotate) ? 14.f : pickR;
		if (m_tool == Tool::Move || m_tool == Tool::Scale)
		{
			for (int a = 0; a < 3; ++a)
			{
				QPointF tp;
				if (!ProjectPoint(o + axes[a] * len, tp))
					continue;
				const float d = DistToSeg(p, op, tp);
				if (d < bestDist)
				{
					bestDist = d;
					best = a;
					*sub = 0;
				}
			}
		}
		else if (m_tool == Tool::Rotate)
		{
			// 环是细目标：采样加密一倍，否则贴着环点也经常落空，
			// 一落空就变成框选，表现为"按 E 拖不动 / 一拖 selection 就没了"
			for (int a = 0; a < 3; ++a)
			{
				const QVector3D u = std::abs(axes[a].y()) < 0.9f ? QVector3D(0,1,0) : QVector3D(1,0,0);
				const QVector3D v1 = QVector3D::crossProduct(axes[a], u).normalized();
				const QVector3D v2 = QVector3D::crossProduct(axes[a], v1).normalized();
				for (int s = 0; s < 96; ++s)
				{
					const float ang = s / 96.f * 2.f * kPi;
					QPointF cp;
					if (!ProjectPoint(o + (v1 * std::cos(ang) + v2 * std::sin(ang)) * len, cp))
						continue;
					const float d = float(std::hypot(p.x() - cp.x(), p.y() - cp.y()));
					if (d < bestDist)
					{
						bestDist = d;
						best = a;
						*sub = s;
					}
				}
			}
		}
		return best;
	}

	void Viewport3D::ApplyGizmoDrag(const QPointF& p)
	{
		if (m_selection.empty())
			return;
		// 第一次真正改动前通知宿主抓撤销快照（一次拖拽只记一步）
		if (!m_gizmoEditBegun)
		{
			m_gizmoEditBegun = true;
			emit EditStarted();
		}
		const QVector3D axes[3] = { {1,0,0},{0,1,0},{0,0,1} };
		const QVector3D center = SelectionCenter();
		QPointF cp;
		float cd = 0.f;
		if (!ProjectPoint(center, cp, &cd))
			return;
		const float wpp = WorldPerPixel(cd);

		if (m_tool == Tool::Move && m_gizmoAxis >= 0)
		{
			// 鼠标位移在轴向屏幕方向上的投影 -> 世界位移
			QPointF tp;
			if (!ProjectPoint(center + m_gizmoAxisDir * 1.35f, tp))
				return;
			QPointF dir(tp.x() - cp.x(), tp.y() - cp.y());
			const float l = float(std::hypot(dir.x(), dir.y()));
			if (l < 1e-3f)
				return;
			dir /= l;
			const QPointF delta(p.x() - m_pressPos.x(), p.y() - m_pressPos.y());
			const float t = float(delta.x() * dir.x() + delta.y() * dir.y()) * wpp;
			QVector3D move = m_gizmoAxisDir * t;
			if (m_snap)
			{
				move.setX(std::round(move.x() * 2.f) * 0.5f);
				move.setY(std::round(move.y() * 2.f) * 0.5f);
				move.setZ(std::round(move.z() * 2.f) * 0.5f);
			}
			// 每个选中物体从各自的起始位置移动（多选整体平移）
			for (size_t k = 0; k < m_selection.size() && k < m_gizmoStartPosList.size(); ++k)
			{
				const int i = m_selection[k];
				if (i >= 0 && i < int(m_objects.size()))
					m_objects[i].pos = m_gizmoStartPosList[k] + move;
			}
		}
		else if (m_tool == Tool::Rotate && m_gizmoAxis >= 0)
		{
			// 旋转不能用"按下点 -> 当前点"的绝对夹角：光标穿屏时局部坐标整体跳一个屏幕宽，
			// 绝对角会瞬间跳掉一大截（表现就是"拖到屏幕边缘突然转很大角度"）。
			// 改成逐帧增量：angle(当前) - angle(上一帧)，上一帧点在穿屏时同步平移，
			// 增量永远连续；再给单帧增量加个上限兜底（事件合并 / 切窗口回来的异常值）。
			const float ang = std::atan2(float(p.y() - cp.y()), float(p.x() - cp.x()));
			const float angPrev = std::atan2(float(m_rotPrevPos.y() - cp.y()),
				float(m_rotPrevPos.x() - cp.x()));
			float dd = ang - angPrev;
			while (dd > kPi) dd -= 2.f * kPi;
			while (dd < -kPi) dd += 2.f * kPi;
			m_rotPrevPos = p;
			dd = std::clamp(dd, -0.6f, 0.6f);       // 单帧最多 ~34°，异常跳变直接吃掉
			m_rotAccumDeg += qRadiansToDegrees(dd);
			// 轴在视图空间里的朝向决定旋转方向
			QVector3D vaxis = (m_view * QVector4D(axes[m_gizmoAxis], 0.f)).toVector3D();
			const float sign = vaxis.z() > 0.f ? -1.f : 1.f;
			float deg = m_rotAccumDeg * sign;
			if (m_snap)
				deg = std::round(deg / 15.f) * 15.f;
			QMatrix4x4 R;
			R.rotate(deg, axes[m_gizmoAxis]);
			// 每个物体：新旋转 = 世界轴旋转 * 起始旋转矩阵，再反解出欧拉角。
			// 之前直接拿矩阵乘欧拉角向量，起始角是 (0,0,0) 时结果恒为 0 —— 物体永远不转。
			for (size_t k = 0; k < m_selection.size() && k < m_gizmoStartRotsList.size(); ++k)
			{
				const int i = m_selection[k];
				if (i < 0 || i >= int(m_objects.size()))
					continue;
				QMatrix4x4 startM;
				startM.rotate(qRadiansToDegrees(m_gizmoStartRotsList[k].z()), 0.f, 0.f, 1.f);
				startM.rotate(qRadiansToDegrees(m_gizmoStartRotsList[k].y()), 0.f, 1.f, 0.f);
				startM.rotate(qRadiansToDegrees(m_gizmoStartRotsList[k].x()), 1.f, 0.f, 0.f);
				const QMatrix4x4 M = R * startM;        // 与 ObjRotation 的 Rz*Ry*Rx 顺序一致
				const float* m = M.data();              // 列主序：m[col*4 + row]
				const float R20 = m[2], R21 = m[6], R22 = m[10], R00 = m[0], R10 = m[1];
				const float ry = std::asin(std::clamp(-R20, -1.f, 1.f));
				const float rx = std::atan2(R21, R22);
				const float rz = std::atan2(R10, R00);
				m_objects[i].rot = QVector3D(rx, ry, rz);
			}
		}
		else if (m_tool == Tool::Scale && (m_gizmoAxis >= 0 || m_gizmoAxis == -2))
		{
			QPointF tp;
			if (m_gizmoAxis == -2)
				tp = cp;
			else if (!ProjectPoint(center + m_gizmoAxisDir * 1.35f, tp))
				return;
			const float sx = (m_gizmoAxis == -2) ? 0.f : float(tp.x() - cp.x());
			const float sy = (m_gizmoAxis == -2) ? 0.f : float(tp.y() - cp.y());
			const float l = (m_gizmoAxis == -2) ? 1.f : float(std::hypot(sx, sy));
			const float nx = (m_gizmoAxis == -2) ? -1.f : sx / l;
			const float ny = (m_gizmoAxis == -2) ? -1.f : sy / l;
			const QPointF delta(p.x() - m_pressPos.x(), p.y() - m_pressPos.y());
			const float t = float(delta.x() * nx + delta.y() * ny) * wpp;
			for (size_t k = 0; k < m_selection.size() && k < m_gizmoStartSizesList.size(); ++k)
			{
				const int i = m_selection[k];
				if (i < 0 || i >= int(m_objects.size()))
					continue;
				Obj& o = m_objects[i];
				const float f = std::max(0.05f, 1.f + t * 2.f);
				if (m_gizmoAxis == -2)
					o.size = m_gizmoStartSizesList[k] * f;
				else
					o.size[m_gizmoAxis] = std::max(0.02f, m_gizmoStartSizesList[k][m_gizmoAxis] * f);
				if (m_snap)
				{
					o.size.setX(std::round(o.size.x() * 4.f) * 0.25f);
					o.size.setY(std::round(o.size.y() * 4.f) * 0.25f);
					o.size.setZ(std::round(o.size.z() * 4.f) * 0.25f);
				}
			}
		}
		RenderNow();
	}

	// ================= 鼠标 =================
	void Viewport3D::mousePressEvent(QMouseEvent* e)
	{
		setFocus();
		// Run Project：整项目运行，不接受任何鼠标输入
		if (m_runtime == RuntimeMode::Project)
			return;
		// Run Scene：只放开相机操作（右键旋转 / 中键平移 / 滚轮缩放），
		// 左键的拾取、框选、gizmo 一律不走
		if (m_runtime == RuntimeMode::Scene && e->button() == Qt::LeftButton)
			return;
		m_pressPos = m_lastPos = e->position();
		m_dragMoved = false;

		if (e->button() == Qt::RightButton)
		{
			m_drag = DragMode::Orbit;
			m_dragButton = e->button();
			setCursor(Qt::ClosedHandCursor);
		}
		else if (e->button() == Qt::MiddleButton)
		{
			m_drag = DragMode::Pan;
			m_dragButton = e->button();
			setCursor(Qt::SizeAllCursor);
		}
		else if (e->button() == Qt::LeftButton)
		{
			// 右上角视图方块：先于拾取 / 框选判定，点中就不再走选择逻辑
			UpdateViewCubeBasis();
			if (const ViewCube::Block b = m_viewCube.HitTest(e->position()); b.Valid())
			{
				m_drag = DragMode::ViewCube;
				m_dragButton = e->button();
				QWidget::mousePressEvent(e);
				return;
			}

			int sub = 0;
			const int axis = GizmoHitTest(e->position(), &sub);
			if (m_tool != Tool::Select && !m_selection.empty() && axis != -1)
			{
				// 开始 Gizmo 拖拽
				m_drag = DragMode::Gizmo;
				m_gizmoAxis = axis;
				static const QVector3D axes[3] = { {1,0,0},{0,1,0},{0,0,1} };
				m_gizmoAxisDir = (axis == -2) ? QVector3D(1,0,0) : axes[axis];
				m_gizmoStartPosList.clear();
				m_gizmoStartSizesList.clear();
				m_gizmoStartRotsList.clear();
				for (int i : m_selection)
				{
					m_gizmoStartPosList.push_back(m_objects[i].pos);
					m_gizmoStartSizesList.push_back(m_objects[i].size);
					m_gizmoStartRotsList.push_back(m_objects[i].rot);
				}
				m_gizmoStartPos = m_objects[m_selection.back()].pos;
				m_gizmoStartSizes = m_objects[m_selection.back()].size;
				m_gizmoStartRots = m_objects[m_selection.back()].rot;
				m_gizmoStartT = 0.f;
				m_gizmoEditBegun = false;             // 等第一次真正位移再叫宿主记快照
				m_rotPrevPos = e->position();         // 旋转增量累加的基准帧
				m_rotAccumDeg = 0.f;
				QPointF cp;
				if (ProjectPoint(SelectionCenter(), cp))
					m_gizmoStartAngle = std::atan2(float(e->position().y() - cp.y()),
						float(e->position().x() - cp.x()));
			}
			else
			{
				m_drag = DragMode::BoxSelect;
				m_bandStart = m_bandEnd = e->position();
				m_bandActive = false;
			}
		}

		// 拖拽开始就抓住鼠标：否则光标一离开窗口就收不到移动事件，
		// 相机拖不出去、也到不了屏幕边缘（边缘虫洞需要能一路收到边缘）
		if (m_drag != DragMode::None && !m_mouseGrabbed)
		{
			grabMouse();
			m_mouseGrabbed = true;
		}
		QWidget::mousePressEvent(e);
	}

	// 拖拽撞到屏幕边缘：光标从对侧穿出来，拖拽可以无限继续（类似 Unity 场景视图）。
	// 只对相机环绕 / 平移 / gizmo 生效；框选和视图方块不参与（矩形和命中判定会被扯断）。
	// 关键在于把按下点、上一帧点一起平移一个屏幕宽（高）度，否则虫洞那一帧 delta 会是一整屏。
	void Viewport3D::WrapCursorAtScreenEdge(const QPointF& localPos, const QPoint& globalPos)
	{
		if (m_drag != DragMode::Orbit && m_drag != DragMode::Pan && m_drag != DragMode::Gizmo)
			return;

		const QPoint g = globalPos;
		const QScreen* scr = QGuiApplication::screenAt(g);
		if (!scr)
			scr = screen();
		if (!scr)
			return;
		const QRect r = scr->geometry();
		if (r.width() <= 16 || r.height() <= 16)
			return;

		// 贴边 1px 就算撞墙。到达边缘后系统不再给移动事件（光标被夹住），
		// 所以这里不看偏移方向，只要还按着键贴着边就穿。
		const int kEdge = 1;
		const int kInset = 8;      // 落到对侧后离边一段距离，避免立刻又被判成撞墙
		QPoint t = g;
		if (g.x() <= r.left() + kEdge)
			t.setX(r.right() - kInset);
		else if (g.x() >= r.right() - kEdge)
			t.setX(r.left() + kInset);
		if (g.y() <= r.top() + kEdge)
			t.setY(r.bottom() - kInset);
		else if (g.y() >= r.bottom() - kEdge)
			t.setY(r.top() + kInset);
		if (t == g)
			return;

		// 光标跳了 shift，Qt 之后报的局部坐标也会整体偏 shift：
		// 按下点 / 上一帧点同步平移，delta 与 gizmo 位移就都连续了
		const QPointF shift(float(t.x() - g.x()), float(t.y() - g.y()));
		m_pressPos += shift;
		m_bandStart += shift;
		m_bandEnd += shift;
		m_rotPrevPos += shift;     // 旋转增量累加的基准点也要跟着跳，否则下一帧角度会突变
		m_lastPos = localPos + shift;
		m_wrapPending = true;      // 下一帧事件如果带回一整屏的跳变，直接吃掉
		QCursor::setPos(t);
	}

	void Viewport3D::mouseMoveEvent(QMouseEvent* e)
	{
		// Project 完全锁死；Scene 允许拖拽相机（m_drag 只可能是 Orbit / Pan）
		if (m_runtime == RuntimeMode::Project)
			return;
		const QPointF p = e->position();

		// 兜底：丢了松键事件（切窗口 / 弹窗抢焦点）时不要一直扣着鼠标
		if (m_mouseGrabbed && e->buttons() == Qt::NoButton)
		{
			releaseMouse();
			m_mouseGrabbed = false;
			m_drag = DragMode::None;
			m_wrapPending = false;
		}

		QPointF d = p - m_lastPos;
		m_lastPos = p;

		// 拖拽中坐标突然跳变（光标被夹在屏幕边缘 / 虫洞回跳 / 切窗口回来 / 事件合并）：
		// 把这一帧的位移吃掉，同时把按下点一起平移这么多，
		// 相当于"基准点跟着重定位"，不会出现"突然移动一大截"。
		// 只对按"按下点 + 当前位置"算位移的操作生效；框选用绝对矩形，跳过。
		if (m_drag == DragMode::Orbit || m_drag == DragMode::Pan || m_drag == DragMode::Gizmo)
		{
			const float jump = float(std::hypot(d.x(), d.y()));
			if (m_wrapPending || jump > kJumpPx)
			{
				m_pressPos += d;
				d = QPointF();
			}
		}
		m_wrapPending = false;

		const float dx = float(d.x());
		const float dy = float(d.y());
		if (QPointF dd = p - m_pressPos; std::hypot(dd.x(), dd.y()) > 3.f)
			m_dragMoved = true;

		switch (m_drag)
		{
			case DragMode::Orbit:
				m_yaw -= dx * 0.008f;
				m_pitch = std::clamp(m_pitch + dy * 0.006f, -1.5f, 1.5f);
				RenderNow();
				break;
			case DragMode::ViewCube:            // 拖动方块 = 环绕相机，手感与右键拖动一致
				m_yaw -= dx * 0.008f;
				m_pitch = std::clamp(m_pitch + dy * 0.006f, -1.5f, 1.5f);
				RenderNow();
				break;
			case DragMode::Pan:
			{
				UpdateMatrices();
				QPointF tp;
				float td = 0.f;
				if (!ProjectPoint(m_target, tp, &td))
					break;
				const float wpp = WorldPerPixel(td);
				// 相机右 / 上方向：直接取视图矩阵前两行（世界空间基向量）。
				// 不能用 cross(视线, (0,1,0))：顶视图 pitch=90° 时两者平行，叉乘退化成
				// 零向量，normalize 得到 NaN/0，平移量变成 0（表现为"顶视图拖不动"）。
				const QVector3D right(m_view(0, 0), m_view(0, 1), m_view(0, 2));
				const QVector3D up(m_view(1, 0), m_view(1, 1), m_view(1, 2));
				m_target += right * (-dx * wpp) + up * (dy * wpp);
				RenderNow();
				break;
			}
			case DragMode::BoxSelect:
				if (m_dragMoved)
				{
					m_bandEnd = p;
					m_bandActive = true;
					RenderNow();            // 重画：把矩形画进 D3D 帧
				}
				break;
			case DragMode::Gizmo:
				ApplyGizmoDrag(p);
				break;
			default:
				break;
		}

		// 撞到屏幕边缘：光标穿到对侧，接着拖（Unity 手感）
		if (m_drag != DragMode::None)
			WrapCursorAtScreenEdge(p, e->globalPosition().toPoint());

		// 悬停视图方块时高亮对应的块（只在非拖动状态更新）
		if (m_drag == DragMode::None && m_runtime == RuntimeMode::None)
		{
			UpdateViewCubeBasis();
			const ViewCube::Block hover = m_viewCube.HitTest(p);
			if (hover != m_viewCube.Hover())
			{
				m_viewCube.SetHover(hover);
				update();                   // 帧图没变，只重画叠加层
			}
		}
		QWidget::mouseMoveEvent(e);
	}

	void Viewport3D::mouseReleaseEvent(QMouseEvent* e)
	{
		// 拖拽结束就放开鼠标（ViewCube 那条提前 return 的路径也走这里）
		if (m_mouseGrabbed && e->buttons() == Qt::NoButton)
		{
			releaseMouse();
			m_mouseGrabbed = false;
			m_wrapPending = false;
		}
		if (m_runtime == RuntimeMode::Project)
			return;

		// 视图方块：没拖动就是"点某一块切视角"
		if (m_drag == DragMode::ViewCube)
		{
			if (!m_dragMoved)
			{
				if (const ViewCube::Block b = m_viewCube.HitTest(e->position()); b.Valid())
					ApplyViewCubePick(b);
			}
			m_drag = DragMode::None;
			m_dragButton = Qt::NoButton;
			setCursor(Qt::ArrowCursor);
			QWidget::mouseReleaseEvent(e);
			return;
		}

		if (e->button() == Qt::LeftButton && m_runtime == RuntimeMode::None)
		{
			if (m_drag == DragMode::Gizmo)
			{
				// 拖拽结束，保持状态即可
				m_gizmoEditBegun = false;
			}
			else if (m_drag == DragMode::BoxSelect)
			{
				m_bandActive = false;
				if (m_dragMoved)
				{
					// 3ds Max 语义：从左向右拖 = 窗口选择（整个物体都在框内才选）；
					// 从右向左拖 = 交叉选择（碰到框就选）。
					const QRectF rect = QRectF(m_pressPos, e->position()).normalized();
					const bool windowMode = e->position().x() >= m_pressPos.x();
					std::vector<int> picked;
					for (int i = 0; i < int(m_objects.size()); ++i)
					{
						QVector3D corners[8];
						ObjCorners(m_objects[i], corners);
						QRectF bb;
						bool any = false;
						for (const QVector3D& c : corners)
						{
							QPointF sp;
							if (!ProjectPoint(c, sp))
								continue;
							if (!any)
							{
								bb = QRectF(sp, sp);
								any = true;
							}
							else
							{
								bb.setLeft(std::min(bb.left(), sp.x()));
								bb.setRight(std::max(bb.right(), sp.x()));
								bb.setTop(std::min(bb.top(), sp.y()));
								bb.setBottom(std::max(bb.bottom(), sp.y()));
							}
						}
						if (!any)
							continue;
						const bool hit = windowMode ? rect.contains(bb) : rect.intersects(bb);
						if (hit)
							picked.push_back(i);
					}
					if (e->modifiers() & Qt::ShiftModifier)
					{
						for (int i : picked)
							if (std::find(m_selection.begin(), m_selection.end(), i) == m_selection.end())
								m_selection.push_back(i);
					}
					else
						m_selection = picked;
					EmitSelection();
				}
				else
				{
					PickAt(e->position(), e->modifiers() & Qt::ShiftModifier);
				}
				RenderNow();            // 清掉框选矩形
			}
		}
		m_drag = DragMode::None;
		m_dragButton = Qt::NoButton;
		setCursor(Qt::ArrowCursor);
		QWidget::mouseReleaseEvent(e);
	}

	void Viewport3D::PickAt(const QPointF& p, bool additive)
	{
		UpdateMatrices();
		const QVector3D o = EyePos();
		const QVector3D d = MouseRayDir(p);
		int best = -1;
		float bestT = 1e9f;
		for (int i = 0; i < int(m_objects.size()); ++i)
		{
			float t = 0.f;
			if (RayObject(i, o, d, &t) && t < bestT)
			{
				bestT = t;
				best = i;
			}
		}
		if (additive)
		{
			auto it = std::find(m_selection.begin(), m_selection.end(), best);
			if (best >= 0 && it != m_selection.end())
				m_selection.erase(it);
			else if (best >= 0)
				m_selection.push_back(best);
		}
		else
		{
			m_selection.clear();
			if (best >= 0)
				m_selection.push_back(best);
		}
		EmitSelection();
	}

	void Viewport3D::wheelEvent(QWheelEvent* e)
	{
		if (m_runtime == RuntimeMode::Project)
			return;
		const float steps = float(e->angleDelta().y()) / 120.f;
		if (std::abs(steps) > 0.01f)
		{
			m_dist = std::clamp(m_dist * std::pow(0.9f, steps), 0.5f, 120.f);
			RenderNow();
		}
		e->accept();
	}

	void Viewport3D::leaveEvent(QEvent* e)
	{
		// 鼠标移出视口后悬停高亮会留在方块上，这里清掉重画一次
		if (m_viewCube.Hover().Valid())
		{
			m_viewCube.SetHover({});
			update();
		}
		QWidget::leaveEvent(e);
	}
}
