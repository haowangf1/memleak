#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

using McnpReadLikeEntryFn = void (*)();
using McnpGetLoggerPtrFn = void* (*)();
using McnpLogInfoFn = void (*)(const char*);
using McnpShutdownFn = void (*)();
using NextmeshModelCtorLikeFn = void (*)(void*);
using NextmeshLogInfoFn = void (*)(void*, const char*);
using NextmeshShutdownFn = void (*)();

namespace
{
using LibraryHandle =
#ifdef _WIN32
    HMODULE;
#else
    void*;
#endif

const char* McnpLibraryName()
{
#ifdef _WIN32
    return "mcnp.dll";
#else
    return "./libmcnp.so";
#endif
}

const char* NextmeshLibraryName()
{
#ifdef _WIN32
    return "nextmesh.dll";
#else
    return "./libnextmesh.so";
#endif
}

LibraryHandle LoadRequiredLibrary(const char* path)
{
#ifdef _WIN32
    auto handle = LoadLibraryA(path);
#else
    auto handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    if (handle == nullptr)
    {
#ifdef _WIN32
        throw std::runtime_error(std::string("LoadLibrary failed: ") + path);
#else
        const char* error = dlerror();
        throw std::runtime_error(std::string("dlopen failed: ") + path + " : " + (error ? error : "unknown"));
#endif
    }
    return handle;
}

void CloseLibrary(LibraryHandle handle)
{
    if (handle == nullptr)
    {
        return;
    }
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}

void* LoadRequiredSymbol(LibraryHandle module, const char* name)
{
#ifdef _WIN32
    auto* symbol = reinterpret_cast<void*>(GetProcAddress(module, name));
#else
    dlerror();
    auto* symbol = dlsym(module, name);
#endif
    if (symbol == nullptr)
    {
#ifdef _WIN32
        throw std::runtime_error(std::string("GetProcAddress failed: ") + name);
#else
        const char* error = dlerror();
        throw std::runtime_error(std::string("dlsym failed: ") + name + " : " + (error ? error : "unknown"));
#endif
    }
    return symbol;
}
} // namespace

namespace
{
class TestCaseLike
{
public:
    TestCaseLike()
    {
        mcnp_ = LoadRequiredLibrary(McnpLibraryName());
        nextmesh_ = LoadRequiredLibrary(NextmeshLibraryName());

        mcnp_read_like_entry_ = reinterpret_cast<McnpReadLikeEntryFn>(LoadRequiredSymbol(mcnp_, "mcnp_read_like_entry"));
        mcnp_get_logger_ptr_ = reinterpret_cast<McnpGetLoggerPtrFn>(LoadRequiredSymbol(mcnp_, "mcnp_get_logger_ptr"));
        mcnp_log_info_ = reinterpret_cast<McnpLogInfoFn>(LoadRequiredSymbol(mcnp_, "mcnp_log_info"));
        mcnp_shutdown_ = reinterpret_cast<McnpShutdownFn>(LoadRequiredSymbol(mcnp_, "mcnp_shutdown"));
        nextmesh_model_ctor_like_ = reinterpret_cast<NextmeshModelCtorLikeFn>(LoadRequiredSymbol(nextmesh_, "nextmesh_model_ctor_like"));
        nextmesh_log_info_ = reinterpret_cast<NextmeshLogInfoFn>(LoadRequiredSymbol(nextmesh_, "nextmesh_log_info"));
        nextmesh_shutdown_ = reinterpret_cast<NextmeshShutdownFn>(LoadRequiredSymbol(nextmesh_, "nextmesh_shutdown"));
    }

    ~TestCaseLike()
    {
        std::cout << "[exe] TestCaseLike destructor begin\n";
        std::cout << "[exe] closing nextmesh without explicit shutdown\n";
        CloseLibrary(nextmesh_);
        nextmesh_ = nullptr;

        std::cout << "[exe] closing mcnp without explicit shutdown\n";
        CloseLibrary(mcnp_);
        mcnp_ = nullptr;
        std::cout << "[exe] TestCaseLike destructor end\n";
    }

    void Run()
    {
        mcnp_read_like_entry_();
        mcnp_log_info_("before nextmesh set_pattern");

        void* logger = mcnp_get_logger_ptr_();
        nextmesh_model_ctor_like_(logger);
        nextmesh_log_info_(logger, "after nextmesh set_pattern");
    }

private:
    LibraryHandle mcnp_{nullptr};
    LibraryHandle nextmesh_{nullptr};
    McnpReadLikeEntryFn mcnp_read_like_entry_{nullptr};
    McnpGetLoggerPtrFn mcnp_get_logger_ptr_{nullptr};
    McnpLogInfoFn mcnp_log_info_{nullptr};
    McnpShutdownFn mcnp_shutdown_{nullptr};
    NextmeshModelCtorLikeFn nextmesh_model_ctor_like_{nullptr};
    NextmeshLogInfoFn nextmesh_log_info_{nullptr};
    NextmeshShutdownFn nextmesh_shutdown_{nullptr};
};
} // namespace

int main()
{
    try
    {
        TestCaseLike test_case;
        test_case.Run();
        std::cout << "done\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
