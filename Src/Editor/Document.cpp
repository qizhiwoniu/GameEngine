#include "Document.h"
#include "Viewport3D.h"
#include "SceneGraph2D.h"
#include "SceneLink.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace GameEditor
{
	namespace Document
	{
		bool Save(const QString& path, Viewport3D* v3d, SceneGraph2D* g2d, QString* err)
		{
			if (!v3d || !g2d)
			{
				if (err) *err = QStringLiteral("内部错误：视口未创建");
				return false;
			}
			QJsonObject root;
			root["app"] = QStringLiteral("3DGameEngine");
			root["format"] = QStringLiteral("yu-scene");
			root["version"] = 1;

			QJsonObject vp;
			v3d->SaveState(vp);
			root["viewport"] = vp;

			QJsonObject g;
			g2d->SaveState(g);
			root["graph2d"] = g;

			QFile f(path);
			if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
			{
				if (err) *err = QStringLiteral("无法写入文件：%1").arg(path);
				return false;
			}
			f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
			return true;
		}

		bool Load(const QString& path, Viewport3D* v3d, SceneGraph2D* g2d, QString* err)
		{
			QFile f(path);
			if (!f.open(QIODevice::ReadOnly))
			{
				if (err) *err = QStringLiteral("无法打开文件：%1").arg(path);
				return false;
			}
			QJsonParseError pe{};
			const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
			if (pe.error != QJsonParseError::NoError || !doc.isObject())
			{
				if (err) *err = QStringLiteral("文件不是有效的场景文档：%1").arg(pe.errorString());
				return false;
			}
			const QJsonObject root = doc.object();

			// 清掉配对历史再整体替换两侧数据，防止旧 Known 集合把刚载入的对象当孤儿删掉
			SceneLink::Reset();
			v3d->LoadState(root["viewport"].toObject());
			g2d->LoadState(root["graph2d"].toObject());
			SceneLink::Reset();     // 载入的节点已带 obj 配对，不需要历史
			return true;
		}

		QString LevelsDir()
		{
			QDir d(QCoreApplication::applicationDirPath());
			for (int i = 0; i < 6; ++i)
			{
				if (QFile::exists(d.filePath(QStringLiteral("CMakeLists.txt"))))
					return d.filePath(QStringLiteral("Levels"));
				if (!d.cdUp())
					break;
			}
			return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Levels"));
		}

		bool LoadRuntime(const QString& path, QJsonObject& viewport, QJsonObject& graph, QString* err)
		{
			QFile f(path);
			if (!f.open(QIODevice::ReadOnly))
			{
				if (err) *err = QStringLiteral("无法打开关卡：%1").arg(path);
				return false;
			}
			QJsonParseError pe{};
			const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
			if (pe.error != QJsonParseError::NoError || !doc.isObject())
			{
				if (err) *err = QStringLiteral("关卡文件不是有效场景：%1").arg(pe.errorString());
				return false;
			}
			const QJsonObject root = doc.object();
			viewport = root["viewport"].toObject();
			graph = root["graph2d"].toObject();
			return true;
		}
	}
}
