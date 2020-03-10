#pragma once

#include "src/dx12/common.h"

namespace capsaicin::dx12
{
class Dx12
{
public:
    Dx12();
    Dx12(const Dx12&) = delete;
    Dx12& operator=(const Dx12&) = delete;

    ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ID3D12CommandAllocator* command_allocator);
    ComPtr<ID3D12CommandAllocator> CreateCommandAllocator();
    ComPtr<ID3D12Fence> CreateFence(UINT64 initial_value = 0);
    ComPtr<ID3D12Resource> CreateUploadBuffer(UINT64 size, void* data = nullptr);
    ComPtr<ID3D12Resource> CreateUAVBuffer(UINT64 size,
                                           D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON);
    ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, UINT descriptor_count);

    ComPtr<IDXGISwapChain3> CreateSwapchain(HWND hwnd, UINT width, UINT height, UINT backbuffer_count);
    ComPtr<ID3D12RootSignature> CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc);
    ComPtr<ID3D12PipelineState> CreatePipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);

    ID3D12Device* device() { return device_.Get(); }
    ID3D12CommandQueue* command_queue() { return command_queue_.Get(); }
    IDXGIFactory4* dxgi_factory() { return dxgi_factory_.Get(); }

private:
    void InitDXGI(D3D_FEATURE_LEVEL feature_level);
    void InitD3D12(D3D_FEATURE_LEVEL feature_level);

    ComPtr<IDXGIFactory4> dxgi_factory_ = nullptr;
    ComPtr<IDXGIAdapter> dxgi_adapter_ = nullptr;
    ComPtr<ID3D12Device> device_ = nullptr;
    ComPtr<ID3D12CommandQueue> command_queue_ = nullptr;
};
}