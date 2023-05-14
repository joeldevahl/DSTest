#include "Render.h"

#include <dxgi1_6.h>
#include <d3dx12.h>
#include <dstorage.h>
#include <winrt/base.h>

using winrt::com_ptr;
using winrt::check_hresult;

#define NUM_QUEUED_FRAMES 3

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 610; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

enum class RenderTargets : int
{
    BackBuffer0 = 0,
    BackBuffer1,
    BackBuffer2,
    BackBufferLast = BackBuffer2, // needs to be NUM_QUEUED_FRAMES - 1

    VBuffer,

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

    com_ptr<ID3D12Resource> depthStencil;
    CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilDSV;

    UINT numInstances;
    UINT maxNumClusters;
    Constants constantBufferData;
    Buffer constantBuffer;
    Buffer instancesBuffer;
    Buffer meshesBuffer;
    Buffer clustersBuffer;
    Buffer vertexDataBuffer;
    Buffer indexDataBuffer;
    Buffer materialsBuffer;
    char* cbvDataBegin;

    Buffer visibleInstances;
    Buffer visibleClusters;
    Buffer visibleInstancesCounter;
    Buffer visibleClustersCounter;

    com_ptr<ID3D12RootSignature> drawRootSignature;
    com_ptr<ID3D12PipelineState> drawMeshPSO;

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

static void OpenFileForGPU(Render* render, LPCWSTR fileName, com_ptr<IDStorageFile>& file, UINT32& size)
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

            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, render->width, render->height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

			check_hresult(render->device->CreateCommittedResource(
				&heapType,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_SOURCE,
				nullptr,
				IID_PPV_ARGS(render->colorBuffer.put())
			));

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
    com_ptr<IDStorageFile> verticesFile;
    com_ptr<IDStorageFile> indicesFile;
    com_ptr<IDStorageFile> materialsFile;
    UINT32 instancesSize = 0;
    UINT32 meshesSize = 0;
    UINT32 clustersSize = 0;
    UINT32 verticesSize = 0;
    UINT32 indicesSize = 0;
    UINT32 materialsSize = 0;
	OpenFileForGPU(render, L"instances.raw", instancesFile, instancesSize);
	OpenFileForGPU(render, L"meshes.raw", meshesFile, meshesSize);
	OpenFileForGPU(render, L"clusters.raw", clustersFile, clustersSize);
	OpenFileForGPU(render, L"vertices.raw", verticesFile, verticesSize);
	OpenFileForGPU(render, L"indices.raw", indicesFile, indicesSize);
	OpenFileForGPU(render, L"materials.raw", materialsFile, materialsSize);
    render->numInstances = instancesSize / sizeof(Instance);
    render->maxNumClusters = MAX_ELEMENTS;

    assert(render->numInstances <= MAX_ELEMENTS);

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
	CreateBuffer(render, &render->vertexDataBuffer,
        BufferDesc(verticesSize / sizeof(Vertex), sizeof(Vertex))
        .WithName(L"VertexDataBuffer")
        .WithSRV(VERTEX_DATA_BUFFER_SRV)
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
        LoadFileToGPU(render, instancesFile, render->instancesBuffer.resource.get(), instancesSize);
        LoadFileToGPU(render, meshesFile, render->meshesBuffer.resource.get(), meshesSize);
        LoadFileToGPU(render, clustersFile, render->clustersBuffer.resource.get(), clustersSize);
        LoadFileToGPU(render, verticesFile, render->vertexDataBuffer.resource.get(), verticesSize);
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
}

void Draw(Render* render)
{
    check_hresult(render->commandAllocators[render->frameIndex]->Reset());
    check_hresult(render->commandList->Reset(render->commandAllocators[render->frameIndex].get(), render->drawMeshPSO.get()));

    XMMATRIX proj = XMMatrixPerspectiveFovRH(XM_PI / 3.0f, (float)render->width / (float)render->height, 1.0f, 1000.0f);
	XMFLOAT3 eye(5.0f, 2.0f, 3.0f);
	XMFLOAT3 at(0.0f, 0.0f, 0.0f);
	XMFLOAT3 up(0.0f, 1.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtRH(XMLoadFloat3(&eye), XMLoadFloat3(&at), XMLoadFloat3(&up));
    XMStoreFloat4x4(&render->constantBufferData.ViewProjectionMatrix, XMMatrixTranspose(XMMatrixMultiply(view, proj)));
    render->constantBufferData.Counts.x = render->numInstances;
    render->constantBufferData.Counts.y = render->maxNumClusters;
    render->constantBufferData.Counts.z = 0;
    render->constantBufferData.Counts.w = 0;
    memcpy(render->cbvDataBegin + sizeof(Constants) * render->frameIndex, &render->constantBufferData, sizeof(render->constantBufferData));

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
            CD3DX12_RESOURCE_BARRIER::Transition(render->colorBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(render->backBuffers[render->frameIndex].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
        };
        render->commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // color buffer to back buffer
    render->commandList->CopyResource(render->backBuffers[render->frameIndex].get(), render->colorBuffer.get());

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
