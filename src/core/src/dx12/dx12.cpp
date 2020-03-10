#include "dx12.h"

#include "src/dx12/d3dx12.h"

namespace capsaicin::dx12
{
Dx12::Dx12()
{
    InitDXGI(D3D_FEATURE_LEVEL_12_0);
    InitD3D12(D3D_FEATURE_LEVEL_12_0);
}

void Dx12::InitD3D12(D3D_FEATURE_LEVEL feature_level)
{
    // Create the DX12 API device object.
    ThrowIfFailed(D3D12CreateDevice(dxgi_adapter_.Get(), feature_level, IID_PPV_ARGS(&device_)),
                  "Cannot create D3D12 device");

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)),
                  "Cannot create command queue");
}

ComPtr<ID3D12GraphicsCommandList> Dx12::CreateCommandList(ID3D12CommandAllocator* command_allocator)
{
    ComPtr<ID3D12GraphicsCommandList> command_list;

    ThrowIfFailed(device()->CreateCommandList(
                      0u, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr, IID_PPV_ARGS(&command_list)),
                  "Cannot create command stream");

    return command_list;
}

ComPtr<ID3D12CommandAllocator> Dx12::CreateCommandAllocator()
{
    ComPtr<ID3D12CommandAllocator> command_allocator;
    ThrowIfFailed(device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)),
                  "Cannot create command allocator");

    return command_allocator;
}

ComPtr<ID3D12Fence> Dx12::CreateFence(UINT64 initial_value)
{
    ComPtr<ID3D12Fence> fence;
    ThrowIfFailed(device()->CreateFence(initial_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
                  "Cannot create fence");
    return fence;
}

ComPtr<ID3D12Resource> Dx12::CreateUploadBuffer(UINT64 size, void* data)
{
    ComPtr<ID3D12Resource> resource = nullptr;
    auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
    ThrowIfFailed(device()->CreateCommittedResource(&heap_properties,
                                                    D3D12_HEAP_FLAG_NONE,
                                                    &buffer_desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ,
                                                    nullptr,
                                                    IID_PPV_ARGS(&resource)),
                  "Cannot create upload buffer");

    if (data)
    {
        void* mapped_ptr = nullptr;
        resource->Map(0, nullptr, &mapped_ptr);
        std::memcpy(mapped_ptr, data, size);
        resource->Unmap(0, nullptr);
    }

    return resource;
}

ComPtr<ID3D12Resource> Dx12::CreateUAVBuffer(UINT64 size, D3D12_RESOURCE_STATES initial_state)
{
    ComPtr<ID3D12Resource> resource = nullptr;
    auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(
        device()->CreateCommittedResource(
            &heap_properties, D3D12_HEAP_FLAG_NONE, &buffer_desc, initial_state, nullptr, IID_PPV_ARGS(&resource)),
        "Cannot create UAV");

    return resource;
}

ComPtr<ID3D12DescriptorHeap> Dx12::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, UINT descriptor_count)
{
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    heap_desc.NumDescriptors = descriptor_count;
    heap_desc.Type = heap_type;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device()->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)), "Cannot create descriptor heap");
    return heap;
}

void Dx12::InitDXGI(D3D_FEATURE_LEVEL feature_level)
{
    bool debug_dxgi = false;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the
    // active device.
    {
        ComPtr<ID3D12Debug> debug_controller;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)),
                      "Direct3D debug device is not available");
        debug_controller->EnableDebugLayer();

        ComPtr<IDXGIInfoQueue> dxgi_info_queue;
        ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_info_queue)), "Failed to retrieve debug interface");

        ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgi_factory_)),
                      "Cannot create debug DXGI factory");

        dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
        dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
    }
#else
    ThrowIfFailed(CreateDXGIFactory2(IID_PPV_ARGS(&dxgi_factory_)), "Cannot create DXGI factory");
#endif

    // Now try to find the adapter.
    UINT selected_id = UINT_MAX;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT id = 0; DXGI_ERROR_NOT_FOUND != dxgi_factory_->EnumAdapters1(id, &adapter); ++id)
    {
        DXGI_ADAPTER_DESC1 desc;
        ThrowIfFailed(adapter->GetDesc1(&desc), "Cannot obtain adapter description");

        auto adapter_name = WideStringToString(desc.Description);

        info("Adapter found: {}", adapter_name);

        if (adapter_name.find("Intel") != std::string::npos)
        {
            info("Skipping crappy Intel HW until they fix their crappy DX12 drivers");
            continue;
        }

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            // Don't select the Basic Render Driver adapter.
            continue;
        }

        // Check to see if the adapter supports Direct3D 12, but don't create
        // the actual device yet.
        ComPtr<ID3D12Device> test_device;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), feature_level, IID_PPV_ARGS(&test_device))))
        {
            selected_id = id;
            info("Adapter selected: {}", adapter_name);
            break;
        }
    }

    if (!adapter)
    {
        Throw("No compatible adapters found");
    }

    dxgi_adapter_ = adapter.Detach();
}

ComPtr<IDXGISwapChain3> Dx12::CreateSwapchain(HWND hwnd, UINT width, UINT height, UINT backbuffer_count)
{
    ComPtr<IDXGISwapChain1> swapchain1;

    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
    swapchain_desc.BufferCount = backbuffer_count;
    swapchain_desc.Width = width;
    swapchain_desc.Height = height;
    swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchain_desc.SampleDesc.Count = 1;
    ThrowIfFailed(
        dxgi_factory()->CreateSwapChainForHwnd(command_queue(), hwnd, &swapchain_desc, nullptr, nullptr, &swapchain1),
        "Cannot create swap chain");

    ThrowIfFailed(dxgi_factory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "Cannot make window association");

    ComPtr<IDXGISwapChain3> swapchain;
    swapchain1->QueryInterface(IID_PPV_ARGS(&swapchain));

    return swapchain;
}
ComPtr<ID3D12RootSignature> Dx12::CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc)
{
    ComPtr<ID3DBlob> error;
    ComPtr<ID3DBlob> signature_blob;
    ComPtr<ID3D12RootSignature> signature;
    ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error),
                  "Cannot serialize root signature");
    ThrowIfFailed(device()->CreateRootSignature(
                      0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&signature)),
                  "Cannot create root signature");
    return signature;
}
ComPtr<ID3D12PipelineState> Dx12::CreatePipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
{
    ComPtr<ID3D12PipelineState> pipeline_state;
    ThrowIfFailed(device()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state)),
                  "Cannot create pipeline state");
    return pipeline_state;
}
}  // namespace capsaicin::dx12