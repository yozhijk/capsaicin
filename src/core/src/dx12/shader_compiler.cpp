#include "shader_compiler.h"

namespace capsaicin::dx12
{
ShaderCompiler& ShaderCompiler::instance()
{
    static ShaderCompiler inst;
    return inst;
}

ShaderCompiler::ShaderCompiler()
{
    hdll_ = LoadLibraryA("dxcompiler.dll");

    if (!hdll_)
    {
        throw std::runtime_error("Cannot load dxcompiler.dll");
    }

    auto create_instance_fn = (DxcCreateInstanceProc)GetProcAddress(hdll_, "DxcCreateInstance");

    auto result = create_instance_fn(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_));
    if (FAILED(result))
    {
        throw std::runtime_error("Cannot create DXC instance");
    }

    result = create_instance_fn(CLSID_DxcLibrary, IID_PPV_ARGS(&library_));
    if (FAILED(result))
    {
        throw std::runtime_error("Cannot create DXC library instance");
    }

    result = library_->CreateIncludeHandler(&include_handler_);
    if (FAILED(result))
    {
        throw std::runtime_error("Cannot create DXC include handler");
    }
}

ShaderCompiler::~ShaderCompiler()
{
    // library_.Reset();
    // compiler_.Reset();
    // FreeLibrary(hdll_);
}

Shader ShaderCompiler::CompileFromFile(const std::string& file_name,
                                       const std::string& shader_model,
                                       const std::string& entry_point)
{
    std::vector<std::string> defs;
    return CompileFromFile(file_name, shader_model, entry_point, defs);
}

Shader ShaderCompiler::CompileFromFile(const std::string& file_name,
                                       const std::string& shader_model,
                                       const std::string& entry_point,
                                       const std::vector<std::string>& defines)
{
    ComPtr<IDxcBlobEncoding> source = nullptr;

    auto result = library_->CreateBlobFromFile(StringToWideString(file_name).c_str(), nullptr, &source);

    if (FAILED(result))
    {
        throw std::runtime_error("Shader source not found: " + file_name);
    }

    std::vector<std::wstring> wdefines;
    wdefines.reserve(defines.size());

    std::vector<DxcDefine> temp;
    for (auto& d : defines)
    {
        wdefines.push_back(StringToWideString(d));
        DxcDefine dxcdef = {};
        dxcdef.Name = wdefines.back().c_str();
        dxcdef.Value = L"1";
        temp.push_back(dxcdef);
    }

    ComPtr<IDxcOperationResult> compiler_output = nullptr;

    result = compiler_->Compile(source.Get(),
                                StringToWideString(file_name).c_str(),
                                StringToWideString(entry_point).c_str(),
                                StringToWideString(shader_model).c_str(),
                                nullptr,
                                0u,
                                temp.size() ? temp.data() : nullptr,
                                (UINT32)temp.size(),
                                include_handler_,
                                &compiler_output);

    if (FAILED(result))
    {
        throw std::runtime_error("Shader compiler failure: " + file_name);
    }

    if (FAILED(compiler_output->GetStatus(&result)) || FAILED(result))
    {
        ComPtr<IDxcBlobEncoding> error;
        compiler_output->GetErrorBuffer(&error);
        std::string error_string(static_cast<char const*>(error->GetBufferPointer()));
        throw std::runtime_error(error_string);
    }

    IDxcBlob* blob = nullptr;
    compiler_output->GetResult(&blob);

    return Shader{blob};
}

Shader ShaderCompiler::CompileFromString(const std::string& source_string,
                                         const std::string& shader_model,
                                         const std::string& entry_point)
{
    std::vector<std::string> defs;
    return CompileFromString(source_string, shader_model, entry_point, defs);
}

Shader ShaderCompiler::CompileFromString(const std::string& source_string,
                                         const std::string& shader_model,
                                         const std::string& entry_point,
                                         const std::vector<std::string>& defines)
{
    ComPtr<IDxcBlobEncoding> source = nullptr;

    auto result =
        library_->CreateBlobWithEncodingFromPinned(source_string.c_str(), (UINT)source_string.size(), 0, &source);

    if (FAILED(result))
    {
        throw std::runtime_error("Cannot create blob from memory");
    }

    std::vector<std::wstring> wdefines;
    wdefines.reserve(defines.size());

    std::vector<DxcDefine> temp;
    for (auto& d : defines)
    {
        wdefines.push_back(StringToWideString(d));
        DxcDefine dxcdef = {};
        dxcdef.Name = wdefines.back().c_str();
        dxcdef.Value = L"1";
        temp.push_back(dxcdef);
    }

    ComPtr<IDxcOperationResult> compiler_output = nullptr;

    result = compiler_->Compile(source.Get(),
                                L"",
                                StringToWideString(entry_point).c_str(),
                                StringToWideString(shader_model).c_str(),
                                nullptr,
                                0u,
                                temp.size() ? temp.data() : nullptr,
                                (UINT32)temp.size(),
                                include_handler_,
                                &compiler_output);

    if (FAILED(result))
    {
        throw std::runtime_error("Shader compiler failure");
    }

    if (FAILED(compiler_output->GetStatus(&result)) || FAILED(result))
    {
        ComPtr<IDxcBlobEncoding> error;
        compiler_output->GetErrorBuffer(&error);
        std::string error_string(static_cast<char const*>(error->GetBufferPointer()));
        throw std::runtime_error(error_string);
    }

    IDxcBlob* blob = nullptr;
    compiler_output->GetResult(&blob);

    return Shader{blob};
}
}  // namespace capsaicin::dx12