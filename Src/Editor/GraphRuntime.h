#pragma once
#include "Viewport3D.h"

#include <QJsonObject>
#include <QSet>
#include <QVector3D>
#include <vector>

namespace GameEditor
{
	// 2D 节点图的运行时求值器（Run Scene / Run Project 用）。
	// 数据来源：SceneGraph2D::SaveState 输出的 graph2d JSON。
	// 每帧按数据流求值：键盘 / 时间 / 数值 -> 相加 / 累加 / 重力 -> 方块 / 平面写回 3D 对象，
	// 镜头节点让相机看向被驱动的方块（注视点跟随，视角环绕仍归鼠标）。数据流环上的引用取上一帧输出（累加 / 重力本身是状态节点）。
	// 场景里没有图（或图里没有节点）时返回 false，视口退回纯重力模拟。
	class GraphRuntime
	{
	public:
		GraphRuntime() = default;
		~GraphRuntime() = default;

		bool  Load(const QJsonObject& graph2d);              // 空 / 坏图返回 false
		bool  IsValid() const { return !m_nodes.empty(); }
		void  SnapshotSizes(const std::vector<Viewport3D::Obj>& objects);  // 缩放基准（进运行时调一次）
		// 起始状态校准（进运行时调一次，必须在第一帧 Step 之前）：
		// 累加 / 累减 / 重力块的状态默认从 0 起算，等于把物体扔回世界原点（平面中间）。
		// 这里按物体当前的编辑位置反推起始值，做到"编辑器里摆在哪儿，运行就从哪儿开始"。
		void  SeedFromObjects(const std::vector<Viewport3D::Obj>& objects);
		const QSet<int>& DrivenUids() const { return m_driven; }           // 位置被节点图接管的 uid（没接线的方块不在内）
		bool  HasCamera() const { return m_hasCamNode; }                   // 图里是否有镜头块（有则接管相机）

		// 运行输出（场景输出）：被「运行输出」块接住的物体才会出现在运行画面里。
		// HasOutputFilter() = 至少有一个运行输出块真的接了线（没接线时不做过滤，全部照旧显示）。
		bool  HasOutputFilter() const { return m_hasOutput; }
		const QSet<int>& OutputUids() const { return m_outUids; }          // 运行时要显示的 uid 集合

		// 每帧执行：keys = 当前按住的键；求值并把结果写回 objects。
		// hasCam/camOff/camUid：图里有镜头块时返回 true、注视点偏移、要看的对象 uid（0 = 自动）
		void  Step(float dt, std::vector<Viewport3D::Obj>& objects, const QSet<int>& keys,
			bool& hasCam, QVector3D& camOff, int& camUid);

		// 碰撞回馈（每帧由视口在物理解算后调用）：
		//   pos   = 解算后的最终位置；delta = 最终位置 - 图想写的位置（被挡下来的那段）
		//   grounded = 脚下有支撑面
		// 作用：① 把 delta 加回驱动该轴的累加 / 重力块状态，避免图把方块一路按进墙里；
		//       ② 把支撑面高度告诉重力块，踩在别的方块顶上时也能起跳。
		void  FeedBack(int uid, const QVector3D& pos, const QVector3D& delta, bool grounded);

	private:
		struct Node
		{
			int   id = 0, kind = 0, obj = 0;
			float val[5] = { 0, 0, 0, 0, 1 };                // 键盘键码 / 数值节点的编辑值
			float out[5] = { 0, 0, 0, 0, 0 };                // 本帧输出
			float prev[5] = { 0, 0, 0, 0, 0 };               // 上一帧输出（打破环）
			float state = 0.f, state2 = 0.f;                 // 状态：累加累计 / 重力的 y 与 vy
			float supportY = 0.f;                            // 重力块：上一帧物理解算出的支撑面高度
			bool  hasSupport = false;                        // 重力块：这帧站在东西上（用它当地面）
			bool  hasState = false;
			bool  visiting = false, done = false;
		};
		struct Link { int from = 0, fromPin = 0, to = 0, toPin = 0; };

		int   Find(int id) const;
		float Input(int idx, int pin);                       // 引脚输入：连线值 / 节点种类默认值
		void  Eval(int idx);

		bool  HasInput(int nodeId, int pin) const;           // 该引脚有没有接线
		// 起始状态校准：沿连线把 value 种到驱动该轴的状态节点上（相加块会先扣掉常量偏移）
		void  SeedChain(int nodeId, int pin, float value, int depth);
		bool  ShiftState(int nodeId, int pin, float d, int depth);   // 沿连线回溯到有状态的节点并加 d
		bool  MarkSupport(int nodeId, int pin, float y, bool on, int depth); // 找到重力块并设置支撑面

		std::vector<Node> m_nodes;
		std::vector<Link> m_links;
		QSet<int>         m_driven;
		QSet<int>         m_outUids;                         // 接到「运行输出」的对象 uid
		bool              m_hasOutput = false;               // 运行输出块是否有连线（无 = 不过滤）
		std::vector<QVector3D> m_baseSizes;                  // 缩放基准（进入运行时的尺寸）
		float m_time = 0.f;
		float m_dt = 1.f / 30.f;

		// Step 期间的临时上下文（求值递归要用）
		std::vector<Viewport3D::Obj>* m_objs = nullptr;
		const QSet<int>* m_keys = nullptr;
		bool      m_hasCamNode = false;                        // Load 时看图里有没有镜头块
		int       m_camUid = 0;                                // 镜头块绑定的对象 uid（0 = 自动）
		bool      m_hasCam = false;
		QVector3D m_camOff;                                    // 镜头块的注视点偏移
	};
}
