#include "../include/platform.h"

// When building this shim into a native module we intentionally provide local
// definitions of the C API entry points that forward to the real runtime via
// GetProcAddress/dlsym. We need to prevent the header from using __declspec(dllimport)
// so we can define these functions ourselves.

// Tell the header we're building Neutron (even though we're not) to avoid dllimport
#define BUILDING_NEUTRON
#include <core/neutron.h>
#undef BUILDING_NEUTRON

// Now redefine NEUTRON_API as empty for our function definitions
#undef NEUTRON_API
#define NEUTRON_API

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <mutex>

// Internal helpers and symbol resolution live in C++ linkage
static std::once_flag resolver_flag;

#ifdef _WIN32
static HMODULE neutron_module_handle = NULL;
static FARPROC resolve_symbol_win(const char* name) {
    if (!neutron_module_handle) {
        neutron_module_handle = GetModuleHandleA("neutron_shared.dll");
        if (!neutron_module_handle) {
            neutron_module_handle = LoadLibraryA("neutron_shared.dll");
        }
    }
    if (!neutron_module_handle) return NULL;
    return GetProcAddress(neutron_module_handle, name);
}
#else
static void* resolve_symbol_posix(const char* name) {
    void* sym = dlsym(RTLD_DEFAULT, name);
    if (!sym) {
        void* h = dlopen("libneutron_shared.so", RTLD_LAZY | RTLD_NOLOAD);
        if (h) sym = dlsym(h, name);
        if (!sym) {
            h = dlopen("neutron_shared.so", RTLD_LAZY | RTLD_NOLOAD);
            if (h) sym = dlsym(h, name);
        }
    }
    return sym;
}
#endif

// Helper template for resolving and casting to the right function pointer
namespace {
    template<typename T>
    T resolver(const char* name) {
#ifdef _WIN32
        return reinterpret_cast<T>(resolve_symbol_win(name));
#else
        return reinterpret_cast<T>(resolve_symbol_posix(name));
#endif
    }
}

extern "C" {

NeutronType neutron_get_type(NeutronValue* value) {
    typedef NeutronType (*fn_t)(NeutronValue*);
    fn_t f = resolver<fn_t>("neutron_get_type");
    return f ? f(value) : NEUTRON_NIL;
}

NEUTRON_API bool neutron_is_nil(NeutronValue* value) {
    typedef bool (*fn_t)(NeutronValue*);
    fn_t f = resolver<fn_t>("neutron_is_nil");
    return f ? f(value) : false;
}

NEUTRON_API bool neutron_is_boolean(NeutronValue* value) {
    typedef bool (*fn_t)(NeutronValue*);
    fn_t f = resolver<fn_t>("neutron_is_boolean");
    return f ? f(value) : false;
}

NEUTRON_API bool neutron_is_number(NeutronValue* value) {
    typedef bool (*fn_t)(NeutronValue*);
    fn_t f = resolver<fn_t>("neutron_is_number");
    return f ? f(value) : false;
}

NEUTRON_API bool neutron_is_string(NeutronValue* value) {
    typedef bool (*fn_t)(NeutronValue*);
    fn_t f = resolver<fn_t>("neutron_is_string");
    return f ? f(value) : false;
}

NEUTRON_API bool neutron_get_boolean(NeutronValue* value) {
    typedef bool (*fn_t)(NeutronValue*);
    fn_t f = resolver<fn_t>("neutron_get_boolean");
    return f ? f(value) : false;
}

NEUTRON_API double neutron_get_number(NeutronValue* value) {
    typedef double (*fn_t)(NeutronValue*);
    fn_t f = resolver<fn_t>("neutron_get_number");
    return f ? f(value) : 0.0;
}

NEUTRON_API const char* neutron_get_string(NeutronValue* value, size_t* length) {
    typedef const char* (*fn_t)(NeutronValue*, size_t*);
    fn_t f = resolver<fn_t>("neutron_get_string");
    return f ? f(value, length) : NULL;
}

NEUTRON_API NeutronValue* neutron_new_nil() {
    typedef NeutronValue* (*fn_t)();
    fn_t f = resolver<fn_t>("neutron_new_nil");
    return f ? f() : NULL;
}

NEUTRON_API NeutronValue* neutron_new_boolean(bool value) {
    typedef NeutronValue* (*fn_t)(bool);
    fn_t f = resolver<fn_t>("neutron_new_boolean");
    return f ? f(value) : NULL;
}

NEUTRON_API NeutronValue* neutron_new_number(double value) {
    typedef NeutronValue* (*fn_t)(double);
    fn_t f = resolver<fn_t>("neutron_new_number");
    return f ? f(value) : NULL;
}

NEUTRON_API NeutronValue* neutron_new_string(NeutronVM* vm, const char* chars, size_t length) {
    typedef NeutronValue* (*fn_t)(NeutronVM*, const char*, size_t);
    fn_t f = resolver<fn_t>("neutron_new_string");
    return f ? f(vm, chars, length) : NULL;
}

NEUTRON_API void neutron_define_native(NeutronVM* vm, const char* name, NeutronNativeFn function, int arity) {
    typedef void (*fn_t)(NeutronVM*, const char*, NeutronNativeFn, int);
    fn_t f = resolver<fn_t>("neutron_define_native");
    if (f) f(vm, name, function, arity);
}

} // extern "C"
