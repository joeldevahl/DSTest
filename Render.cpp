#include "Render.h"

#include <dxgi1_6.h>
#include "d3dx12.h"
#include <dstorage.h>
#include <winrt/base.h>
#include <directxmath.h> // for XMFLOAT4x4

using namespace DirectX;
using winrt::com_ptr;
using winrt::check_hresult;

#define NUM_QUEUED_FRAMES 3

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 608; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

enum RenderTargets
{
    BackBuffer0 = 0,
    BackBuffer1,
    BackBuffer2,
    BackBufferLast = BackBuffer2, // needs to be NUM_QUEUED_FRAMES - 1

    VBuffer,

    RenderTargetCount,
};

enum DepthStencilTargets
{
    MainDepthStencil = 0,

    DepthStencilTargetCount,
};

_declspec(align(256u)) struct SceneConstantBuffer
{
    XMFLOAT4X4 MVP;
    UINT Counts[4];
    UINT NumMeshes;
    UINT NumMaterials;
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

    com_ptr<ID3D12Resource> depthStencil;
    CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilDSV;

    SceneConstantBuffer constantBufferData;
    com_ptr<ID3D12Resource> constantBuffer;
    com_ptr<ID3D12Resource> instancesBuffer;
    com_ptr<ID3D12Resource> meshesBuffer;
    com_ptr<ID3D12Resource> clustersBuffer;
    com_ptr<ID3D12Resource> vertexDataBuffer;
    com_ptr<ID3D12Resource> indexDataBuffer;
    com_ptr<ID3D12Resource> materialsBuffer;
    char* cbvDataBegin;

    com_ptr<ID3D12RootSignature> drawRootSignature;
    com_ptr<ID3D12PipelineState> drawMeshPSO;

    com_ptr<ID3D12PipelineState> cullingComputePSO;

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

static void CreateBufferResource(Render* render, com_ptr<ID3D12Resource> &resource, UINT64 size, D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON)
{
	auto heapProps = CD3DX12_HEAP_PROPERTIES(heapType);
	auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
	check_hresult(render->device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		state,
		nullptr,
		IID_PPV_ARGS(resource.put())));
}

static void CreateBufferSRV(Render* render, const com_ptr<ID3D12Resource> &resource, UINT count, UINT stride, D3D12_CPU_DESCRIPTOR_HANDLE handle, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, D3D12_BUFFER_SRV_FLAGS flags = D3D12_BUFFER_SRV_FLAG_NONE)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = format;
	desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.Buffer.FirstElement = 0;
	desc.Buffer.NumElements = count;
	desc.Buffer.StructureByteStride = stride;
	desc.Buffer.Flags = flags;

	render->device->CreateShaderResourceView(resource.get(), &desc, handle);
}

static void CreateBuffer(Render* render, com_ptr<ID3D12Resource> &resource, UINT count, UINT stride, UINT descriptorOffset, bool isRaw = false)
{
    UINT size = stride * count;
	CreateBufferResource(render, resource, size);

	CD3DX12_CPU_DESCRIPTOR_HANDLE handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(render->uniHeap->GetCPUDescriptorHandleForHeapStart(), descriptorOffset, render->uniDescriptorSize);
    DXGI_FORMAT format = isRaw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
    D3D12_BUFFER_SRV_FLAGS flags = isRaw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
	CreateBufferSRV(render, resource, isRaw ? (size / 4) : count, isRaw ? 0 : stride, handle, format, flags);
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
        HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
        if (SUCCEEDED(hr))
        {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
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
        render->storageFactory->SetStagingBufferSize(32 * 1024 * 1024);

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

	render->rtvHeap = CreateDescriptorHeap(render, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RenderTargets::RenderTargetCount);
	render->dsvHeap = CreateDescriptorHeap(render, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DepthStencilTargets::DepthStencilTargetCount);
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
            render->backBufferRTVs[n] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvBaseHandle, RenderTargets::BackBuffer0 + n, render->rtvDescriptorSize);
            render->device->CreateRenderTargetView(render->backBuffers[n].get(), nullptr, render->backBufferRTVs[n]);
        
            check_hresult(render->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(render->commandAllocators[n].put())));
        }

        // VBuffer
        {
            CD3DX12_HEAP_PROPERTIES heapType(D3D12_HEAP_TYPE_DEFAULT);

			D3D12_CLEAR_VALUE vBufferOptimizedClearValue = {};
			vBufferOptimizedClearValue.Format = DXGI_FORMAT_R32G32_UINT;
            vBufferOptimizedClearValue.Color[0] = 0.0f;
            vBufferOptimizedClearValue.Color[1] = 0.0f;
            vBufferOptimizedClearValue.Color[2] = 0.0f;
            vBufferOptimizedClearValue.Color[3] = 0.0f;

            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32_UINT, render->width, render->height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

			check_hresult(render->device->CreateCommittedResource(
				&heapType,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				&vBufferOptimizedClearValue,
				IID_PPV_ARGS(render->vBuffer.put())
			));

            render->vBufferRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvBaseHandle, RenderTargets::VBuffer, render->rtvDescriptorSize);
            render->device->CreateRenderTargetView(render->vBuffer.get(), nullptr, render->vBufferRTV);
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
        render->depthStencilDSV = CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvBaseHandle, DepthStencilTargets::MainDepthStencil, render->dsvDescriptorSize);
        render->device->CreateDepthStencilView(render->depthStencil.get(), &depthStencilDesc, render->depthStencilDSV);
    }

    /*
     * Global Constant Buffer
     */
    {
        const UINT64 constantBufferSize = sizeof(SceneConstantBuffer) * NUM_QUEUED_FRAMES;
        CreateBufferResource(render, render->constantBuffer, constantBufferSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = render->constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = constantBufferSize;

        render->device->CreateConstantBufferView(&cbvDesc, render->uniHeap->GetCPUDescriptorHandleForHeapStart());

        CD3DX12_RANGE readRange(0, 0);
        check_hresult(render->constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&render->cbvDataBegin)));
    }

    /*
     * Open files to query sizes
     */
    com_ptr<IDStorageFile> instancesFile;
    com_ptr<IDStorageFile> meshesFile;
    com_ptr<IDStorageFile> clustersFile;
    com_ptr<IDStorageFile> verticesFile;
    com_ptr<IDStorageFile> indicesFile;
    UINT32 instancesSize = 0;
    UINT32 meshesSize = 0;
    UINT32 clustersSize = 0;
    UINT32 verticesSize = 0;
    UINT32 indicesSize = 0;
	OpenFileForGPU(render, L"instances.raw", instancesFile, instancesSize);
	OpenFileForGPU(render, L"meshes.raw", meshesFile, meshesSize);
	OpenFileForGPU(render, L"clusters.raw", clustersFile, clustersSize);
	OpenFileForGPU(render, L"vertices.raw", verticesFile, verticesSize);
	OpenFileForGPU(render, L"indices.raw", indicesFile, indicesSize);

    /*
     * Mesh and Instance Pool
     */
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE baseHandle(render->uniHeap->GetCPUDescriptorHandleForHeapStart());

		CreateBuffer(render, render->instancesBuffer, instancesSize / sizeof(Instance), sizeof(Instance), 0);
		CreateBuffer(render, render->meshesBuffer, meshesSize / sizeof(Mesh), sizeof(Mesh), 1);
		CreateBuffer(render, render->clustersBuffer, 1024 * 1024, sizeof(Cluster), 2);
		CreateBuffer(render, render->vertexDataBuffer, 128 * 1024, 3 * sizeof(float), 3, true);
		CreateBuffer(render, render->indexDataBuffer, 128 * 1024, sizeof(UINT), 4, true);
    }

    /*
     * Root Signature
     */
    {
        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].InitAsConstantBufferView(0);

        auto desc = CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC(1, rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

        com_ptr<ID3DBlob> rootBlob;
        com_ptr<ID3DBlob> errorBlob;
        check_hresult(D3D12SerializeVersionedRootSignature(&desc, rootBlob.put(), errorBlob.put()));

        check_hresult(render->device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(render->drawRootSignature.put())));
    }

    /*
     * Graphics PSO
     */
    {
        struct
        {
            byte* data;
            uint32_t size;
        } amplificationShader, meshShader, pixelShader;

        ReadDataFromFile(L"x64/Debug/MeshletAS.cso", &amplificationShader.data, &amplificationShader.size);
        ReadDataFromFile(L"x64/Debug/MeshletMS.cso", &meshShader.data, &meshShader.size);
        ReadDataFromFile(L"x64/Debug/MeshletPS.cso", &pixelShader.data, &pixelShader.size);

        D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = render->drawRootSignature.get();
        psoDesc.AS = { amplificationShader.data, amplificationShader.size };
        psoDesc.MS = { meshShader.data, meshShader.size };
        psoDesc.PS = { pixelShader.data, pixelShader.size };
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = render->backBuffers[0]->GetDesc().Format;
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

        free(amplificationShader.data);
        free(meshShader.data);
        free(pixelShader.data);
    }

    /*
     * Compute PSO
     */
    {
        struct
        {
            byte* data;
            uint32_t size;
        } cullingComputeShader;

        ReadDataFromFile(L"x64/Debug/Culling.cso", &cullingComputeShader.data, &cullingComputeShader.size);

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc {};
		desc.pRootSignature = render->drawRootSignature.get();
        desc.CS = { cullingComputeShader.data, cullingComputeShader.size};

        check_hresult(render->device->CreateComputePipelineState(&desc, IID_PPV_ARGS(render->cullingComputePSO.put())));

        free(cullingComputeShader.data);
    }

    check_hresult(render->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, render->commandAllocators[render->frameIndex].get(), nullptr, IID_PPV_ARGS(render->commandList.put())));
    check_hresult(render->commandList->Close());

    /*
     * Load data using DirectStorage
     */
    {
        LoadFileToGPU(render, instancesFile, render->instancesBuffer.get(), instancesSize);
        LoadFileToGPU(render, meshesFile, render->meshesBuffer.get(), meshesSize);
        LoadFileToGPU(render, clustersFile, render->clustersBuffer.get(), clustersSize);
        LoadFileToGPU(render, verticesFile, render->vertexDataBuffer.get(), verticesSize);
        LoadFileToGPU(render, indicesFile, render->indexDataBuffer.get(), indicesSize);

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
    XMStoreFloat4x4(&render->constantBufferData.MVP, XMMatrixTranspose(proj));
    render->constantBufferData.Counts[0] = 230;
    render->constantBufferData.Counts[1] = 130;
    render->constantBufferData.Counts[2] = 0;
    render->constantBufferData.Counts[3] = 0;
    render->constantBufferData.NumMeshes = 256;
    render->constantBufferData.NumMaterials = 1024;
    memcpy(render->cbvDataBegin + sizeof(SceneConstantBuffer) * render->frameIndex, &render->constantBufferData, sizeof(render->constantBufferData));

    render->commandList->RSSetViewports(1, &render->viewport);
    render->commandList->RSSetScissorRects(1, &render->scissorRect);

    CD3DX12_RESOURCE_BARRIER rtBarrier = CD3DX12_RESOURCE_BARRIER::Transition(render->backBuffers[render->frameIndex].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    render->commandList->ResourceBarrier(1, &rtBarrier);

    render->commandList->OMSetRenderTargets(1, &render->backBufferRTVs[render->frameIndex], FALSE, &render->depthStencilDSV);

    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    render->commandList->ClearRenderTargetView(render->backBufferRTVs[render->frameIndex], clearColor, 0, nullptr);
    render->commandList->ClearDepthStencilView(render->depthStencilDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    ID3D12DescriptorHeap* heaps[] = {
        render->uniHeap.get(),
    };
    render->commandList->SetDescriptorHeaps(1, heaps);

    render->commandList->SetComputeRootSignature(render->drawRootSignature.get());
    render->commandList->SetPipelineState(render->cullingComputePSO.get());
    render->commandList->SetComputeRootConstantBufferView(0, render->constantBuffer->GetGPUVirtualAddress() + sizeof(SceneConstantBuffer) * render->frameIndex);
    render->commandList->Dispatch(1, 1, 1);

    render->commandList->SetGraphicsRootSignature(render->drawRootSignature.get());
    render->commandList->SetPipelineState(render->drawMeshPSO.get());
    render->commandList->SetGraphicsRootConstantBufferView(0, render->constantBuffer->GetGPUVirtualAddress() + sizeof(SceneConstantBuffer) * render->frameIndex);
	render->commandList->DispatchMesh(render->constantBufferData.Counts[0], render->constantBufferData.Counts[1], 1);

    auto presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(render->backBuffers[render->frameIndex].get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    render->commandList->ResourceBarrier(1, &presentBarrier);

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
