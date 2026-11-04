#pragma once
#include <QString>

class QJsonObject;

namespace GameEditor
{
	class Viewport3D;
	class SceneGraph2D;

	// .yu 场景文档：把 3D 视口（对象 + 相机）与 2D 节点图（节点 / 连线 / 组）
	// 序列化成一个 JSON 文本文件（扩展名 .yu，沿用原版工程的文档格式）。
	namespace Document
	{
		bool Save(const QString& path, Viewport3D* v3d, SceneGraph2D* g2d, QString* err = nullptr);
		bool Load(const QString& path, Viewport3D* v3d, SceneGraph2D* g2d, QString* err = nullptr);
	}
}
