#pragma once
#include <QWidget>
#include <QColor>
#include <QMatrix4x4>
#include <QSet>
#include <QVector3D>
#include <vector>
#include <string>

#include "Hotkeys.h"
#include "ViewCube.h"

class QLabel;
class QPainter;
class QJsonObject;
struct SceneObject;

namespace GameEditor
{
	class D3D12Renderer;
	class GraphRuntime;

	// 中央 3D 视口：D3D12 离屏渲染 -> GPU 回读 -> QPainter 上屏。
	// 交互：右键拖动=旋转轨道相机，中键拖动=平移，滚轮=缩放；
	//       左键单击=拾取，左键拖动=框选（Shift 追加/移除）；
	//       Q/W/E/R = 选择/移动/旋转/缩放 Gizmo 工具；键位可在"设置"里改。
	class Viewport3D : public QWidget
	{
		Q_OBJECT
	public:
		enum class Tool { Select, Move, Rotate, Scale };

		// 运行模式：
		//   None    = 编辑器视口（可编辑、可选中、有 gizmo）
		//   Scene   = Run Scene：运行当前场景，保留网格 / 坐标轴 / 调试信息
		//   Project = Run Project：整项目运行，隐藏网格等编辑器参数，只显示 FPS
		enum class RuntimeMode { None, Scene, Project };

		struct Obj
		{
			std::string name;
			QColor      color{ 150, 150, 150 };
			QVector3D   pos{ 0, 0.4f, 0 };
			QVector3D   rot;                              // 欧拉角（弧度）
			QVector3D   size{ 0.8f, 0.8f, 0.8f };
			bool        selected = false;
			bool        solid = true;                     // 参与方块间碰撞（AABB）
			bool        gravity = true;                   // 运行时受重力（新建默认开）
			int         uid = 0;                          // 与 2D 节点配对的唯一 id（0 = 未配对）
			int         kind = 0;                         // 0 = 方块，1 = 平面，2 = 跳转精灵
			std::string target;                           // 跳转精灵：目标 Level 文件名（空 = 自动下一关）
			bool        active = true;                    // 运行时是否显示（由「运行输出」白名单决定，不存档）
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
		void    SetObjectTarget(int uid, const std::string& target);   // 跳转精灵的目标关卡
		void    SetObjectTransform(int uid, const QVector3D& pos, const QVector3D& size, float rotZRad);
		std::vector<int> SelectedUids() const;
		void    SelectByUids(const std::vector<int>& uids);

		// ---- 文档存取（.yu，由 Document 调用）----
		void SaveState(QJsonObject& out) const;             // 对象 + 相机 -> JSON
		void LoadState(const QJsonObject& in);              // JSON -> 对象 + 相机
		void    SetSelected(int index);                       // 主选中（-1 = 清空）
		QString SelectedName() const;
		int     SelectedCount() const { return int(m_selection.size()); }
		bool    SelectedGravity() const;                      // 选中物体是否受重力（多选取第一个）
		void    SetSelectedGravity(bool on);                  // 应用到所有选中物体
		int     SelectedKind() const;                         // 主选中物体的 kind（-1 = 未选择）
		QString SelectedLevelTarget() const;                  // 跳转精灵：目标 Level（空 = 自动下一关）
		void    SetSelectedLevelTarget(const QString& target);
		void    AddBox();
		void    AddPlane();
		void    AddTrigger();                                 // 跳转精灵（运行时不可见，方块碰到就切关卡）
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

		// ---- 右上角视图方块（ViewCube）外观 ----
		void SetViewCubeSize(float px)    { m_viewCube.SetScalePx(px); update(); }   // <=0 = 自动
		void SetViewCubeOpacity(float a)  { m_viewCube.SetOpacity(a); update(); }
		float ViewCubeSize() const        { return m_viewCube.ScalePx(); }
		float ViewCubeOpacity() const     { return m_viewCube.Opacity(); }

		// ---- 运行模式（Run Scene / Run Project）----
		void        EnterRuntime(RuntimeMode mode);           // 进入运行：清选择、按模式设叠加显示、开始模拟
		void        SetRuntimeGraph(const QJsonObject& graph); // 运行时执行的 2D 节点图（空 = 无图纯重力）
		RuntimeMode Runtime() const { return m_runtime; }

	signals:
		void SelectionChanged(const QString& name);
		void ToolChanged(int tool);
		void RuntimeExitRequested();                          // 运行中按 ESC
		void LevelSwitchRequested(const QString& target);      // 方块碰到跳转精灵（target 空 = 下一关）
		// gizmo 拖拽第一次真正改动对象之前发出：宿主在这里记撤销快照，
		// 否则连续拖拽不记步，Ctrl+Z 回不到拖动前（表现为"撤销不好使"）
		void EditStarted();

	protected:
		void showEvent(QShowEvent*) override;
		void resizeEvent(QResizeEvent*) override;
		void paintEvent(QPaintEvent*) override;
		void keyPressEvent(QKeyEvent*) override;
		void keyReleaseEvent(QKeyEvent*) override;
		void focusOutEvent(QFocusEvent*) override;
		void mousePressEvent(QMouseEvent*) override;
		void mouseMoveEvent(QMouseEvent*) override;
		void mouseReleaseEvent(QMouseEvent*) override;
		void wheelEvent(QWheelEvent*) override;
		void leaveEvent(QEvent*) override;                  // 鼠标离开视口：清掉视图方块的悬停高亮

	private:
		// 拖拽撞到屏幕边缘时把光标"虫洞"到对侧（Unity 场景视图那种无限拖），
		// 同时平移按下点 / 上一帧点，保证 delta 和 gizmo 位移都连续
		void WrapCursorAtScreenEdge(const QPointF& localPos, const QPoint& globalPos);
		// ---- 渲染 ----
		void RenderNow();
		void BuildFrame();
		void DrawOverlay(QPainter& p);                      // 左上角状态文字（直接画在帧上）
		void DrawAxisGizmo(QPainter& p);                    // 左下角方向标（随相机旋转，固定像素尺寸）
		void DrawViewCube(QPainter& p);                     // 右上角视图方块（点面/棱/角切换视角）
		void UpdateViewCubeBasis();                         // 把相机基向量喂给视图方块
		void ApplyViewCubePick(const ViewCube::Block& b);    // 点中某一块 -> 改 yaw / pitch
		void PushTri(const QVector3D& a, const QVector3D& b, const QVector3D& c, const QColor& col);
		void PushLine(const QVector3D& a, const QVector3D& b, const QColor& col);
		void PushLineNoOffset(const QVector3D& a, const QVector3D& b, const QColor& col);
		void PushBoxWire(const QVector3D corners[8], const QColor& col);
		void PushBoxFaces(const QVector3D corners[8], const QColor& col);
		void PushGrid();
		void PushGizmo();
		void StepPhysics(float dt);                         // 运行时：节点图 / 重力 / AABB 碰撞 / 地面
		void ApplyOutputFilter();                            // 按「运行输出」白名单决定哪些物体出现在运行画面

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
		enum class DragMode { None, Orbit, Pan, BoxSelect, Gizmo, ViewCube };
		DragMode  m_drag = DragMode::None;
		QPointF   m_lastPos, m_pressPos;
		bool      m_dragMoved = false;                      // 区分点击与框选
		bool      m_wrapPending = false;                    // 刚做过光标虫洞，下一帧位移要吃掉
		bool      m_mouseGrabbed = false;                   // 拖拽期间是否 grabMouse（要能收到窗口外的移动）
		int       m_gizmoAxis = -1;                         // 正在拖的 gizmo 轴（-2 = 均匀缩放中心柄）
		bool      m_gizmoEditBegun = false;                 // 本次拖拽是否已发过 EditStarted
		QVector3D m_gizmoStartPos, m_gizmoStartSizes, m_gizmoStartRots;
		std::vector<QVector3D> m_gizmoStartPosList;            // 多选拖拽时每个物体的起始位姿
		std::vector<QVector3D> m_gizmoStartSizesList;
		std::vector<QVector3D> m_gizmoStartRotsList;           // 每个物体的起始欧拉角（旋转拖拽用）
		QVector3D m_gizmoAxisDir;                           // 世界空间轴向
		float     m_gizmoStartT = 0.f;
		float     m_gizmoStartAngle = 0.f;
		QPointF   m_rotPrevPos;                             // 旋转拖拽：上一帧屏幕点（穿屏时同步平移）
		float     m_rotAccumDeg = 0.f;                       // 旋转拖拽：逐帧增量累加的角度（度）
		Qt::MouseButtons m_dragButton = Qt::NoButton;

		// 左键框选矩形：画进 D3D 帧里（与场景一起回读上屏）。
		// 从左向右拖 = 全包含才选中（window），
		// 从右向左拖 = 碰到就选中（crossing），与 3ds Max 一致。
		QPointF m_bandStart, m_bandEnd;
		bool    m_bandActive = false;

		// 右上角视图方块（只画 + 命中测试，相机状态仍由本类的 m_yaw / m_pitch 管）
		ViewCube m_viewCube;

		// FPS 统计
		int    m_frames = 0;
		int    m_fpsValue = 0;
		qint64 m_fpsClock = 0;

		// 运行状态 / 简单刚体（重力）
		RuntimeMode m_runtime = RuntimeMode::None;
		std::vector<QVector3D> m_vel;                       // 每个对象的线速度（与 m_objects 同序）
		float   m_simTime = 0.f;                            // 运行时长（秒）
		float   m_gravity = -20.f;                          // 重力加速度（与节点图"重力"节点默认值一致）

		// 运行时的按键表（2D 节点图的键盘 block 从这里取输入）
		QSet<int> m_keysDown;                               // 当前按住的键（松开/失焦时清除）

		// 2D 节点图运行时求值器（nullptr / 空图 = 场景没有逻辑，只跑纯重力）
		GraphRuntime* m_graph = nullptr;
		bool  m_camFollowInit = false;                      // 进入运行后镜头块第一帧直接对准，之后平滑跟随
		bool  m_outputFiltered = false;                     // 本次运行是否按「运行输出」白名单过滤
		bool  m_levelFired = false;                         // 本次运行是否已经触发过关（防重复跳转）
	};
}
