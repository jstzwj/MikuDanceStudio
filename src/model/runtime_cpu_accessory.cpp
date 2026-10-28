#include "../render/legacy_model_cpu_math.hpp"
#include "mikudancestudio/runtime_cpu_accessory.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <tuple>

// Minimal D3DX file-reader ABI. It only parses file data, unlike ID3DXMesh.
// Kept private to this Windows import boundary, never exposed by the RHI.
struct ID3DXFileEnumObject;
struct ID3DXFileData : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetEnum(ID3DXFileEnumObject**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetName(char*, SIZE_T*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetId(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Lock(SIZE_T*, const void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetType(GUID*) = 0;
    virtual BOOL STDMETHODCALLTYPE IsReference() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChildren(SIZE_T*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChild(SIZE_T, ID3DXFileData**) = 0;
};
struct ID3DXFileEnumObject : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetFile(ID3DXFile**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChildren(SIZE_T*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChild(SIZE_T, ID3DXFileData**) = 0;
};
struct ID3DXFile : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateEnumObject(const void*, DWORD,
                                                       ID3DXFileEnumObject**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSaveObject(const void*, DWORD, DWORD, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterTemplates(const void*, SIZE_T) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterEnumTemplates(ID3DXFileEnumObject*) = 0;
};

namespace mikudancestudio {
namespace {
using namespace render;
using Microsoft::WRL::ComPtr;
constexpr unsigned Limit = 1000000;
const GUID Mesh = {0x3D82AB44, 0x62DA, 0x11cf, {0xAB, 0x39, 0, 0x20, 0xAF, 0x71, 0xE4, 0x33}};
const GUID Material = {0x3D82AB4D, 0x62DA, 0x11cf, {0xAB, 0x39, 0, 0x20, 0xAF, 0x71, 0xE4, 0x33}};
const GUID Header = {0x3D82AB43, 0x62DA, 0x11cf, {0xAB, 0x39, 0, 0x20, 0xAF, 0x71, 0xE4, 0x33}};
const GUID Normals = {0xF6F23F43, 0x7686, 0x11cf, {0x8F, 0x52, 0, 0x40, 0x33, 0x35, 0x94, 0xA3}};
const GUID Uvs = {0xF6F23F40, 0x7686, 0x11cf, {0x8F, 0x52, 0, 0x40, 0x33, 0x35, 0x94, 0xA3}};
const GUID Materials = {0xF6F23F42, 0x7686, 0x11cf, {0x8F, 0x52, 0, 0x40, 0x33, 0x35, 0x94, 0xA3}};
const GUID Texture = {0xA42790E1, 0x7810, 0x11cf, {0x8F, 0x52, 0, 0x40, 0x33, 0x35, 0x94, 0xA3}};
// Original schema declarations; standard public X-file template identifiers.
const char Templates[] = R"(xof 0303txt 0032
template Vector { <3D82AB5E-62DA-11CF-AB39-0020AF71E433> FLOAT x; FLOAT y; FLOAT z; }
template Coords2d { <F6F23F44-7686-11CF-8F52-0040333594A3> FLOAT u; FLOAT v; }
template ColorRGBA { <35FF44E0-6C7C-11CF-8F52-0040333594A3> FLOAT r; FLOAT g; FLOAT b; FLOAT a; }
template ColorRGB { <D3E16E81-7835-11CF-8F52-0040333594A3> FLOAT r; FLOAT g; FLOAT b; }
template MeshFace { <3D82AB5F-62DA-11CF-AB39-0020AF71E433> DWORD count; array DWORD indices[count]; }
template Mesh { <3D82AB44-62DA-11CF-AB39-0020AF71E433> DWORD vertices; array Vector positions[vertices]; DWORD faces; array MeshFace polygons[faces]; [...] }
template MeshNormals { <F6F23F43-7686-11CF-8F52-0040333594A3> DWORD normals; array Vector values[normals]; DWORD faces; array MeshFace indices[faces]; }
template MeshTextureCoords { <F6F23F40-7686-11CF-8F52-0040333594A3> DWORD count; array Coords2d values[count]; }
template TextureFilename { <A42790E1-7810-11CF-8F52-0040333594A3> STRING filename; }
template Material { <3D82AB4D-62DA-11CF-AB39-0020AF71E433> ColorRGBA faceColor; FLOAT power; ColorRGB specular; ColorRGB emissive; [...] }
template MeshMaterialList { <F6F23F42-7686-11CF-8F52-0040333594A3> DWORD materials; DWORD faces; array DWORD indices[faces]; [Material] }
)";
void Require(bool okay, const char* reason) {
    if (!okay)
        throw std::runtime_error(reason);
}
struct Data {
    ID3DXFileData* object;
    const unsigned char* bytes = nullptr;
    SIZE_T size = 0, at = 0;
    explicit Data(ID3DXFileData* p) : object(p) {
        const void* raw = nullptr;
        Require(SUCCEEDED(p->Lock(&size, &raw)), "x_data_lock");
        bytes = static_cast<const unsigned char*>(raw);
    }
    ~Data() { object->Unlock(); }
    template <class T> T Get() {
        Require(at <= size && sizeof(T) <= size - at, "x_truncated_payload");
        T value;
        std::memcpy(&value, bytes + at, sizeof(T));
        at += sizeof(T);
        return value;
    }
    unsigned Count() {
        auto n = Get<DWORD>();
        Require(n <= Limit, "x_count_limit");
        return n;
    }
    float Float() {
        auto f = Get<float>();
        if (!std::isfinite(f)) {
            unsigned bits = 0;
            std::memcpy(&bits, &f, 4);
            throw std::runtime_error("x_nonfinite_payload_at_" + std::to_string(at - 4) + "_size_" +
                                     std::to_string(size) + "_bits_" + std::to_string(bits));
        }
        return f;
    }
    void End() { Require(at == size, "x_payload_size"); }
};
template <class F> void Children(ID3DXFileData* node, F visit) {
    SIZE_T count = 0;
    Require(SUCCEEDED(node->GetChildren(&count)) && count <= Limit, "x_child_count");
    for (SIZE_T n = 0; n < count; ++n) {
        ComPtr<ID3DXFileData> child;
        Require(SUCCEEDED(node->GetChild(n, &child)), "x_child");
        GUID type;
        Require(SUCCEEDED(child->GetType(&type)), "x_type");
        visit(child.Get(), type);
    }
}
std::filesystem::path Path(const std::string& raw) {
    if (raw.empty())
        return {};
    const int n =
        MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()), nullptr, 0);
    Require(n > 0, "x_texture_encoding");
    std::wstring text(n, L'\0');
    MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()), text.data(), n);
    return text;
}
struct Mat {
    ModelMaterialInputs input;
    std::string filename;
    unsigned textureType = 0;
    std::array<rhi::MaterialTextureAsset, 3> textures;
};
Mat ReadMaterial(ID3DXFileData* node) {
    Mat material;
    Data data(node);
    auto& m = material.input;
    for (auto& f : m.diffuse)
        f = data.Float();
    m.ambient = m.diffuse;
    m.shininess = data.Float();
    for (unsigned c = 0; c < 3; ++c)
        m.specular[c] = data.Float();
    for (unsigned c = 0; c < 3; ++c)
        m.emissive[c] = data.Float();
    data.End();
    for (unsigned c = 0; c < 3; ++c) {
        m.diffuse[c] *= .1f;
        m.specular[c] *= .1f;
    }
    Children(node, [&](ID3DXFileData* child, const GUID& type) {
        Require(type == Texture && material.filename.empty(), "x_material_extension_not_supported");
        Data name(child);
        Require(name.size && name.size <= 4097, "x_texture_payload");
        const auto* end = static_cast<const unsigned char*>(std::memchr(name.bytes, 0, name.size));
        Require(end && end == name.bytes + name.size - 1, "x_texture_name_length");
        material.filename.assign(reinterpret_cast<const char*>(name.bytes),
                                 std::size_t(end - name.bytes));
    });
    return material;
}
}
struct RuntimeCpuAccessory::Impl {
    render::ModelGeometrySnapshot geometry;
    std::vector<Mat> materials;
    std::vector<render::AccessorySubset> subsets;
    render::ModelSubmissionBuilder builder;
};
RuntimeCpuAccessory::RuntimeCpuAccessory() = default;
RuntimeCpuAccessory::~RuntimeCpuAccessory() = default;
bool RuntimeCpuAccessory::Load(const std::filesystem::path& path, std::uint64_t id,
                               std::string& reason) {
    try {
        Require(id && id < UINT64_MAX - 2, "x_geometry_identity");
        std::ifstream source(path, std::ios::binary | std::ios::ate);
        Require(bool(source) && source.tellg() > 0 && source.tellg() <= 64 * 1024 * 1024,
                "x_file_size");
        std::vector<unsigned char> bytes(std::size_t(source.tellg()));
        source.seekg(0);
        Require(bool(source.read(reinterpret_cast<char*>(bytes.data()), bytes.size())),
                "x_file_read");
        struct Memory {
            const void* data;
            SIZE_T size;
        } memory{bytes.data(), bytes.size()};
        ComPtr<ID3DXFile> file;
        ComPtr<ID3DXFileEnumObject> objects;
        Require(SUCCEEDED(D3DXFileCreate(&file)), "x_file_parser");
        Require(SUCCEEDED(file->RegisterTemplates(Templates, sizeof(Templates) - 1)),
                "x_standard_templates");
        Require(SUCCEEDED(file->CreateEnumObject(&memory, 3, &objects)), "x_file_parse");
        SIZE_T count = 0;
        Require(SUCCEEDED(objects->GetChildren(&count)) && count <= Limit, "x_root_count");
        ComPtr<ID3DXFileData> mesh;
        for (SIZE_T n = 0; n < count; ++n) {
            ComPtr<ID3DXFileData> item;
            GUID type;
            Require(SUCCEEDED(objects->GetChild(n, &item)) && SUCCEEDED(item->GetType(&type)),
                    "x_root");
            if (type == Header)
                continue;
            Require(type == Mesh && !mesh, "x_only_one_static_mesh_supported");
            mesh = item;
        }
        Require(bool(mesh), "x_mesh_missing");
        auto next = std::make_unique<Impl>();
        std::vector<std::array<float, 3>> positions, normals;
        std::vector<std::array<float, 2>> uvs;
        std::vector<std::array<unsigned, 3>> faces, normalFaces;
        std::vector<unsigned> faceMaterials;
        {
            Data data(mesh.Get());
            positions.resize(data.Count());
            for (auto& p : positions)
                for (auto& f : p)
                    f = data.Float();
            faces.resize(data.Count());
            Require(!positions.empty() && !faces.empty(), "x_empty_mesh");
            for (auto& face : faces) {
                Require(data.Count() == 3, "x_nontriangle_face_not_supported");
                for (auto& index : face) {
                    index = data.Count();
                    Require(index < positions.size(), "x_vertex_index");
                }
            }
            data.End();
        }
        bool materialList = false;
        Children(mesh.Get(), [&](ID3DXFileData* child, const GUID& type) {
            if (type == Normals) {
                Require(normals.empty(), "x_duplicate_normals");
                Data data(child);
                normals.resize(data.Count());
                for (auto& p : normals)
                    for (auto& f : p)
                        f = data.Float();
                normalFaces.resize(data.Count());
                Require(normalFaces.size() == faces.size(), "x_normal_faces");
                for (auto& face : normalFaces) {
                    Require(data.Count() == 3, "x_normal_polygon");
                    for (auto& index : face) {
                        index = data.Count();
                        Require(index < normals.size(), "x_normal_index");
                    }
                }
                data.End();
            } else if (type == Uvs) {
                Require(uvs.empty(), "x_duplicate_uvs");
                Data data(child);
                uvs.resize(data.Count());
                Require(uvs.size() == positions.size(), "x_uv_count");
                for (auto& uv : uvs)
                    for (auto& f : uv)
                        f = data.Float();
                data.End();
            } else if (type == Materials) {
                Require(!materialList, "x_duplicate_materials");
                materialList = true;
                Data data(child);
                const auto mats = data.Count();
                faceMaterials.resize(data.Count());
                Require(mats && faceMaterials.size() == faces.size(), "x_material_faces");
                for (auto& index : faceMaterials) {
                    index = data.Count();
                    Require(index < mats, "x_material_index");
                }
                data.End();
                Children(child, [&](ID3DXFileData* entry, const GUID& kind) {
                    Require(kind == Material, "x_material_child");
                    next->materials.push_back(ReadMaterial(entry));
                });
                Require(next->materials.size() == mats, "x_material_count");
            } else
                Require(false, "x_mesh_extension_not_supported");
        });
        Require(materialList && !normals.empty(), "x_explicit_normals_and_materials_required");
        // D3DXLoadMeshFromX applies the accumulated frame transform even for a
        // root Mesh. Keep the coordinate reciprocal and explicit normal
        // normalization; both are observable even without a Frame node.
        D3DXMATRIX transform{};
        for (unsigned n = 0; n < 4; ++n)
            transform.m[n][n] = 1;
        for (auto& p : positions) {
            D3DXVECTOR3 out;
            D3DXVec3TransformCoord(&out, reinterpret_cast<const D3DXVECTOR3*>(p.data()),
                                   &transform);
            std::memcpy(p.data(), &out, 12);
        }
        for (auto& normal : normals) {
            D3DXVECTOR3 out;
            D3DXVec3Normalize(&out, reinterpret_cast<const D3DXVECTOR3*>(normal.data()));
            std::memcpy(normal.data(), &out, 12);
        }
        std::vector<ModelVertex> vertices;
        std::vector<std::vector<unsigned>> groups(next->materials.size());
        std::map<std::pair<unsigned, unsigned>, unsigned> vertexMap;
        for (unsigned f = 0; f < faces.size(); ++f)
            for (unsigned c = 0; c < 3; ++c) {
                const auto p = faces[f][c], n = normalFaces[f][c];
                auto found = vertexMap.find({p, n});
                if (found == vertexMap.end()) {
                    Require(vertices.size() < Limit, "x_split_vertex_limit");
                    ModelVertex v{};
                    std::copy(positions[p].begin(), positions[p].end(), v.position);
                    std::copy(normals[n].begin(), normals[n].end(), v.normal);
                    if (!uvs.empty())
                        std::copy(uvs[p].begin(), uvs[p].end(), v.uv);
                    found =
                        vertexMap.emplace(std::make_pair(p, n), unsigned(vertices.size())).first;
                    vertices.push_back(v);
                }
                groups[faceMaterials[f]].push_back(found->second);
            }
        ModelGeometry geometry(id, unsigned(vertices.size()));
        std::memcpy(geometry.MainData(), vertices.data(), vertices.size() * sizeof(ModelVertex));
        std::vector<unsigned> indices;
        for (unsigned m = 0; m < groups.size(); ++m) {
            if (!groups[m].empty())
                next->subsets.push_back({m, unsigned(indices.size()), unsigned(groups[m].size())});
            indices.insert(indices.end(), groups[m].begin(), groups[m].end());
        }
        Require(geometry.SetIndices(rhi::IndexFormat::UInt32, indices.data(), indices.size() * 4) &&
                    geometry.Publish(),
                "x_geometry_publish");
        next->geometry = geometry.Snapshot();
        for (auto& m : next->materials) {
            auto primary = m.filename;
            std::string sphere;
            const auto star = primary.find('*');
            if (star != std::string::npos) {
                sphere = primary.substr(star + 1);
                primary.resize(star);
                m.textureType = sphere.find(".sph") != std::string::npos   ? 4
                                : sphere.find(".spa") != std::string::npos ? 5
                                                                           : 0;
            } else
                m.textureType = primary.find("screen.bmp") != std::string::npos ? 3
                                : primary.find(".sph") != std::string::npos     ? 1
                                : primary.find(".spa") != std::string::npos     ? 2
                                                                                : 0;
            Require(m.textureType != 3, "x_dynamic_screen_texture_not_supported");
            const std::string names[]{primary, sphere};
            for (unsigned s = 0; s < 2; ++s)
                if (!names[s].empty()) {
                    Require(rhi::LoadMaterialTexture(path.parent_path(), Path(names[s]),
                                                     m.textures[s]) ==
                                rhi::MaterialResourceStatus::Ready,
                            "x_texture_unavailable");
                    Require(rhi::PrepareMaterialMipChain(m.textures[s],
                                                         rhi::MipPreparationPolicy::BoxLinear),
                            "x_texture_mips");
                }
        }
        impl = std::move(next);
        reason.clear();
        return true;
    } catch (const std::exception& error) {
        reason = error.what();
        return false;
    }
}
bool RuntimeCpuAccessory::Build(const render::ModelFrameInputs& input,
                                const render::ModelMatrix& placement,
                                const format::PmmEvaluatedAccessory& pose, bool additive,
                                bool after, const rhi::HistoryDesc& target, rhi::ClearColor clear,
                                const std::function<void(render::SceneDrawReplacement)>& append,
                                std::string& reason) {
    if (!impl || !append) {
        reason = "cpu_accessory_not_loaded";
        return false;
    }
    if (!pose.visible) {
        reason.clear();
        return true;
    }
    auto frame = input;
    using M = d3dx::D3DXMATRIXF;
    M local, world, result;
    std::memcpy(&local, placement.data(), 64);
    std::memcpy(&world, input.world.data(), 64);
    d3dx::Get().multiply(&result, &local, &world);
    std::memcpy(frame.world.data(), &result, 64);
    std::vector<render::ModelMaterialPacket> packets;
    for (const auto& subset : impl->subsets) {
        const auto& m = impl->materials[subset.material];
        render::AccessoryPassInputs p;
        p.material = m.input;
        p.opacity = pose.opacity;
        p.additive = additive;
        p.afterModelPass = after;
        p.textureType = m.textureType;
        p.primaryTexture = m.textures[0].content.revision != 0;
        p.sphereTexture = m.textures[1].content.revision != 0;
        render::ModelMaterialPacket packet;
        packet.material = subset.material;
        packet.firstIndex = subset.firstIndex;
        packet.indexCount = subset.indexCount;
        if (!render::BuildAccessoryPassInputs(frame, p, {}, packet.pipeline,
                                              LegacyCpuRenderMath())) {
            reason = "cpu_accessory_pipeline";
            return false;
        }
        packet.textures = m.textures;
        packets.push_back(std::move(packet));
    }
    rhi::ModelReplayStream stream;
    const auto built = impl->builder.Build(impl->geometry, target, clear, packets, stream);
    if (!built) {
        reason = built.reason;
        return false;
    }
    stream.source.frame = frame.scene.frame;
    unsigned index = 0;
    for (const auto& e : stream.events)
        if (e.op == rhi::ModelReplayOp::Draw) {
            const auto& packet = packets[index++];
            render::SceneDrawReplacement draw;
            draw.family = render::SceneDrawFamily::AccessoryMaterial;
            draw.model = pose.slot;
            draw.material = packet.material;
            draw.cpu = stream;
            draw.cpu.events = {{1, rhi::ModelReplayOp::Clear}};
            for (const auto& resource : stream.resources)
                draw.cpu.events.push_back({draw.cpu.events.size() + 1,
                                           rhi::ModelReplayOp::PublishResource, resource.key});
            auto event = e;
            event.sequence = draw.cpu.events.size() + 1;
            draw.cpu.events.push_back(event);
            for (unsigned s = 0; s < 3; ++s)
                draw.textureIdentities[s] = packet.textures[s].identity;
            append(std::move(draw));
        }
    reason.clear();
    return true;
}
const render::ModelGeometrySnapshot* RuntimeCpuAccessory::Geometry() const {
    return impl ? &impl->geometry : nullptr;
}
}
