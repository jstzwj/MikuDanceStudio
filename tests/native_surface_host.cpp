#include "native_surface_host.hpp"
#include <stdexcept>
#include <cstdlib>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
namespace {
struct Host final : NativeSurfaceHost {
    HWND hwnd = nullptr;
    Host() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MDSPortableSurfaceAcceptance";
        RegisterClassW(&wc);
        hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP,
                               0, 0, width, height, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd || IsWindowVisible(hwnd))
            throw std::runtime_error("hidden_window_creation");
        window = {mikudancestudio::rhi::WindowSystem::Win32,
                  reinterpret_cast<std::uintptr_t>(hwnd)};
    }
    ~Host() override {
        if (hwnd)
            DestroyWindow(hwnd);
    }
    bool Resize(unsigned w, unsigned h) override {
        if (!SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
            return false;
        width = w;
        height = h;
        return Pump();
    }
    bool Pump() override {
        MSG msg;
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return IsWindow(hwnd) && !IsWindowVisible(hwnd);
    }
};
}
std::shared_ptr<NativeSurfaceHost> CreateTestSurfaceHost(const std::string&) {
    return std::make_shared<Host>();
}
#elif !defined(__APPLE__)
#include <xcb/xcb.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
namespace {
struct XcbHost final : NativeSurfaceHost {
    xcb_connection_t* connection = nullptr;
    xcb_window_t id = 0;
    XcbHost() {
        connection = xcb_connect(nullptr, nullptr);
        if (!connection || xcb_connection_has_error(connection))
            throw std::runtime_error("xcb_connect");
        auto* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
        id = xcb_generate_id(connection);
        xcb_create_window(connection, XCB_COPY_FROM_PARENT, id, screen->root, 0, 0, width, height,
                          0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
        xcb_map_window(connection, id);
        xcb_flush(connection);
        window = {mikudancestudio::rhi::WindowSystem::Xcb, id};
        display = reinterpret_cast<std::uintptr_t>(connection);
        if (!Pump())
            throw std::runtime_error("xcb_window_creation");
    }
    ~XcbHost() override {
        if (connection) {
            if (id)
                xcb_destroy_window(connection, id);
            xcb_disconnect(connection);
        }
    }
    bool Resize(unsigned w, unsigned h) override {
        const std::uint32_t values[]{w, h};
        xcb_configure_window(connection, id, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                             values);
        xcb_flush(connection);
        width = w;
        height = h;
        return Pump();
    }
    bool Pump() override {
        auto* reply = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, id), nullptr);
        if (!reply)
            return false;
        width = reply->width;
        height = reply->height;
        free(reply);
        while (auto* event = xcb_poll_for_event(connection))
            free(event);
        return !xcb_connection_has_error(connection);
    }
};
struct WaylandHost final : NativeSurfaceHost {
    wl_display* connection = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    xdg_wm_base* shell = nullptr;
    wl_surface* surface = nullptr;
    xdg_surface* role = nullptr;
    xdg_toplevel* top = nullptr;
    bool configured = false, closed = false;
    static void Global(void* data, wl_registry* r, std::uint32_t name, const char* interface,
                       std::uint32_t) {
        auto& h = *static_cast<WaylandHost*>(data);
        if (std::string(interface) == wl_compositor_interface.name)
            h.compositor =
                static_cast<wl_compositor*>(wl_registry_bind(r, name, &wl_compositor_interface, 1));
        if (std::string(interface) == xdg_wm_base_interface.name)
            h.shell =
                static_cast<xdg_wm_base*>(wl_registry_bind(r, name, &xdg_wm_base_interface, 1));
    }
    static void Removed(void*, wl_registry*, std::uint32_t) {}
    static void Ping(void*, xdg_wm_base* s, std::uint32_t serial) { xdg_wm_base_pong(s, serial); }
    static void Configure(void* data, xdg_surface* s, std::uint32_t serial) {
        xdg_surface_ack_configure(s, serial);
        static_cast<WaylandHost*>(data)->configured = true;
    }
    static void Size(void* data, xdg_toplevel*, std::int32_t w, std::int32_t h, wl_array*) {
        auto& host = *static_cast<WaylandHost*>(data);
        if (w > 0 && h > 0) {
            host.width = unsigned(w);
            host.height = unsigned(h);
        }
    }
    static void Close(void* data, xdg_toplevel*) { static_cast<WaylandHost*>(data)->closed = true; }
    WaylandHost() {
        connection = wl_display_connect(nullptr);
        if (!connection)
            throw std::runtime_error("wayland_connect");
        registry = wl_display_get_registry(connection);
        static const wl_registry_listener registryListener{Global, Removed};
        wl_registry_add_listener(registry, &registryListener, this);
        if (wl_display_roundtrip(connection) < 0 || !compositor || !shell)
            throw std::runtime_error("wayland_globals");
        static const xdg_wm_base_listener shellListener{Ping};
        xdg_wm_base_add_listener(shell, &shellListener, this);
        surface = wl_compositor_create_surface(compositor);
        role = xdg_wm_base_get_xdg_surface(shell, surface);
        static const xdg_surface_listener surfaceListener{Configure};
        xdg_surface_add_listener(role, &surfaceListener, this);
        top = xdg_surface_get_toplevel(role);
        static const xdg_toplevel_listener topListener{Size, Close};
        xdg_toplevel_add_listener(top, &topListener, this);
        window = {mikudancestudio::rhi::WindowSystem::Wayland,
                  reinterpret_cast<std::uintptr_t>(surface)};
        display = reinterpret_cast<std::uintptr_t>(connection);
        xdg_toplevel_set_min_size(top, width, height);
        xdg_toplevel_set_max_size(top, width, height);
        wl_surface_commit(surface);
        if (!Pump() || !configured)
            throw std::runtime_error("wayland_initial_configure");
    }
    ~WaylandHost() override {
        if (top)
            xdg_toplevel_destroy(top);
        if (role)
            xdg_surface_destroy(role);
        if (surface)
            wl_surface_destroy(surface);
        if (shell)
            xdg_wm_base_destroy(shell);
        if (compositor)
            wl_compositor_destroy(compositor);
        if (registry)
            wl_registry_destroy(registry);
        if (connection)
            wl_display_disconnect(connection);
    }
    bool Resize(unsigned w, unsigned h) override {
        width = w;
        height = h;
        xdg_toplevel_set_min_size(top, w, h);
        xdg_toplevel_set_max_size(top, w, h);
        wl_surface_commit(surface);
        return Pump();
    }
    bool Pump() override { return !closed && wl_display_roundtrip(connection) >= 0; }
};
}
std::shared_ptr<NativeSurfaceHost> CreateTestSurfaceHost(const std::string& system) {
    if (system == "wayland")
        return std::make_shared<WaylandHost>();
    return std::make_shared<XcbHost>();
}
#endif
