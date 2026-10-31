#pragma once
#include "mikudancestudio/model_runtime.hpp"
#include "mikudancestudio/scene_runtime.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <sstream>

namespace cpu_scene_fixture {
// Test-only, in-memory PMM carrier. Includes names and generated motion, never
// model geometry or texture bytes. The host resolver supplies the real model.
inline std::string Cp932(const std::string& text) {
    static const auto table = [] {
        std::unordered_map<std::string, std::string> result;
        for (unsigned n = 1; n <= 65535; ++n) {
            if (n >= 256 && !((n >> 8) >= 0x81 && (n >> 8) <= 0x9f) &&
                !((n >> 8) >= 0xe0 && (n >> 8) <= 0xfc))
                continue;
            const char bytes[]{char(n >> 8), char(n)};
            const std::string encoded(bytes + (n < 256), n < 256 ? 1 : 2);
            std::string decoded;
            if (mikudancestudio::text::DecodeCp932(encoded, decoded))
                result.emplace(decoded, encoded);
        }
        return result;
    }();
    std::string result;
    for (std::size_t at = 0; at < text.size();) {
        const auto c = static_cast<unsigned char>(text[at]);
        const unsigned length = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        auto found = table.find(text.substr(at, length));
        if (found == table.end())
            throw std::runtime_error("test carrier name cannot be represented in CP932");
        result += found->second;
        at += length;
    }
    return result;
}
struct Bytes : std::string {
    void u8(unsigned value) { push_back(char(value)); }
    void u32(unsigned value) {
        for (unsigned n = 0; n < 4; ++n)
            u8(value >> (n * 8));
    }
    void f(float value) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        u32(bits);
    }
    void zero(unsigned count) { append(count, '\0'); }
    void fixed(const char* text, unsigned count) {
        append(text);
        zero(count - unsigned(std::strlen(text)));
    }
    void name(const std::string& text) {
        const auto bytes = Cp932(text);
        if (bytes.size() > 255)
            throw std::runtime_error("test carrier name is too long");
        u8(unsigned(bytes.size()));
        append(bytes);
    }
};
inline std::string Session(const mikudancestudio::runtime::ModelResource& resource,
                           std::size_t animatedBone) {
    Bytes b;
    const auto bones = unsigned(resource.BoneNames().size());
    if (animatedBone >= bones)
        throw std::runtime_error("test carrier bone index is out of range");
    b.fixed("Polygon Movie maker 0002", 30);
    b.u32(640);
    b.u32(480);
    b.u32(200);
    b.f(45);
    b.zero(7);
    b.u8(0);
    b.u8(1);
    b.u8(0);
    b.name("CPU model");
    b.name("CPU model");
    b.fixed("model.pmx", 256);
    b.u8(1);
    b.u32(bones);
    for (const auto& name : resource.BoneNames())
        b.name(name);
    b.u32(unsigned(resource.MorphNames().size()));
    for (const auto& name : resource.MorphNames())
        b.name(name);
    b.u32(0);
    b.u32(0); // Default IK enabled; no external-parent selector.
    b.u8(0);
    b.u8(1);
    b.u32(0);
    for (unsigned n = 0; n < 4; ++n)
        b.u32(0);
    b.u8(0);
    b.u8(1);
    b.u32(0);
    b.u32(100);
    const auto bone = [&](unsigned frame, unsigned previous, unsigned next, float y) {
        b.u32(frame);
        b.u32(previous);
        b.u32(next);
        for (unsigned n = 0; n < 8; ++n)
            b.u8(20);
        for (unsigned n = 0; n < 8; ++n)
            b.u8(107);
        b.f(0);
        b.f(y);
        b.f(0);
        b.f(0);
        b.f(0);
        b.f(0);
        b.f(1);
        b.u8(1);
        b.u8(0);
    };
    for (unsigned n = 0; n < bones; ++n)
        bone(0, 0, n == animatedBone ? bones : 0, 0);
    b.u32(2);
    b.u32(bones);
    bone(30, unsigned(animatedBone), bones + 1, 2);
    b.u32(bones + 1);
    bone(60, bones, 0, 0);
    for (std::size_t n = 0; n < resource.MorphNames().size(); ++n) {
        b.zero(12);
        b.f(0);
        b.u8(1);
    }
    b.u32(0); // Sparse morph keys.
    b.zero(12);
    b.u8(1);
    b.u8(1);
    b.u32(0); // Display root visible/allocated flags and sparse count.
    for (unsigned n = 0; n < bones; ++n) {
        for (unsigned c = 0; c < 6; ++c)
            b.f(0);
        b.f(1);
        b.zero(3);
    }
    for (std::size_t n = 0; n < resource.MorphNames().size(); ++n)
        b.f(0);
    b.u8(0);
    b.f(1);
    b.u8(0);
    b.u8(0);
    // Camera, light, no accessories, default configuration and v2 tail.
    b.zero(12);
    b.f(-45);
    for (unsigned n = 0; n < 6; ++n)
        b.f(0);
    b.u32(0xffffffff);
    b.u32(0xffffffff);
    b.zero(24);
    b.u8(1);
    b.u32(45);
    b.u8(0);
    b.u32(0);
    for (unsigned n = 0; n < 9; ++n)
        b.f(n == 5 ? -45.f : 0.f);
    b.u8(1);
    b.zero(12);
    for (float f : {.6f, .6f, .6f, -.5f, -1.f, -.5f})
        b.f(f);
    b.u8(1);
    b.u32(0);
    for (float f : {.6f, .6f, .6f, -.5f, -1.f, -.5f})
        b.f(f);
    b.u8(0);
    b.u32(0);
    b.u8(0);
    b.zero(28);
    b.zero(257);
    b.zero(272);
    b.zero(269);
    b.u8(0);
    b.u8(0);
    b.u8(0);
    b.f(60);
    b.u32(0);
    b.u32(0);
    b.f(.5f);
    b.u8(0);
    b.u8(2);
    b.f(9.8f);
    b.u32(10);
    b.f(0);
    b.f(-1);
    b.f(0);
    b.u8(0);
    b.zero(12);
    b.u8(0);
    b.u32(10);
    b.f(9.8f);
    b.f(0);
    b.f(-1);
    b.f(0);
    b.u8(1);
    b.u32(0);
    b.u8(0);
    b.u32(0);
    b.zero(18);
    b.u32(0);
    b.zero(92);
    b.u8(1);
    b.u8(0);
    b.u32(5);
    return b;
}
inline bool RoundTrip(const mikudancestudio::format::ModelDocument& model,
                      const mikudancestudio::runtime::ModelResource& resource,
                      std::size_t animatedBone, std::string& reason) {
    using namespace mikudancestudio;
    const auto read = [](void* p, void* out, std::size_t count) {
        auto& in = *static_cast<std::istream*>(p);
        in.read(static_cast<char*>(out), count);
        return int(in.gcount());
    };
    const auto resolve = [](void* p, std::uint8_t, const std::array<char, 256>&,
                            format::ModelDocument& out) {
        out = *static_cast<const format::ModelDocument*>(p);
        return true;
    };
    const auto write = [](void* p, const void* data, std::size_t count) {
        auto& out = *static_cast<std::ostream*>(p);
        out.write(static_cast<const char*>(data), count);
        return bool(out);
    };
    std::istringstream input(Session(resource, animatedBone), std::ios::binary);
    format::SceneSession session;
    if (!format::SceneSession::Load(read, &input, resolve,
                                    const_cast<format::ModelDocument*>(&model), session)) {
        reason = "real-carrier-session-read-failed";
        return false;
    }
    runtime::SceneRuntime before;
    if (!before.Load(session, {{0, model}}, reason))
        return false;
    const auto run = [&](runtime::SceneRuntime& scene) {
        for (unsigned frame : {30u, 10u, 0u})
            if (!scene.Seek(frame, reason) ||
                (frame == 30 && scene.Snapshot(0)->bones[animatedBone].translation[1] != 2.f))
                return false;
        if (!scene.StartPlayback(reason))
            return false;
        for (unsigned n = 1; n <= 30; ++n)
            if (!scene.Advance(float(n) / 30, 1.f / 30, reason))
                return false;
        scene.Pause();
        return true;
    };
    if (!run(before))
        return false;
    std::ostringstream saved(std::ios::binary);
    if (!before.Session()->Save(write, &saved))
        return false;
    format::SceneSession sessionAfter;
    std::istringstream reloaded(saved.str(), std::ios::binary);
    runtime::SceneRuntime after;
    if (!format::SceneSession::Load(read, &reloaded, resolve,
                                    const_cast<format::ModelDocument*>(&model), sessionAfter) ||
        !after.Load(sessionAfter, {{0, model}}, reason) || !run(after))
        return false;
    const auto* a = before.Snapshot(0);
    const auto* b = after.Snapshot(0);
    if (before.Bodies(0) != after.Bodies(0) || a->vertices.size() != b->vertices.size())
        return false;
    for (std::size_t n = 0; n < a->vertices.size(); ++n)
        if (a->vertices[n].position != b->vertices[n].position ||
            a->vertices[n].normal != b->vertices[n].normal ||
            a->vertices[n].uv != b->vertices[n].uv)
            return false;
    return before.Reset(reason) && after.Reset(reason);
}
} // namespace cpu_scene_fixture
