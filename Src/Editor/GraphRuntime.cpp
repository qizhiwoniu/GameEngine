#include "GraphRuntime.h"
#include "SceneGraph2D.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace GameEditor
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		// 与 SceneGraph2D 的 kKeyNames 一一对应（键盘节点的 val[0] = 键名下标）
		const int kKeyVks[14] = {
			Qt::Key_W, Qt::Key_A, Qt::Key_S, Qt::Key_D,
			Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right,
			Qt::Key_Space, Qt::Key_Shift, Qt::Key_Control, Qt::Key_Z, Qt::Key_X, Qt::Key_C
		};
	}

	bool GraphRuntime::Load(const QJsonObject& g)
	{
		m_nodes.clear();
		m_links.clear();
		m_driven.clear();
		m_outUids.clear();
		m_hasOutput = false;
		m_baseSizes.clear();
		m_time = 0.f;
		m_hasCamNode = false;
		m_camUid = 0;
		m_camOff = QVector3D(0, 0.4f, 0);
		if (g.isEmpty())
			return false;

		for (const QJsonValue& v : g["nodes"].toArray())
		{
			const QJsonObject j = v.toObject();
			Node n;
			n.id = j["id"].toInt();
			n.kind = j["kind"].toInt();
			n.obj = j["obj"].toInt();
			const QJsonArray val = j["val"].toArray();
			for (int i = 0; i < 5 && i < val.size(); ++i)
				n.val[i] = float(val.at(i).toDouble());
			if (n.id <= 0)
				continue;
			m_nodes.push_back(n);
			if (n.kind == SceneGraph2D::nkCamera)
			{
				m_hasCamNode = true;
				m_camUid = n.obj;                // 0 = 自动（第一个被驱动的方块）
			}
		}
		for (const QJsonValue& v : g["links"].toArray())
		{
			const QJsonObject j = v.toObject();
			Link l;
			l.from = j["from"].toInt();
			l.fromPin = j["fromPin"].toInt();
			l.to = j["to"].toInt();
			l.toPin = j["toPin"].toInt();
			m_links.push_back(l);
		}
		// 运行输出：只有「物体」引脚接到运行输出块的方块 / 平面才算进运行场景。
		// 只要有一个运行输出块真的接了线，就按白名单显示；一个都没接 = 不做过滤（兼容旧场景）。
		for (const Node& n : m_nodes)
		{
			if (n.kind != SceneGraph2D::nkOutput)
				continue;
			for (const Link& l : m_links)
			{
				if (l.to != n.id)
					continue;
				const int s = Find(l.from);
				if (s < 0)
					continue;
				const Node& sn = m_nodes[s];
				if ((sn.kind == SceneGraph2D::nkBox || sn.kind == SceneGraph2D::nkPlane) && sn.obj > 0)
				{
					m_outUids.insert(sn.obj);
					m_hasOutput = true;
				}
			}
		}

		// 只有位置引脚（X/Y/Z）真的接了线的方块 / 平面才算"图接管"：
		// 没接线的节点不写位置，物体保持原样、照常吃重力与碰撞
		for (const Node& n : m_nodes)
		{
			if ((n.kind != SceneGraph2D::nkBox && n.kind != SceneGraph2D::nkPlane) || n.obj <= 0)
				continue;
			for (const Link& l : m_links)
				if (l.to == n.id && l.toPin >= 0 && l.toPin <= 2)
				{
					m_driven.insert(n.obj);
					break;
				}
		}
		return !m_nodes.empty();
	}

	void GraphRuntime::SnapshotSizes(const std::vector<Viewport3D::Obj>& objects)
	{
		m_baseSizes.clear();
		for (const Viewport3D::Obj& o : objects)
			m_baseSizes.push_back(o.size);
	}

	void GraphRuntime::SeedFromObjects(const std::vector<Viewport3D::Obj>& objects)
	{
		for (const Node& n : m_nodes)
		{
			if (n.kind != SceneGraph2D::nkBox && n.kind != SceneGraph2D::nkPlane &&
				n.kind != SceneGraph2D::nkTrigger)
				continue;
			if (n.obj <= 0)
				continue;
			const Viewport3D::Obj* o = nullptr;
			for (const Viewport3D::Obj& ob : objects)
				if (ob.uid == n.obj) { o = &ob; break; }
			if (!o)
				continue;
			SeedChain(n.id, 0, o->pos.x(), 0);
			SeedChain(n.id, 1, o->pos.y(), 0);
			SeedChain(n.id, 2, o->pos.z(), 0);
		}
	}

	// 沿连线回溯：累加 / 累减 = 累计值，重力 = 高度（同时清掉这次运行的速度）。
	// 中间经过「相加」块时把常量那一路减掉（位置 = 状态 + 常量），保证起始位置对得上。
	void GraphRuntime::SeedChain(int nodeId, int pin, float value, int depth)
	{
		if (depth > 16)
			return;
		for (const Link& l : m_links)
		{
			if (l.to != nodeId || l.toPin != pin)
				continue;
			const int s = Find(l.from);
			if (s < 0)
				return;
			Node& src = m_nodes[s];
			switch (src.kind)
			{
				case SceneGraph2D::nkAccum:
				case SceneGraph2D::nkSub:
					src.state = value;
					src.hasState = true;
					return;
				case SceneGraph2D::nkGravity:
					src.state = value;
					src.state2 = 0.f;
					src.hasState = true;
					return;
				case SceneGraph2D::nkAdd:
				{
					const SceneGraph2D::KindDef& d = SceneGraph2D::KindOf(src.kind);
					// 先把所有常量（数值块）那几路扣掉，再往下种；顺序不能反，
					// 否则链那一路先被访问时还没扣常量，起始位置会偏。
					float v = value;
					for (int p = 0; p < d.nIn; ++p)
					{
						for (const Link& l2 : m_links)
						{
							if (l2.to != src.id || l2.toPin != p)
								continue;
							const int si = Find(l2.from);
							if (si >= 0 && m_nodes[si].kind == SceneGraph2D::nkValue)
								v -= m_nodes[si].val[0];
							break;
						}
					}
					for (int p = 0; p < d.nIn; ++p)
					{
						int fromId = -1;
						for (const Link& l2 : m_links)
							if (l2.to == src.id && l2.toPin == p) { fromId = l2.from; break; }
						if (fromId < 0)
							continue;
						const int si = Find(fromId);
						if (si < 0 || m_nodes[si].kind == SceneGraph2D::nkValue)
							continue;
						SeedChain(src.id, p, v, depth + 1);
					}
					return;
				}
				default:
					return;
			}
		}
	}

	int GraphRuntime::Find(int id) const
	{
		for (int i = 0; i < int(m_nodes.size()); ++i)
			if (m_nodes[i].id == id)
				return i;
		return -1;
	}

	// 引脚输入：有连线取来源节点输出（环则退回上一帧值），没有连线用该节点种类的默认值
	float GraphRuntime::Input(int idx, int pin)
	{
		const Node& n = m_nodes[idx];
		for (const Link& l : m_links)
		{
			if (l.to != n.id || l.toPin != pin)
				continue;
			const int s = Find(l.from);
			if (s < 0)
				return 0.f;
			Eval(s);
			return m_nodes[s].done ? m_nodes[s].out[l.fromPin] : m_nodes[s].prev[l.fromPin];
		}
		const SceneGraph2D::KindDef& d = SceneGraph2D::KindOf(n.kind);
		return (pin >= 0 && pin < d.nIn) ? d.in[pin].def : 0.f;
	}

	void GraphRuntime::Eval(int idx)
	{
		Node& n = m_nodes[idx];
		if (n.done)
			return;
		if (n.visiting)
			return;                              // 数据流环：调用方会退回用 prev

		n.visiting = true;
		switch (n.kind)
		{
			case SceneGraph2D::nkBox:
			case SceneGraph2D::nkPlane:
			{
				// 位置 XYZ / 旋转(度) / 缩放 -> 写回配对的 3D 对象
				const float x = Input(idx, 0), y = Input(idx, 1), z = Input(idx, 2);
				const float rotDeg = Input(idx, 3);
				const float scale = std::max(0.02f, Input(idx, 4));
				// 只有真的接了线的轴才写：接线少的节点不会把另外两轴按成默认值 0 / 0.5
				const bool wx = HasInput(n.id, 0), wy = HasInput(n.id, 1), wz = HasInput(n.id, 2);
				n.out[0] = 1.f;
				if (n.obj > 0 && m_objs)
				{
					for (size_t i = 0; i < m_objs->size(); ++i)
					{
						Viewport3D::Obj& o = (*m_objs)[i];
						if (o.uid != n.obj)
							continue;
						if (wx) o.pos.setX(x);
						if (wy) o.pos.setY(y);
						if (wz) o.pos.setZ(z);
						o.rot = QVector3D(0.f, 0.f, rotDeg * kPi / 180.f);
						if (i < m_baseSizes.size())
						{
							const QVector3D& b = m_baseSizes[i];
							o.size = QVector3D(std::max(0.02f, b.x() * scale),
								std::max(0.02f, b.y() * scale),
								std::max(0.02f, b.z() * scale));
						}
						break;
					}
				}
				break;
			}
			case SceneGraph2D::nkTrigger:             // 跳转精灵：位置 / 尺寸（不接线就保持 3D 里摆的）
			{
				const bool wx = HasInput(n.id, 0), wy = HasInput(n.id, 1), wz = HasInput(n.id, 2);
				n.out[0] = 1.f;
				if (n.obj > 0 && m_objs)
				{
					const float s = std::max(0.2f, Input(idx, 3));
					for (size_t i = 0; i < m_objs->size(); ++i)
					{
						Viewport3D::Obj& o = (*m_objs)[i];
						if (o.uid != n.obj)
							continue;
						if (wx) o.pos.setX(Input(idx, 0));
						if (wy) o.pos.setY(Input(idx, 1));
						if (wz) o.pos.setZ(Input(idx, 2));
						if (HasInput(n.id, 3))
							o.size = QVector3D(s, s, s);
						break;
					}
				}
				break;
			}
			case SceneGraph2D::nkTime:
			{
				n.out[0] = m_time;
				n.out[1] = std::sin(m_time);
				n.out[2] = std::cos(m_time);
				n.out[3] = m_time * 180.f / kPi;
				break;
			}
			case SceneGraph2D::nkKey:                 // 键盘输入：水平 / 垂直 / 空格
			{
				auto held = [this](int vk) { return m_keys && m_keys->contains(vk); };
				const float h = float((held(Qt::Key_D) || held(Qt::Key_Right)) - (held(Qt::Key_A) || held(Qt::Key_Left)));
				const float vv = float((held(Qt::Key_W) || held(Qt::Key_Up)) - (held(Qt::Key_S) || held(Qt::Key_Down)));
				n.out[0] = h;
				n.out[1] = vv;
				n.out[2] = held(Qt::Key_Space) ? 1.f : 0.f;
				break;
			}
			case SceneGraph2D::nkAccum:               // 累计 += 数值 * 速度 * dt
			{
				if (!n.hasState) { n.state = 0.f; n.hasState = true; }
				n.state += Input(idx, 0) * Input(idx, 1) * m_dt;
				n.out[0] = n.state;
				break;
			}
			case SceneGraph2D::nkSub:                 // 累计 -= 数值 * 速度 * dt
			{
				if (!n.hasState) { n.state = 0.f; n.hasState = true; }
				n.state -= Input(idx, 0) * Input(idx, 1) * m_dt;
				n.out[0] = n.state;
				break;
			}
			case SceneGraph2D::nkValue:
			{
				n.out[0] = n.val[0];
				break;
			}
			case SceneGraph2D::nkGravity:             // 触发 / 起跳速度 / 重力 / 地面高度 -> 高度
			{
				const float trigger = Input(idx, 0), jumpV = Input(idx, 1);
				const float g = Input(idx, 2);
				const float groundIn = Input(idx, 3);
				// 脚下踩着别的方块时，用物理解算出的支撑面当地面：
				// 这样踩在方块顶上不会一直往下陷，也能从方块顶上起跳
				const float ground = n.hasSupport ? n.supportY : groundIn;
				if (!n.hasState) { n.state = ground; n.state2 = 0.f; n.hasState = true; }
				n.state2 -= g * m_dt;
				if (trigger > 0.5f && n.state <= ground + 0.01f)
					n.state2 = jumpV;                // 落地状态 + 触发 -> 起跳
				n.state += n.state2 * m_dt;
				if (n.state <= ground)
				{
					n.state = ground;
					if (n.state2 < 0.f)              // 触发当帧 y 还没离开地面，别把起跳速度抹掉
						n.state2 = 0.f;
				}
				n.out[0] = n.state;
				break;
			}
			case SceneGraph2D::nkAdd:
			{
				n.out[0] = Input(idx, 0) + Input(idx, 1);
				break;
			}
		case SceneGraph2D::nkKeys:                // 单键：按下 / 反向
		{
			int k = int(n.val[0] + 0.5f);
			int vk;
			if (k >= 0 && k < 14)
				vk = kKeyVks[k];                  // 内置 14 键：下标查表
			else
				vk = k;                           // 自定义键：val[0] 即 Qt::Key（字母数字 = VK 码）
			const float p = (m_keys && m_keys->contains(vk)) ? 1.f : 0.f;
			n.out[0] = p;
			n.out[1] = -p;
			break;
		}
			case SceneGraph2D::nkMouse:
			{
				n.out[0] = 0.f;                      // 运行时鼠标位置暂不接入
				n.out[1] = 0.f;
				break;
			}
		case SceneGraph2D::nkCamera:              // 镜头：相机看向被驱动的方块，输入 = 注视点偏移
		{
			m_hasCam = true;
			m_camOff = QVector3D(Input(idx, 0), Input(idx, 1), Input(idx, 2));
			break;
		}
			case SceneGraph2D::nkOutput:
			default:
				break;
		}

		for (int i = 0; i < 5; ++i)
			n.prev[i] = n.out[i];
		n.visiting = false;
		n.done = true;
	}

	// ---- 碰撞回馈的内部实现 ----

	bool GraphRuntime::HasInput(int nodeId, int pin) const
	{
		for (const Link& l : m_links)
			if (l.to == nodeId && l.toPin == pin)
				return true;
		return false;
	}

	// 沿连线回溯，把 delta 加到"真正存位置"的状态节点上：
	//   累加 / 累减 = 累计值，重力 = 高度。中间经过"相加"块时沿已接线的那一路继续往回找。
	bool GraphRuntime::ShiftState(int nodeId, int pin, float d, int depth)
	{
		if (depth > 16)
			return false;
		for (const Link& l : m_links)
		{
			if (l.to != nodeId || l.toPin != pin)
				continue;
			const int s = Find(l.from);
			if (s < 0)
				return false;
			Node& sn = m_nodes[s];
			switch (sn.kind)
			{
				case SceneGraph2D::nkAccum:   sn.state += d; return true;
				case SceneGraph2D::nkSub:     sn.state += d; return true;   // 输出就是累计值本身
				case SceneGraph2D::nkGravity:
					sn.state += d;
					if (d > 0.f && sn.state2 < 0.f)
						sn.state2 = 0.f;      // 被顶起来：清掉下落速度，别再往下陷
					return true;
				case SceneGraph2D::nkAdd:
					for (int p = 0; p < 2; ++p)
						if (HasInput(sn.id, p) && ShiftState(sn.id, p, d, depth + 1))
							return true;
					return false;
				default:
					return false;             // 数值 / 时间 / 键盘：不是状态，改不了
			}
		}
		return false;
	}

	// 沿 Y 的连线找到重力块，把"脚下支撑面高度"告诉它
	bool GraphRuntime::MarkSupport(int nodeId, int pin, float y, bool on, int depth)
	{
		if (depth > 16)
			return false;
		for (const Link& l : m_links)
		{
			if (l.to != nodeId || l.toPin != pin)
				continue;
			const int s = Find(l.from);
			if (s < 0)
				return false;
			Node& sn = m_nodes[s];
			if (sn.kind == SceneGraph2D::nkGravity)
			{
				sn.hasSupport = on;
				if (on)
					sn.supportY = y;
				return true;
			}
			if (sn.kind == SceneGraph2D::nkAdd)
				for (int p = 0; p < 2; ++p)
					if (MarkSupport(sn.id, p, y, on, depth + 1))
						return true;
			return false;
		}
		return false;
	}

	void GraphRuntime::FeedBack(int uid, const QVector3D& pos, const QVector3D& delta, bool grounded)
	{
		if (uid <= 0)
			return;
		for (int i = 0; i < int(m_nodes.size()); ++i)
		{
			Node& n = m_nodes[i];
			if ((n.kind != SceneGraph2D::nkBox && n.kind != SceneGraph2D::nkPlane) || n.obj != uid)
				continue;

			// ① 被挡下的位移加回驱动该轴的状态节点
			for (int axis = 0; axis < 3; ++axis)
			{
				const float d = (axis == 0 ? delta.x() : axis == 1 ? delta.y() : delta.z());
				if (std::abs(d) < 1e-4f)
					continue;
				if (!HasInput(n.id, axis))
					continue;
				ShiftState(n.id, axis, d, 0);
			}

			// ② 支撑面：踩在方块顶上时重力块以它为地面（能起跳、不下陷）
			MarkSupport(n.id, 1, pos.y(), grounded, 0);
			return;
		}
	}

	void GraphRuntime::Step(float dt, std::vector<Viewport3D::Obj>& objects,
		const QSet<int>& keys, bool& hasCam, QVector3D& camOff, int& camUid)
	{
		m_dt = std::clamp(dt, 0.f, 0.05f);
		m_time += m_dt;
		m_objs = &objects;
		m_keys = &keys;
		m_hasCam = false;

		for (Node& n : m_nodes)
		{
			n.done = false;
			n.visiting = false;
		}
		for (int i = 0; i < int(m_nodes.size()); ++i)
			Eval(i);

		hasCam = m_hasCam;
		camOff = m_camOff;
		camUid = m_camUid;
		m_objs = nullptr;
		m_keys = nullptr;
	}
}
