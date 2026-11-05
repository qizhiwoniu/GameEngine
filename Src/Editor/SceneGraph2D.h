#pragma once
#include <QWidget>
#include <QColor>
#include <QPointF>
#include <QStringList>
#include <QPainterPath>
#include <QSet>
#include <vector>
#include <utility>

class QMenu;
class QJsonObject;

namespace GameEditor
{
	// 2D 节点图（数据流编辑器，对齐 3DGameEngine 的 Viewport2D.h）
	//   - 12 种节点：方块/平面/时间/键盘输入/累加/累减/数值/重力/相加/运行输出/键盘/鼠标/镜头
	//   - 引脚有类型：数值（绿）/ 物体（蓝），只有同类型才能连；数值输入只接一条，物体输入可接多条
	//   - 拖动输出引脚到输入引脚建连线（也可从已连接的输入引脚把线拎起来重连）
	//   - 双击标题折叠/展开节点；双击「数值」节点主体改数值；双击「键盘」节点主体改按键
	//   - 右键菜单：创建 / 编辑(复制·粘贴·重命名) / 组(成组·解组) / 键盘 / 鼠标 / 镜头
	//   - 成组：选中 2 个以上节点包进带标题的框；拖标题移动整组，双击标题折叠整组
	//   - 方块 / 平面节点与 3D 场景对象双向配对（见 SceneLink.h）
	class SceneGraph2D : public QWidget
	{
		Q_OBJECT
	public:
		enum NodeKind
		{
			nkBox, nkPlane, nkTime, nkKey, nkAccum, nkSub, nkValue,
			nkGravity, nkAdd, nkOutput, nkKeys, nkMouse, nkCamera, NK_COUNT
		};
		enum { ptNumber = 0, ptObject = 1 };

		struct PinDef { QString name; int type; float def; };
		struct KindDef
		{
			QString name;
			QColor  head;
			int     nIn;
			PinDef  in[5];
			int     nOut;
			PinDef  out[4];
		};

		// 给 SceneLink 用的节点快照（只含方块 / 平面）
		struct LinkInfo { int id = 0; int kind = 0; int obj = 0; QString name; float v[5] = { 0, 0, 0, 0, 1 }; };

		explicit SceneGraph2D(QWidget* parent = nullptr);

		static const KindDef& KindOf(int kind);
		static int RowsOf(int kind);

		// ---- 与 3D 场景配对（SceneLink 调用）----
		void GetLinkNodes(std::vector<LinkInfo>& out) const;
		int  AddLinkedNode(int kind, const QString& name, int objUid, const float v[5]);
		void SetNodeObj(int id, int objUid);
		void SetNodeName(int id, const QString& name);
		void SetNodeVals(int id, const float v[5]);
		void SelectNodesByObj(const std::vector<int>& uids);
		void RemoveNodeById(int id);

		// ---- 文档存取（.yu，由 Document 调用）----
		void SaveState(QJsonObject& out) const;             // 节点 / 连线 / 组 -> JSON
		void LoadState(const QJsonObject& in);              // JSON -> 节点 / 连线 / 组
		void ResetDocument();                               // 清空全部（新建文档）

		// 删除选中节点（Delete 键 / 编辑菜单都会走到这里）
		void DeleteSelected();

		// ---- 编辑菜单 / 剪贴板（EditorWindow 调用）----
		void CopyToClipboard() { CopySelected(); }   // 复制选中节点（含内部连线 / 组）
		void PasteAtCenter();                        // 粘贴到视图中心
		void SelectAllNodes();                       // 全选可见节点

	protected:
		void paintEvent(QPaintEvent*) override;
		void resizeEvent(QResizeEvent*) override;
		void wheelEvent(QWheelEvent*) override;
		void mousePressEvent(QMouseEvent*) override;
		void mouseDoubleClickEvent(QMouseEvent*) override;
		void mouseMoveEvent(QMouseEvent*) override;
		void mouseReleaseEvent(QMouseEvent*) override;
		void keyPressEvent(QKeyEvent*) override;

	private:
		struct Node
		{
			int      id = 0;
			int      kind = 0;
			QString  name;
			QPointF  pos;               // 视图坐标（左上角）
			bool     sel = false;
			int      group = 0;         // 0 = 不在组里
			int      obj = 0;           // 配对的 3D 对象 uid
			bool     collapsed = false;
			bool     hasVal = false;
			float    val[5] = { 0, 0, 0, 0, 1 };

			float Width() const { return 150.f; }
			float Height() const;
			QRectF Rect() const { return QRectF(pos, QSizeF(Width(), Height())); }
			QRectF HeadRect() const { return QRectF(pos, QSizeF(Width(), 24.f)); }
		};
		struct Link { int from = 0, fromPin = 0, to = 0, toPin = 0; };
		struct Group { int id = 0; QString name; bool collapsed = false; };

		enum DragMode { dgNone, dgPan, dgMove, dgLink, dgRubber };
		enum HitKind { hkNone, hkNode, hkIn, hkOut, hkGroup };
		struct Hit { int kind = hkNone; int node = 0; int pin = 0; };

		// ---- 场景 ----
		void BuildDemoScene();
		int  FindNode(int id) const;
		int  FindGroup(int id) const;
		QRectF GroupRect(int gid) const;
		bool NodeHidden(const Node& n) const;
		void PinPos(const Node& n, bool out, int pin, QPointF& p) const;
		QPointF LinkStart(const Link& l) const;
		QPointF LinkEnd(const Link& l) const;
		QPainterPath LinkPath(const QPointF& a, const QPointF& b) const;
		Hit  HitTest(const QPointF& vp) const;
		Hit  HitTestNodes(const QPointF& vp) const;

		// ---- 编辑 ----
		void AddNode(int kind, const QPointF& vp);
		void CopySelected();
		void PasteAt(const QPointF& vp);
		void RenameNode(int id);
		void RenameGroup(int gid);
		void EditValue(int id);
		void EditKey(int id);
		void GroupSelected();
		void UngroupSelected();
		void CleanGroups();
		bool AnySelectedGrouped() const;
		int  SelCount() const;
		void ClearSel();
		void BringToFront(int id);
		void BeginMove(const QPointF& vp);
		void RemoveLinksOf(int id);
		bool Reaches(int from, int target, QSet<int>& seen) const;
		void TryLink(const Hit& h);
		bool ToggleFold(int id);                        // 双击标题
		bool ToggleGroupFold(int gid);
		QString KeyNameOf(const Node& n) const;

		// ---- 视图 ----
		void LocateSelected();
		void FitAll();
		void ZoomActual();
		QPointF ToView(const QPointF& screenPos) const;

		// ---- 菜单 ----
		void ShowMenu(const QPoint& globalPos, const QPointF& viewPos);
		void SetKindValue(int kind, float v, const QPointF& at, int menuNode);

		// ---- 绘制 ----
		void DrawNode(QPainter& p, const Node& n, const Hit& hover);
		void DrawStatusText(QPainter& p);

		std::vector<Node>  m_nodes;      // 绘制顺序 = z 序
		std::vector<Link>  m_links;
		std::vector<Group> m_groups;
		std::vector<Node>  m_clipNodes;
		std::vector<Link>  m_clipLinks;
		std::vector<Group> m_clipGroups;

		int m_nextId = 1;
		int m_nextGroup = 1;
		int m_groupCounter = 0;
		int m_counter[NK_COUNT] = { 0 };

		float   m_zoom = 0.83f;
		QPointF m_pan{ 0, 0 };
		bool    m_panInit = false;                     // 首次布局时把视图原点移到控件中心

		DragMode m_drag = dgNone;
		QPointF  m_pressPos, m_pressPan;
		bool     m_moved = false;
		int      m_dragNode = 0;
		QPointF  m_dragStart;
		std::vector<std::pair<int, QPointF>> m_moveStart;   // 拖动开始时各选中节点的位置
		int      m_linkNode = 0, m_linkPin = 0;
		bool     m_linkFromOut = true;
		QPointF  m_linkPos;
		QPointF  m_bandStart;
		QPointF  m_bandEnd;

		Hit      m_menuHit;               // 右键按下时的命中
		QPointF  m_menuWorld;

		// 双击检测（标题 / 组标题）
		qint64   m_clickTick = 0;
		int      m_clickId = 0;
		QPointF  m_clickPos;
	};
}
