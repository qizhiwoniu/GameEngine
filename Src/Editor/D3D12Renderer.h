#pragma once
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

namespace GameEditor
{
	using Microsoft::WRL::ComPtr;

	// 顶点：世界坐标 + 颜色（GPU 里做 view*proj 变换）
	struct GpuVertex
	{
		float x, y, z;
		float r, g, b, a;
	};

	// 最小 D3D12 渲染管线：设备 + 命令队列 + 交换链（绑定视口 HWND）+ 深度缓冲
	// + 动态顶点缓冲（每帧 CPU 填充、GPU 变换绘制）。
	// 用法：Init -> Begin(相机矩阵) -> DrawTris/DrawLines -> End(执行+Present)。
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
		void End();                                        // 执行命令并 Present

	private:
		bool CreateDeviceAndSwapchain();
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
		ComPtr<IDXGISwapChain3>     m_swapchain;
		ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
		ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
		ComPtr<ID3D12Resource>       m_depth;
		ComPtr<ID3D12Resource>       m_backbuffer[2];   // flip 模型下交换链有 2 个后备缓冲

		ComPtr<ID3D12RootSignature>  m_rootSig;
		ComPtr<ID3D12PipelineState>  m_psoTri;
		ComPtr<ID3D12PipelineState>  m_psoLine;

		ComPtr<ID3D12CommandAllocator> m_alloc;
		ComPtr<ID3D12GraphicsCommandList> m_cmd;
		ComPtr<ID3D12Fence>   m_fence;
		HANDLE                m_fenceEvent = nullptr;
		UINT64                m_fenceValue = 1;

		// 常量缓冲（view/proj，上传堆）
		ComPtr<ID3D12Resource> m_cb;
		UINT8* m_cbData = nullptr;

		// 动态顶点缓冲（上传堆，CPU 持续映射）
		ComPtr<ID3D12Resource> m_vb;
		UINT8* m_vbData = nullptr;
		size_t m_vbCapacity = 0;        // 顶点数
		UINT   m_vbStride = sizeof(GpuVertex);

		// CPU 端收集的本帧几何
		std::vector<GpuVertex> m_tri;
		std::vector<GpuVertex> m_line;

		UINT  m_frameIndex = 0;
		bool  m_cmdOpen = false;
		bool  m_flipped = false;        // true = flip model 交换链
		float m_clear[3] = { 0.12f, 0.12f, 0.13f };
	};
}
