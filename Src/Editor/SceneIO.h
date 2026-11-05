#pragma once
#include <QString>

namespace GameEditor
{
	class Viewport3D;
	class SceneGraph2D;

	// 文件 ▸ 导入 / 导出 的实现（独立模块，不参与编辑器其它逻辑）
	//
	//   * 导出 OBJ：把方块 / 平面按当前尺寸与旋转写成四边面网格，可只在选中范围内导出
	//   * 导入 OBJ：按 o / g 分组，每组按其包围盒生成一个方块对象（本引擎只渲染方块，不做三角网格）
	//   * 导出场景 JSON：与 .yu 文档同格式，可直接再打开
	//   * 导出截图 PNG：抓当前视口画面
	namespace SceneIO
	{
		bool ExportObj(const QString& path, Viewport3D* v3d, bool selectedOnly, QString* err = nullptr);
		int  ImportObj(const QString& path, Viewport3D* v3d, QString* err = nullptr);   // 返回新增对象数
		bool ExportJson(const QString& path, Viewport3D* v3d, SceneGraph2D* g2d, QString* err = nullptr);
		bool ExportPng(const QString& path, Viewport3D* v3d, QString* err = nullptr);
	}
}
