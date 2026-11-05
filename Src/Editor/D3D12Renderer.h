#pragma once
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <QImage>

namespace GameEditor
{
	using Microsoft::WRL::ComPtr;

	// 顶点：世界坐标 + 颜色（GPU 里做 view*proj 变换）
	struct GpuVertex
	{
		float x, y, z;
		float r, g, b, a;
	};

	// 最小 D3D12 渲染管线（离屏渲染版）：
	// 设备 + 命令队列 + 离屏渲染纹理 + 回读堆 + 深度缓冲 + 动态顶点缓冲。
	// 用法：Init -> Resize -> Begin(相机矩阵) -> DrawTris/DrawLines -> End(回读到 QImage)。
	//
	// 重要：不要给任何 D3D 交换链挂到窗口 HWND 上！flip/blt 呈现到 WS_CHILD
	// 都会劫持整个窗口树的合成，Qt 主窗口（菜单栏/停靠面板）会随机整片白块、
	// 表现为"窗口隐藏/取消隐藏失灵"。这里渲染完直接 GPU->CPU 回读成 QImage，
	// 由 Viewport3D::paintEvent 用 QPainter 画出来，窗口合成完全走 Qt。
	class D3D12Renderer
	{
	public:
		~D3D12Renderer() { Shutdown(); }

		bool Init(HWND hwnd);
		void Shutdown();
		bool Ok() const { return m_device != nullptr; }

		void Resize(int w, int h);

		void Begin(const float view[16], const float proj[16],
			float bgR, float bgG, float bgB);
		void DrawTris(const GpuVertex* v, size_t count);   // TRIANGLE LIST
		void DrawLines(const GpuVertex* v, size_t count);  // LINE LIST
		void End();                                        // 执行命令并回读到 QImage

		// 最近一帧的渲染结果（End 之后有效），paintEvent 直接画
		const QImage& Frame() const { return m_frame; }

	private:
		bool CreateDevice();
		bool CreatePipeline();
		bool CreateSizeDependent(int w, int h);
		bool EnsureVertexCapacity(size_t vertCount);
		void FlushCommands();       // 执行当前命令列表并等待完成
		void WaitForGpu();

		HWND m_hwnd = nullptr;
		int  m_w = 0, m_h = 0;

		ComPtr<ID3D12Device>        m_device;
		ComPtr<ID3D12CommandQueue>  m_queue;
		ComPtr<IDXGIFactory4>       m_factory;
		ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
		ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
		ComPtr<ID3D12Resource>       m_depth;
		ComPtr<ID3D12Resource>       m_rtTex;      // 离屏渲染目标（DEFAULT 堆）
		ComPtr<ID3D12Resource>       m_readback;   // 回读堆 buffer（GPU->CPU 拷贝目标）
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_fp = {};  // 回读 footprint
		UINT m_rowPitch = 0;

		ComPtr<ID3D12RootSignature>  m_rootSig;
		ComPtr<ID3D12PipelineState>  m_psoTri;
		ComPtr<ID3D12PipelineState>  m_psoLine;

		ComPtr<ID3D12CommandAllocator> m_alloc;
		ComPtr<ID3D12GraphicsCommandList> m_cmd;
		ComPtr<ID3D12Fence>   m_fence;
		HANDLE                m_fenceEvent = nullptr;
		UINT64                m_fenceValue = 0;

		// 常量缓冲（view/proj，上传堆）
		ComPtr<ID3D12Resource> m_cb;
		UINT8* m_cbData = nullptr;

		// 动态顶点缓冲（上传堆，CPU 持续映射）
		ComPtr<ID3D12Resource> m_vb;
		UINT8* m_vbData = nullptr;
		size_t m_vbCapacity = 0;        // 顶点数
		size_t m_vbUsed = 0;            // 本帧已写入的顶点数（Begin 清零）
		UINT   m_vbStride = sizeof(GpuVertex);

		// CPU 端收集的本帧几何
		std::vector<GpuVertex> m_tri;
		std::vector<GpuVertex> m_line;

		bool  m_cmdOpen = false;
		float m_clear[3] = { 0.12f, 0.12f, 0.13f };
		QImage m_frame;                 // End 之后持有最新一帧
	};
}
