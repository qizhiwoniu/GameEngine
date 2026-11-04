#include "SceneLink.h"
#include "Viewport3D.h"
#include "SceneGraph2D.h"

#include <QColor>
#include <QDebug>
#include <QString>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace GameEditor
{
	namespace SceneLink
	{
		namespace
		{
			// 上一轮同步时存在的 uid / 双方共用的名字 / 上一轮的 3D 选中
			std::set<int>                 g_known;
			std::map<int, QString>        g_lastName;
			std::vector<int>              g_prevSel;

			// 3D 对象 -> 节点上的 5 个数值
			void ToVals(const Viewport3D::Obj& o, float v[5])
			{
				v[0] = o.pos.x();
				v[1] = o.pos.z();
				v[2] = o.pos.y();
				v[3] = o.rot.z() * 57.2957795f;                 // 弧度 -> 度
				v[4] = (o.kind == 1) ? o.size.x() / 5.f : o.size.x();
			}

			// 节点上的 5 个数值 -> 3D 对象
			Viewport3D::Obj FromVals(int kind, const QString& name, const float v[5])
			{
				Viewport3D::Obj o;
				o.name = name.toStdString();
				o.pos = QVector3D(v[0], v[2], v[1]);
				o.rot = QVector3D(0.f, 0.f, v[3] * 0.0174532925f);
				const float sc = std::max(0.05f, std::fabs(v[4]));
				if (kind == SceneGraph2D::nkPlane)
				{
					o.kind = 1;
					o.size = QVector3D(5.f * sc, 0.02f, 5.f * sc);
					o.pos.setY(std::max(0.01f, v[2]));
					o.color = QColor(100, 160, 210);
				}
				else
				{
					o.kind = 0;
					o.size = QVector3D(sc, sc, sc);
					o.color = QColor(210, 170, 80);
				}
				o.uid = 0;
				return o;
			}
		}

		bool Sync(Viewport3D* v3d, SceneGraph2D* v2d)
		{
			if (!v3d || !v2d)
				return false;
			bool created = false;

			std::vector<SceneGraph2D::LinkInfo> nodes;
			v2d->GetLinkNodes(nodes);

			std::set<int> alive;
			for (int i = 0; i < v3d->ObjectCount(); ++i)
				alive.insert(v3d->ObjAt(i).uid);
			std::set<int> paired;                 // 已经有节点的对象

			// ---- 1) 节点一侧 ----
			for (auto& n : nodes)
			{
				if (n.obj != 0 && paired.count(n.obj))     // 两个节点抢一个对象：后来的是新对象
				{
					v2d->SetNodeObj(n.id, 0);
					n.obj = 0;
				}
				if (n.obj != 0 && !alive.count(n.obj))
				{
					if (g_known.count(n.obj))              // 对象在 3D 里被删了
					{
						g_lastName.erase(n.obj);
						v2d->RemoveNodeById(n.id);
						continue;
					}
					n.obj = 0;
				}
				if (n.obj == 0)                            // 新节点：创建它的 3D 对象
				{
					const Viewport3D::Obj o = FromVals(n.kind, n.name, n.v);
					n.obj = v3d->AddObjectRaw(o);
					v2d->SetNodeObj(n.id, n.obj);
					alive.insert(n.obj);
					g_lastName[n.obj] = n.name;
					created = true;
				}
				paired.insert(n.obj);
			}

			// ---- 2) 没有节点的对象 ----
			std::vector<int> toDelete;
			for (int i = 0; i < v3d->ObjectCount(); ++i)
			{
				const Viewport3D::Obj& o = v3d->ObjAt(i);
				if (paired.count(o.uid))
					continue;
				if (g_known.count(o.uid))                  // 它的节点在 2D 里被删了
				{
					toDelete.push_back(o.uid);
					continue;
				}
				float v[5];
				ToVals(o, v);
				const int kind = (o.kind == 1) ? SceneGraph2D::nkPlane : SceneGraph2D::nkBox;
				v2d->AddLinkedNode(kind, QString::fromStdString(o.name), o.uid, v);
				g_lastName[o.uid] = QString::fromStdString(o.name);
				paired.insert(o.uid);
				created = true;
			}
			std::set<int> newKnown = paired;
			for (int uid : toDelete)
			{
				if (v3d->RemoveByUid(uid))
					g_lastName.erase(uid);
				else
					newKnown.insert(uid);                  // 这次删不掉，下一轮再试
			}

			// ---- 3) 名字与数值 ----
			v2d->GetLinkNodes(nodes);
			for (auto& n : nodes)
			{
				if (n.obj == 0)
					continue;
				const int idx = v3d->IndexOfUid(n.obj);
				if (idx < 0)
					continue;
				const Viewport3D::Obj& o = v3d->ObjAt(idx);
				QString& last = g_lastName[n.obj];
				const QString objName = QString::fromStdString(o.name);
				if (last.isNull() || last.isEmpty())
					last = objName;
				if (n.name != last)                        // 2D 改名
				{
					v3d->SetObjectName(n.obj, n.name.toStdString());
					last = n.name;
				}
				else if (objName != last)                  // 3D 改名
				{
					v2d->SetNodeName(n.id, objName);
					last = objName;
				}
				float v[5];
				ToVals(o, v);
				v2d->SetNodeVals(n.id, v);
			}
			g_known = newKnown;

			// ---- 4) 选中：3D -> 2D ----
			const std::vector<int> sel = v3d->SelectedUids();
			if (created || sel != g_prevSel)
			{
				v2d->SelectNodesByObj(sel);
				g_prevSel = sel;
			}
			return created;
		}
	}
}
