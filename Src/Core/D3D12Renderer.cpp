#include "D3D12Renderer.h"

#include <d3dcompiler.h>
#include <DXGI.h>
#include <QtGlobal>
#include <QDebug>
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
	if (!CreateDevice())
	{
		qWarning("D3D12Renderer: Init failed at line 66");
		return false;
	}
	if (!CreatePipeline())
	{
		qWarning("D3D12Renderer: Init failed at line 68");
		return false;
	}
	RECT rc = {};
	GetClientRect(hwnd, &rc);
	m_w = std::max<LONG>(1, rc.right - rc.left);
	m_h = std::max<LONG>(1, rc.bottom - rc.top);
	if (!CreateSizeDependent(m_w, m_h))
	{
		qWarning("D3D12Renderer: Init failed at line 74");
		return false;
	}
	FlushCommands();
	qInfo("D3D12Renderer: init ok %dx%d", m_w, m_h);
	return true;
}

void D3D12Renderer::Shutdown()
{
	if (m_device)
		WaitForGpu();
	m_depth.Reset();
	m_rtTex.Reset(); m_readback.Reset();
	m_rtvHeap.Reset(); m_dsvHeap.Reset();
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

bool D3D12Renderer::CreateDevice()
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
	{
		qWarning("D3D12Renderer: CreateDevice failed at line 110");
		return false;
	}

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
		{
			qWarning("D3D12Renderer: CreateDevice failed at line 127");
			return false;
		}
	}

	D3D12_COMMAND_QUEUE_DESC qd = {};
	qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue))))
	{
		qWarning("D3D12Renderer: CreateDevice failed at line 133");
		return false;
	}

	if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc))))
	{
		qWarning("D3D12Renderer: CreateDevice failed at line 136");
		return false;
	}
	if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(), nullptr,
		IID_PPV_ARGS(&m_cmd))))
	{
		qWarning("D3D12Renderer: CreateDevice failed at line 139");
		return false;
	}
	m_cmd->Close();

	if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
	{
		qWarning("D3D12Renderer: CreateDevice failed at line 143");
		return false;
	}
	m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

	// RTV / DSV 描述符堆
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.NumDescriptors = 8;
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if (FAILED(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap))))
	{
		qWarning("D3D12Renderer: CreateDevice failed at line 151");
		return false;
	}
	hd.NumDescriptors = 1;
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	if (FAILED(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_dsvHeap))))
	{
		qWarning("D3D12Renderer: CreateDevice failed at line 155");
		return false;
	}
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
	{
		qWarning("D3D12Renderer: CreatePipeline failed at line 173");
		return false;
	}
	if (FAILED(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_rootSig))))
	{
		qWarning("D3D12Renderer: CreatePipeline failed at line 176");
		return false;
	}

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
	{
		qWarning("D3D12Renderer: CreatePipeline failed at line 188");
		return false;
	}
	D3D12_RANGE read = { 0, 0 };
	if (FAILED(m_cb->Map(0, &read, reinterpret_cast<void**>(&m_cbData))))
	{
		qWarning("D3D12Renderer: CreatePipeline failed at line 191");
		return false;
	}

	// PSO（三角形 / 线两种拓扑）
	ComPtr<ID3DBlob> vs = Compile(kVs, "vs_5_0");
	ComPtr<ID3DBlob> ps = Compile(kPs, "ps_5_0");
	if (!vs || !ps)
	{
		qWarning("D3D12Renderer: CreatePipeline failed at line 197");
		return false;
	}

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
	{
		qWarning("D3D12Renderer: CreatePipeline failed at line 218");
		return false;
	}
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	if (FAILED(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_psoLine))))
	{
		qWarning("D3D12Renderer: CreatePipeline failed at line 221");
		return false;
	}
	return true;
}

bool D3D12Renderer::CreateSizeDependent(int w, int h)
{
	w = std::max(1, w); h = std::max(1, h);
	// 重建离屏渲染目标 / 回读堆 / 深度
	m_rtTex.Reset();
	m_readback.Reset();
	m_depth.Reset();
	// CPU 侧帧缓冲：BGRA -> QImage::Format_ARGB32 在小端机上字面值一致
	m_frame = QImage(w, h, QImage::Format_ARGB32);
	m_frame.fill(QColor(30, 30, 33));

	// 离屏渲染目标（GPU 私有，跨 API 屏障一律不需要）
	D3D12_HEAP_PROPERTIES dp = {};
	dp.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC rd = {};
	rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rd.Width = UINT(w); rd.Height = UINT(h);
	rd.DepthOrArraySize = 1; rd.MipLevels = 1;
	rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	rd.SampleDesc.Count = 1;
	rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if (FAILED(m_device->CreateCommittedResource(&dp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&m_rtTex))))
	{
		qWarning("D3D12Renderer: CreateSizeDependent failed at line 248");
		return false;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE rtv0 = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	m_device->CreateRenderTargetView(m_rtTex.Get(), nullptr, rtv0);

	// 回读堆：用 READBACK 堆上的 buffer（最规范、兼容性最好的回读方式，
	// 不依赖"READBACK 纹理"这一各驱动差异较大的路径）
	D3D12_RESOURCE_DESC rtDesc = m_rtTex->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
	UINT64 totalBytes = 0;
	m_device->GetCopyableFootprints(&rtDesc, 0, 1, 0, &fp, nullptr, nullptr, &totalBytes);
	m_fp = fp;
	m_rowPitch = fp.Footprint.RowPitch;

	D3D12_HEAP_PROPERTIES rp = {};
	rp.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC bd = {};
	bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bd.Width = totalBytes;
	bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
	bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	HRESULT hr;
	if (hr = m_device->CreateCommittedResource(&rp, D3D12_HEAP_FLAG_NONE, &bd,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readback)); FAILED(hr))
	{
		qWarning("D3D12Renderer: readback tex failed 0x%08lx w=%d h=%d", (unsigned long)hr, w, h);
		return false;
	}

	// 深度
	D3D12_CLEAR_VALUE cv = {};
	cv.Format = DXGI_FORMAT_D32_FLOAT;
	cv.DepthStencil.Depth = 1.0f;
	{
		D3D12_RESOURCE_DESC dd = {};
		dd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		dd.Width = UINT(w); dd.Height = UINT(h);
		dd.DepthOrArraySize = 1; dd.MipLevels = 1;
		dd.Format = DXGI_FORMAT_D32_FLOAT;
		dd.SampleDesc.Count = 1;
		dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		if (FAILED(m_device->CreateCommittedResource(&dp, D3D12_HEAP_FLAG_NONE, &dd,
			D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&m_depth))))
		{
			qWarning("D3D12Renderer: CreateSizeDependent failed at line 279");
			return false;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
		m_device->CreateDepthStencilView(m_depth.Get(), nullptr, dsv);
	}
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
	const UINT64 v = ++m_fenceValue;      // m_fenceValue 始终等于"最近一次 Signal 的值"
	m_queue->Signal(m_fence.Get(), v);
	m_fence->SetEventOnCompletion(v, m_fenceEvent);
	WaitForSingleObject(m_fenceEvent, 5000);
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
	// 一帧记录命令期间不允许"重建"缓冲（已记录的 draw 引用旧缓冲数据）。
	// 但首次创建（m_vb 为空）必须放行——否则第一帧的所有 draw 都会被丢弃。
	if (m_cmdOpen && m_vb)
	{
		qWarning("D3D12Renderer: vertex buffer full (%zu verts), dropping draws", m_vbCapacity);
		return false;
	}
	// 指数扩容，至少 1M 顶点
	size_t cap = m_vbCapacity ? m_vbCapacity : (1u << 20);
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
	{
		qWarning("D3D12Renderer: EnsureVertexCapacity failed at line 341");
		return false;
	}
	if (m_vbData) { D3D12_RANGE r = { 0, 0 }; m_vb->Unmap(0, &r); }
	m_vb = nv;
	D3D12_RANGE read = { 0, 0 };
	if (FAILED(m_vb->Map(0, &read, reinterpret_cast<void**>(&m_vbData))))
	{
		qWarning("D3D12Renderer: EnsureVertexCapacity failed at line 346");
		return false;
	}
	m_vbCapacity = cap;
	return true;
}

void D3D12Renderer::Begin(const float view[16], const float proj[16],
	float bgR, float bgG, float bgB)
{
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

	// 重新打开命令列表；顶点缓冲游标归零（本帧的 draw 从头追加写入）
	m_vbUsed = 0;
	m_alloc->Reset();
	m_cmd->Reset(m_alloc.Get(), nullptr);
	m_cmdOpen = true;

	// 离屏纹理：COPY_SOURCE -> RENDER_TARGET
	{
		D3D12_RESOURCE_BARRIER bar = {};
		bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		bar.Transition.pResource = m_rtTex.Get();
		bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_cmd->ResourceBarrier(1, &bar);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
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
	if (!m_device || !m_cmdOpen || !v || count < 3)
		return;
	// 关键：每笔 draw 追加到缓冲的不同区段（Begin 里清零游标）。
	// 之前所有 draw 都写偏移 0，命令列表执行时只剩最后一笔数据，
	// 表现为"画面上只有一个三角形"。
	const size_t base = m_vbUsed;
	if (!EnsureVertexCapacity(base + count))
		return;
	std::memcpy(m_vbData + base * sizeof(GpuVertex), v, count * sizeof(GpuVertex));
	m_vbUsed = base + count;

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = m_vb->GetGPUVirtualAddress() + base * sizeof(GpuVertex);
	vbv.SizeInBytes = UINT(count * sizeof(GpuVertex));
	vbv.StrideInBytes = m_vbStride;

	m_cmd->SetPipelineState(m_psoTri.Get());
	m_cmd->SetGraphicsRootSignature(m_rootSig.Get());
	m_cmd->SetGraphicsRootConstantBufferView(0, m_cb->GetGPUVirtualAddress());
	m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_cmd->IASetVertexBuffers(0, 1, &vbv);
	m_cmd->DrawInstanced(UINT(count), 1, 0, 0);
}

void D3D12Renderer::DrawLines(const GpuVertex* v, size_t count)
{
	if (!m_device || !m_cmdOpen || !v || count < 2)
		return;
	const size_t base = m_vbUsed;
	if (!EnsureVertexCapacity(base + count))
		return;
	std::memcpy(m_vbData + base * sizeof(GpuVertex), v, count * sizeof(GpuVertex));
	m_vbUsed = base + count;

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = m_vb->GetGPUVirtualAddress() + base * sizeof(GpuVertex);
	vbv.SizeInBytes = UINT(count * sizeof(GpuVertex));
	vbv.StrideInBytes = m_vbStride;

	m_cmd->SetPipelineState(m_psoLine.Get());
	m_cmd->SetGraphicsRootSignature(m_rootSig.Get());
	m_cmd->SetGraphicsRootConstantBufferView(0, m_cb->GetGPUVirtualAddress());
	m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	m_cmd->IASetVertexBuffers(0, 1, &vbv);
	m_cmd->DrawInstanced(UINT(count), 1, 0, 0);
}

void D3D12Renderer::End()
{
	if (!m_device || !m_cmdOpen || !m_rtTex || !m_readback)
		return;

	// 离屏纹理：RENDER_TARGET -> COPY_SOURCE
	{
		D3D12_RESOURCE_BARRIER bar = {};
		bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		bar.Transition.pResource = m_rtTex.Get();
		bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_cmd->ResourceBarrier(1, &bar);
	}
	// 回读堆：COPY_DEST 常驻，不需要反向屏障。
	// 纹理 -> buffer 拷贝必须走 CopyTextureRegion + footprint。
	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = m_readback.Get();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dstLoc.PlacedFootprint = m_fp;
	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = m_rtTex.Get();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	srcLoc.SubresourceIndex = 0;
	m_cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

	// 关闭列表 + 执行 + 等完成（单帧在飞，简单可靠）
	m_cmd->Close();
	ID3D12CommandList* lists[] = { m_cmd.Get() };
	m_queue->ExecuteCommandLists(1, lists);
	m_cmdOpen = false;
	WaitForGpu();
	m_alloc->Reset();

	// GPU -> CPU 回读
	void* data = nullptr;
	HRESULT mh = m_readback->Map(0, nullptr, &data);   // READBACK 堆传 nullptr 表示整块
	if (FAILED(mh) || !data)
	{
		qWarning("D3D12Renderer: readback Map failed 0x%08lx rowPitch=%u h=%d", (unsigned long)mh, m_rowPitch, m_h);
		return;
	}
	if (m_frame.width() != m_w || m_frame.height() != m_h)
		m_frame = QImage(m_w, m_h, QImage::Format_ARGB32);
	const int dstBytes = m_w * 4;
	const BYTE* src = static_cast<const BYTE*>(data);
	for (int y = 0; y < m_h; ++y)
		std::memcpy(m_frame.scanLine(y), src + size_t(y) * m_rowPitch, dstBytes);
	m_readback->Unmap(0, nullptr);

}

