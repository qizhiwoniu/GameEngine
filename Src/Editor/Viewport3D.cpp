#include "Viewport3D.h"
#include "D3D12Renderer.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRubberBand>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QDateTime>
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
		// 这里改为：普通原生子窗口 + OpaquePaintEvent，paintEvent 什么都不画，
		// 像素完全由 D3D12 交换链 Present；Qt 只在脏区域回拷，不影响 D3D 内容。
		setAttribute(Qt::WA_OpaquePaintEvent);
		setFocusPolicy(Qt::StrongFocus);
		setMouseTracking(true);

		m_r = new D3D12Renderer;

		m_rubber = new QRubberBand(QRubberBand::Rectangle, this);

		// FPS 打开时才持续渲染（平时按需渲染）；页面切到 2D 节点图时暂停
		auto* timer = new QTimer(this);
		timer->setInterval(33);
		connect(timer, &QTimer::timeout, this, [this] { if (m_showFps && isVisible()) RenderNow(); });
		timer->start();

		// 左上角状态叠加层：低频刷新位置/内容（跟随面板显隐引起的布局变化）
		auto* otimer = new QTimer(this);
		otimer->setInterval(100);
		connect(otimer, &QTimer::timeout, this, &Viewport3D::UpdateOverlay);
		otimer->start();

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
		// m_overlayW 由 Qt 父子关系自动销毁
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
					m_initialized = m_r->Init(reinterpret_cast<HWND>(winId()));
					if (m_initialized)
					{
						EnsureOverlay();          // 左上角状态叠加层
						RenderNow();
					}
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
		// 故意不画：像素由 D3D12 Present（见构造函数里的说明）。
		// Q_UNUSED 避免警告。
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
		for (int i = -8; i <= 8; ++i)
		{
			const QColor& c = (i % 4 == 0) ? major : minor;
			PushLine({ float(i), 0, -8 }, { float(i), 0, 8 }, c);
			PushLine({ -8, 0, float(i) }, { 8, 0, float(i) }, c);
		}
	}

	void Viewport3D::PushAxes()
	{
		PushLine({ 0,0.001f,0 }, { 1.4f,0.001f,0 }, QColor(214, 64, 64));
		PushLine({ 0,0,0 }, { 0,1.4f,0 }, QColor(96, 200, 96));
		PushLine({ 0,0.001f,0 }, { 0,0.001f,1.4f }, QColor(80, 120, 220));
	}

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

		// 网格 / 坐标轴
		if (m_showGrid)
			PushGrid();
		PushAxes();

		// 物体
		for (const Obj& o : m_objects)
		{
			QVector3D corners[8];
			ObjCorners(o, corners);
			if (m_wireFrame)
			{
				PushBoxWire(corners, o.color.lightness() > 0 ? o.color : QColor(200, 200, 200));
			}
			else
			{
				PushBoxFaces(corners, o.color);
				if (o.selected)
					PushBoxWire(corners, QColor(255, 214, 64));
			}
		}

		PushGizmo();

		// QMatrix4x4 为列主序存储；直接取 .data() 顺序传给着色器
		m_r->End();
	}

	void Viewport3D::RenderNow()
	{
		if (!m_initialized)
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
	}

	// ================= 左上角状态叠加层 =================
	// 左上角状态文字：画在一个无边框、半透明、点击穿透的 Qt Tool 顶层窗口上。
	// - flip-model 交换链会把窗口 DC 上的 GDI 绘制全部盖掉，所以文字必须
	//   位于独立 HWND；但 WS_CHILD 分层子窗口不支持 UpdateLayeredWindow，
	//   裸 Win32 弹窗又会在主窗口移动/最小化时被系统回收（文字"自己消失"）。
	// - Qt::Tool 顶层窗口由 Qt 作为 owned window 托管：自动跟随父窗口移动、
	//   随父窗口最小化/还原；叠加 100ms 心跳兜底重新 show，不会永久丢失。
	// - WA_TranslucentBackground 走 DWM 合成；点击穿透由
	//   Qt::WindowTransparentForInput 保证。
	namespace
	{
		class OverlayWidget : public QWidget
		{
		public:
			struct Line { QString text; QColor color; int x = 0, y = 0; };
			std::vector<Line> lines;
			OverlayWidget(QWidget* parent = nullptr) : QWidget(parent)
			{
				setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
					Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint);
				setAttribute(Qt::WA_TranslucentBackground);
				setAttribute(Qt::WA_ShowWithoutActivating);
				setAttribute(Qt::WA_TransparentForMouseEvents);
				setFixedSize(360, 90);
			}
		protected:
			void paintEvent(QPaintEvent*) override
			{
				QPainter p(this);
				QFont f = p.font();
				f.setPointSizeF(9.5f);
				p.setFont(f);
				for (const Line& l : lines)
				{
					p.setPen(l.color);
					p.drawText(QPoint(l.x, l.y), l.text);
				}
			}
		};
	}

	void Viewport3D::EnsureOverlay()
	{
		if (m_overlayW)
			return;
		m_overlayW = new OverlayWidget(window());
	}

	void Viewport3D::UpdateOverlay()
	{
		auto* ov = static_cast<OverlayWidget*>(m_overlayW);
		if (!ov)
			return;
		if (!isVisible() || !m_initialized)
		{
			if (ov->isVisible())
				ov->hide();
			return;
		}

		// 组装文字行
		using Line = OverlayWidget::Line;
		std::vector<Line> lines;
		lines.push_back({ QString("%1 + %2")
			.arg(m_ortho ? "orthographic" : "perspective")
			.arg(m_wireFrame ? "wireframe" : "stand"),
			QColor(198, 198, 204), 0, 14 });

		// 第 2 行：工具 + snap/angle（开的亮绿、关的暗灰，与 Max 一致）
		const char* toolName = "select";
		switch (m_tool)
		{
			case Tool::Move:   toolName = "move";   break;
			case Tool::Rotate: toolName = "rotate"; break;
			case Tool::Scale:  toolName = "scale";  break;
			default: break;
		}
		lines.push_back({ toolName, QColor(198, 198, 204), 0, 34 });
		const QFontMetrics fm(ov->font());
		const int tw = fm.horizontalAdvance(QLatin1String(toolName));
		const QColor on(159, 216, 160), off(112, 112, 120);
		lines.push_back({ m_snap ? "on" : "off", m_snap ? on : off, tw + 14, 34 });
		lines.push_back({ m_angleSnap ? "on" : "off", m_angleSnap ? on : off, tw + 14 + 58, 34 });

		if (m_showFps)
			lines.push_back({ QString("FPS: %1").arg(m_fpsValue), on, 0, 54 });

		ov->lines = std::move(lines);
		ov->update();
		ov->move(mapToGlobal(QPoint(8, 6)));
		if (!ov->isVisible())
			ov->show();
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

	// ================= 键盘 =================
	void Viewport3D::keyPressEvent(QKeyEvent* e)
	{
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
		float bestDist = pickR;
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
			for (int a = 0; a < 3; ++a)
			{
				const QVector3D u = std::abs(axes[a].y()) < 0.9f ? QVector3D(0,1,0) : QVector3D(1,0,0);
				const QVector3D v1 = QVector3D::crossProduct(axes[a], u).normalized();
				const QVector3D v2 = QVector3D::crossProduct(axes[a], v1).normalized();
				for (int s = 0; s < 48; ++s)
				{
					const float ang = s / 48.f * 2.f * kPi;
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
			const float ang = std::atan2(float(p.y() - cp.y()), float(p.x() - cp.x()));
			float d = ang - m_gizmoStartAngle;
			while (d > kPi) d -= 2.f * kPi;
			while (d < -kPi) d += 2.f * kPi;
			// 轴在视图空间里的朝向决定旋转方向
			QVector3D vaxis = (m_view * QVector4D(axes[m_gizmoAxis], 0.f)).toVector3D();
			const float sign = vaxis.z() > 0.f ? -1.f : 1.f;
			float deg = qRadiansToDegrees(d) * sign;
			if (m_snap)
				deg = std::round(deg / 15.f) * 15.f;
			QMatrix4x4 rot;
			rot.rotate(deg, axes[m_gizmoAxis]);
			// 绕各自物体中心旋转
			for (int i : m_selection)
				if (i >= 0 && i < int(m_objects.size()))
					m_objects[i].rot = rot * m_gizmoStartRots;
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
				for (int i : m_selection)
				{
					m_gizmoStartPosList.push_back(m_objects[i].pos);
					m_gizmoStartSizesList.push_back(m_objects[i].size);
				}
				m_gizmoStartPos = m_objects[m_selection.back()].pos;
				m_gizmoStartSizes = m_objects[m_selection.back()].size;
				m_gizmoStartRots = m_objects[m_selection.back()].rot;
				m_gizmoStartT = 0.f;
				QPointF cp;
				if (ProjectPoint(SelectionCenter(), cp))
					m_gizmoStartAngle = std::atan2(float(e->position().y() - cp.y()),
						float(e->position().x() - cp.x()));
			}
			else
			{
				m_drag = DragMode::BoxSelect;
				m_rubber->setGeometry(QRect(int(e->position().x()), int(e->position().y()), 0, 0));
			}
		}
		QWidget::mousePressEvent(e);
	}

	void Viewport3D::mouseMoveEvent(QMouseEvent* e)
	{
		const QPointF p = e->position();
		const float dx = float(p.x() - m_lastPos.x());
		const float dy = float(p.y() - m_lastPos.y());
		m_lastPos = p;
		if (QPointF d = p - m_pressPos; std::hypot(d.x(), d.y()) > 3.f)
			m_dragMoved = true;

		switch (m_drag)
		{
			case DragMode::Orbit:
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
				// 相机右 / 上方向
				const QVector3D fwd = (m_target - EyePos()).normalized();
				QVector3D right = QVector3D::crossProduct(fwd, QVector3D(0, 1, 0)).normalized();
				QVector3D up = QVector3D::crossProduct(right, fwd).normalized();
				m_target += right * (-dx * wpp) + up * (dy * wpp);
				RenderNow();
				break;
			}
			case DragMode::BoxSelect:
				if (m_dragMoved)
				{
					m_rubber->setGeometry(QRect(QPoint(int(m_pressPos.x()), int(m_pressPos.y())),
						QPoint(int(p.x()), int(p.y()))).normalized());
					m_rubber->show();
				}
				break;
			case DragMode::Gizmo:
				ApplyGizmoDrag(p);
				break;
			default:
				break;
		}
		QWidget::mouseMoveEvent(e);
	}

	void Viewport3D::mouseReleaseEvent(QMouseEvent* e)
	{
		if (e->button() == Qt::LeftButton)
		{
			if (m_drag == DragMode::Gizmo)
			{
				// 拖拽结束，保持状态即可
			}
			else if (m_drag == DragMode::BoxSelect)
			{
				m_rubber->hide();
				if (m_dragMoved)
				{
					// 框选：任一 OBB 角点落在矩形内即选中
					const QRect rect = QRect(QPoint(int(m_pressPos.x()), int(m_pressPos.y())),
						QPoint(int(e->position().x()), int(e->position().y()))).normalized();
					std::vector<int> picked;
					for (int i = 0; i < int(m_objects.size()); ++i)
					{
						QVector3D corners[8];
						ObjCorners(m_objects[i], corners);
						for (const QVector3D& c : corners)
						{
							QPointF sp;
							if (!ProjectPoint(c, sp))
								continue;
							if (rect.contains(QPoint(int(sp.x()), int(sp.y()))))
							{
								picked.push_back(i);
								break;
							}
						}
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
		const float steps = float(e->angleDelta().y()) / 120.f;
		if (std::abs(steps) > 0.01f)
		{
			m_dist = std::clamp(m_dist * std::pow(0.9f, steps), 0.5f, 120.f);
			RenderNow();
		}
		e->accept();
	}
}
