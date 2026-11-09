#include "SceneGraph2D.h"

#include <QCursor>
#include <QDateTime>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeySequence>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

namespace GameEditor
{
	namespace
	{
		// 2D 节点尺寸（视图坐标，随缩放等比显示）
		constexpr float kNodeW = 150.f;
		constexpr float kHeadH = 24.f;
		constexpr float kRowH  = 26.f;
		constexpr float kPinR  = 4.5f;
		constexpr float kGrpPad = 14.f;
		constexpr float kGrpTitle = 22.f;

		const QColor kNumPin(140, 205, 90);      // 数值引脚
		const QColor kObjPin(90, 160, 235);      // 物体引脚
		const QColor kSel(255, 170, 60);

		const QString kKeyNames[14] = {
			QStringLiteral("W"), QStringLiteral("A"), QStringLiteral("S"), QStringLiteral("D"),
			QStringLiteral("上"), QStringLiteral("下"), QStringLiteral("左"), QStringLiteral("右"),
			QStringLiteral("空格"), QStringLiteral("Shift"), QStringLiteral("Ctrl"),
			QStringLiteral("Z"), QStringLiteral("X"), QStringLiteral("C")
		};

		QString MenuStyle()
		{
			return QStringLiteral(
				"QMenu { background:#2b2b31; color:#d8d8dc; border:1px solid #1a1a1f; }"
				"QMenu::item { padding:4px 26px 4px 12px; }"
				"QMenu::item:selected { background:#3a5a8c; }"
				"QMenu::item:disabled { color:#77777e; }"
				"QMenu::separator { height:1px; background:#1a1a1f; }");
		}
	}

	// ================= 节点种类表 =================
	const SceneGraph2D::KindDef& SceneGraph2D::KindOf(int k)
	{
		static const KindDef D[NK_COUNT] = {
			// 方块：位置 XYZ / 旋转 / 缩放 -> 物体
			{ QStringLiteral("方块"), QColor(56, 120, 170), 5,
			  { { QStringLiteral("位置X"), ptNumber, 0.f }, { QStringLiteral("位置Y"), ptNumber, 0.5f },
				{ QStringLiteral("位置Z"), ptNumber, 0.f }, { QStringLiteral("旋转"), ptNumber, 0.f },
				{ QStringLiteral("缩放"),   ptNumber, 1.f } },
			  1, { { QStringLiteral("物体"), ptObject, 0.f } } },
			// 平面
			{ QStringLiteral("平面"), QColor(170, 110, 50), 5,
			  { { QStringLiteral("位置X"), ptNumber, 0.f }, { QStringLiteral("位置Y"), ptNumber, 0.f },
				{ QStringLiteral("位置Z"), ptNumber, 0.f }, { QStringLiteral("旋转"), ptNumber, 0.f },
				{ QStringLiteral("缩放"),   ptNumber, 1.f } },
			  1, { { QStringLiteral("物体"), ptObject, 0.f } } },
			// 时间：时间 / 正弦 / 余弦 / 角度
			{ QStringLiteral("时间"), QColor(130, 80, 170), 0, {},
			  4, { { QStringLiteral("时间"), ptNumber, 0.f }, { QStringLiteral("正弦"), ptNumber, 0.f },
				   { QStringLiteral("余弦"), ptNumber, 0.f }, { QStringLiteral("角度"), ptNumber, 0.f } } },
			// 键盘输入：水平 / 垂直 / 空格
			{ QStringLiteral("键盘输入"), QColor(40, 140, 130), 0, {},
			  3, { { QStringLiteral("水平"), ptNumber, 0.f }, { QStringLiteral("垂直"), ptNumber, 0.f },
				   { QStringLiteral("空格"), ptNumber, 0.f } } },
			// 累加
			{ QStringLiteral("累加"), QColor(110, 130, 60), 2,
			  { { QStringLiteral("数值"), ptNumber, 0.f }, { QStringLiteral("速度"), ptNumber, 3.f } },
			  1, { { QStringLiteral("累计"), ptNumber, 0.f } } },
			// 累减
			{ QStringLiteral("累减"), QColor(150, 100, 60), 2,
			  { { QStringLiteral("数值"), ptNumber, 0.f }, { QStringLiteral("速度"), ptNumber, 3.f } },
			  1, { { QStringLiteral("累计"), ptNumber, 0.f } } },
			// 数值
			{ QStringLiteral("数值"), QColor(150, 120, 60), 0, {},
			  1, { { QStringLiteral("数值"), ptNumber, 0.f } } },
			// 重力
			{ QStringLiteral("重力"), QColor(60, 110, 160), 4,
			  { { QStringLiteral("触发"), ptNumber, 0.f }, { QStringLiteral("起跳速度"), ptNumber, 8.f },
				{ QStringLiteral("重力"), ptNumber, 20.f }, { QStringLiteral("地面高度"), ptNumber, 0.5f } },
			  1, { { QStringLiteral("高度"), ptNumber, 0.f } } },
			// 相加
			{ QStringLiteral("相加"), QColor(90, 140, 150), 2,
			  { { QStringLiteral("A"), ptNumber, 0.f }, { QStringLiteral("B"), ptNumber, 0.f } },
			  1, { { QStringLiteral("输出"), ptNumber, 0.f } } },
			// 运行输出
			{ QStringLiteral("运行输出"), QColor(175, 60, 60), 1,
			  { { QStringLiteral("场景"), ptObject, 0.f } }, 0, {} },
			// 键盘（单键）
			{ QStringLiteral("键盘"), QColor(40, 140, 100), 0, {},
			  2, { { QStringLiteral("按下"), ptNumber, 0.f }, { QStringLiteral("反向"), ptNumber, 0.f } } },
			// 鼠标
			{ QStringLiteral("鼠标"), QColor(150, 90, 110), 0, {},
			  2, { { QStringLiteral("X"), ptNumber, 0.f }, { QStringLiteral("Y"), ptNumber, 0.f } } },
			// 镜头（相机看向被驱动的方块，输入 = 注视点偏移）
			{ QStringLiteral("镜头"), QColor(140, 110, 60), 3,
			  { { QStringLiteral("偏移X"), ptNumber, 0.f }, { QStringLiteral("偏移Y"), ptNumber, 0.4f },
				{ QStringLiteral("偏移Z"), ptNumber, 0.f } },
			  0, {} },
			// 跳转精灵：3D 里的触发体，方块碰到就切关卡；位置/尺寸不接线就保持 3D 里摆的位置
			{ QStringLiteral("跳转精灵"), QColor(70, 190, 180), 4,
			  { { QStringLiteral("位置X"), ptNumber, 0.f }, { QStringLiteral("位置Y"), ptNumber, 0.75f },
				{ QStringLiteral("位置Z"), ptNumber, 0.f }, { QStringLiteral("尺寸"), ptNumber, 1.5f } },
			  1, { { QStringLiteral("物体"), ptObject, 0.f } } },
		};
		return D[(k >= 0 && k < NK_COUNT) ? k : 0];
	}

	int SceneGraph2D::RowsOf(int kind)
	{
		const KindDef& d = KindOf(kind);
		return std::max(1, std::max(d.nIn, d.nOut));
	}

	float SceneGraph2D::Node::Height() const
	{
		// 镜头块绑定了看向对象 / 跳转精灵有目标关卡时，底部多一行说明
		const bool extraRow = (kind == nkCamera && !lookName.isEmpty()) || (kind == nkTrigger);
		const float extra = extraRow ? kRowH : 0.f;
		return kHeadH + (collapsed ? 0.f : kRowH * RowsOf(kind) + extra);
	}

	SceneGraph2D::SceneGraph2D(QWidget* parent)
		: QWidget(parent)
	{
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		BuildDemoScene();
	}

	// 3D 里已有的三个盒子会由 SceneLink 自动生成节点；这里只放两个纯逻辑节点
	void SceneGraph2D::BuildDemoScene()
	{
		AddNode(nkTime, QPointF(-190, -200));
		AddNode(nkOutput, QPointF(130, -200));
		ClearSel();
		update();
	}

	// ================= 基本查询 =================
	int SceneGraph2D::FindNode(int id) const
	{
		for (int i = 0; i < int(m_nodes.size()); ++i)
			if (m_nodes[i].id == id)
				return i;
		return -1;
	}

	int SceneGraph2D::FindGroup(int id) const
	{
		for (int i = 0; i < int(m_groups.size()); ++i)
			if (m_groups[i].id == id)
				return i;
		return -1;
	}

	bool SceneGraph2D::NodeHidden(const Node& n) const
	{
		if (n.group == 0)
			return false;
		const int g = FindGroup(n.group);
		return g >= 0 && m_groups[g].collapsed;
	}

	QRectF SceneGraph2D::GroupRect(int gid) const
	{
		bool any = false;
		QRectF r;
		for (const Node& n : m_nodes)
		{
			if (n.group != gid)
				continue;
			const QRectF b = n.Rect();
			if (!any) { r = b; any = true; }
			else r = r.united(b);
		}
		if (!any)
			return {};
		r.adjust(-kGrpPad, -kGrpPad - kGrpTitle, kGrpPad, kGrpPad);
		const int gi = FindGroup(gid);
		if (gi >= 0 && m_groups[gi].collapsed)
		{
			r.setWidth(kNodeW + 2.f * kGrpPad);
			r.setHeight(kGrpTitle);
		}
		return r;
	}

	void SceneGraph2D::PinPos(const Node& n, bool out, int pin, QPointF& p) const
	{
		if (NodeHidden(n))
		{
			const QRectF r = GroupRect(n.group);
			p = QPointF(out ? r.right() : r.left(), r.top() + kGrpTitle * 0.5f);
			return;
		}
		p.setX(n.pos.x() + (out ? n.Width() : 0.f));
		p.setY(n.collapsed ? n.pos.y() + kHeadH * 0.5f
						   : n.pos.y() + kHeadH + kRowH * (pin + 0.5f));
	}

	QPointF SceneGraph2D::LinkStart(const Link& l) const
	{
		const int i = FindNode(l.from);
		QPointF p;
		if (i >= 0)
			PinPos(m_nodes[i], true, l.fromPin, p);
		return p;
	}

	QPointF SceneGraph2D::LinkEnd(const Link& l) const
	{
		const int i = FindNode(l.to);
		QPointF p;
		if (i >= 0)
			PinPos(m_nodes[i], false, l.toPin, p);
		return p;
	}

	QPainterPath SceneGraph2D::LinkPath(const QPointF& a, const QPointF& b) const
	{
		const float dx = std::max(40.f, std::abs(float(b.x() - a.x())) * 0.5f);
		QPainterPath path(a);
		path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
		return path;
	}

	SceneGraph2D::Hit SceneGraph2D::HitTestNodes(const QPointF& vp) const
	{
		Hit h;
		const float pr = std::max(9.f, kPinR * m_zoom + 4.f) / m_zoom;
		for (int i = int(m_nodes.size()) - 1; i >= 0; --i)
		{
			const Node& n = m_nodes[i];
			if (NodeHidden(n))
				continue;
			const KindDef& d = KindOf(n.kind);
			if (!n.collapsed)
			{
				QPointF p;
				for (int k = 0; k < d.nIn; ++k)
				{
					PinPos(n, false, k, p);
					if (std::hypot(vp.x() - p.x(), vp.y() - p.y()) <= pr)
					{
						h.kind = hkIn; h.node = n.id; h.pin = k;
						return h;
					}
				}
				for (int k = 0; k < d.nOut; ++k)
				{
					PinPos(n, true, k, p);
					if (std::hypot(vp.x() - p.x(), vp.y() - p.y()) <= pr)
					{
						h.kind = hkOut; h.node = n.id; h.pin = k;
						return h;
					}
				}
			}
			if (n.Rect().contains(vp))
			{
				h.kind = hkNode; h.node = n.id;
				return h;
			}
		}
		return h;
	}

	SceneGraph2D::Hit SceneGraph2D::HitTest(const QPointF& vp) const
	{
		Hit h = HitTestNodes(vp);
		if (h.kind != hkNone)
			return h;
		for (int g = int(m_groups.size()) - 1; g >= 0; --g)
		{
			const QRectF r = GroupRect(m_groups[g].id);
			if (r.isNull())
				continue;
			const QRectF title(r.left(), r.top(), r.width(), kGrpTitle);
			if (title.contains(vp))
			{
				h.kind = hkGroup; h.node = m_groups[g].id;
				return h;
			}
		}
		return h;
	}

	// ================= 绘制 =================
	void SceneGraph2D::paintEvent(QPaintEvent*)
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);

		p.fillRect(rect(), QColor(0x1e, 0x1e, 0x22));

		// 网格
		const float minor = 24.f * m_zoom;
		const float major = minor * 5.f;
		p.setPen(QPen(QColor(0x27, 0x27, 0x2c), 1));
		for (double x = std::fmod(double(m_pan.x()), double(minor)); x < width(); x += minor)
			p.drawLine(QPointF(x, 0), QPointF(x, height()));
		for (double y = std::fmod(double(m_pan.y()), double(minor)); y < height(); y += minor)
			p.drawLine(QPointF(0, y), QPointF(width(), y));
		p.setPen(QPen(QColor(0x34, 0x34, 0x3a), 1));
		for (double x = std::fmod(double(m_pan.x()), double(major)); x < width(); x += major)
			p.drawLine(QPointF(x, 0), QPointF(x, height()));
		for (double y = std::fmod(double(m_pan.y()), double(major)); y < height(); y += major)
			p.drawLine(QPointF(0, y), QPointF(width(), y));

		p.translate(m_pan);
		p.scale(m_zoom, m_zoom);

		// ---- 组框（在连线与节点之下）----
		for (const Group& g : m_groups)
		{
			const QRectF r = GroupRect(g.id);
			if (r.isNull())
				continue;
			bool allSel = true, anyMember = false;
			for (const Node& n : m_nodes)
				if (n.group == g.id) { anyMember = true; if (!n.sel) allSel = false; }
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(90, 115, 160, 26));
			p.drawRoundedRect(r, 6, 6);
			p.setBrush(QColor(90, 115, 160, 110));
			p.drawRoundedRect(QRectF(r.left(), r.top(), r.width(), kGrpTitle), 6, 6);
			p.drawRect(QRectF(r.left(), r.top() + kGrpTitle - 6, r.width(), 6));
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(allSel && anyMember ? kSel : QColor(130, 160, 210, 190),
						  allSel && anyMember ? 2.f : 1.f));
			p.drawRoundedRect(r, 6, 6);

			QPolygonF tri;
			const QPointF c(r.right() - 12, r.top() + kGrpTitle * 0.5f);
			if (g.collapsed)
				tri << c + QPointF(-2.5, -3.5) << c + QPointF(3.5, 0) << c + QPointF(-2.5, 3.5);
			else
				tri << c + QPointF(-3.5, -2) << c + QPointF(3.5, -2) << c + QPointF(0, 3.5);
			p.setPen(QPen(QColor(235, 235, 240), 1.2));
			p.drawPolyline(tri);

			QFont f(QStringLiteral("Microsoft YaHei UI"));
			f.setPixelSize(12);
			f.setBold(true);
			p.setFont(f);
			p.setPen(QColor(245, 245, 250));
			p.drawText(QRectF(r.left() + 10, r.top(), r.width() - 30, kGrpTitle),
				Qt::AlignVCenter | Qt::AlignLeft, g.name);
		}

		// ---- 连线 ----
		for (const Link& l : m_links)
		{
			const int ia = FindNode(l.from), ib = FindNode(l.to);
			if (ia < 0 || ib < 0)
				continue;
			const Node& a = m_nodes[ia];
			const Node& b = m_nodes[ib];
			if (a.group != 0 && a.group == b.group && NodeHidden(a))
				continue;
			const KindDef& da = KindOf(a.kind);
			const QColor c = (l.fromPin < da.nOut && da.out[l.fromPin].type == ptObject) ? kObjPin : kNumPin;
			const bool sel = a.sel || b.sel;
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(sel ? kSel : c, sel ? 2.6 : 1.6));
			p.drawPath(LinkPath(LinkStart(l), LinkEnd(l)));
		}

		// ---- 正在拖的新连线 ----
		if (m_drag == dgLink)
		{
			const int i = FindNode(m_linkNode);
			if (i >= 0)
			{
				QPointF pt;
				PinPos(m_nodes[i], m_linkFromOut, m_linkPin, pt);
				const KindDef& d = KindOf(m_nodes[i].kind);
				const bool objPin = m_linkFromOut ? (m_linkPin < d.nOut && d.out[m_linkPin].type == ptObject)
												  : (m_linkPin < d.nIn && d.in[m_linkPin].type == ptObject);
				p.setBrush(Qt::NoBrush);
				p.setPen(QPen(objPin ? kObjPin : kNumPin, 1.8, Qt::DashLine));
				p.drawPath(m_linkFromOut ? LinkPath(pt, m_linkPos) : LinkPath(m_linkPos, pt));
			}
		}

		// ---- 节点 ----
		Hit hover;
		if (m_drag == dgNone || m_drag == dgLink)
			hover = HitTest(ToView(mapFromGlobal(QCursor::pos())));
		for (const Node& n : m_nodes)
			DrawNode(p, n, hover);

		// ---- 框选 ----
		if (m_drag == dgRubber && m_moved)
		{
			const QRectF r = QRectF(m_bandStart, m_bandEnd).normalized();
			p.setPen(QPen(QColor(80, 140, 220), 1));
			p.setBrush(QColor(80, 140, 220, 50));
			p.drawRect(r);
		}

		p.resetTransform();
		DrawStatusText(p);
	}

	void SceneGraph2D::DrawNode(QPainter& p, const Node& n, const Hit& hover)
	{
		if (NodeHidden(n))
			return;
		const KindDef& d = KindOf(n.kind);
		const QRectF r = n.Rect();
		const QRectF head(r.left(), r.top(), r.width(), kHeadH);

		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 90));
		p.drawRoundedRect(r.translated(2, 3), 4, 4);

		p.setBrush(QColor(0x2c, 0x2e, 0x34));
		p.setPen(QPen(n.sel ? kSel : QColor(0x3c, 0x3c, 0x44), n.sel ? 2.f : 1.f));
		p.drawRoundedRect(r, 4, 4);

		// 标题条
		p.setPen(Qt::NoPen);
		p.setBrush(d.head);
		p.drawRoundedRect(head, 4, 4);
		p.drawRect(QRectF(head.left(), head.top() + 8, head.width(), kHeadH - 8));

		QFont f(QStringLiteral("Microsoft YaHei UI"));
		f.setPixelSize(12);
		f.setBold(true);
		p.setFont(f);
		p.setPen(QColor(245, 245, 250));
		p.drawText(head.adjusted(9, 0, -28, 0), Qt::AlignVCenter | Qt::AlignLeft, n.name);

		// 折叠箭头
		QPolygonF tri;
		const QPointF ac(head.right() - 12, head.center().y());
		if (n.collapsed)
			tri << ac + QPointF(-2.5, -3.5) << ac + QPointF(3.5, 0) << ac + QPointF(-2.5, 3.5);
		else
			tri << ac + QPointF(-3.5, -2) << ac + QPointF(3.5, -2) << ac + QPointF(0, 3.5);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(235, 235, 240), 1.2));
		p.drawPolyline(tri);

		if (n.collapsed)
		{
			// 收起的节点：标题右侧显示关键值
			QString extra;
			if (n.kind == nkValue)
				extra = QString::number(n.val[0], 'g', 5);
			else if (n.kind == nkKeys)
				extra = KeyNameOf(n);
			else if (n.kind == nkMouse)
				extra = n.val[0] > 0.5f ? QStringLiteral("不点击控制") : QStringLiteral("点击控制");
			if (!extra.isEmpty())
			{
				QFont ef(QStringLiteral("Microsoft YaHei UI"));
				ef.setPixelSize(11);
				p.setFont(ef);
				p.setPen(QColor(255, 225, 160));
				p.drawText(head.adjusted(0, 0, -22, 0), Qt::AlignVCenter | Qt::AlignRight, extra);
			}
			return;
		}

		QFont pf(QStringLiteral("Microsoft YaHei UI"));
		pf.setPixelSize(11);
		p.setFont(pf);

		for (int k = 0; k < d.nIn; ++k)
		{
			QPointF pt;
			PinPos(n, false, k, pt);
			const bool hot = hover.kind == hkIn && hover.node == n.id && hover.pin == k;
			const float rr = kPinR * (hot ? 1.45f : 1.f);
			p.setPen(QPen(QColor(18, 18, 20), 1));
			p.setBrush(QColor(18, 18, 20));
			p.drawEllipse(pt, rr + 1.4, rr + 1.4);
			p.setPen(Qt::NoPen);
			p.setBrush(d.in[k].type == ptObject ? kObjPin : (hot ? QColor(235, 255, 220) : kNumPin));
			p.drawEllipse(pt, rr, rr);
			p.setPen(QColor(210, 210, 218));
			const QRectF row(r.left(), pt.y() - kRowH * 0.5f, r.width(), kRowH);
			p.drawText(row.adjusted(14, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft, d.in[k].name);
		}
		// 镜头块：看向对象显示在底部额外行
		if (n.kind == nkCamera && !n.lookName.isEmpty())
		{
			p.setPen(QColor(255, 225, 160));
			p.drawText(QRectF(r.left() + 10, r.bottom() - kRowH, r.width() - 20, kRowH),
				Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("看向 ") + n.lookName);
		}
		// 跳转精灵：底部显示要跳到哪一关（空 = 自动下一关）
		if (n.kind == nkTrigger)
		{
			p.setPen(QColor(150, 235, 225));
			p.drawText(QRectF(r.left() + 10, r.bottom() - kRowH, r.width() - 20, kRowH),
				Qt::AlignVCenter | Qt::AlignLeft,
				QStringLiteral("→ ") + (n.target.isEmpty() ? QStringLiteral("自动下一关") : n.target));
		}
		for (int k = 0; k < d.nOut; ++k)
		{
			QPointF pt;
			PinPos(n, true, k, pt);
			const bool hot = hover.kind == hkOut && hover.node == n.id && hover.pin == k;
			const float rr = kPinR * (hot ? 1.45f : 1.f);
			p.setPen(QPen(QColor(18, 18, 20), 1));
			p.setBrush(QColor(18, 18, 20));
			p.drawEllipse(pt, rr + 1.4, rr + 1.4);
			p.setPen(Qt::NoPen);
			p.setBrush(d.out[k].type == ptObject ? kObjPin : (hot ? QColor(235, 255, 220) : kNumPin));
			p.drawEllipse(pt, rr, rr);
			p.setPen(QColor(210, 210, 218));
			QString label = d.out[k].name;
			if (n.kind == nkValue && k == 0)
				label = QString::number(n.val[0], 'g', 5);
			else if (n.kind == nkKeys && k == 0)
				label = KeyNameOf(n) + QStringLiteral(" 按下");
			else if (n.kind == nkMouse)
				label = n.val[0] > 0.5f ? QStringLiteral("不点击控制") : QStringLiteral("点击控制");
			const QRectF row(r.left(), pt.y() - kRowH * 0.5f, r.width(), kRowH);
			p.drawText(row.adjusted(14, 0, -14, 0), Qt::AlignVCenter | Qt::AlignRight, label);
		}
		// 行分隔线
		p.setPen(QPen(QColor(0x35, 0x35, 0x3c), 1));
		for (int k = 1; k < RowsOf(n.kind); ++k)
		{
			const float y = r.top() + kHeadH + kRowH * k;
			p.drawLine(QPointF(r.left() + 1, y), QPointF(r.right() - 1, y));
		}
	}

	void SceneGraph2D::DrawStatusText(QPainter& p)
	{
		p.setFont(QFont(QStringLiteral("Consolas"), 10));
		p.setPen(QColor(208, 208, 214));
		p.drawText(8, 18, QStringLiteral("2D  %1%  [rev 1525  create-menu items: 10]")
			.arg(int(m_zoom * 100.f + 0.5f)));
	}

	// ================= 视图 =================
	QPointF SceneGraph2D::ToView(const QPointF& screenPos) const
	{
		return (screenPos - m_pan) / m_zoom;
	}

	void SceneGraph2D::wheelEvent(QWheelEvent* e)
	{
		const float f = std::pow(1.1f, float(e->angleDelta().y()) / 120.f);
		const float nz = std::clamp(m_zoom * f, 0.15f, 4.f);
		const float k = nz / m_zoom;
		const QPointF a = e->position();
		m_pan = a - (a - m_pan) * k;
		m_zoom = nz;
		update();
	}

	void SceneGraph2D::LocateSelected()
	{
		bool any = false;
		QRectF b;
		for (const Node& n : m_nodes)
		{
			if (!n.sel)
				continue;
			if (!any) { b = n.Rect(); any = true; }
			else b = b.united(n.Rect());
		}
		if (!any)
			return;
		m_pan = QPointF(width() * 0.5, height() * 0.5)
			  - QPointF(b.center().x() * m_zoom, b.center().y() * m_zoom);
		update();
	}

	void SceneGraph2D::FitAll()
	{
		bool any = false;
		QRectF b;
		for (const Node& n : m_nodes)
		{
			if (NodeHidden(n))
				continue;
			if (!any) { b = n.Rect(); any = true; }
			else b = b.united(n.Rect());
		}
		for (const Group& g : m_groups)
		{
			const QRectF r = GroupRect(g.id);
			if (r.isNull()) continue;
			if (!any) { b = r; any = true; }
			else b = b.united(r);
		}
		if (!any)
			return;
		const float z = std::clamp(std::min(float(width()) * 0.9f / std::max(1.f, float(b.width())),
											float(height()) * 0.9f / std::max(1.f, float(b.height()))), 0.15f, 1.5f);
		m_zoom = z;
		m_pan = QPointF(width() * 0.5, height() * 0.5)
			  - QPointF(b.center().x() * z, b.center().y() * z);
		update();
	}

	void SceneGraph2D::ZoomActual()
	{
		const QPointF c(width() * 0.5, height() * 0.5);
		const QPointF w = (c - m_pan) / m_zoom;
		m_zoom = 1.f;
		m_pan = c - w;
		update();
	}

	// ================= 节点编辑 =================
	void SceneGraph2D::AddNode(int kind, const QPointF& vp)
	{
		Node n;
		n.id = m_nextId++;
		n.kind = kind;
		n.name = KindOf(kind).name + QStringLiteral(" ") + QString::number(++m_counter[kind]);
		n.pos = vp;
		n.hasVal = (kind == nkValue || kind == nkKeys || kind == nkMouse);
		for (int k = 0; k < 5; ++k)
			n.val[k] = 0.f;
		if (kind == nkValue)
			n.val[0] = 1.f;
		ClearSel();
		n.sel = true;
		m_nodes.push_back(n);
		update();
	}

	void SceneGraph2D::DeleteSelected()
	{
		for (int i = int(m_nodes.size()) - 1; i >= 0; --i)
		{
			if (!m_nodes[i].sel)
				continue;
			RemoveLinksOf(m_nodes[i].id);
			m_nodes.erase(m_nodes.begin() + i);
		}
		CleanGroups();
		update();
	}

	void SceneGraph2D::RemoveLinksOf(int id)
	{
		for (int i = int(m_links.size()) - 1; i >= 0; --i)
			if (m_links[i].from == id || m_links[i].to == id)
				m_links.erase(m_links.begin() + i);
	}

	void SceneGraph2D::CopySelected()
	{
		m_clipNodes.clear();
		m_clipLinks.clear();
		m_clipGroups.clear();
		for (const Node& n : m_nodes)
			if (n.sel)
				m_clipNodes.push_back(n);
		for (const Group& g : m_groups)
			for (const Node& n : m_clipNodes)
				if (n.group == g.id) { m_clipGroups.push_back(g); break; }
		for (const Link& l : m_links)
		{
			bool a = false, b = false;
			for (const Node& n : m_clipNodes)
			{
				if (n.id == l.from) a = true;
				if (n.id == l.to) b = true;
			}
			if (a && b)
				m_clipLinks.push_back(l);
		}
	}

	void SceneGraph2D::PasteAt(const QPointF& vp)
	{
		if (m_clipNodes.empty())
			return;
		float minx = float(m_clipNodes[0].pos.x()), miny = float(m_clipNodes[0].pos.y());
		for (const Node& n : m_clipNodes)
		{
			minx = std::min(minx, float(n.pos.x()));
			miny = std::min(miny, float(n.pos.y()));
		}
		ClearSel();
		std::vector<std::pair<int, int>> groupMap;
		for (const Group& g : m_clipGroups)
		{
			Group g2 = g;
			g2.id = m_nextGroup++;
			groupMap.push_back({ g.id, g2.id });
			m_groups.push_back(g2);
		}
		auto mapGroup = [&](int gid) {
			for (auto& p : groupMap)
				if (p.first == gid) return p.second;
			return 0;
		};
		std::vector<std::pair<int, int>> idMap;
		for (const Node& src : m_clipNodes)
		{
			Node n = src;
			n.id = m_nextId++;
			n.group = mapGroup(src.group);
			n.pos = vp + (src.pos - QPointF(minx, miny));
			n.name = src.name + QStringLiteral(" 复制");
			n.obj = 0;                     // 粘贴出来的方块 / 平面是新的 3D 对象
			n.sel = true;
			idMap.push_back({ src.id, n.id });
			m_nodes.push_back(n);
		}
		auto mapId = [&](int id) {
			for (auto& p : idMap)
				if (p.first == id) return p.second;
			return 0;
		};
		for (const Link& l : m_clipLinks)
		{
			Link l2 = l;
			l2.from = mapId(l.from);
			l2.to = mapId(l.to);
			m_links.push_back(l2);
		}
		CleanGroups();
		update();
	}

	void SceneGraph2D::RenameNode(int id)
	{
		const int i = FindNode(id);
		if (i < 0)
			return;
		bool ok = false;
		const QString s = QInputDialog::getText(this, QStringLiteral("重命名"), QStringLiteral("名称"),
			QLineEdit::Normal, m_nodes[i].name, &ok);
		if (ok && !s.trimmed().isEmpty())
		{
			m_nodes[i].name = s.trimmed();
			update();
		}
	}

	void SceneGraph2D::RenameGroup(int gid)
	{
		const int i = FindGroup(gid);
		if (i < 0)
			return;
		bool ok = false;
		const QString s = QInputDialog::getText(this, QStringLiteral("重命名"), QStringLiteral("名称"),
			QLineEdit::Normal, m_groups[i].name, &ok);
		if (ok && !s.trimmed().isEmpty())
		{
			m_groups[i].name = s.trimmed();
			update();
		}
	}

	void SceneGraph2D::EditValue(int id)
	{
		const int i = FindNode(id);
		if (i < 0 || m_nodes[i].kind != nkValue)
			return;
		bool ok = false;
		const double v = QInputDialog::getDouble(this, QStringLiteral("数值"), QStringLiteral("数值"),
			m_nodes[i].val[0], -1e9, 1e9, 4, &ok);
		if (ok)
		{
			m_nodes[i].val[0] = float(v);
			update();
		}
	}

	QString SceneGraph2D::KeyNameOf(const Node& n) const
	{
		const int k = int(n.val[0] + 0.5f);
		if (k >= 0 && k < 14)
			return kKeyNames[k];
		if (k >= 14)                             // 自定义键：val[0] 直接存 Qt::Key（字母 / 数字）
			return QKeySequence(k).toString(QKeySequence::NativeText);
		return kKeyNames[0];
	}

	void SceneGraph2D::EditKey(int id)
	{
		const int i = FindNode(id);
		if (i < 0 || m_nodes[i].kind != nkKeys)
			return;
		bool ok = false;
		const QString s = QInputDialog::getText(this, QStringLiteral("按键"),
			QStringLiteral("输入按键（W A S D 上 下 左 右 空格 Shift Ctrl Z X C，或任意字母 / 数字）"),
			QLineEdit::Normal, KeyNameOf(m_nodes[i]), &ok);
		if (!ok)
			return;
		const QString u = s.trimmed();
		int found = -1;
		for (int k = 0; k < 14; ++k)
			if (kKeyNames[k].compare(u, Qt::CaseInsensitive) == 0)
			{
				found = k;
				break;
			}
		static const char* extra[][2] = { { "UP", "\u4E0A" }, { "DOWN", "\u4E0B" }, { "LEFT", "\u5DE6" },
										  { "RIGHT", "\u53F3" }, { "SPACE", "\u7A7A\u683C" }, { "CONTROL", "Ctrl" } };
		if (found < 0)
			for (auto& e2 : extra)
				if (u.compare(QLatin1String(e2[0]), Qt::CaseInsensitive) == 0)
					for (int k = 0; k < 14; ++k)
						if (kKeyNames[k] == QString::fromUtf8(e2[1]))
							found = k;
		// 自定义键：单个字母 / 数字，val[0] 直接存 Qt::Key（字母数字键码与 Windows VK 一致）
		QChar custom = u'\0';
		if (found < 0 && u.length() == 1)
		{
			const QChar ch = u.at(0).toUpper();
			if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
				custom = ch;
		}
		if (found >= 0)
		{
			m_nodes[i].val[0] = float(found);
			update();
		}
		else if (custom != u'\0')
		{
			m_nodes[i].val[0] = float(custom.unicode());
			update();
		}
		else
			QMessageBox::information(this, QStringLiteral("按键"), QStringLiteral("无法识别的按键（支持内置 14 键与任意字母 / 数字）"));
	}

	// ================= 组 =================
	void SceneGraph2D::CleanGroups()
	{
		for (int g = int(m_groups.size()) - 1; g >= 0; --g)
		{
			int c = 0;
			for (const Node& n : m_nodes)
				if (n.group == m_groups[g].id)
					++c;
			if (c < 2)
			{
				for (Node& n : m_nodes)
					if (n.group == m_groups[g].id)
						n.group = 0;
				m_groups.erase(m_groups.begin() + g);
			}
		}
	}

	void SceneGraph2D::GroupSelected()
	{
		if (SelCount() < 2)
			return;
		Group g;
		g.id = m_nextGroup++;
		g.name = QStringLiteral("组 ") + QString::number(++m_groupCounter);
		m_groups.push_back(g);
		const int gid = g.id;
		for (Node& n : m_nodes)
			if (n.sel)
				n.group = gid;
		CleanGroups();
		update();
	}

	void SceneGraph2D::UngroupSelected()
	{
		for (Node& n : m_nodes)
			if (n.sel)
				n.group = 0;
		CleanGroups();
		update();
	}

	bool SceneGraph2D::AnySelectedGrouped() const
	{
		for (const Node& n : m_nodes)
			if (n.sel && n.group != 0)
				return true;
		return false;
	}

	int SceneGraph2D::SelCount() const
	{
		int c = 0;
		for (const Node& n : m_nodes)
			if (n.sel)
				++c;
		return c;
	}

	void SceneGraph2D::ClearSel()
	{
		for (Node& n : m_nodes)
			n.sel = false;
	}

	void SceneGraph2D::BringToFront(int id)
	{
		const int i = FindNode(id);
		if (i < 0 || i == int(m_nodes.size()) - 1)
			return;
		Node n = m_nodes[i];
		m_nodes.erase(m_nodes.begin() + i);
		m_nodes.push_back(n);
	}

	// ================= 连线 =================
	bool SceneGraph2D::Reaches(int from, int target, QSet<int>& seen) const
	{
		if (from == target)
			return true;
		if (seen.contains(from))
			return false;
		seen.insert(from);
		for (const Link& l : m_links)
			if (l.from == from && Reaches(l.to, target, seen))
				return true;
		return false;
	}

	void SceneGraph2D::TryLink(const Hit& h)
	{
		int oN = 0, oP = 0, iN = 0, iP = 0;
		if (m_linkFromOut)
		{
			if (h.kind != hkIn) return;
			oN = m_linkNode; oP = m_linkPin; iN = h.node; iP = h.pin;
		}
		else
		{
			if (h.kind != hkOut) return;
			oN = h.node; oP = h.pin; iN = m_linkNode; iP = m_linkPin;
		}
		if (oN == iN)
			return;
		const int io = FindNode(oN), ii = FindNode(iN);
		if (io < 0 || ii < 0)
			return;
		const KindDef& dO = KindOf(m_nodes[io].kind);
		const KindDef& dI = KindOf(m_nodes[ii].kind);
		if (oP >= dO.nOut || iP >= dI.nIn)
			return;
		if (dO.out[oP].type != dI.in[iP].type)
			return;                              // 数值只能接数值，物体只能接物体
		QSet<int> seen;
		if (Reaches(iN, oN, seen))
			return;                              // 会形成环
		const bool multi = (dI.in[iP].type == ptObject);
		for (int i = int(m_links.size()) - 1; i >= 0; --i)
		{
			const Link& l = m_links[i];
			if (l.to == iN && l.toPin == iP && (!multi || (l.from == oN && l.fromPin == oP)))
				m_links.erase(m_links.begin() + i);
		}
		Link l;
		l.from = oN; l.fromPin = oP; l.to = iN; l.toPin = iP;
		m_links.push_back(l);
		update();
	}

	// ================= 折叠 =================
	bool SceneGraph2D::ToggleFold(int id)
	{
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		const bool dbl = (id == m_clickId) && (now - m_clickTick <= 400);
		m_clickId = id;
		m_clickTick = now;
		if (!dbl)
			return false;
		const int i = FindNode(id);
		if (i < 0)
			return false;
		m_nodes[i].collapsed = !m_nodes[i].collapsed;
		m_clickId = 0;
		update();
		return true;
	}

	bool SceneGraph2D::ToggleGroupFold(int gid)
	{
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		const bool dbl = (-gid == m_clickId) && (now - m_clickTick <= 400);
		m_clickId = -gid;
		m_clickTick = now;
		if (!dbl)
			return false;
		const int g = FindGroup(gid);
		if (g >= 0)
			m_groups[g].collapsed = !m_groups[g].collapsed;
		m_clickId = 0;
		update();
		return true;
	}

	// ================= 鼠标 =================
	void SceneGraph2D::BeginMove(const QPointF& vp)
	{
		m_drag = dgMove;
		m_dragStart = vp;
		m_moveStart.clear();
		for (const Node& n : m_nodes)
			if (n.sel)
				m_moveStart.push_back({ n.id, n.pos });
	}

	void SceneGraph2D::mousePressEvent(QMouseEvent* e)
	{
		setFocus();
		m_pressPos = e->position();
		m_pressPan = m_pan;
		m_moved = false;

		if (e->button() == Qt::MiddleButton)
		{
			m_drag = dgPan;
			return;
		}
		if (e->button() == Qt::RightButton)
		{
			m_menuHit = HitTest(ToView(e->position()));
			m_menuWorld = ToView(e->position()) - QPointF(kNodeW * 0.5f, 0);
			if (m_menuHit.kind == hkNode || m_menuHit.kind == hkIn || m_menuHit.kind == hkOut)
			{
				const int i = FindNode(m_menuHit.node);
				if (i >= 0 && !m_nodes[i].sel)
				{
					ClearSel();
					m_nodes[i].sel = true;
				}
			}
			else if (m_menuHit.kind == hkNone)
				ClearSel();
			update();
			return;
		}
		if (e->button() != Qt::LeftButton)
			return;

		const QPointF vp = ToView(e->position());
		const Hit h = HitTest(vp);

		if (h.kind == hkOut)
		{
			m_drag = dgLink;
			m_linkNode = h.node;
			m_linkPin = h.pin;
			m_linkFromOut = true;
			m_linkPos = vp;
		}
		else if (h.kind == hkIn)
		{
			m_drag = dgLink;
			int li = -1;
			for (int i = int(m_links.size()) - 1; i >= 0; --i)
				if (m_links[i].to == h.node && m_links[i].toPin == h.pin) { li = i; break; }
			if (li >= 0)             // 从已连接的输入脚把线拎起来
			{
				m_linkNode = m_links[li].from;
				m_linkPin = m_links[li].fromPin;
				m_linkFromOut = true;
				m_links.erase(m_links.begin() + li);
			}
			else
			{
				m_linkNode = h.node;
				m_linkPin = h.pin;
				m_linkFromOut = false;
			}
			m_linkPos = vp;
		}
		else if (h.kind == hkGroup)
		{
			if (!ToggleGroupFold(h.node))
			{
				ClearSel();
				for (Node& n : m_nodes)
					if (n.group == h.node)
						n.sel = true;
				BeginMove(vp);
			}
		}
		else if (h.kind == hkNode)
		{
			int i = FindNode(h.node);
			if (i < 0)
				return;
			if (e->modifiers() & Qt::ControlModifier)
				m_nodes[i].sel = !m_nodes[i].sel;
			else if (!m_nodes[i].sel)
			{
				ClearSel();
				m_nodes[i].sel = true;
			}
			BringToFront(h.node);
			i = FindNode(h.node);
			if (m_nodes[i].HeadRect().contains(vp))
			{
				if (ToggleFold(h.node))      // 双击标题：折叠 / 展开
				{
					m_drag = dgNone;
					return;
				}
				BeginMove(vp);
			}
			else
			{
				const int kind = m_nodes[i].kind;
				if (kind == nkValue)
					EditValue(h.node);
				else if (kind == nkKeys)
					EditKey(h.node);
				else if (kind == nkMouse)
					m_nodes[i].val[0] = m_nodes[i].val[0] > 0.5f ? 0.f : 1.f;
				else
					BeginMove(vp);
			}
		}
		else
		{
			if (!(e->modifiers() & Qt::ControlModifier))
				ClearSel();
			m_drag = dgRubber;
			m_bandStart = m_bandEnd = vp;
		}
		update();
	}

	void SceneGraph2D::mouseDoubleClickEvent(QMouseEvent* e)
	{
		if (e->button() != Qt::LeftButton)
			return;
		const Hit h = HitTest(ToView(e->position()));
		if (h.kind == hkGroup)
		{
			const int g = FindGroup(h.node);
			if (g >= 0)
			{
				m_groups[g].collapsed = !m_groups[g].collapsed;
				update();
			}
		}
	}

	void SceneGraph2D::mouseMoveEvent(QMouseEvent* e)
	{
		const QPointF d = e->position() - m_pressPos;
		if (std::abs(d.x()) + std::abs(d.y()) > 4)
			m_moved = true;

		switch (m_drag)
		{
			case dgPan:
				m_pan = m_pressPan + d;
				update();
				break;
			case dgMove:
			{
				const QPointF dd = d / m_zoom;
				for (auto& p : m_moveStart)
				{
					const int i = FindNode(p.first);
					if (i >= 0)
						m_nodes[i].pos = p.second + dd;
				}
				update();
				break;
			}
			case dgLink:
				m_linkPos = ToView(e->position());
				update();
				break;
			case dgRubber:
				m_bandEnd = ToView(e->position());
				update();
				break;
			default:
				break;
		}
	}

	void SceneGraph2D::mouseReleaseEvent(QMouseEvent* e)
	{
		if (e->button() == Qt::RightButton)
		{
			if (!m_moved)
				ShowMenu(e->globalPosition().toPoint(), ToView(e->position()));
			return;
		}
		if (e->button() == Qt::MiddleButton)
		{
			// 关键：中键释放也要清掉拖动状态。之前走不到下面的清空逻辑，
			// m_drag 一直是 dgPan —— 单击滚轮松开后，鼠标一动视图就跟着跑
			//（看起来像"点击滚轮移动"）。
			m_drag = dgNone;
			m_moveStart.clear();
			return;
		}
		if (e->button() != Qt::LeftButton)
			return;

		if (m_drag == dgLink)
		{
			const Hit h = HitTest(ToView(e->position()));
			if (h.kind == hkIn || h.kind == hkOut)
				TryLink(h);
		}
		else if (m_drag == dgRubber && m_moved)
		{
			const QRectF r = QRectF(m_bandStart, m_bandEnd).normalized();
			for (Node& n : m_nodes)
			{
				if (NodeHidden(n))
					continue;
				if (r.intersects(n.Rect()))
					n.sel = true;
			}
		}
		m_drag = dgNone;
		m_moveStart.clear();
		update();
	}

	// ================= 键盘 =================
	void SceneGraph2D::keyPressEvent(QKeyEvent* e)
	{
		const bool ctrl = e->modifiers() & Qt::ControlModifier;
		const bool shift = e->modifiers() & Qt::ShiftModifier;
		switch (e->key())
		{
			case Qt::Key_Delete:
				DeleteSelected();
				return;
			case Qt::Key_F2:
			{
				int id = 0, c = 0;
				for (const Node& n : m_nodes)
					if (n.sel) { id = n.id; ++c; }
				if (c == 1)
					RenameNode(id);
				return;
			}
			case Qt::Key_C:
				if (ctrl) { CopySelected(); return; }
				break;
			case Qt::Key_V:
				if (ctrl) { PasteAt(ToView(mapFromGlobal(QCursor::pos()))); return; }
				break;
			case Qt::Key_G:
				if (ctrl) { shift ? UngroupSelected() : GroupSelected(); return; }
				break;
			case Qt::Key_Z:
				if (!ctrl) { LocateSelected(); return; }
				break;
			case Qt::Key_F:
				if (!ctrl) { FitAll(); return; }
				break;
			case Qt::Key_X:
				if (!ctrl) { ZoomActual(); return; }
				break;
			default:
				break;
		}
		QWidget::keyPressEvent(e);
	}

	// ================= 右键菜单 =================
	void SceneGraph2D::SetKindValue(int kind, float v, const QPointF& at, int menuNode)
	{
		bool done = false;
		const int hit = menuNode ? FindNode(menuNode) : -1;
		if (hit >= 0 && m_nodes[hit].kind == kind)
		{
			m_nodes[hit].val[0] = v;
			m_nodes[hit].hasVal = true;
			done = true;
		}
		else
		{
			for (Node& n : m_nodes)
				if (n.sel && n.kind == kind)
				{
					n.val[0] = v;
					n.hasVal = true;
					done = true;
				}
		}
		if (!done)
		{
			AddNode(kind, at);
			m_nodes.back().val[0] = v;
			m_nodes.back().hasVal = true;
		}
		update();
	}

	void SceneGraph2D::ShowMenu(const QPoint& globalPos, const QPointF& viewPos)
	{
		Q_UNUSED(viewPos);
		const int menuNode = (m_menuHit.kind == hkNode || m_menuHit.kind == hkIn || m_menuHit.kind == hkOut)
							 ? m_menuHit.node : 0;
		const QPointF at = m_menuWorld;

		QMenu menu(this);
		menu.setStyleSheet(MenuStyle());

		// ---- 创建（方块 / 平面 / 跳转精灵 在前，其余逻辑节点随后）----
		QMenu* mc = menu.addMenu(QStringLiteral("创建"));
		for (int k = 0; k < nkKeys; ++k)
		{
			QAction* a = mc->addAction(KindOf(k).name);
			a->setData(100 + k);
			if (k == nkPlane)                 // 平面之后插入跳转精灵（与创建菜单同序）
			{
				QAction* at2 = mc->addAction(KindOf(nkTrigger).name);
				at2->setData(100 + nkTrigger);
			}
		}

		// ---- 编辑 ----
		QMenu* me = menu.addMenu(QStringLiteral("编辑"));
		QAction* aCopy = me->addAction(QStringLiteral("复制"));
		aCopy->setData(11);
		aCopy->setEnabled(SelCount() > 0);
		QAction* aPaste = me->addAction(QStringLiteral("粘贴"));
		aPaste->setData(12);
		aPaste->setEnabled(!m_clipNodes.empty());
		QAction* aRename = me->addAction(QStringLiteral("重命名"));
		aRename->setData(13);
		int single = 0, cnt = 0;
		for (const Node& n : m_nodes)
			if (n.sel) { single = n.id; ++cnt; }
		const bool groupUnderCursor = (m_menuHit.kind == hkGroup);
		aRename->setEnabled(groupUnderCursor || menuNode != 0 || cnt == 1);

		// ---- 组 ----
		QMenu* mg = menu.addMenu(QStringLiteral("组"));
		QAction* aGroup = mg->addAction(QStringLiteral("成组"));
		aGroup->setData(21);
		aGroup->setEnabled(SelCount() >= 2);
		QAction* aUngroup = mg->addAction(QStringLiteral("解组"));
		aUngroup->setData(22);
		aUngroup->setEnabled(AnySelectedGrouped());

		menu.addSeparator();

		// ---- 键盘（14 个按键）----
		QMenu* mk = menu.addMenu(QStringLiteral("键盘"));
		for (int i = 0; i < 14; ++i)
		{
			QAction* a = mk->addAction(kKeyNames[i]);
			a->setData(200 + i);
		}

		// ---- 鼠标 ----
		QMenu* mm = menu.addMenu(QStringLiteral("鼠标"));
		QAction* aClick = mm->addAction(QStringLiteral("点击控制"));
		aClick->setData(41);
		QAction* aNoClick = mm->addAction(QStringLiteral("不点击控制"));
		aNoClick->setData(42);

		// ---- 镜头 ----
		QMenu* mcam = menu.addMenu(QStringLiteral("镜头"));
		QAction* aCam = mcam->addAction(QStringLiteral("看向方块"));
		aCam->setData(51);
		// 右键镜头块：选择它看向哪个 3D 方块（运行时相机注视点锁定该对象）
		const int camIdx = menuNode ? FindNode(menuNode) : -1;
		if (camIdx >= 0 && m_nodes[camIdx].kind == nkCamera && ObjLister)
		{
			QMenu* mlook = mcam->addMenu(QStringLiteral("看向"));
			QAction* aAuto = mlook->addAction(QStringLiteral("自动（第一个被驱动的方块）"));
			aAuto->setData(60);
			aAuto->setCheckable(true);
			aAuto->setChecked(m_nodes[camIdx].obj == 0);
			const auto objs = ObjLister();
			for (int i = 0; i < int(objs.size()) && i < 30; ++i)
			{
				QAction* a = mlook->addAction(objs[i].second);
				a->setData(70 + i);
				a->setCheckable(true);
				a->setChecked(m_nodes[camIdx].obj == objs[i].first);
			}
		}

		menu.addSeparator();
		QAction* build = menu.addAction(QStringLiteral("rev 1525  创建菜单 10 项"));
		build->setEnabled(false);

		QAction* act = menu.exec(globalPos);
		if (!act)
			return;
		bool ok = false;
		const int t = act->data().toInt(&ok);
		if (!ok)
			return;

		if (t >= 100 && t < 100 + NK_COUNT)
		{
			AddNode(t - 100, at);
			return;
		}
		if (t >= 200 && t < 214)
		{
			SetKindValue(nkKeys, float(t - 200), at, menuNode);
			return;
		}
		if (t == 60 || (t >= 70 && t < 100))          // 镜头块「看向」选择
		{
			if (camIdx >= 0 && m_nodes[camIdx].kind == nkCamera)
			{
				if (t == 60)
				{
					m_nodes[camIdx].obj = 0;
					m_nodes[camIdx].lookName.clear();
				}
				else if (ObjLister)
				{
					const auto objs = ObjLister();
					const int i = t - 70;
					if (i < int(objs.size()))
					{
						m_nodes[camIdx].obj = objs[i].first;
						m_nodes[camIdx].lookName = objs[i].second;
					}
				}
				update();
			}
			return;
		}
		switch (t)
		{
			case 11: CopySelected(); break;
			case 12: PasteAt(at); break;
			case 13:
				if (groupUnderCursor) { RenameGroup(m_menuHit.node); break; }
				RenameNode(menuNode ? menuNode : single);
				break;
			case 21: GroupSelected(); break;
			case 22: UngroupSelected(); break;
			case 41: SetKindValue(nkMouse, 0.f, at, menuNode); break;
			case 42: SetKindValue(nkMouse, 1.f, at, menuNode); break;
			case 51: AddNode(nkCamera, at); break;
			default: break;
		}
		update();
	}

	void SceneGraph2D::resizeEvent(QResizeEvent*)
	{
		if (!m_panInit && width() > 50 && height() > 50)
		{
			m_pan = QPointF(width() * 0.5, height() * 0.5);   // 视图 (0,0) 居中
			m_panInit = true;
		}
		update();
	}

	// ================= 与 3D 配对 =================
	void SceneGraph2D::GetLinkNodes(std::vector<LinkInfo>& out) const
	{
		out.clear();
		for (const Node& n : m_nodes)
		{
			if (n.kind != nkBox && n.kind != nkPlane && n.kind != nkTrigger)
				continue;
			LinkInfo li;
			li.id = n.id;
			li.kind = n.kind;
			li.obj = n.obj;
			li.name = n.name;
			li.target = n.target;
			const KindDef& d = KindOf(n.kind);
			for (int k = 0; k < 5; ++k)
				li.v[k] = n.hasVal ? n.val[k] : d.in[k].def;
			// 跳转精灵只有 4 个引脚（位置 XYZ + 尺寸），第 5 个值给 FromVals 当尺寸用
			if (n.kind == nkTrigger)
				li.v[4] = n.hasVal ? n.val[3] : 1.5f;
			out.push_back(li);
		}
	}

	int SceneGraph2D::AddLinkedNode(int kind, const QString& name, int objUid, const float v[5])
	{
		Node n;
		n.id = m_nextId++;
		n.kind = kind;
		n.name = name;
		n.obj = objUid;
		n.hasVal = true;
		for (int k = 0; k < 5; ++k)
			n.val[k] = v[k];
		float minx = -300.f, bottom = -240.f;
		if (!m_nodes.empty())
		{
			minx = float(m_nodes[0].pos.x());
			bottom = float(m_nodes[0].pos.y() + m_nodes[0].Height());
			for (const Node& o : m_nodes)
			{
				minx = std::min(minx, float(o.pos.x()));
				bottom = std::max(bottom, float(o.pos.y() + o.Height()));
			}
		}
		n.pos = QPointF(minx, bottom + 40.f);
		++m_counter[kind];
		m_nodes.push_back(n);
		update();
		return n.id;
	}

	void SceneGraph2D::SetNodeObj(int id, int objUid)
	{
		const int i = FindNode(id);
		if (i >= 0)
			m_nodes[i].obj = objUid;
	}

	void SceneGraph2D::SetNodeName(int id, const QString& name)
	{
		const int i = FindNode(id);
		if (i >= 0 && m_nodes[i].name != name)
		{
			m_nodes[i].name = name;
			update();
		}
	}

	void SceneGraph2D::SetNodeTarget(int id, const QString& target)
	{
		const int i = FindNode(id);
		if (i >= 0 && m_nodes[i].target != target)
		{
			m_nodes[i].target = target;
			update();
		}
	}

	void SceneGraph2D::SetNodeVals(int id, const float v[5])
	{
		const int i = FindNode(id);
		if (i < 0)
			return;
		m_nodes[i].hasVal = true;
		bool changed = false;
		for (int k = 0; k < 5; ++k)
		{
			if (std::abs(m_nodes[i].val[k] - v[k]) > 1e-4f)
				changed = true;
			m_nodes[i].val[k] = v[k];
		}
		if (changed)
			update();
	}

	void SceneGraph2D::SelectNodesByObj(const std::vector<int>& uids)
	{
		bool changed = false;
		for (Node& n : m_nodes)
		{
			const bool on = n.obj != 0 && std::find(uids.begin(), uids.end(), n.obj) != uids.end();
			if (n.sel != on)
			{
				n.sel = on;
				changed = true;
			}
		}
		if (changed)
			update();
	}

	void SceneGraph2D::RemoveNodeById(int id)
	{
		const int i = FindNode(id);
		if (i < 0)
			return;
		RemoveLinksOf(id);
		m_nodes.erase(m_nodes.begin() + i);
		CleanGroups();
		update();
	}

	// ================= 文档存取（.yu） =================
	void SceneGraph2D::SaveState(QJsonObject& out) const
	{
		QJsonArray nodes;
		for (const Node& n : m_nodes)
		{
			QJsonObject o;
			o["id"] = n.id;
			o["kind"] = n.kind;
			o["name"] = n.name;
			o["x"] = n.pos.x();
			o["y"] = n.pos.y();
			o["group"] = n.group;
			o["obj"] = n.obj;
			if (!n.lookName.isEmpty())
				o["lookName"] = n.lookName;
			if (!n.target.isEmpty())
				o["target"] = n.target;
			o["collapsed"] = n.collapsed;
			o["hasVal"] = n.hasVal;
			QJsonArray v;
			for (int k = 0; k < 5; ++k)
				v.append(double(n.val[k]));
			o["val"] = v;
			nodes.append(o);
		}
		QJsonArray links;
		for (const Link& l : m_links)
		{
			QJsonObject o;
			o["from"] = l.from;
			o["fromPin"] = l.fromPin;
			o["to"] = l.to;
			o["toPin"] = l.toPin;
			links.append(o);
		}
		QJsonArray groups;
		for (const Group& g : m_groups)
		{
			QJsonObject o;
			o["id"] = g.id;
			o["name"] = g.name;
			o["collapsed"] = g.collapsed;
			groups.append(o);
		}
		out["nodes"] = nodes;
		out["links"] = links;
		out["groups"] = groups;
		out["nextId"] = m_nextId;
		out["nextGroup"] = m_nextGroup;
		out["groupCounter"] = m_groupCounter;
	}

	void SceneGraph2D::LoadState(const QJsonObject& in)
	{
		m_nodes.clear();
		m_links.clear();
		m_groups.clear();
		m_clipNodes.clear();
		m_clipLinks.clear();
		m_clipGroups.clear();
		m_moveStart.clear();
		for (int k = 0; k < NK_COUNT; ++k)
			m_counter[k] = 0;

		const QJsonArray nodes = in["nodes"].toArray();
		for (const QJsonValue& v : nodes)
		{
			const QJsonObject o = v.toObject();
			Node n;
			n.id = o["id"].toInt();
			n.kind = o["kind"].toInt();
			n.name = o["name"].toString();
			n.pos = QPointF(o["x"].toDouble(), o["y"].toDouble());
			n.group = o["group"].toInt();
			n.obj = o["obj"].toInt();
			n.lookName = o["lookName"].toString();
			n.target = o["target"].toString();
			n.collapsed = o["collapsed"].toBool();
			n.hasVal = o["hasVal"].toBool();
			const QJsonArray vals = o["val"].toArray();
			for (int k = 0; k < 5 && k < vals.size(); ++k)
				n.val[k] = float(vals.at(k).toDouble());
			m_nodes.push_back(n);
			if (n.kind >= 0 && n.kind < NK_COUNT)
				++m_counter[n.kind];
		}
		const QJsonArray links = in["links"].toArray();
		for (const QJsonValue& v : links)
		{
			const QJsonObject o = v.toObject();
			Link l;
			l.from = o["from"].toInt();
			l.fromPin = o["fromPin"].toInt();
			l.to = o["to"].toInt();
			l.toPin = o["toPin"].toInt();
			m_links.push_back(l);
		}
		const QJsonArray groups = in["groups"].toArray();
		for (const QJsonValue& v : groups)
		{
			const QJsonObject o = v.toObject();
			Group g;
			g.id = o["id"].toInt();
			g.name = o["name"].toString();
			g.collapsed = o["collapsed"].toBool();
			m_groups.push_back(g);
		}

		// id 计数：优先用保存值，缺失时按现有最大 id 推
		int maxId = 0, maxGrp = 0;
		for (const Node& n : m_nodes)
			maxId = std::max(maxId, n.id);
		for (const Group& g : m_groups)
			maxGrp = std::max(maxGrp, g.id);
		m_nextId = std::max(in["nextId"].toInt(maxId + 1), maxId + 1);
		m_nextGroup = std::max(in["nextGroup"].toInt(maxGrp + 1), maxGrp + 1);
		m_groupCounter = in["groupCounter"].toInt(m_groups.size());

		m_drag = dgNone;
		update();
	}

	void SceneGraph2D::ResetDocument()
	{
		LoadState(QJsonObject{});
	}

	// ================= 编辑菜单入口 =================
	void SceneGraph2D::PasteAtCenter()
	{
		// 菜单粘贴没有鼠标位置，落在视图中心
		PasteAt(ToView(QPointF(width() * 0.5, height() * 0.5)));
	}

	void SceneGraph2D::SelectAllNodes()
	{
		ClearSel();
		for (Node& n : m_nodes)
			if (!NodeHidden(n))
				n.sel = true;
		update();
	}
}
