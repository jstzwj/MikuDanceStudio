#pragma once

// Test-only adapter for the verified original executable's public exports.
// x64 still stores a 32-bit active identity, followed immediately by the pass
// and repeat counter. The port retains full pointers in its typed state.
class NativeMmdQueries {
    OriginalPhysicsModule& module;
    std::unique_ptr<MMDAppState> state = std::make_unique<MMDAppState>();
    MMDAppState** global;
    MMDAppState* previous;
    std::unique_ptr<D3DRenderer> renderer = std::make_unique<D3DRenderer>();
    std::array<_locale_t, 4> locales{};

public:
    explicit NativeMmdQueries(OriginalPhysicsModule& image) : module(image) {
        global = module.Function<MMDAppState**>(sizeof(void*) == 8 ? 0x1445F8 : 0x14593C);
        previous = *global;
#if defined(_M_X64)
        const auto create = reinterpret_cast<_locale_t (*)(int, const char*)>(
            GetProcAddress(GetModuleHandleW(L"msvcr90.dll"), "_create_locale"));
#else
        const auto create = module.Function<_locale_t (*)(int, const char*)>(0x10602F);
#endif
        for (auto& locale : locales) {
            locale = create(LC_ALL, "Japanese_Japan.932");
            Check(locale != nullptr, "native query locale");
        }
        *global = state.get();
    }
    ~NativeMmdQueries() {
        *global = previous;
#if defined(_M_X64)
        const auto release = reinterpret_cast<void (*)(_locale_t)>(
            GetProcAddress(GetModuleHandleW(L"msvcr90.dll"), "_free_locale"));
        for (auto locale : locales)
            release(locale);
#endif
        // The x86 static CRT releases its private heap with the mapped module.
    }
    void Sync(MMDApp& app) {
        *state = app.state;
        if (app.Renderer()) {
            *renderer = *app.Renderer();
            std::copy(locales.begin(), locales.end(), renderer->localeTable);
            state->renderer = renderer.get();
        }
#if defined(_M_X64)
        Put(0xA11D8,
            static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(app.ActiveRenderObject())));
        Put(0xA11DC, app.state.activeRenderPass);
        Put(0xA11E0, app.state.renderPassCount);
#endif
    }
    template <class T> void Put(std::size_t offset, const T& value) {
        Check(offset + sizeof value <= sizeof(*state), "native query state bounds");
        std::memcpy(reinterpret_cast<char*>(state.get()) + offset, &value, sizeof value);
    }
    template <class R, class... Args> R Call(const char* name, Args... args) const {
        return module.Export<R(__cdecl*)(Args...)>(name)(args...);
    }
    FARPROC Export(const char* name) const { return module.Export<FARPROC>(name); }
};
