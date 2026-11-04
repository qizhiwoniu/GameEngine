#pragma once
#include <QWidget>
#include <QColor>
#include <QMatrix4x4>
#include <QVector3D>
#include <vector>
#include <string>

#include "Hotkeys.h"

class QLabel;
class QRubberBand;
struct SceneObject;

namespace GameEditor
{
	class D3D12Renderer;

	// 中央 3D 视口：D3D12 真实渲染管线（设备 + 交换链绑在本控件 HWND 上）
	// 交互：右键拖动=旋转轨道相机，中键拖动=平移，滚轮=缩放；
	//       左键单击=拾取，左键拖动=框选（Shift 追加/移除）；
	//       Q/W/E/R = 选择/移动/旋转/缩放 Gizmo 工具；键位可在"设置"里改。
	class Viewport3D : public QWidget
	{
		Q_OBJECT
	public:
		enum class Tool { Select, Move, Rotate, Scale };

		struct Obj
		{
			std::string name;
			QColor      color{ 150, 150, 150 };
			QVector3D   pos{ 0, 0.4f, 0 };
			QVector3D   rot;                              // 欧拉角（弧度）
			QVector3D   size{ 0.8f, 0.8f, 0.8f };
			bool        selected = false;
			int         uid = 0;                          // 与 2D 节点配对的唯一 id（0 = 未配对）
			int         kind = 0;                         // 0 = 方块，1 = 平面
		};

		explicit Viewport3D(QWidget* parent = nullptr);
		~Viewport3D() override;

		const std::vector<Obj>& Objects() const { return m_objects; }
		int     ObjectCount() const { return int(m_objects.size()); }
		const Obj& ObjAt(int index) const { return m_objects[index]; }
		int     IndexOfUid(int uid) const;                    // -1 = 不存在
		int     AddObjectRaw(const Obj& o);                   // 返回新 uid
		bool    RemoveByUid(int uid);
		void    SetObjectName(int uid, const std::string& name);
		void    SetObjectTransform(int uid, const QVector3D& pos, const QVector3D& size, float rotZRad);
		std::vector<int> SelectedUids() const;
		void    SelectByUids(const std::vector<int>& uids);
		void    SetSelected(int index);                       // 主选中（-1 = 清空）
		QString SelectedName() const;
		int     SelectedCount() const { return int(m_selection.size()); }
		void    AddBox();
		void    AddPlane();
		void    DeleteSelected();
		void    SetWireFrame(bool on) { m_wireFrame = on; RenderNow(); }
		void    SetView3D(bool on);                           // false = 正交俯视（2D）
		void    SetSnap(bool on) { m_snap = on; }
		void    SetShowGrid(bool on) { m_showGrid = on; RenderNow(); }
		bool    ShowGrid() const { return m_showGrid; }
		void    SetShowFps(bool on);
		bool    ShowFps() const { return m_showFps; }
		void    SetTool(Tool t) { m_tool = t; RenderNow(); }
		Tool    GetTool() const { return m_tool; }
		void    ResetCamera();                                // 回到默认 3D 视角

	signals:
		void SelectionChanged(const QString& name);
		void ToolChanged(int tool);

	protected:
		void showEvent(QShowEvent*) override;
		void resizeEvent(QResizeEvent*) override;
		void paintEvent(QPaintEvent*) override;
		void keyPressEvent(QKeyEvent*) override;
		void mousePressEvent(QMouseEvent*) override;
		void mouseMoveEvent(QMouseEvent*) override;
		void mouseReleaseEvent(QMouseEvent*) override;
		void wheelEvent(QWheelEvent*) override;

	private:
		// ---- 渲染 ----
		void RenderNow();
		void BuildFrame();
		void EnsureOverlay();                               // 左上角状态分层窗口
		void UpdateOverlay();
		void PushTri(const QVector3D& a, const QVector3D& b, const QVector3D& c, const QColor& col);
		void PushLine(const QVector3D& a, const QVector3D& b, const QColor& col);
		void PushBoxWire(const QVector3D corners[8], const QColor& col);
		void PushBoxFaces(const QVector3D corners[8], const QColor& col);
		void PushGrid();
		void PushAxes();
		void PushGizmo();

		// ---- 相机 / 投影 ----
		QVector3D EyePos() const;
		void      UpdateMatrices();                          // 刷新 m_view/m_proj/m_vp/m_invVp
		bool      ProjectPoint(const QVector3D& w, QPointF& out, float* depth = nullptr) const;
		float     WorldPerPixel(float depth) const;
		QVector3D MouseRayDir(const QPointF& p) const;
		bool      RayObject(int index, const QVector3D& o, const QVector3D& d, float* t) const;

		// ---- 交互 ----
		void  PickAt(const QPointF& p, bool additive);
		int   GizmoHitTest(const QPointF& p, int* sub) const; // 返回轴 0/1/2，-1 未命中
		void  ApplyGizmoDrag(const QPointF& p);
		void  EmitSelection();
		void  SyncSelectionFlags();
		QVector3D SelectionCenter() const;

		D3D12Renderer* m_r = nullptr;
		bool      m_initialized = false;

		std::vector<Obj> m_objects;
		std::vector<int> m_selection;                       // 选中集合（最后一个是主选中）
		int  m_nextUid = 1;
		bool m_wireFrame = false;
		bool m_snap = false;
		bool m_angleSnap = false;                           // 角度捕捉（Shift+P）
		bool m_showGrid = true;
		bool m_showFps = true;                              // 左上角 FPS:XXX
		Tool m_tool = Tool::Select;

		// 轨道相机
		float m_yaw = -0.55f, m_pitch = 0.45f, m_dist = 8.f;
		QVector3D m_target{ 0, 0.4f, 0 };
		bool  m_ortho = false;                              // 2D 模式用正交投影

		// 矩阵缓存
		QMatrix4x4 m_view, m_proj, m_vp, m_invVp;

		// 鼠标状态
		enum class DragMode { None, Orbit, Pan, BoxSelect, Gizmo };
		DragMode  m_drag = DragMode::None;
		QPointF   m_lastPos, m_pressPos;
		bool      m_dragMoved = false;                      // 区分点击与框选
		int       m_gizmoAxis = -1;                         // 正在拖的 gizmo 轴（-2 = 均匀缩放中心柄）
		QVector3D m_gizmoStartPos, m_gizmoStartSizes, m_gizmoStartRots;
		std::vector<QVector3D> m_gizmoStartPosList;            // 多选拖拽时每个物体的起始位姿
		std::vector<QVector3D> m_gizmoStartSizesList;
		QVector3D m_gizmoAxisDir;                           // 世界空间轴向
		float     m_gizmoStartT = 0.f;
		float     m_gizmoStartAngle = 0.f;
		Qt::MouseButtons m_dragButton = Qt::NoButton;

		QRubberBand* m_rubber = nullptr;

		// FPS 统计
		int    m_frames = 0;
		int    m_fpsValue = 0;
		qint64 m_fpsClock = 0;

		// 左上角状态叠加层：无边框、半透明、点击穿透的 Qt Tool 顶层窗口，
		// 拥有者为当前顶层窗口（Qt 自动跟随移动 / 还原，DWM 合成不受
		// flip-model 交换链盖 GDI 的影响，也不会像 Win32 弹窗那样被系统回收）
		QWidget* m_overlayW = nullptr;
	};
}
