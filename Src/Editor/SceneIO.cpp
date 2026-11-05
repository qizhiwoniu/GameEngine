#include "SceneIO.h"
#include "Viewport3D.h"
#include "SceneGraph2D.h"
#include "Document.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QPixmap>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>
#include <QVector3D>
#include <QtMath>

#include <algorithm>
#include <limits>

namespace GameEditor
{
	namespace SceneIO
	{
		namespace
		{
			// 与 Viewport3D.cpp 的 ObjRotation 保持一致（Z -> Y -> X 依次旋转）
			QMatrix4x4 RotationOf(const QVector3D& rotRad)
			{
				QMatrix4x4 m;
				m.rotate(float(qRadiansToDegrees(rotRad.z())), 0, 0, 1);
				m.rotate(float(qRadiansToDegrees(rotRad.y())), 0, 1, 0);
				m.rotate(float(qRadiansToDegrees(rotRad.x())), 1, 0, 0);
				return m;
			}

			void CornersOf(const Viewport3D::Obj& o, QVector3D out[8])
			{
				const QVector3D h = o.size * 0.5f;
				const QMatrix4x4 R = RotationOf(o.rot);
				const QVector3D loc[8] = {
					{ -h.x(), -h.y(), -h.z() }, { +h.x(), -h.y(), -h.z() },
					{ +h.x(), +h.y(), -h.z() }, { -h.x(), +h.y(), -h.z() },
					{ -h.x(), -h.y(), +h.z() }, { +h.x(), -h.y(), +h.z() },
					{ +h.x(), +h.y(), +h.z() }, { -h.x(), +h.y(), +h.z() },
				};
				for (int i = 0; i < 8; ++i)
					out[i] = o.pos + R * loc[i];
			}

			// 六个面（顶点序号 1..8，与 CornersOf 的顺序对应）
			const int kFaces[6][4] = {
				{ 1, 2, 3, 4 },   // -Z
				{ 5, 8, 7, 6 },   // +Z
				{ 1, 4, 8, 5 },   // -X
				{ 2, 6, 7, 3 },   // +X
				{ 4, 3, 7, 8 },   // +Y
				{ 1, 5, 6, 2 },   // -Y
			};

			const QColor kPalette[] = {
				QColor(120, 190, 120), QColor(190, 140, 90), QColor(150, 120, 190),
				QColor(90, 180, 180), QColor(200, 130, 110),
			};

			QString SafeName(QString n)
			{
				n.replace(' ', '_');
				return n.isEmpty() ? QStringLiteral("Object") : n;
			}
		}

		// ================= 导出 OBJ =================
		bool ExportObj(const QString& path, Viewport3D* v3d, bool selectedOnly, QString* err)
		{
			if (!v3d)
			{
				if (err) *err = QStringLiteral("内部错误：视口未创建");
				return false;
			}
			QFile f(path);
			if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
			{
				if (err) *err = QStringLiteral("无法写入文件：%1").arg(path);
				return false;
			}

			QTextStream ts(&f);
			ts.setEncoding(QStringConverter::Utf8);
			ts << "# exported by 3DGameEngine\n";

			int base = 1;      // OBJ 顶点下标从 1 开始
			int exported = 0;
			for (const auto& o : v3d->Objects())
			{
				if (selectedOnly && !o.selected)
					continue;
				QVector3D c[8];
				CornersOf(o, c);
				ts << "o " << SafeName(QString::fromStdString(o.name)) << "\n";
				for (int i = 0; i < 8; ++i)
					ts << "v " << c[i].x() << ' ' << c[i].y() << ' ' << c[i].z() << "\n";
				for (const auto& fc : kFaces)
					ts << "f " << base + fc[0] - 1 << ' ' << base + fc[1] - 1 << ' '
					   << base + fc[2] - 1 << ' ' << base + fc[3] - 1 << "\n";
				base += 8;
				++exported;
			}
			ts.flush();

			if (exported == 0)
			{
				if (err) *err = QStringLiteral("没有可导出的对象%1")
					.arg(selectedOnly ? QStringLiteral("（当前没有选中项）") : QString());
				return false;
			}
			return true;
		}

		// ================= 导入 OBJ =================
		// 只取顶点与面，按 o / g 分组；每组用包围盒变成一个方块（本引擎渲染的是方块）。
		int ImportObj(const QString& path, Viewport3D* v3d, QString* err)
		{
			if (!v3d)
			{
				if (err) *err = QStringLiteral("内部错误：视口未创建");
				return 0;
			}
			QFile f(path);
			if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
			{
				if (err) *err = QStringLiteral("无法打开文件：%1").arg(path);
				return 0;
			}
			QTextStream ts(&f);
			ts.setEncoding(QStringConverter::Utf8);

			struct Group { QString name; std::vector<QVector3D> pts; };
			std::vector<QVector3D> verts;
			std::vector<Group> groups;
			Group cur;
			cur.name = QStringLiteral("Object");

			auto flush = [&]
				{
					if (!cur.pts.empty())
						groups.push_back(cur);
					cur = Group{ QStringLiteral("Object"), {} };
				};

			while (!ts.atEnd())
			{
				const QString line = ts.readLine().trimmed();
				if (line.isEmpty() || line.startsWith('#'))
					continue;
				const QStringList tok = line.split(QRegularExpression(QStringLiteral("\\s+")),
					Qt::SkipEmptyParts);
				if (tok.isEmpty())
					continue;

				if (tok[0] == QLatin1String("o") || tok[0] == QLatin1String("g"))
				{
					flush();
					if (tok.size() > 1)
						cur.name = tok.mid(1).join('_');
					continue;
				}
				if (tok[0] == QLatin1String("v") && tok.size() >= 4)
				{
					verts.emplace_back(tok[1].toFloat(), tok[2].toFloat(), tok[3].toFloat());
					continue;
				}
				if (tok[0] == QLatin1String("f") && tok.size() >= 4)
				{
					for (int i = 1; i < tok.size(); ++i)
					{
						// 支持 "v"、"v/vt"、"v//vn"、"v/vt/vn"
						const int slash = tok[i].indexOf('/');
						const QString idxText = slash < 0 ? tok[i] : tok[i].left(slash);
						int idx = idxText.toInt();
						if (idx < 0)
							idx = int(verts.size()) + idx + 1;   // 负下标 = 从末尾倒着数
						if (idx >= 1 && idx <= int(verts.size()))
							cur.pts.push_back(verts[size_t(idx - 1)]);
					}
				}
			}
			flush();

			if (groups.empty())
			{
				if (err) *err = QStringLiteral("文件里没有可用的面数据：%1").arg(QFileInfo(path).fileName());
				return 0;
			}

			std::vector<int> newUids;
			int n = 0;
			for (const Group& g : groups)
			{
				QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
					std::numeric_limits<float>::max());
				QVector3D hi(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
					-std::numeric_limits<float>::max());
				for (const QVector3D& p : g.pts)
				{
					lo.setX(std::min(lo.x(), p.x())); lo.setY(std::min(lo.y(), p.y())); lo.setZ(std::min(lo.z(), p.z()));
					hi.setX(std::max(hi.x(), p.x())); hi.setY(std::max(hi.y(), p.y())); hi.setZ(std::max(hi.z(), p.z()));
				}

				Viewport3D::Obj o;
				const QString rawName = g.name.isEmpty() ? QStringLiteral("Object") : g.name;
				o.name = SafeName(rawName).toStdString();
				o.color = kPalette[n % 5];
				o.pos = (lo + hi) * 0.5f;
				o.size = QVector3D(std::max(0.05f, hi.x() - lo.x()),
					std::max(0.05f, hi.y() - lo.y()),
					std::max(0.05f, hi.z() - lo.z()));
				o.kind = 0;
				o.selected = false;
				newUids.push_back(v3d->AddObjectRaw(o));
				++n;
			}

			if (!newUids.empty())
				v3d->SelectByUids(newUids);
			return n;
		}

		// ================= 导出场景 JSON / 截图 =================
		bool ExportJson(const QString& path, Viewport3D* v3d, SceneGraph2D* g2d, QString* err)
		{
			return Document::Save(path, v3d, g2d, err);
		}

		bool ExportPng(const QString& path, Viewport3D* v3d, QString* err)
		{
			if (!v3d)
			{
				if (err) *err = QStringLiteral("内部错误：视口未创建");
				return false;
			}
			const QPixmap shot = v3d->grab();
			if (shot.isNull() || !shot.save(path, "PNG"))
			{
				if (err) *err = QStringLiteral("无法保存截图：%1").arg(path);
				return false;
			}
			return true;
		}
	}
}
