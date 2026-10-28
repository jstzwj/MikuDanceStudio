#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "mikudancestudio/runtime_cpu_scene_producer.hpp"
#include "mikudancestudio/rhi_d3d11.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "self_shadow_dx9_reference.hpp"
#include <dbghelp.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
using namespace mikudancestudio::render;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " + #x + \
                                     " " + reason);                                                \
    } while (0)
namespace {
struct Difference {
    std::size_t elements = 0;
    double maximum = 0;
};
Difference Compare(const AttachmentReadback& a, const AttachmentReadback& b) {
    if (a.bytes.size() != b.bytes.size())
        throw std::runtime_error("readback size mismatch");
    Difference d;
    if (a.description.format == TextureFormat::R32Float) {
        for (std::size_t i = 0; i < a.bytes.size(); i += 4) {
            float x, y;
            std::memcpy(&x, a.bytes.data() + i, 4);
            std::memcpy(&y, b.bytes.data() + i, 4);
            if (!std::isfinite(x) || !std::isfinite(y))
                throw std::runtime_error("nonfinite shadow output");
            if (std::memcmp(&x, &y, 4))
                ++d.elements;
            d.maximum = std::max(d.maximum, double(std::fabs(x - y)));
        }
    } else
        for (std::size_t i = 0; i < a.bytes.size(); ++i) {
            const auto delta = std::abs(int(a.bytes[i]) - int(b.bytes[i]));
            d.elements += delta != 0;
            d.maximum = std::max(d.maximum, double(delta));
        }
    return d;
}
void Save(const std::filesystem::path& path, const AttachmentReadback& image) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << image.description.width << " " << image.description.height << "\n255\n";
    for (std::size_t i = 0; i < image.bytes.size(); i += 4) {
        if (image.description.format == TextureFormat::R32Float) {
            float depth;
            std::memcpy(&depth, image.bytes.data() + i, 4);
            const char byte = char(std::clamp(depth, 0.f, 1.f) * 255);
            out.write(&byte, 1);
            out.write(&byte, 1);
            out.write(&byte, 1);
        } else
            out.write(reinterpret_cast<const char*>(image.bytes.data() + i), 3);
    }
}
std::size_t Coverage(const AttachmentReadback& image) {
    std::size_t covered = 0;
    for (std::size_t i = 0; i < image.bytes.size(); i += 4) {
        if (image.description.format == TextureFormat::R32Float) {
            float v;
            std::memcpy(&v, image.bytes.data() + i, 4);
            if (v < 1)
                ++covered;
        } else if (image.bytes[i] != 255 || image.bytes[i + 1] != 255 || image.bytes[i + 2] != 255)
            ++covered;
    }
    return covered;
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exception) -> LONG {
        std::cerr << "exception=" << std::hex << exception->ExceptionRecord->ExceptionCode
                  << " address=" << exception->ExceptionRecord->ExceptionAddress << "\n";
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        void* stack[40]{};
        const auto count = CaptureStackBackTrace(0, 40, stack, nullptr);
        for (unsigned i = 0; i < count; ++i) {
            alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 1024]{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 1023;
            DWORD64 displacement = 0;
            if (SymFromAddr(GetCurrentProcess(), DWORD64(stack[i]), &displacement, symbol))
                std::cerr << symbol->Name << "+" << displacement << "\n";
        }
        return EXCEPTION_EXECUTE_HANDLER;
    });
    if (argc < 5)
        return 2;
    std::string reason;
    const auto model = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    const auto assetsPath = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    const std::string variant = argv[3];
    const bool strict = std::string(argv[4]) == "strict";
    const auto evidence = argc > 5 ? std::filesystem::u8path(argv[5]) : std::filesystem::path{};
    if (!evidence.empty())
        std::filesystem::create_directories(evidence);
    std::ofstream report;
    if (!evidence.empty()) {
        report.open(evidence / "comparison.csv");
        report << "mode,width,color_channels,max_byte,shadow_texels,max_float\n";
    }
    FileAssetSource assets(assetsPath);
    ToonTextureResolver textures(model.parent_path(), assets);
    std::cerr << "CPU import\n";
    RuntimeCpuScene scene;
    CHECK(scene.LoadModel(model, reason));
    std::cerr << "CPU materials\n";
    for (auto slot : scene.Order())
        CHECK(scene.Model(slot)->PrepareMaterials(textures, reason));
    format::SceneResourceResolver resolver(model);
    RuntimeCpuSceneProducer producer;
    CHECK(producer.Load(scene, resolver, true, assets, reason));
    const bool dx12 = variant.find("dx12") != std::string::npos;
    const bool warp = variant.find("warp") != std::string::npos;
    auto eleven =
        dx12
            ? D3D11Bundle{}
            : CreateD3D11OffscreenDevice(128, 96, warp ? D3D11Driver::Warp : D3D11Driver::Hardware);
    auto twelve = dx12 ? CreateD3D12OffscreenDevice(
                             128, 96, warp ? D3D12Driver::Warp : D3D12Driver::Hardware, {{}, true})
                       : D3D12Bundle{};
    CHECK(dx12 ? bool(twelve) : bool(eleven));
    SceneRenderer renderer(dx12 ? *twelve.device : *eleven.device);
    shadow_reference::Reference reference(assetsPath / "skin_effect_sm3.fx");
    bool exact = true;
    for (int mode : {1, 2})
        for (unsigned width : {128u, 160u, 128u}) {
            HistoryDesc target;
            target.width = width;
            target.height = 96;
            ModelFrameInputs inputs;
            inputs.view.viewport = {0, 0, width, 96, 0, 1};
            inputs.view.aspect = float(width) / 96;
            SelfShadowSettings settings;
            settings.mode = mode;
            settings.mapSize = 128;
            RenderFrame frame;
            CHECK(producer.BuildFrame(scene, inputs, target, settings, frame, reason));
            CHECK(frame.passes.size() == 2 && !frame.passes[0].draws.empty() &&
                  !frame.passes[1].draws.empty());
            CHECK(ValidateRenderFrame(frame));
            // Main and caster packets must share immutable geometry IDs/versions.
            for (const auto& cast : frame.passes[0].draws)
                CHECK(std::any_of(frame.passes[1].draws.begin(), frame.passes[1].draws.end(),
                                  [&](const auto& main) {
                                      return cast.vertices.id == main.vertices.id &&
                                             cast.vertices.version == main.vertices.version;
                                  }));
            PreparedModelFrame prepared;
            CompletionToken token;
            CHECK(renderer.Prepare(1, frame, prepared));
            CHECK(renderer.Submit(prepared, token));
            AttachmentReadback color, depth;
            CHECK(renderer.ReadTarget(1, 1, color) && renderer.ReadTarget(1, 3, depth));
            CHECK(Coverage(color) > 50 && Coverage(depth) > 5);
            CHECK(renderer.Submit(prepared, token));
            AttachmentReadback repeated;
            CHECK(renderer.ReadTarget(1, 1, repeated) && repeated.bytes == color.bytes);
            reference.Render(frame);
            const auto nativeColor = reference.Read(1), nativeDepth = reference.Read(3);
            const auto c = Compare(color, nativeColor), d = Compare(depth, nativeDepth);
            std::cout << "mode=" << mode << " size=" << width << " color_channels=" << c.elements
                      << " max_byte=" << c.maximum << " shadow_texels=" << d.elements
                      << " max_float=" << d.maximum << " coverage=" << Coverage(color) << "/"
                      << Coverage(depth) << "\n";
            exact &= !c.elements && !d.elements;
            if (report)
                report << mode << "," << width << "," << c.elements << "," << c.maximum << ","
                       << d.elements << "," << d.maximum << "\n";
            if (!evidence.empty()) {
                const auto stem = std::to_string(mode) + "-" + std::to_string(width);
                Save(evidence / (stem + "-rhi.ppm"), color);
                Save(evidence / (stem + "-dx9.ppm"), nativeColor);
                Save(evidence / (stem + "-shadow-rhi.ppm"), depth);
                Save(evidence / (stem + "-shadow-dx9.ppm"), nativeDepth);
                const std::pair<const char*,const AttachmentReadback*> shadows[] = {
                    {"-shadow-rhi.r32",&depth},{"-shadow-dx9.r32",&nativeDepth}};
                for (const auto& pair : shadows) {
                    std::ofstream raw(evidence / (stem + pair.first), std::ios::binary);
                    raw.write(reinterpret_cast<const char*>(pair.second->bytes.data()),
                              pair.second->bytes.size());
                }
            }
            auto bad = settings;
            bad.mode = 3;
            RenderFrame unchanged = frame;
            CHECK(!producer.BuildFrame(scene, inputs, target, bad, unchanged, reason));
            CHECK(ValidateRenderFrame(unchanged));
        }
    CHECK(renderer.Unload(1) && renderer.Detach());
    if (dx12)
        CHECK(QueryD3D12Stats(*twelve.device).debugErrors == 0);
    if (strict && !exact) {
        std::cerr << "FAIL strict DX9 comparison: no tolerance was applied\n";
        return 1;
    }
    std::cout << (exact ? "PASS exact DX9 images"
                        : "MEASURED DX9 differences; parity gate remains open")
              << "\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
}
