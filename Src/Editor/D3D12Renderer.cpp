#include "D3D12Renderer.h"

#include <d3dcompiler.h>
#include <DXGI.h>
#include <QtGlobal>
#include <cstring>
#include <cstdarg>
#include <algorithm>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace GameEditor;

namespace
{
	// ---- 着色器（运行时用 D3DCompile 编译）----
	// 行主序矩阵：pos * View * Proj
	const char* kVs = R"(
		cbuffer Scene : register(b0)
		{
			float4x4 View;
			float4x4 Proj;
		};
		struct VSIn  { float3 pos : POSITION; float4 col : COLOR; };
		struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };
		VSOut main(VSIn i)
		{
			VSOut o;
			float4 vp = mul(View, float4(i.pos, 1.0f));
			o.pos = mul(Proj, vp);
			o.col = i.col;
			return o;
		}
	)";
	const char* kPs = R"(
		struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR; };
		float4 main(PSIn i) : SV_TARGET { return i.col; }
	)";

	ComPtr<ID3DBlob> Compile(const char* src, const char* target)
	{
		ComPtr<ID3DBlob> code, err;
		if (FAILED(D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr,
			"main", target, D3DCOMPILE_OPTIMIZATION_LEVEL2, 0, &code, &err)))
		{
			OutputDebugStringA(err ? (const char*)err->GetBufferPointer() : "D3DCompile failed\n");
			return nullptr;
		}
		return code;
	}

	D3D12_INPUT_ELEMENT_DESC kInput[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

bool D3D12Renderer::Init(HWND hwnd)
{
	Shutdown();
	m_hwnd = hwnd;
	if (!CreateDeviceAndSwapchain())
		return false;
	if (!CreatePipeline())
		return false;
	RECT rc = {};
	GetClientRect(hwnd, &rc);
	m_w = std::max<LONG>(1, rc.right - rc.left);
	m_h = std::max<LONG>(1, rc.bottom - rc.top);
	if (!CreateSizeDependent(m_w, m_h))
		return false;
	FlushCommands();
	return true;
}

void D3D12Renderer::Shutdown()
{
	if (m_device)
		WaitForGpu();
	m_backbuffer[0].Reset(); m_backbuffer[1].Reset(); m_depth.Reset();
	m_swapchain.Reset(); m_rtvHeap.Reset(); m_dsvHeap.Reset();
	m_psoTri.Reset(); m_psoLine.Reset(); m_rootSig.Reset();
	m_cmd.Reset(); m_alloc.Reset();
	if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
	m_fence.Reset();
	m_vb.Reset(); m_cb.Reset();
	m_device.Reset(); m_queue.Reset(); m_factory.Reset();
	m_vbData = nullptr; m_cbData = nullptr; m_vbCapacity = 0;
	m_tri.clear(); m_line.clear();
	m_hwnd = nullptr;
}

bool D3D12Renderer::CreateDeviceAndSwapchain()
{
	ComPtr<ID3D12Debug> dbg;
	// 调试层会显著拖慢设备创建/每帧校验（数秒级），默认关闭；
	// 需要排查渲染问题时设 GE_D3DDEBUG=1 再跑。
	if (qEnvironmentVariableIsSet("GE_D3DDEBUG")
		&& SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
		dbg->EnableDebugLayer();

	UINT dxgiFlags = 0;
	if (dbg)
		dxgiFlags = DXGI_CREATE_FACTORY_DEBUG;
	if (FAILED(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&m_factory))))
		return false;

	// 选第一个支持 D3D12 的适配器（优先硬件）
	ComPtr<IDXGIAdapter1> adapter;
	for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_ADAPTER_DESC1 desc = {};
		adapter->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
			break;
	}
	if (!m_device)   // 兜底 WARP
	{
		m_factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
		if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
			return false;
	}

	D3D12_COMMAND_QUEUE_DESC qd = {};
	qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue))))
		return false;

	if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc))))
		return false;
	if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(), nullptr,
		IID_PPV_ARGS(&m_cmd))))
		return false;
	m_cmd->Close();

	if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
		return false;
	m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

	// RTV / DSV 描述符堆
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.NumDescriptors = 8;
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if (FAILED(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap))))
		return false;
	hd.NumDescriptors = 1;
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	if (FAILED(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_dsvHeap))))
		return false;

	// 交换链（子 HWND 窗口；先尝试 flip，失败退 blt）
	RECT rc = {};
	GetClientRect(m_hwnd, &rc);
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	sd.Width = std::max<UINT>(1, UINT(rc.right - rc.left));
	sd.Height = std::max<UINT>(1, UINT(rc.bottom - rc.top));
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.SampleDesc.Count = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	ComPtr<IDXGISwapChain1> sc1;
	HRESULT hr = m_factory->CreateSwapChainForHwnd(m_queue.Get(), m_hwnd, &sd, nullptr, nullptr, &sc1);
	if (FAILED(hr))
	{
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		sd.Scaling = DXGI_SCALING(0);   // DXGI_SCALING_UNSPECIFIED（部分 SDK 头未导出枚举名）
		hr = m_factory->CreateSwapChainForHwnd(m_queue.Get(), m_hwnd, &sd, nullptr, nullptr, &sc1);
		m_flipped = false;
	}
	else
		m_flipped = true;
	if (FAILED(hr) || FAILED(sc1.As(&m_swapchain)))
		return false;
	m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

	m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
	return true;
}

bool D3D12Renderer::CreatePipeline()
{
	// 根签名：b0 常量缓冲（根描述符，免建 CBV_SRV_UAV 堆）
	D3D12_ROOT_PARAMETER rp = {};
	rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rp.Descriptor.ShaderRegister = 0;
	rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 1;
	rsd.pParameters = &rp;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	ComPtr<ID3DBlob> sig, sigErr;
	if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &sigErr)))
		return false;
	if (FAILED(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_rootSig))))
		return false;

	// 常量缓冲：256 字节对齐上传堆
	D3D12_HEAP_PROPERTIES hp = {};
	hp.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC cd = {};
	cd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cd.Width = 256;
	cd.Height = 1; cd.DepthOrArraySize = 1; cd.MipLevels = 1;
	cd.SampleDesc.Count = 1; cd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &cd,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cb))))
		return false;
	D3D12_RANGE read = { 0, 0 };
	if (FAILED(m_cb->Map(0, &read, reinterpret_cast<void**>(&m_cbData))))
		return false;

	// PSO（三角形 / 线两种拓扑）
	ComPtr<ID3DBlob> vs = Compile(kVs, "vs_5_0");
	ComPtr<ID3DBlob> ps = Compile(kPs, "ps_5_0");
	if (!vs || !ps)
		return false;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
	pd.InputLayout = { kInput, UINT(std::size(kInput)) };
	pd.pRootSignature = m_rootSig.Get();
	pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pd.RasterizerState.DepthClipEnable = TRUE;
	pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pd.DepthStencilState.DepthEnable = TRUE;
	pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	pd.SampleMask = UINT_MAX;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 1;
	pd.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
	pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pd.SampleDesc.Count = 1;
	if (FAILED(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_psoTri))))
		return false;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	if (FAILED(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_psoLine))))
		return false;
	return true;
}

bool D3D12Renderer::CreateSizeDependent(int w, int h)
{
	w = std::max(1, w); h = std::max(1, h);
	// 重建 RTV / 深度 / 交换链缓冲
	// 关键：必须先释放对旧后备缓冲的所有引用（含 RTV 描述符背后的资源），
	// 否则 ResizeBuffers 报 "outstanding buffer references" 失败，随后用到失效
	// 描述符会让显卡驱动直接崩溃（nvwgf2umx 里 0xC0000005）。
	m_backbuffer[0].Reset();
	m_backbuffer[1].Reset();
	m_depth.Reset();
	// 注意：flags 必须与创建交换链时完全一致（这里都是 0），
	// 否则 ResizeBuffers 会报"不能增删 FRAME_LATENCY_WAITABLE_OBJECT 标志"并失败。
	HRESULT hr = m_swapchain->ResizeBuffers(0, UINT(w), UINT(h), DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr))
		return false;

	// 为每个后备缓冲建 RTV（flip 模型下每帧轮换，必须按当前 index 取）
	D3D12_CPU_DESCRIPTOR_HANDLE rtvBase = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	const UINT rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (UINT i = 0; i < 2; ++i)
	{
		m_backbuffer[i].Reset();
		if (FAILED(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backbuffer[i]))))
			return false;
		m_device->CreateRenderTargetView(m_backbuffer[i].Get(), nullptr, rtvBase);
		rtvBase.ptr += SIZE_T(rtvSize);
	}

	D3D12_HEAP_PROPERTIES dp = {};
	dp.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC dd = {};
	dd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	dd.Width = UINT(w); dd.Height = UINT(h);
	dd.DepthOrArraySize = 1; dd.MipLevels = 1;
	dd.Format = DXGI_FORMAT_D32_FLOAT;
	dd.SampleDesc.Count = 1;
	dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE cv = {};
	cv.Format = dd.Format;
	cv.DepthStencil.Depth = 1.0f;
	m_device->CreateCommittedResource(&dp, D3D12_HEAP_FLAG_NONE, &dd,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&m_depth));
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_device->CreateDepthStencilView(m_depth.Get(), nullptr, dsv);

	m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
	return true;
}

void D3D12Renderer::Resize(int w, int h)
{
	w = std::max(1, w); h = std::max(1, h);
	if (!m_device || (w == m_w && h == m_h))
		return;
	FlushCommands();   // 确保 GPU 不再使用交换链缓冲
	m_w = w; m_h = h;
	CreateSizeDependent(w, h);
}

void D3D12Renderer::WaitForGpu()
{
	if (!m_queue || !m_fence)
		return;
	const UINT64 v = m_fenceValue;
	m_queue->Signal(m_fence.Get(), v);
	m_fence->SetEventOnCompletion(v, m_fenceEvent);
	WaitForSingleObject(m_fenceEvent, 5000);
	m_fenceValue = v + 1;
}

void D3D12Renderer::FlushCommands()
{
	if (m_cmdOpen)
	{
		m_cmd->Close();
		ID3D12CommandList* lists[] = { m_cmd.Get() };
		m_queue->ExecuteCommandLists(1, lists);
		m_cmdOpen = false;
	}
	// 无论有没有未执行完的列表，都要等 GPU 真正空闲：
	// 否则随后释放的交换链缓冲/深度缓冲可能还在被 GPU 读写，
	// 触发 OBJECT_DELETED_WHILE_STILL_IN_USE 并导致设备被移除。
	WaitForGpu();
	m_alloc->Reset();
}

bool D3D12Renderer::EnsureVertexCapacity(size_t vertCount)
{
	if (m_vb && m_vbCapacity >= vertCount)
		return true;
	// 指数扩容，至少 65536 顶点
	size_t cap = m_vbCapacity ? m_vbCapacity : 65536;
	while (cap < vertCount)
		cap *= 2;
	D3D12_HEAP_PROPERTIES hp = {};
	hp.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bd = {};
	bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bd.Width = UINT64(cap * sizeof(GpuVertex));
	bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
	bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bd.Flags = D3D12_RESOURCE_FLAG_NONE;
	ComPtr<ID3D12Resource> nv;
	if (FAILED(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&nv))))
		return false;
	if (m_vbData) { D3D12_RANGE r = { 0, 0 }; m_vb->Unmap(0, &r); }
	m_vb = nv;
	D3D12_RANGE read = { 0, 0 };
	if (FAILED(m_vb->Map(0, &read, reinterpret_cast<void**>(&m_vbData))))
		return false;
	m_vbCapacity = cap;
	return true;
}

void D3D12Renderer::Begin(const float view[16], const float proj[16],
	float bgR, float bgG, float bgB)
{
	m_clear[0] = bgR; m_clear[1] = bgG; m_clear[2] = bgB;
	m_tri.clear();
	m_line.clear();
	if (!m_device)
		return;

	// 等 GPU 完成（单帧在飞，简单可靠）
	const UINT64 done = m_fence->GetCompletedValue();
	if (done < m_fenceValue)
	{
		m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, 5000);
	}

	// 写常量缓冲
	struct Cb { float view[16]; float proj[16]; };
	Cb cb;
	std::memcpy(cb.view, view, sizeof(cb.view));
	std::memcpy(cb.proj, proj, sizeof(cb.proj));
	if (m_cbData)
		std::memcpy(m_cbData, &cb, sizeof(cb));

	// 重新打开命令列表
	m_alloc->Reset();
	m_cmd->Reset(m_alloc.Get(), nullptr);
	m_cmdOpen = true;

	// flip 模型下必须画"当前"后备缓冲，否则 ExecuteCommandLists 直接报错并移除设备
	m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
	ID3D12Resource* back = m_backbuffer[m_frameIndex].Get();

	// 资源屏障：backbuffer -> RT
	D3D12_RESOURCE_BARRIER bar = {};
	bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	bar.Transition.pResource = back;
	bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_cmd->ResourceBarrier(1, &bar);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtv.ptr += SIZE_T(m_frameIndex *
		m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	FLOAT clearCol[4] = { bgR, bgG, bgB, 1.f };
	m_cmd->ClearRenderTargetView(rtv, clearCol, 0, nullptr);
	m_cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	D3D12_VIEWPORT vp = { 0.f, 0.f, float(m_w), float(m_h), 0.f, 1.f };
	D3D12_RECT sc = { 0, 0, LONG(m_w), LONG(m_h) };
	m_cmd->RSSetViewports(1, &vp);
	m_cmd->RSSetScissorRects(1, &sc);
}

void D3D12Renderer::DrawTris(const GpuVertex* v, size_t count)
{
	m_tri.insert(m_tri.end(), v, v + count);
}

void D3D12Renderer::DrawLines(const GpuVertex* v, size_t count)
{
	m_line.insert(m_line.end(), v, v + count);
}

void D3D12Renderer::End()
{
	if (!m_device || !m_cmdOpen)
		return;
	const size_t total = m_tri.size() + m_line.size();
	if (total > 0 && EnsureVertexCapacity(total) && m_vbData)
	{
		std::memcpy(m_vbData, m_tri.data(), m_tri.size() * sizeof(GpuVertex));
		std::memcpy(m_vbData + m_tri.size() * sizeof(GpuVertex),
			m_line.data(), m_line.size() * sizeof(GpuVertex));
	}

	m_cmd->SetGraphicsRootSignature(m_rootSig.Get());
	m_cmd->SetGraphicsRootConstantBufferView(0, m_cb->GetGPUVirtualAddress());

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = m_vb ? m_vb->GetGPUVirtualAddress() : 0;
	vbv.StrideInBytes = m_vbStride;
	vbv.SizeInBytes = UINT(total * sizeof(GpuVertex));
	m_cmd->IASetVertexBuffers(0, 1, &vbv);

	if (!m_tri.empty())
	{
		m_cmd->SetPipelineState(m_psoTri.Get());
		m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_cmd->DrawInstanced(UINT(m_tri.size()), 1, 0, 0);
	}
	if (!m_line.empty())
	{
		m_cmd->SetPipelineState(m_psoLine.Get());
		m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		m_cmd->DrawInstanced(UINT(m_line.size()), 1, UINT(m_tri.size()), 0);
	}

	// backbuffer -> PRESENT
	D3D12_RESOURCE_BARRIER bar = {};
	bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	bar.Transition.pResource = m_backbuffer[m_frameIndex].Get();
	bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_cmd->ResourceBarrier(1, &bar);

	m_cmd->Close();
	m_cmdOpen = false;
	ID3D12CommandList* lists[] = { m_cmd.Get() };
	m_queue->ExecuteCommandLists(1, lists);
	m_fenceValue++;
	m_queue->Signal(m_fence.Get(), m_fenceValue);

	m_swapchain->Present(1, 0);
	m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}
