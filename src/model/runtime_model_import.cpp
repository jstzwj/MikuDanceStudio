#include "runtime_model_import.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>
#include <cstring>

namespace mikudancestudio::runtime {
bool Cp932Identifier(const char* name, std::size_t size, std::string& out) {
    std::size_t length = 0;
    while (length < size && name[length])
        ++length;
    return length && text::DecodeCp932({name, length}, out) && out.find('\0') == std::string::npos;
}
namespace {
bool Name(const char* input, std::size_t size, format::PmxText& out) {
    std::string utf8;
    if (!Cp932Identifier(input, size, utf8))
        return false;
    // DecodeCp932 only produces valid BMP scalars. Retain raw PMD names in
    // BoneData as well, for the original knee-name and grant rules.
    for (std::size_t n = 0; n < utf8.size();) {
        unsigned c = static_cast<unsigned char>(utf8[n++]);
        if (c >= 0xe0) {
            c = (c & 15) << 12;
            c |= (static_cast<unsigned char>(utf8[n++]) & 63) << 6;
            c |= static_cast<unsigned char>(utf8[n++]) & 63;
        } else if (c >= 0xc0) {
            c = (c & 31) << 6;
            c |= static_cast<unsigned char>(utf8[n++]) & 63;
        }
        out.push_back(c & 255);
        out.push_back(c >> 8);
    }
    return true;
}
}
bool NormalizePmd(const format::PmdDocument& source, format::PmxDocument& out,
                  std::string& reason) {
    format::PmxDocument result;
    result.header.version = 2;
    result.header.textEncoding = 0;
    for (const auto& v : source.vertices) {
        if (v.weightPercent > 100) {
            reason = "invalid-pmd-weight";
            return false;
        }
        format::PmxVertexData vertex;
        std::copy_n(v.position, 3, vertex.position);
        std::copy_n(v.normal, 3, vertex.normal);
        std::copy_n(v.uv, 2, vertex.uv);
        vertex.weightType = 1;
        vertex.bone[0] = v.bone[0] == 65535 ? -1 : v.bone[0];
        vertex.bone[1] = v.bone[1] == 65535 ? -1 : v.bone[1];
        vertex.weight[0] = float(v.weightPercent) * .009999999776482582f;
        vertex.edgeScale = v.edgeDisabled ? 0.f : 1.f;
        result.vertices.push_back(vertex);
    }
    result.indices.assign(source.indices.begin(), source.indices.end());
    for (const auto& b : source.bones) {
        format::PmxBoneDocument bone;
        bone.fields = b;
        bone.fields.flags = 8;
        if (!Name(b.pmdName, sizeof b.pmdName, bone.names[0])) {
            reason = "invalid-pmd-bone-name";
            return false;
        }
        result.bones.push_back(std::move(bone));
    }
    for (const auto& m : source.materials) {
        format::PmxMaterialDocument material;
        material.fields = m;
        result.materials.push_back(std::move(material));
    }
    for (std::size_t n = 0; n < source.morphs.size(); ++n) {
        const auto& m = source.morphs[n];
        format::PmxMorphDocument morph;
        morph.header.type = 1;
        morph.header.panel = m.header.panel;
        if (!Name(m.header.name, sizeof m.header.name, morph.names[0])) {
            reason = "invalid-pmd-morph-name";
            return false;
        }
        for (const auto& offset : m.offsets) {
            if (n == 0) {
                if (offset.vertexIndex >= result.vertices.size()) {
                    reason = "invalid-pmd-base-morph";
                    return false;
                }
                std::copy_n(offset.offset, 3, result.vertices[offset.vertexIndex].position);
            } else {
                if (source.morphs.empty() ||
                    offset.vertexIndex >= source.morphs[0].offsets.size()) {
                    reason = "invalid-pmd-morph-reference";
                    return false;
                }
                format::PmxMorphOffsetData entry;
                entry.index = source.morphs[0].offsets[offset.vertexIndex].vertexIndex;
                std::copy_n(offset.offset, 3, entry.values);
                morph.offsets.push_back(entry);
            }
        }
        result.morphs.push_back(std::move(morph));
    }
    for (const auto& rigid : source.rigids) {
        format::PmxRigidDocument value;
        value.fields = rigid;
        result.rigids.push_back(value);
    }
    for (const auto& joint : source.joints) {
        format::PmxJointDocument value;
        value.fields = joint;
        result.joints.push_back(value);
    }
    out = std::move(result);
    reason.clear();
    return true;
}
bool BuildSkeleton(const format::PmxDocument& model, format::ModelKind kind,
                   const std::vector<format::PmdIkData>& pmd, const std::vector<std::size_t>& order,
                   const std::vector<std::string>& names, std::vector<cpu_model::Bone>& bones,
                   std::vector<cpu_model::IkChain>& chains, std::string& reason) {
    using namespace cpu_bone;
    bones.resize(model.bones.size());
    const auto index = [&](int i) { return i >= 0 && std::size_t(i) < bones.size(); };
    const auto fail = [&](const char* code, std::size_t n) {
        reason = std::string(code) + ":" + std::to_string(n);
        return false;
    };
    for (std::size_t n = 0; n < bones.size(); ++n) {
        const auto& source = model.bones[n].fields;
        auto& bone = bones[n];
        if (source.flags & ~0x3f3f)
            return fail("unsupported-bone-flags", n);
        if (source.layer < 0 || source.layer > 65535)
            return fail("invalid-bone-layer", n);
        if ((source.flags & kBoneFlagInheritMask) &&
            (!index(source.tailIdx) || !std::isfinite(source.inheritRatio)))
            return fail("invalid-bone-inheritance", n);
        bone.parent = source.parent;
        bone.tailBone = source.tailBone;
        bone.tailIdx = source.tailIdx;
        bone.layer = source.layer;
        bone.flags = source.flags;
        bone.inheritRatio = source.inheritRatio;
        std::copy_n(source.position, 3, bone.position);
        if (kind == format::ModelKind::Pmd) {
            if (source.type > 9)
                return fail("invalid-pmd-bone-type", n);
            bone.type = static_cast<BoneType>(source.type);
            std::copy_n(source.pmdName, 20, bone.name);
            if (bone.type == BoneType::CoRotate && !index(source.tailBone))
                return fail("invalid-co-rotation-source", n);
            if (bone.type == BoneType::RotateGrant && !index(source.tailIdx))
                return fail("invalid-bone-inheritance", n);
        } else {
            bone.type = (source.flags & kBoneFlagMovable) ? BoneType::Move : BoneType::RotateMove;
            if (source.flags & kBoneFlagFixedAxis) {
                bone.type = BoneType::FixedAxis;
                cpu_model::Math{}.normalize(bone.axis, source.axis);
                for (float f : bone.axis)
                    if (!std::isfinite(f))
                        return fail("invalid-bone-axis", n);
            }
            if (!(source.flags & kBoneFlagVisible))
                bone.type = BoneType::InertTip;
            if (names[n] == u8"左ひざ")
                std::memcpy(bone.name, "\x8d\xb6\x82\xd0\x82\xb4", 6);
            if (names[n] == u8"右ひざ")
                std::memcpy(bone.name, "\x89\x45\x82\xd0\x82\xb4", 6);
        }
    }
    const auto add = [&](int owner, int target, int loops, float angle,
                         const std::vector<format::IkLinkData>& links, bool pmx) {
        if (!index(owner) || !index(target) || links.size() > 255 || loops < 0 || loops > 65535 ||
            !std::isfinite(angle) || angle < 0)
            return false;
        cpu_model::IkChain chain;
        chain.boneIndex = owner;
        chain.targetBone = target;
        chain.iterations = loops;
        chain.maxAngle = pmx ? angle * .25f : angle;
        if (pmx && chain.maxAngle != 0) {
            const float limit = std::min(360.f, 3.141592025756836f / chain.maxAngle);
            chain.iterations = std::max(chain.iterations, int(limit));
        }
        if (pmx) {
            bones[owner].type = BoneType::Ik;
            bones[target].type = BoneType::Effector;
        }
        for (const auto& link : links) {
            if (!index(link.boneIndex) || link.boneIndex > 65535)
                return false;
            chain.storage.push_back(static_cast<std::uint16_t>(link.boneIndex));
            auto& bone = bones[link.boneIndex];
            if (pmx)
                bone.type = BoneType::UnderIk;
            if (link.hasLimits) {
                if (link.hasLimits != 1)
                    return false;
                bone.twistEnable = true;
                for (int c = 0; c < 3; ++c) {
                    if (!std::isfinite(link.minimum[c]) || !std::isfinite(link.maximum[c]) ||
                        link.minimum[c] > link.maximum[c])
                        return false;
                    bone.ikLimitMin[c] = link.minimum[c];
                    bone.ikLimitMax[c] = link.maximum[c];
                }
            }
        }
        chain.linkCount = int(chain.storage.size());
        chains.push_back(std::move(chain));
        return true;
    };
    if (kind == format::ModelKind::Pmx) {
        for (std::size_t n = 0; n < bones.size(); ++n) {
            const auto& b = model.bones[n].fields;
            if ((b.flags & kBoneFlagIk) &&
                !add(int(n), b.ikTarget, b.ikLoop, b.ikAngle, b.ikLinks, true))
                return fail("invalid-ik", n);
            if (!(b.flags & kBoneFlagIk) && !b.ikLinks.empty())
                return fail("invalid-ik", n);
        }
    } else
        for (std::size_t n = 0; n < pmd.size(); ++n) {
            const auto& source = pmd[n];
            std::vector<format::IkLinkData> links;
            for (auto i : source.links) {
                format::IkLinkData link;
                link.boneIndex = i;
                links.push_back(link);
            }
            if (!add(source.boneIndex, source.targetBone, source.iterations, source.maxAngle, links,
                     false))
                return fail("invalid-ik", n);
        }
    int slot = 1;
    for (auto n : order) {
        auto& bone = bones[n];
        const auto& parent = cpu_model::Traits::Parent(bones.data(), bone.parent);
        const auto& tail =
            cpu_model::Traits::Parent(bones.data(), index(bone.tailIdx) ? bone.tailIdx : -1);
        bone.hasFlag = parent.hasFlag ||
                       ((bone.type == BoneType::RotateGrant ||
                         ((bone.flags & kBoneFlagInheritMask) && kind == format::ModelKind::Pmx)) &&
                        (tail.type == BoneType::UnderIk ||
                         (kind == format::ModelKind::Pmd && tail.type == BoneType::Ik)));
        if (!bone.hasFlag && bone.parent >= 0 && kind == format::ModelKind::Pmx) {
            bone.layer = std::max(bone.layer, parent.layer);
            if (bone.type != BoneType::UnderIk && bone.type != BoneType::Effector &&
                (parent.type == BoneType::UnderIk || parent.type == BoneType::Effector))
                ++bone.layer;
        }
        if (bone.type == BoneType::Move || bone.type == BoneType::Ik)
            bone.slotIndex = slot++;
    }
    return true;
}
}
