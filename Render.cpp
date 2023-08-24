#include "Render.h"

#include <dxgi1_6.h>
#include <d3dx12.h>
#include <dstorage.h>
#include <winrt/base.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

using winrt::com_ptr;
using winrt::check_hresult;

#define PI 3.14159265358979323846f
#define PI_2 (PI * 2.0f)
#define PI_HALF (PI * 0.5f)

#define NUM_QUEUED_FRAMES 3

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 711; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

enum class RenderTargets : int
{
    BackBuffer0 = 0,
    BackBuffer1,
    BackBuffer2,
    BackBufferLast = BackBuffer2, // needs to be NUM_QUEUED_FRAMES - 1

    VBuffer,
    ColorBuffer,

    RenderTargetCount,
};

enum class DepthStencilTargets : int
{
    MainDepth = 0,

    DepthStencilTargetCount,
};

enum BufferFlags
{
    BUFFER_FLAG_NONE = 0,
    BUFFER_FLAG_RAW = 1,
    BUFFER_FLAG_SRV = 2,
    BUFFER_FLAG_UAV = 4,
    BUFFER_FLAG_CBV = 8,
};

struct BufferDesc
{
    UINT count;
    UINT stride;
    UINT flags = BUFFER_FLAG_NONE;
    UINT srvDescriptorOffset = UINT_MAX;
    UINT uavDescriptorOffset = UINT_MAX;
    //UINT cbvDescriptorOffset;
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_STATES resrourceStates = D3D12_RESOURCE_STATE_COMMON;
    LPCWSTR name = nullptr;

    BufferDesc(UINT _count, UINT _stride) : count(_count), stride(_stride) {}
    BufferDesc& WithSRV(UINT offset) { srvDescriptorOffset = offset; flags |= BUFFER_FLAG_SRV; return *this; }
    BufferDesc& WithUAV(UINT offset) { uavDescriptorOffset = offset; flags |= BUFFER_FLAG_UAV; return *this; }
    BufferDesc& WithRAW() { flags |= BUFFER_FLAG_RAW; return *this; }
    BufferDesc& WithHeapType(D3D12_HEAP_TYPE _heapType) { heapType = _heapType; return *this; }
    BufferDesc& WithResourceState(D3D12_RESOURCE_STATES _resrourceStates) { resrourceStates = _resrourceStates; return *this; }
    BufferDesc& WithName(LPCWSTR str) { name = str; return *this; }
};

struct Buffer
{
    com_ptr<ID3D12Resource> resource;
};

struct WireContainer
{
    Buffer vertexBuffer;
    float3* vertexPtr;
    UINT maxVertices;
    UINT numVertices;

    void AddWireUnchecked(float3 p0, float3 p1)
    {
        vertexPtr[numVertices + 0] = p0;
        vertexPtr[numVertices + 1] = p1;

        numVertices += 2;
    }

    void AddWire(float3 p0, float3 p1)
    {
        if (numVertices + 2 > maxVertices)
            return;

        AddWireUnchecked(p0, p1);
    }

    void AddAABB(CenterExtentsAABB& aabb)
    {
        if (numVertices + 24 > maxVertices)
            return;

        float3 p0 = aabb.Center + float3(-aabb.Extents.x, -aabb.Extents.y, -aabb.Extents.z);
        float3 p1 = aabb.Center + float3( aabb.Extents.x, -aabb.Extents.y, -aabb.Extents.z);
        float3 p2 = aabb.Center + float3(-aabb.Extents.x,  aabb.Extents.y, -aabb.Extents.z);
        float3 p3 = aabb.Center + float3( aabb.Extents.x,  aabb.Extents.y, -aabb.Extents.z);

        float3 p4 = aabb.Center + float3(-aabb.Extents.x, -aabb.Extents.y,  aabb.Extents.z);
        float3 p5 = aabb.Center + float3( aabb.Extents.x, -aabb.Extents.y,  aabb.Extents.z);
        float3 p6 = aabb.Center + float3(-aabb.Extents.x,  aabb.Extents.y,  aabb.Extents.z);
        float3 p7 = aabb.Center + float3( aabb.Extents.x,  aabb.Extents.y,  aabb.Extents.z);

		AddWireUnchecked(p0, p1);
		AddWireUnchecked(p2, p3);
		AddWireUnchecked(p0, p2);
		AddWireUnchecked(p1, p3);

		AddWireUnchecked(p4, p5);
		AddWireUnchecked(p6, p7);
		AddWireUnchecked(p4, p6);
		AddWireUnchecked(p5, p7);

		AddWireUnchecked(p0, p4);
		AddWireUnchecked(p1, p5);
		AddWireUnchecked(p2, p6);
		AddWireUnchecked(p3, p7);
    }

    void AddFrustum(float4x4& mat)
	{
        if (numVertices + 24 > maxVertices)
            return;

		float4 p0nt = transform(float4(-1.0f, -1.0f, 0.0f, 1.0f), mat);
		float4 p1nt = transform(float4(1.0f, -1.0f, 0.0f, 1.0f), mat);
		float4 p2nt = transform(float4(-1.0f, 1.0f, 0.0f, 1.0f), mat);
		float4 p3nt = transform(float4(1.0f, 1.0f, 0.0f, 1.0f), mat);

		float4 p0ft = transform(float4(-1.0f, -1.0f, 0.9f, 1.0f), mat);
		float4 p1ft = transform(float4(1.0f, -1.0f, 0.9f, 1.0f), mat);
		float4 p2ft = transform(float4(-1.0f, 1.0f, 0.9f, 1.0f), mat);
		float4 p3ft = transform(float4(1.0f, 1.0f, 0.9f, 1.0f), mat);

		float3 p0n = float3(p0nt.x, p0nt.y, p0nt.z) / p0nt.w;
		float3 p1n = float3(p1nt.x, p1nt.y, p1nt.z) / p1nt.w;
		float3 p2n = float3(p2nt.x, p2nt.y, p2nt.z) / p2nt.w;
		float3 p3n = float3(p3nt.x, p3nt.y, p3nt.z) / p3nt.w;

		float3 p0f = float3(p0ft.x, p0ft.y, p0ft.z) / p0ft.w;
		float3 p1f = float3(p1ft.x, p1ft.y, p1ft.z) / p1ft.w;
		float3 p2f = float3(p2ft.x, p2ft.y, p2ft.z) / p2ft.w;
		float3 p3f = float3(p3ft.x, p3ft.y, p3ft.z) / p3ft.w;

		AddWireUnchecked(p0n, p1n);
		AddWireUnchecked(p2n, p3n);
		AddWireUnchecked(p0n, p2n);
		AddWireUnchecked(p1n, p3n);

		AddWireUnchecked(p0f, p1f);
		AddWireUnchecked(p2f, p3f);
		AddWireUnchecked(p0f, p2f);
		AddWireUnchecked(p1f, p3f);

		AddWireUnchecked(p0n, p0f);
		AddWireUnchecked(p1n, p1f);
		AddWireUnchecked(p2n, p2f);
		AddWireUnchecked(p3n, p3f);
	}
};

struct Render
{
    UINT width = 1280;
    UINT height = 720;

    com_ptr<IDXGIFactory6> dxgiFactory;
    com_ptr<ID3D12Device6> device;
    com_ptr<IDStorageFactory> storageFactory;
    com_ptr<ID3D12CommandQueue> commandQueue;
    com_ptr<IDStorageQueue> storageQueue;
    com_ptr<IDXGISwapChain3> swapChain;
    UINT frameIndex;

    com_ptr<ID3D12DescriptorHeap> rtvHeap;
    com_ptr<ID3D12DescriptorHeap> dsvHeap;
    com_ptr<ID3D12DescriptorHeap> uniHeap;
    UINT rtvDescriptorSize;
    UINT dsvDescriptorSize;
    UINT uniDescriptorSize;

    com_ptr<ID3D12Resource> backBuffers[NUM_QUEUED_FRAMES];
    CD3DX12_CPU_DESCRIPTOR_HANDLE backBufferRTVs[NUM_QUEUED_FRAMES];
    com_ptr<ID3D12CommandAllocator> commandAllocators[NUM_QUEUED_FRAMES];

    com_ptr<ID3D12Resource> vBuffer;
    CD3DX12_CPU_DESCRIPTOR_HANDLE vBufferRTV;

    com_ptr<ID3D12Resource> colorBuffer;
    CD3DX12_CPU_DESCRIPTOR_HANDLE colorBufferRTV;

    com_ptr<ID3D12Resource> depthStencil;
    CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilDSV;

    UINT numInstances;
    UINT numClusters;
    UINT maxNumClusters;
    Constants constantBufferData;
    Buffer constantBuffer;
    Buffer instancesBuffer;
    Buffer meshesBuffer;
    Buffer clustersBuffer;
    Buffer positionsBuffer;
    Buffer normalsBuffer;
    Buffer tangentsBuffer;
    Buffer texcoordsBuffer;
    Buffer indexDataBuffer;
    Buffer materialsBuffer;
    char* cbvDataBegin;

    Instance* instancesCpu;

    Buffer visibleInstances;
    Buffer visibleClusters;
    Buffer visibleInstancesCounter;
    Buffer visibleClustersCounter;

    Buffer readbackBuffer;

    com_ptr<ID3D12RootSignature> drawRootSignature;
    com_ptr<ID3D12RootSignature> drawWireRootSignature;
    com_ptr<ID3D12PipelineState> drawMeshPSO;
    com_ptr<ID3D12PipelineState> drawWirePSO;

    com_ptr<ID3D12PipelineState> frameSetupPSO;
    com_ptr<ID3D12PipelineState> instanceCullingPSO;
    com_ptr<ID3D12PipelineState> clusterCullingPSO;
    com_ptr<ID3D12PipelineState> materialPSO;

    com_ptr<ID3D12GraphicsCommandList6> commandList;

    com_ptr<ID3D12Fence1> fence;
    UINT64 fenceValues[NUM_QUEUED_FRAMES];
    HANDLE fenceEvent;

    com_ptr<ID3D12CommandSignature> commandSignature;

    CD3DX12_VIEWPORT viewport;
    CD3DX12_RECT scissorRect;

    struct Camera
    {
        float4x4 viewMat;
        float4x4 projMat;
        float pitch;
        float yaw;
        float3 pos;
    } cullingCamera, drawingCamera;

    double lastTime;

    WireContainer wireContainer[NUM_QUEUED_FRAMES];
};

struct handle_closer
{
    void operator()(HANDLE h) noexcept
    {
        assert(h != INVALID_HANDLE_VALUE);
        if (h)
        {
            CloseHandle(h);
        }
    }
};
using ScopedHandle = std::unique_ptr<void, handle_closer>;

static void GetHardwareAdapter(IDXGIFactory6* factory, IDXGIAdapter4** ppAdapter)
{
    *ppAdapter = nullptr;

    com_ptr<IDXGIAdapter4> adapter;

	for(UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		if (SUCCEEDED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr)))
			break;
	}

    *ppAdapter = adapter.detach();
}

static HRESULT ReadDataFromFile(LPCWSTR filename, byte** data, UINT* size)
{
    CREATEFILE2_EXTENDED_PARAMETERS extendedParams = {};
    extendedParams.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
    extendedParams.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    extendedParams.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
    extendedParams.dwSecurityQosFlags = SECURITY_ANONYMOUS;
    extendedParams.lpSecurityAttributes = nullptr;
    extendedParams.hTemplateFile = nullptr;

    ScopedHandle file(CreateFile2(filename, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &extendedParams));
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        throw std::exception();
    }

    FILE_STANDARD_INFO fileInfo = {};
    if (!GetFileInformationByHandleEx(file.get(), FileStandardInfo, &fileInfo, sizeof(fileInfo)))
    {
        throw std::exception();
    }

    if (fileInfo.EndOfFile.HighPart != 0)
    {
        throw std::exception();
    }

    *data = reinterpret_cast<byte*>(malloc(fileInfo.EndOfFile.LowPart));
    *size = fileInfo.EndOfFile.LowPart;

    if (!ReadFile(file.get(), *data, fileInfo.EndOfFile.LowPart, nullptr, nullptr))
    {
        throw std::exception();
    }

    return S_OK;
}

static com_ptr<ID3D12DescriptorHeap> CreateDescriptorHeap(Render* render, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE)
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = count;
	heapDesc.Type = type;
	heapDesc.Flags = flags;

    com_ptr<ID3D12DescriptorHeap> heap;
	check_hresult(render->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)));

    return heap;
}

static void CreateBuffer(Render* render, Buffer* out_buffer, const BufferDesc& desc)
{
    UINT size = desc.stride * desc.count;

    auto resourceFlags = D3D12_RESOURCE_FLAG_NONE;
    bool isRaw = desc.flags & BUFFER_FLAG_RAW;
    bool isUAV = desc.flags & BUFFER_FLAG_UAV;
    if (isUAV)
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(desc.heapType);
	auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size, resourceFlags);
	check_hresult(render->device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(out_buffer->resource.put())));
    out_buffer->resource->SetName(desc.name);


    if (desc.flags & BUFFER_FLAG_SRV)
    {
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(render->uniHeap->GetCPUDescriptorHandleForHeapStart(), desc.srvDescriptorOffset, render->uniDescriptorSize);
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		D3D12_BUFFER_SRV_FLAGS srvFlags = D3D12_BUFFER_SRV_FLAG_NONE;
		if (isRaw)
		{
			format = DXGI_FORMAT_R32_TYPELESS;
			srvFlags = D3D12_BUFFER_SRV_FLAG_RAW;
		}

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = isRaw ? (size / 4) : desc.count;
        srvDesc.Buffer.StructureByteStride = isRaw ? 0 : desc.stride;
        srvDesc.Buffer.Flags = srvFlags;

        render->device->CreateShaderResourceView(out_buffer->resource.get(), &srvDesc, handle);
    }

    if (desc.flags & BUFFER_FLAG_UAV)
    {
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(render->uniHeap->GetCPUDescriptorHandleForHeapStart(), desc.uavDescriptorOffset, render->uniDescriptorSize);
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        D3D12_BUFFER_UAV_FLAGS uavFlags = D3D12_BUFFER_UAV_FLAG_NONE;
        if (isRaw)
        {
            format = DXGI_FORMAT_R32_TYPELESS;
            uavFlags = D3D12_BUFFER_UAV_FLAG_RAW;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = isRaw ? (size / 4) : desc.count;
        uavDesc.Buffer.StructureByteStride = isRaw ? 0 : desc.stride;
        uavDesc.Buffer.Flags = uavFlags;
        render->device->CreateUnorderedAccessView(out_buffer->resource.get(), nullptr, &uavDesc, handle);
    }
}

static void OpenFileForLoading(Render* render, LPCWSTR fileName, com_ptr<IDStorageFile>& file, UINT32& size)
{
	check_hresult(render->storageFactory->OpenFile(fileName, IID_PPV_ARGS(file.put())));

	BY_HANDLE_FILE_INFORMATION info = {};
	check_hresult(file->GetFileInformation(&info));
    size = info.nFileSizeLow;
}

static void LoadFileToGPU(Render* render, const com_ptr<IDStorageFile>& file, ID3D12Resource* resource, UINT32 size)
{
	DSTORAGE_REQUEST request = {};
	request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
    request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
	request.Source.File.Source = file.get();
	request.Source.File.Offset = 0;
	request.Source.File.Size = size;
	request.UncompressedSize = size;
	request.Destination.Buffer.Resource = resource;
	request.Destination.Buffer.Offset = 0;
	request.Destination.Buffer.Size = size;

	render->storageQueue->EnqueueRequest(&request);
}

static void LoadFileToCPU(Render* render, const com_ptr<IDStorageFile>& file, void* buffer, UINT32 size)
{
	DSTORAGE_REQUEST request = {};
	request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
    request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
	request.Source.File.Source = file.get();
	request.Source.File.Offset = 0;
	request.Source.File.Size = size;
	request.UncompressedSize = size;
    request.Destination.Memory.Buffer = buffer;
	request.Destination.Memory.Size = size;

	render->storageQueue->EnqueueRequest(&request);
}

Render* CreateRender(UINT width, UINT height)
{
    Render* render = new Render;

    render->width = width;
    render->height = height;

    render->frameIndex = 0;

    render->rtvDescriptorSize = 0;
    render->dsvDescriptorSize = 0;

    return render;
}

void Destroy(Render* render)
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    free(render->instancesCpu);

	delete render;
}

void Initialize(Render* render, HWND hwnd)
{
    UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
    {
        com_ptr<ID3D12Debug> debugController;
        HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(debugController.put()));
        if (SUCCEEDED(hr))
        {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

            com_ptr<ID3D12Debug1> debugController1;
            hr = debugController->QueryInterface(IID_PPV_ARGS(debugController1.put()));
            if (SUCCEEDED(hr))
            {
                debugController1->SetEnableGPUBasedValidation(true);
            }
        }
    }
#endif

    {
        com_ptr<IDXGIFactory4> initialFactory;
        check_hresult(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&initialFactory)));

        check_hresult(initialFactory->QueryInterface(IID_PPV_ARGS(render->dxgiFactory.put())));
    }

    bool useWarpDevice = false;
    if (useWarpDevice)
    {
        com_ptr<IDXGIAdapter3> warpAdapter;
        check_hresult(render->dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
        check_hresult(D3D12CreateDevice(warpAdapter.get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(render->device.put())));
    }
    else
    {
        com_ptr<IDXGIAdapter4> hardwareAdapter;
        GetHardwareAdapter(render->dxgiFactory.get(), hardwareAdapter.put());
        check_hresult(D3D12CreateDevice(hardwareAdapter.get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(render->device.put())));
    }

    {
        DSTORAGE_CONFIGURATION dsConfig{};
        DStorageSetConfiguration(&dsConfig);

        check_hresult(DStorageGetFactory(IID_PPV_ARGS(render->storageFactory.put())));
        render->storageFactory->SetDebugFlags(DSTORAGE_DEBUG_BREAK_ON_ERROR | DSTORAGE_DEBUG_SHOW_ERRORS);
        render->storageFactory->SetStagingBufferSize(128 * 1024 * 1024);

        DSTORAGE_QUEUE_DESC queueDesc = {};
        queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
        queueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
        queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        queueDesc.Device = render->device.get();

        check_hresult(render->storageFactory->CreateQueue(&queueDesc, IID_PPV_ARGS(render->storageQueue.put())));
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    check_hresult(render->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&render->commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = NUM_QUEUED_FRAMES;
    swapChainDesc.Width = render->width;
    swapChainDesc.Height = render->height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    com_ptr<IDXGISwapChain1> swapChain1;
    check_hresult(render->dxgiFactory->CreateSwapChainForHwnd(
        render->commandQueue.get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        swapChain1.put()
    ));
    check_hresult(swapChain1.as(IID_PPV_ARGS(render->swapChain.put())));

    check_hresult(render->dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    render->frameIndex = render->swapChain->GetCurrentBackBufferIndex();

	render->rtvHeap = CreateDescriptorHeap(render, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, (int)RenderTargets::RenderTargetCount);
	render->dsvHeap = CreateDescriptorHeap(render, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, (int)DepthStencilTargets::DepthStencilTargetCount);
	render->uniHeap = CreateDescriptorHeap(render, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
	render->rtvDescriptorSize = render->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	render->dsvDescriptorSize = render->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	render->uniDescriptorSize = render->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    /*
     * Render Targets
     */
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvBaseHandle(render->rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Back buffers
        for (UINT n = 0; n < NUM_QUEUED_FRAMES; n++)
        {
            check_hresult(render->swapChain->GetBuffer(n, IID_PPV_ARGS(render->backBuffers[n].put())));
            render->backBufferRTVs[n] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvBaseHandle, (int)RenderTargets::BackBuffer0 + n, render->rtvDescriptorSize);
            render->device->CreateRenderTargetView(render->backBuffers[n].get(), nullptr, render->backBufferRTVs[n]);
        
            check_hresult(render->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(render->commandAllocators[n].put())));
        }

        // VBuffer
        {
            CD3DX12_HEAP_PROPERTIES heapType(D3D12_HEAP_TYPE_DEFAULT);

			D3D12_CLEAR_VALUE vBufferOptimizedClearValue = {};
            vBufferOptimizedClearValue.Format = DXGI_FORMAT_R32_UINT;
            vBufferOptimizedClearValue.Color[0] = 0.0f;
            vBufferOptimizedClearValue.Color[1] = 0.0f;
            vBufferOptimizedClearValue.Color[2] = 0.0f;
            vBufferOptimizedClearValue.Color[3] = 0.0f;

            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_UINT, render->width, render->height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

			check_hresult(render->device->CreateCommittedResource(
				&heapType,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				&vBufferOptimizedClearValue,
				IID_PPV_ARGS(render->vBuffer.put())
			));

            render->vBufferRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvBaseHandle, (int)RenderTargets::VBuffer, render->rtvDescriptorSize);
            render->device->CreateRenderTargetView(render->vBuffer.get(), nullptr, render->vBufferRTV);

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R32_UINT;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            render->device->CreateShaderResourceView(render->vBuffer.get(), &srvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(render->uniHeap->GetCPUDescriptorHandleForHeapStart(), VBUFFER_SRV, render->uniDescriptorSize));
        }

        // Color buffer
        {
            CD3DX12_HEAP_PROPERTIES heapType(D3D12_HEAP_TYPE_DEFAULT);

            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, render->width, render->height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

			check_hresult(render->device->CreateCommittedResource(
				&heapType,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_SOURCE,
				nullptr,
				IID_PPV_ARGS(render->colorBuffer.put())
			));

            render->colorBufferRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvBaseHandle, (int)RenderTargets::ColorBuffer, render->rtvDescriptorSize);
            render->device->CreateRenderTargetView(render->colorBuffer.get(), nullptr, render->colorBufferRTV);

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.PlaneSlice = 0;
            uavDesc.Texture2D.MipSlice = 0;
            render->device->CreateUnorderedAccessView(render->colorBuffer.get(), nullptr, &uavDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(render->uniHeap->GetCPUDescriptorHandleForHeapStart(), COLORBUFFER_UAV, render->uniDescriptorSize));
        }
    }

    /*
     * DepthStencil Targets
     */
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
        depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        CD3DX12_HEAP_PROPERTIES heapType(D3D12_HEAP_TYPE_DEFAULT);

        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, render->width, render->height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        check_hresult(render->device->CreateCommittedResource(
            &heapType,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(render->depthStencil.put())
        ));

        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvBaseHandle(render->dsvHeap->GetCPUDescriptorHandleForHeapStart());
        render->depthStencilDSV = CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvBaseHandle, (int)DepthStencilTargets::MainDepth, render->dsvDescriptorSize);
        render->device->CreateDepthStencilView(render->depthStencil.get(), &depthStencilDesc, render->depthStencilDSV);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvBaseHandle(render->uniHeap->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvBaseHandle, DEPTHBUFFER_SRV, render->uniDescriptorSize);
        render->device->CreateShaderResourceView(render->depthStencil.get(), &srvDesc, srvHandle);
    }

    /*
     * Global Constant Buffer
     */
    {
        const UINT64 constantBufferSize = sizeof(Constants) * NUM_QUEUED_FRAMES;
        CreateBuffer(render, &render->constantBuffer,
            BufferDesc(NUM_QUEUED_FRAMES, sizeof(Constants))
            .WithName(L"ConstantBuffer")
            .WithHeapType(D3D12_HEAP_TYPE_UPLOAD)
            .WithResourceState(D3D12_RESOURCE_STATE_GENERIC_READ));

        CD3DX12_RANGE readRange(0, 0);
        check_hresult(render->constantBuffer.resource->Map(0, &readRange, reinterpret_cast<void**>(&render->cbvDataBegin)));
    }

    /*
     * Open files to query sizes
     */
    com_ptr<IDStorageFile> instancesFile;
    com_ptr<IDStorageFile> meshesFile;
    com_ptr<IDStorageFile> clustersFile;
    com_ptr<IDStorageFile> positionsFile;
    com_ptr<IDStorageFile> normalsFile;
    com_ptr<IDStorageFile> tangentsFile;
    com_ptr<IDStorageFile> texcoordsFile;
    com_ptr<IDStorageFile> indicesFile;
    com_ptr<IDStorageFile> materialsFile;
    UINT32 instancesSize = 0;
    UINT32 meshesSize = 0;
    UINT32 clustersSize = 0;
    UINT32 positionsSize = 0;
    UINT32 normalsSize = 0;
    UINT32 tangentsSize = 0;
    UINT32 texcoordsSize = 0;
    UINT32 indicesSize = 0;
    UINT32 materialsSize = 0;
	OpenFileForLoading(render, L"instances.raw", instancesFile, instancesSize);
	OpenFileForLoading(render, L"meshes.raw", meshesFile, meshesSize);
	OpenFileForLoading(render, L"clusters.raw", clustersFile, clustersSize);
	OpenFileForLoading(render, L"positions.raw", positionsFile, positionsSize);
	OpenFileForLoading(render, L"normals.raw", normalsFile, normalsSize);
	OpenFileForLoading(render, L"tangents.raw", tangentsFile, tangentsSize);
	OpenFileForLoading(render, L"texcoords.raw", texcoordsFile, texcoordsSize);
	OpenFileForLoading(render, L"indices.raw", indicesFile, indicesSize);
	OpenFileForLoading(render, L"materials.raw", materialsFile, materialsSize);
    render->numInstances = instancesSize / sizeof(Instance);
    render->numClusters = clustersSize / sizeof(Cluster);
    render->maxNumClusters = MAX_ELEMENTS;

    assert(render->numInstances <= MAX_ELEMENTS);
    assert(render->numClusters <= MAX_ELEMENTS);

    /*
     * Mesh and Instance Pool
     */
	CreateBuffer(render, &render->instancesBuffer,
        BufferDesc(instancesSize / sizeof(Instance), sizeof(Instance))
        .WithName(L"InstancesBuffer")
        .WithSRV(INSTANCE_BUFFER_SRV));
	CreateBuffer(render, &render->meshesBuffer,
        BufferDesc(meshesSize / sizeof(Mesh), sizeof(Mesh))
        .WithName(L"MeshesBuffer")
        .WithSRV(MESH_BUFFER_SRV));
	CreateBuffer(render, &render->clustersBuffer,
        BufferDesc(clustersSize / sizeof(Cluster), sizeof(Cluster))
        .WithName(L"ClustersBuffer")
        .WithSRV(CLUSTER_BUFFER_SRV));
	CreateBuffer(render, &render->positionsBuffer,
        BufferDesc(positionsSize / sizeof(float3), sizeof(float3))
        .WithName(L"PositionsBuffer")
        .WithSRV(POSITION_DATA_BUFFER_SRV)
        .WithRAW());
	CreateBuffer(render, &render->normalsBuffer,
        BufferDesc(normalsSize / sizeof(float3), sizeof(float3))
        .WithName(L"NormalsBuffer")
        .WithSRV(NORMAL_DATA_BUFFER_SRV)
        .WithRAW());
	CreateBuffer(render, &render->tangentsBuffer,
        BufferDesc(tangentsSize / sizeof(float4), sizeof(float4))
        .WithName(L"TangentsBuffer")
        .WithSRV(TANGENT_DATA_BUFFER_SRV)
        .WithRAW());
	CreateBuffer(render, &render->texcoordsBuffer,
        BufferDesc(texcoordsSize / sizeof(float2), sizeof(float2))
        .WithName(L"TexcoordsBuffer")
        .WithSRV(TEXCOORD_DATA_BUFFER_SRV)
        .WithRAW());
	CreateBuffer(render, &render->indexDataBuffer,
        BufferDesc(indicesSize / sizeof(UINT), sizeof(UINT))
        .WithName(L"IndexDataBuffer")
        .WithSRV(INDEX_DATA_BUFFER_SRV)
        .WithRAW());
	CreateBuffer(render, &render->materialsBuffer,
        BufferDesc(materialsSize / sizeof(Material), sizeof(Material))
        .WithName(L"MaterialsBuffer")
        .WithSRV(MATERIAL_BUFFER_SRV));

    /*
     * Intermediate buffers
     */
	CreateBuffer(render, &render->visibleInstances,
        BufferDesc(render->numInstances, sizeof(UINT))
        .WithName(L"VisibleInstancesBuffer")
        .WithSRV(VISIBLE_INSTANCES_SRV)
        .WithUAV(VISIBLE_INSTANCES_UAV)
        .WithRAW());
    CreateBuffer(render, &render->visibleClusters,
        BufferDesc(render->maxNumClusters, sizeof(UINT))
        .WithName(L"VisibleClustersBuffer")
        .WithSRV(VISIBLE_CLUSTERS_SRV)
        .WithUAV(VISIBLE_CLUSTERS_UAV)
        .WithRAW());
    CreateBuffer(render, &render->visibleInstancesCounter,
        BufferDesc(1, sizeof(UINT))
        .WithName(L"VisibleInstancesCounter")
        .WithUAV(VISIBLE_INSTANCES_COUNTER_UAV)
        .WithRAW());
    CreateBuffer(render, &render->visibleClustersCounter,
        BufferDesc(1, 3 * sizeof(UINT))
        .WithName(L"VisibleClustersCounter")
        .WithUAV(VISIBLE_CLUSTERS_COUNTER_UAV)
        .WithRAW());

    CreateBuffer(render, &render->readbackBuffer,
        BufferDesc((1 + 3) * NUM_QUEUED_FRAMES, sizeof(UINT))
        .WithName(L"ReadBackBuffer")
        .WithHeapType(D3D12_HEAP_TYPE_READBACK));

    /*
     * Root Signature
     */
    {
        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].InitAsConstantBufferView(0);

        auto desc = CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

        com_ptr<ID3DBlob> rootBlob;
        com_ptr<ID3DBlob> errorBlob;
        check_hresult(D3D12SerializeVersionedRootSignature(&desc, rootBlob.put(), errorBlob.put()));

        check_hresult(render->device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(render->drawRootSignature.put())));
    }

    /*
     * Wire root Signature
     */
    {
        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].InitAsConstantBufferView(0);

        auto desc = CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        com_ptr<ID3DBlob> rootBlob;
        com_ptr<ID3DBlob> errorBlob;
        check_hresult(D3D12SerializeVersionedRootSignature(&desc, rootBlob.put(), errorBlob.put()));

        check_hresult(render->device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(render->drawWireRootSignature.put())));
    }

    /*
     * VBuffer PSO
     */
    {
        struct
        {
            byte* data;
            uint32_t size;
        } meshShader, pixelShader;

        ReadDataFromFile(L"x64/Debug/VBufferMS.cso", &meshShader.data, &meshShader.size);
        ReadDataFromFile(L"x64/Debug/VBufferPS.cso", &pixelShader.data, &pixelShader.size);

        D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = render->drawRootSignature.get();
        psoDesc.MS = { meshShader.data, meshShader.size };
        psoDesc.PS = { pixelShader.data, pixelShader.size };
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = render->vBuffer->GetDesc().Format;
        psoDesc.DSVFormat = render->depthStencil->GetDesc().Format;
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.SampleDesc = DefaultSampleDesc();

        auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);

        D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
        streamDesc.pPipelineStateSubobjectStream = &psoStream;
        streamDesc.SizeInBytes = sizeof(psoStream);

        check_hresult(render->device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(render->drawMeshPSO.put())));

        free(meshShader.data);
        free(pixelShader.data);
    }

    /*
     * Wire PSO
     */
    {
        struct
        {
            byte* data;
            uint32_t size;
        } vertexShader, pixelShader;

        ReadDataFromFile(L"x64/Debug/WireVS.cso", &vertexShader.data, &vertexShader.size);
        ReadDataFromFile(L"x64/Debug/WirePS.cso", &pixelShader.data, &pixelShader.size);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = render->drawWireRootSignature.get();
        psoDesc.VS = { vertexShader.data, vertexShader.size };
        psoDesc.PS = { pixelShader.data, pixelShader.size };
		//psoDesc.StreamOutput;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
		psoDesc.SampleMask = 1;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
		//psoDesc.DepthStencilState;
        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        psoDesc.InputLayout = { inputLayout, 1 };
		//psoDesc.IBStripCutValue;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		//psoDesc.DSVFormat;
        psoDesc.SampleDesc = { 1, 0 };
		//psoDesc.NodeMask;
		//psoDesc.CachedPSO;
		//psoDesc.Flags;

		check_hresult(render->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(render->drawWirePSO.put())));

        free(vertexShader.data);
        free(pixelShader.data);
    }

    /*
     * Compute PSOs
     */
    {
        struct
        {
            byte* data;
            uint32_t size;
        } frameSetup, instanceCullingComputeShader, clusterCullingComputeShader, materialComputeShader;

        ReadDataFromFile(L"x64/Debug/FrameSetup.cso", &frameSetup.data, &frameSetup.size);
        ReadDataFromFile(L"x64/Debug/InstanceCulling.cso", &instanceCullingComputeShader.data, &instanceCullingComputeShader.size);
        ReadDataFromFile(L"x64/Debug/ClusterCulling.cso", &clusterCullingComputeShader.data, &clusterCullingComputeShader.size);
        ReadDataFromFile(L"x64/Debug/Material.cso", &materialComputeShader.data, &materialComputeShader.size);

        {
			D3D12_COMPUTE_PIPELINE_STATE_DESC desc {};
			desc.pRootSignature = render->drawRootSignature.get();
			desc.CS = { frameSetup.data, frameSetup.size};

			check_hresult(render->device->CreateComputePipelineState(&desc, IID_PPV_ARGS(render->frameSetupPSO.put())));
        }

        {
			D3D12_COMPUTE_PIPELINE_STATE_DESC desc {};
			desc.pRootSignature = render->drawRootSignature.get();
			desc.CS = { instanceCullingComputeShader.data, instanceCullingComputeShader.size};

			check_hresult(render->device->CreateComputePipelineState(&desc, IID_PPV_ARGS(render->instanceCullingPSO.put())));
        }

        {
			D3D12_COMPUTE_PIPELINE_STATE_DESC desc {};
			desc.pRootSignature = render->drawRootSignature.get();
			desc.CS = { clusterCullingComputeShader.data, clusterCullingComputeShader.size};

			check_hresult(render->device->CreateComputePipelineState(&desc, IID_PPV_ARGS(render->clusterCullingPSO.put())));
        }

        {
			D3D12_COMPUTE_PIPELINE_STATE_DESC desc {};
			desc.pRootSignature = render->drawRootSignature.get();
			desc.CS = { materialComputeShader.data, materialComputeShader.size};

			check_hresult(render->device->CreateComputePipelineState(&desc, IID_PPV_ARGS(render->materialPSO.put())));
        }

        free(instanceCullingComputeShader.data);
        free(clusterCullingComputeShader.data);
        free(materialComputeShader.data);
    }

    check_hresult(render->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, render->commandAllocators[render->frameIndex].get(), nullptr, IID_PPV_ARGS(render->commandList.put())));
    check_hresult(render->commandList->Close());

    /*
     * Load data using DirectStorage
     */
    {
        render->instancesCpu = (Instance*)malloc(instancesSize);

        LoadFileToGPU(render, instancesFile, render->instancesBuffer.resource.get(), instancesSize);
        LoadFileToCPU(render, instancesFile, render->instancesCpu, instancesSize);
        LoadFileToGPU(render, meshesFile, render->meshesBuffer.resource.get(), meshesSize);
        LoadFileToGPU(render, clustersFile, render->clustersBuffer.resource.get(), clustersSize);
        LoadFileToGPU(render, positionsFile, render->positionsBuffer.resource.get(), positionsSize);
        LoadFileToGPU(render, normalsFile, render->normalsBuffer.resource.get(), normalsSize);
        LoadFileToGPU(render, tangentsFile, render->tangentsBuffer.resource.get(), tangentsSize);
        LoadFileToGPU(render, texcoordsFile, render->texcoordsBuffer.resource.get(), texcoordsSize);
        LoadFileToGPU(render, indicesFile, render->indexDataBuffer.resource.get(), indicesSize);
        LoadFileToGPU(render, materialsFile, render->materialsBuffer.resource.get(), materialsSize);

        // Issue a fence and wait for it
        {
            com_ptr<ID3D12Fence> fence;
            check_hresult(render->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put())));

            ScopedHandle fenceEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr));
            constexpr uint64_t fenceValue = 1;
            check_hresult(fence->SetEventOnCompletion(fenceValue, fenceEvent.get()));
            render->storageQueue->EnqueueSignal(fence.get(), fenceValue);

            render->storageQueue->Submit();

            WaitForSingleObject(fenceEvent.get(), INFINITE);

            DSTORAGE_ERROR_RECORD errorRecord{};
            render->storageQueue->RetrieveErrorRecord(&errorRecord);
            if (FAILED(errorRecord.FirstFailure.HResult))
            {
                __debugbreak();
            }
        }
    }

    {
        check_hresult(render->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(render->fence.put())));
        render->fenceValues[render->frameIndex]++;

        render->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (render->fenceEvent == nullptr)
        {
            check_hresult(HRESULT_FROM_WIN32(GetLastError()));
        }
    }

    /*
     * Command signature
     */
    {
        D3D12_INDIRECT_ARGUMENT_DESC arg {};
        arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
        D3D12_COMMAND_SIGNATURE_DESC desc {};
        desc.ByteStride = 3 * sizeof(UINT);
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs = &arg;
        check_hresult(render->device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(render->commandSignature.put())));
    }

    render->viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)render->width, (float)render->height);
    render->scissorRect = CD3DX12_RECT(0, 0, render->width, render->height);
	
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	
	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(render->uniHeap->GetCPUDescriptorHandleForHeapStart(), IMGUI_FONT_SRV, render->uniDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(render->uniHeap->GetGPUDescriptorHandleForHeapStart(), IMGUI_FONT_SRV, render->uniDescriptorSize);
	ImGui_ImplDX12_Init(render->device.get(), NUM_QUEUED_FRAMES,
		DXGI_FORMAT_R8G8B8A8_UNORM, render->uniHeap.get(),
		cpuHandle,
		gpuHandle);


    render->cullingCamera.projMat = make_float4x4_perspective_field_of_view(XM_PI / 3.0f, (float)render->width / (float)render->height, 1.0f, 10000.0f);
    render->cullingCamera.pitch = 0.0f;
    render->cullingCamera.yaw = 0.0f;
    render->cullingCamera.pos = float3(0.0f, 0.0f, 0.0f);
    render->cullingCamera.viewMat = make_float4x4_translation(render->cullingCamera.pos);
    render->drawingCamera = render->cullingCamera;
    render->lastTime = ImGui::GetTime();

    uint wireContainerSize = render->numClusters * 12 * 2 + 1024;
    for (int i = 0; i < NUM_QUEUED_FRAMES; i++)
    {
        CreateBuffer(render, &render->wireContainer[i].vertexBuffer,
            BufferDesc(wireContainerSize, sizeof(float3))
            .WithHeapType(D3D12_HEAP_TYPE_UPLOAD)
            .WithName(L"WireContainer")
            .WithResourceState(D3D12_RESOURCE_STATE_GENERIC_READ)
        );

        CD3DX12_RANGE readRange(0, 0);
        check_hresult(render->wireContainer[i].vertexBuffer.resource->Map(0,
            &readRange,
            reinterpret_cast<void**>(&render->wireContainer[i].vertexPtr)));
        render->wireContainer->maxVertices = wireContainerSize;
        render->wireContainer->numVertices = 0;
    }
}

void ExtractPlanesD3D(plane* planes, const float4x4& comboMatrix, bool normalizePlanes)
{
    // Left clipping plane
    planes[0].normal.x = comboMatrix.m14 + comboMatrix.m11;
    planes[0].normal.y = comboMatrix.m24 + comboMatrix.m21;
    planes[0].normal.z = comboMatrix.m34 + comboMatrix.m31;
    planes[0].d        = comboMatrix.m44 + comboMatrix.m41;

    // Right clipping plane
    planes[1].normal.x = comboMatrix.m14 - comboMatrix.m11;
    planes[1].normal.y = comboMatrix.m24 - comboMatrix.m21;
    planes[1].normal.z = comboMatrix.m34 - comboMatrix.m31;
    planes[1].d        = comboMatrix.m44 - comboMatrix.m41;

    // Top clipping plane
    planes[2].normal.x = comboMatrix.m14 - comboMatrix.m12;
    planes[2].normal.y = comboMatrix.m24 - comboMatrix.m22;
    planes[2].normal.z = comboMatrix.m34 - comboMatrix.m32;
    planes[2].d        = comboMatrix.m44 - comboMatrix.m42;

    // Bottom clipping plane
    planes[3].normal.x = comboMatrix.m14 + comboMatrix.m12;
    planes[3].normal.y = comboMatrix.m24 + comboMatrix.m22;
    planes[3].normal.z = comboMatrix.m34 + comboMatrix.m32;
    planes[3].d        = comboMatrix.m44 + comboMatrix.m42;

    // Near clipping plane
    planes[4].normal.x = comboMatrix.m13;
    planes[4].normal.y = comboMatrix.m23;
    planes[4].normal.z = comboMatrix.m33;
    planes[4].d        = comboMatrix.m43;

    // Far clipping plane
    planes[5].normal.x = comboMatrix.m14 - comboMatrix.m13;
    planes[5].normal.y = comboMatrix.m24 - comboMatrix.m23;
    planes[5].normal.z = comboMatrix.m34 - comboMatrix.m33;
    planes[5].d        = comboMatrix.m44 - comboMatrix.m43;

    // Normalize the plane equations, if requested
    if (normalizePlanes)
    {
        planes[0] = normalize(planes[0]);
        planes[1] = normalize(planes[1]);
        planes[2] = normalize(planes[2]);
        planes[3] = normalize(planes[3]);
        planes[4] = normalize(planes[4]);
        planes[5] = normalize(planes[5]);
    }
}

void Draw(Render* render)
{
    check_hresult(render->commandAllocators[render->frameIndex]->Reset());
    check_hresult(render->commandList->Reset(render->commandAllocators[render->frameIndex].get(), render->drawMeshPSO.get()));

    D3D12_RANGE readbackBufferRange{
        (1 + 3) * sizeof(UINT) * render->frameIndex,
        (1 + 3) * sizeof(UINT) * (render->frameIndex + 1),
    };
    UINT* readbackPtr;
    render->readbackBuffer.resource->Map(0, &readbackBufferRange, (void**)&readbackPtr);
    UINT numInstancesPassedCulling = readbackPtr[0];
    UINT numClustersPassedCulling = readbackPtr[1];
    render->readbackBuffer.resource->Unmap(0, nullptr);

    WireContainer* wireContainer = &render->wireContainer[render->frameIndex];
    wireContainer->numVertices = 0;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        Render::Camera* cam = &render->drawingCamera;
        double time = ImGui::GetTime();
        float dt = (float)(time - render->lastTime);
        float moveSpeed = 1.0f;
        float lookSpeed = 0.3f;
        render->lastTime = time;
        
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
            moveSpeed *= 10.0f;

        ImVec2 delta = ImGui::GetMouseDragDelta();
        cam->pitch += delta.y * lookSpeed * dt;
        cam->yaw += delta.x * lookSpeed * dt;
        while (cam->yaw >= PI_2) cam->yaw -= PI_2;
        while (cam->yaw < 0.0f) cam->yaw += PI_2;
        if (cam->pitch < -PI_HALF) cam->pitch = -PI_HALF;
        if (cam->pitch > PI_HALF) cam->pitch = PI_HALF;
        ImGui::ResetMouseDragDelta();

        float4x4 rotMat = make_float4x4_rotation_y(cam->yaw)
            * make_float4x4_rotation_x(cam->pitch);
        float3 forward = float3(rotMat.m13, rotMat.m23, rotMat.m33);
        float3 right = float3(rotMat.m11, rotMat.m21, rotMat.m31);

        bool w_pressed = ImGui::IsKeyDown(ImGuiKey_W);
        bool a_pressed = ImGui::IsKeyDown(ImGuiKey_A);
        bool s_pressed = ImGui::IsKeyDown(ImGuiKey_S);
        bool d_pressed = ImGui::IsKeyDown(ImGuiKey_D);

        float dist = dt * moveSpeed;
        if (w_pressed)
            cam->pos += forward * dist;
        if (a_pressed)
            cam->pos += right * dist;
        if (s_pressed)
            cam->pos -= forward * dist;
        if (d_pressed)
            cam->pos -= right * dist;

		cam->viewMat = make_float4x4_translation(cam->pos)
			* make_float4x4_rotation_y(cam->yaw)
			* make_float4x4_rotation_x(cam->pitch);
    }

        
    if (ImGui::IsKeyDown(ImGuiKey_L))
        render->cullingCamera = render->drawingCamera;

    {
        float4x4 viewProj = render->cullingCamera.viewMat * render->cullingCamera.projMat;
        float4x4 invViewProj;
        invert(viewProj, &invViewProj);
        float4x4 invProj;
        invert(render->cullingCamera.projMat, &invProj);

        render->constantBufferData.CullingCamera.ViewMatrix = render->cullingCamera.viewMat;
        render->constantBufferData.CullingCamera.ViewProjectionMatrix = viewProj;
        render->constantBufferData.CullingCamera.InverseProjectionMatrix = invProj;
        render->constantBufferData.CullingCamera.InverseViewProjectionMatrix = invViewProj;
        ExtractPlanesD3D((plane*)render->constantBufferData.CullingCamera.FrustumPlanes, viewProj, true);
    }

    {
        float4x4 viewProj = render->drawingCamera.viewMat * render->drawingCamera.projMat;
        float4x4 invViewProj;
        invert(viewProj, &invViewProj);
        float4x4 invProj;
        invert(render->drawingCamera.projMat, &invProj);

        render->constantBufferData.DrawingCamera.ViewMatrix = render->drawingCamera.viewMat;
        render->constantBufferData.DrawingCamera.ViewProjectionMatrix = viewProj;
        render->constantBufferData.DrawingCamera.InverseProjectionMatrix = invProj;
        render->constantBufferData.DrawingCamera.InverseViewProjectionMatrix = invViewProj;
        ExtractPlanesD3D((plane*)render->constantBufferData.DrawingCamera.FrustumPlanes, viewProj, true);
    }

    render->constantBufferData.Counts.x = render->numInstances;
    render->constantBufferData.Counts.y = render->maxNumClusters;
    render->constantBufferData.Counts.z = 0;
    render->constantBufferData.Counts.w = 0;
    memcpy(render->cbvDataBegin + sizeof(Constants) * render->frameIndex, &render->constantBufferData, sizeof(render->constantBufferData));


    // Debug visualization
    {
        float4x4 viewProj = render->cullingCamera.viewMat * render->cullingCamera.projMat;
        float4x4 invViewProj;
        invert(viewProj, &invViewProj);
        wireContainer->AddFrustum(invViewProj);

        for (int i = 0; i < render->numInstances; ++i)
        {
            Instance* instance = render->instancesCpu + i;

            wireContainer->AddAABB(instance->Box);
        }
    }

    ID3D12DescriptorHeap* heaps[] = {
        render->uniHeap.get(),
    };
    render->commandList->SetDescriptorHeaps(_countof(heaps), heaps);

    render->commandList->SetComputeRootSignature(render->drawRootSignature.get());
    render->commandList->SetComputeRootConstantBufferView(0, render->constantBuffer.resource->GetGPUVirtualAddress() + sizeof(Constants) * render->frameIndex);

    // Setup buffers and counters for the frame
    render->commandList->SetPipelineState(render->frameSetupPSO.get());
    render->commandList->Dispatch(1, 1, 1);

    {
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(render->visibleInstancesCounter.resource.get()),
            CD3DX12_RESOURCE_BARRIER::UAV(render->visibleClustersCounter.resource.get()),
        };
        render->commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // Cull instances
    render->commandList->SetPipelineState(render->instanceCullingPSO.get());
    render->commandList->Dispatch((render->numInstances + 127) / 128, 1, 1);

    {
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(render->visibleInstances.resource.get()),
            CD3DX12_RESOURCE_BARRIER::UAV(render->visibleInstancesCounter.resource.get()),
        };
        render->commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // Cull clusters
    render->commandList->SetPipelineState(render->clusterCullingPSO.get());
	render->commandList->Dispatch((render->maxNumClusters + 127) / 128, 1, 1);

    {
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(render->visibleClusters.resource.get()),
            CD3DX12_RESOURCE_BARRIER::UAV(render->visibleClustersCounter.resource.get()),
            CD3DX12_RESOURCE_BARRIER::Transition(render->vBuffer.get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET),
        };
        render->commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // Do the draws
    render->commandList->RSSetViewports(1, &render->viewport);
    render->commandList->RSSetScissorRects(1, &render->scissorRect);
    render->commandList->ClearDepthStencilView(render->depthStencilDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    render->commandList->ClearRenderTargetView(render->vBufferRTV, color, 0, nullptr);
    render->commandList->OMSetRenderTargets(1, &render->vBufferRTV, FALSE, &render->depthStencilDSV);
    render->commandList->SetGraphicsRootSignature(render->drawRootSignature.get());
    render->commandList->SetPipelineState(render->drawMeshPSO.get());
    render->commandList->SetGraphicsRootConstantBufferView(0, render->constantBuffer.resource->GetGPUVirtualAddress() + sizeof(Constants) * render->frameIndex);

    render->commandList->ExecuteIndirect(render->commandSignature.get(), 1, render->visibleClustersCounter.resource.get(), 0, nullptr, 0);

    {
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(render->vBuffer.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ),
            CD3DX12_RESOURCE_BARRIER::Transition(render->colorBuffer.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        };
        render->commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // VBuffer to color buffer
    render->commandList->SetPipelineState(render->materialPSO.get());
    render->commandList->Dispatch((render->width + 7) / 8, (render->height + 7) / 8, 1);

    {
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(render->colorBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET),
        };
        render->commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::Begin("Hello, world!", nullptr, windowFlags);
    ImGui::Text("Instances: %d (of %d)", numInstancesPassedCulling, render->numInstances);
    ImGui::Text("Clusters: %d (of %d)", numClustersPassedCulling, render->numClusters);
    ImGui::End();
    ImGui::Render();

    render->commandList->OMSetRenderTargets(1, &render->colorBufferRTV, FALSE, nullptr);
    render->commandList->SetGraphicsRootSignature(render->drawWireRootSignature.get());
    render->commandList->SetPipelineState(render->drawWirePSO.get());
    render->commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    D3D12_VERTEX_BUFFER_VIEW vbView{
        wireContainer->vertexBuffer.resource->GetGPUVirtualAddress(),
        wireContainer->numVertices * sizeof(float3),
        sizeof(float3)
    };
    render->commandList->IASetVertexBuffers(0, 1, &vbView);
    render->commandList->DrawInstanced(wireContainer->numVertices, 1, 0, 0);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), render->commandList.get());

    {
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(render->colorBuffer.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(render->backBuffers[render->frameIndex].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
        };
        render->commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // color buffer to back buffer
    render->commandList->CopyResource(render->backBuffers[render->frameIndex].get(), render->colorBuffer.get());

    render->commandList->CopyBufferRegion(render->readbackBuffer.resource.get(), ((1 + 3) * render->frameIndex + 0) * sizeof(UINT),
        render->visibleInstancesCounter.resource.get(), 0, 1 * sizeof(UINT));
    render->commandList->CopyBufferRegion(render->readbackBuffer.resource.get(), ((1 + 3) * render->frameIndex + 1) * sizeof(UINT),
        render->visibleClustersCounter.resource.get(), 0, 3 * sizeof(UINT));

    {
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(render->backBuffers[render->frameIndex].get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT),
        };
        render->commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    check_hresult(render->commandList->Close());

    ID3D12CommandList* ppCommandLists[] = { render->commandList.get() };
    render->commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    check_hresult(render->swapChain->Present(1, 0));

    const UINT64 currentFenceValue = render->fenceValues[render->frameIndex];
    check_hresult(render->commandQueue->Signal(render->fence.get(), currentFenceValue));

    render->frameIndex = render->swapChain->GetCurrentBackBufferIndex();

    if (render->fence->GetCompletedValue() < render->fenceValues[render->frameIndex])
    {
        // TODO: do this wait on frame begin instead of frame end
        check_hresult(render->fence->SetEventOnCompletion(render->fenceValues[render->frameIndex], render->fenceEvent));
        WaitForSingleObjectEx(render->fenceEvent, INFINITE, FALSE);
    }

    render->fenceValues[render->frameIndex] = currentFenceValue + 1;
}
