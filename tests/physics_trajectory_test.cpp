#include "scene_fixture.hpp"
#include "original_physics_module.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/math_runtime.hpp"
#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h"
#include "LinearMath/btTransformUtil.h"
#include "BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <iomanip>
#include <sstream>
#include "BulletCollision/NarrowPhaseCollision/btGjkPairDetector.h"
#include "BulletCollision/NarrowPhaseCollision/btGjkEpaPenetrationDepthSolver.h"
#include "BulletCollision/CollisionDispatch/btBoxBoxDetector.h"

extern bool matrixToEulerXYZ(const btMatrix3x3&, btVector3&);

namespace mikudancestudio {
extern float g_CatchupDtBudget;
void CmdViewMenu(MMDApp*, HWND, std::uint16_t, std::uint16_t);
}
namespace {
constexpr float kStep = 1.0f / 60.0f;
btVector3 FixtureGravity() {
    float direction[3] = {0, -1, 0};
    d3dx::Get().vec3Normalize(direction, direction);
    return btVector3((direction[0] * 9.8f) * 10.0f, (direction[1] * 9.8f) * 10.0f,
                     (direction[2] * 9.8f) * 10.0f);
}

struct Sample {
    std::vector<float> values;
    std::vector<int> flags;
    std::vector<std::pair<std::size_t, std::string>> sections;
    void Section(std::string name) { sections.emplace_back(values.size(), std::move(name)); }
    std::string Field(std::size_t index) const {
        for (auto it = sections.rbegin(); it != sections.rend(); ++it)
            if (index >= it->first)
                return it->second + "+" + std::to_string(index - it->first);
        return std::to_string(index);
    }
    void Floats(const float* data, std::size_t size) {
        values.insert(values.end(), data, data + size);
    }
    void Vector(const btVector3& v) { values.insert(values.end(), {v.x(), v.y(), v.z()}); }
    void Transform(const btTransform& t) {
        for (int i = 0; i < 3; ++i)
            Vector(t.getBasis()[i]);
        Vector(t.getOrigin());
    }
};
std::uint64_t TrajectoryDigest(const std::vector<Sample>& trajectory, std::size_t count) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&](std::uint32_t bits) {
        for (int i = 0; i < 4; ++i) {
            hash ^= (bits >> (i * 8)) & 255;
            hash *= 1099511628211ull;
        }
    };
    for (std::size_t i = 0; i < count; ++i) {
        for (float value : trajectory[i].values) {
            std::uint32_t bits;
            std::memcpy(&bits, &value, 4);
            mix(bits);
        }
        for (int flag : trajectory[i].flags)
            mix(static_cast<std::uint32_t>(flag));
    }
    return hash;
}

#if defined(_M_X64)
// IDA x64 RVA 0x1181F0 / 0x31740: VC9 places the constraint frames
// sixteen bytes before the modern compiler's C++ layout. Only the local
// oracle uses this view; production continues to use Bullet's own members.
struct OriginalConstraintView {
    unsigned char prefix[112];
    btTransform frameA;
    btTransform frameB;
    unsigned char gap[576];
    btTranslationalLimitMotor linear;
    btRotationalLimitMotor angular[3];
};
static_assert(offsetof(OriginalConstraintView, frameA) == 112, "VC9 frame A");
static_assert(offsetof(OriginalConstraintView, linear) == 816, "VC9 linear motor");
static_assert(offsetof(OriginalConstraintView, angular) == 960, "VC9 angular motors");
#endif

btTypedConstraint* FindConstraint(PhysicsScene& scene, int uid, bool original = false) {
    for (int i = 0; i < scene.world->getNumConstraints(); ++i) {
        auto* constraint = scene.world->getConstraint(i);
        int candidate = constraint->getUid();
#if defined(_M_X64)
        if (original)
            std::memcpy(&candidate, reinterpret_cast<const unsigned char*>(constraint) + 96,
                        sizeof(candidate));
#endif
        if (candidate == uid)
            return constraint;
    }
    Check(false, "joint UID is missing from the physics world");
    return nullptr;
}

void SeedMotion(mdl::ModelRecord& model) {
    int controller = -1;
    for (unsigned i = 0; i < model.ikChainCount; ++i)
        if (model.ikChains[i].linkCount == 2) {
            controller = model.ikChains[i].boneIndex;
            break;
        }
    Check(controller > 0, "fixture needs a two-link leg IK controller");
    unsigned next = model.boneCount;
    for (int bone : {0, controller}) {
        auto& base = model.boneKeys[bone];
        auto& middle = model.boneKeys[next++];
        auto& last = model.boneKeys[next++];
        middle = last = base;
        base.next = next - 2;
        middle.frame = 60;
        middle.previous = bone;
        middle.next = next - 1;
        middle.allocated = 1;
        middle.position[0] = bone ? .15f : .25f;
        middle.position[1] = bone ? .2f : .1f;
        middle.position[2] = bone ? -.1f : -.2f;
        last.frame = 120;
        last.previous = next - 2;
        last.next = 0;
        last.allocated = 1;
    }
    model.maxFrame = 120;
}

Sample Capture(mdl::ModelRecord& model, bool original = false) {
    Sample sample;
    for (unsigned i = 0; i < model.rigidCount; ++i) {
        sample.Section("rigid[" + std::to_string(i) + "]");
        const auto& rigid = model.rigidTable[i];
        auto& body = *rigid.body;
        sample.Transform(body.getWorldTransform());
        sample.Transform(body.getInterpolationWorldTransform());
        sample.Vector(body.getLinearVelocity());
        sample.Vector(body.getAngularVelocity());
        sample.Vector(body.getInterpolationLinearVelocity());
        sample.Vector(body.getInterpolationAngularVelocity());
        sample.Vector(body.getTotalForce());
        sample.Vector(body.getTotalTorque());
        sample.Vector(body.getInvInertiaDiagLocal());
        for (int axis = 0; axis < 3; ++axis)
            sample.Vector(body.getInvInertiaTensorWorld()[axis]);
        sample.values.insert(sample.values.end(),
                             {body.getInvMass(), body.getFriction(), body.getRestitution(),
                              body.getLinearDamping(), body.getAngularDamping()});
        sample.flags.insert(sample.flags.end(),
                            {rigid.mode, body.getCollisionFlags(), body.getActivationState()});
    }
    for (unsigned i = 0; i < model.boneCount; ++i) {
        sample.Section("bone[" + std::to_string(i) + "]");
        const auto& bone = model.boneTable[i];
        sample.Floats(bone.matInit, 16);
        sample.Floats(bone.matLocal, 16);
        sample.Floats(bone.ikBackup, 7);
        sample.flags.push_back(bone.physicsDisabled);
    }
    for (unsigned i = 0; i < model.jointCount; ++i) {
        sample.Section("jointStretch[" + std::to_string(i) + "]");
        const auto& joint = model.jointTable[i];
        const auto delta = model.rigidTable[joint.rigidB].body->getWorldTransform().getOrigin() -
                           model.rigidTable[joint.rigidA].body->getWorldTransform().getOrigin();
        sample.values.push_back(delta.length() - joint.radiusBound);
        auto* constraint = static_cast<btGeneric6DofSpringConstraint*>(
            FindConstraint(*model.scenePtr, joint.constraint, original));
        const btTransform* frameA = &constraint->getFrameOffsetA();
        const btTransform* frameB = &constraint->getFrameOffsetB();
        auto* linear = constraint->getTranslationalLimitMotor();
#if defined(_M_X64)
        auto* view = reinterpret_cast<OriginalConstraintView*>(constraint);
        if (original) {
            frameA = &view->frameA;
            frameB = &view->frameB;
            linear = &view->linear;
        }
#endif
        const auto relative =
            (model.rigidTable[joint.rigidA].body->getWorldTransform() * *frameA).inverse() *
            (model.rigidTable[joint.rigidB].body->getWorldTransform() * *frameB);
        sample.Section("jointError[" + std::to_string(i) + "]");
        sample.Transform(relative);
        btVector3 angles;
        matrixToEulerXYZ(relative.getBasis(), angles);
        for (int axis = 0; axis < 3; ++axis) {
            const float lower = linear->m_lowerLimit[axis], upper = linear->m_upperLimit[axis];
            const float position = relative.getOrigin()[axis];
            sample.values.push_back(
                lower > upper ? 0
                              : (std::max)(lower - position, (std::max)(position - upper, 0.0f)));
            auto* angular = constraint->getRotationalLimitMotor(axis);
#if defined(_M_X64)
            if (original)
                angular = &view->angular[axis];
#endif
            sample.values.push_back(
                angular->m_loLimit > angular->m_hiLimit
                    ? 0
                    : (std::max)(angular->m_loLimit - angles[axis],
                                 (std::max)(angles[axis] - angular->m_hiLimit, 0.0f)));
        }
        sample.Section("joint[" + std::to_string(i) + "]");
        sample.Transform(*frameA);
        sample.Transform(*frameB);
        sample.Vector(linear->m_lowerLimit);
        sample.Vector(linear->m_upperLimit);
        for (int axis = 0; axis < 3; ++axis) {
            auto* angular = constraint->getRotationalLimitMotor(axis);
#if defined(_M_X64)
            if (original)
                angular = &view->angular[axis];
#endif
            sample.values.push_back(angular->m_loLimit);
            sample.values.push_back(angular->m_hiLimit);
        }
    }
    for (float value : sample.values)
        Check(std::isfinite(value), "physics state contains a nonfinite value");
    return sample;
}

bool Compare(const Sample& a, const Sample& b, int frame, const char* stage, float tolerance,
             bool fatal = true) {
    Check(a.values.size() == b.values.size() && a.flags.size() == b.flags.size(),
          "trajectory sample shape differs");
    for (std::size_t i = 0; i < a.flags.size(); ++i) {
        if (a.flags[i] != b.flags[i]) {
            std::fprintf(stderr,
                         "FIRST DIFFERENCE frame=%d stage=%s flag=%zu port=%d reference=%d\n",
                         frame, stage, i, a.flags[i], b.flags[i]);
            if (fatal)
                throw std::runtime_error("physics execution state differs");
            return false;
        }
    }
    for (std::size_t i = 0; i < a.values.size(); ++i) {
        if (std::fabs(a.values[i] - b.values[i]) > tolerance) {
            std::fprintf(
                stderr,
                "FIRST DIFFERENCE frame=%d stage=%s field=%s port=%.9g reference=%.9g delta=%.9g\n",
                frame, stage, a.Field(i).c_str(), a.values[i], b.values[i],
                a.values[i] - b.values[i]);
            if (fatal)
                throw std::runtime_error("physics trajectory differs");
            return false;
        }
    }
    return true;
}

struct TrajectoryComparison {
    struct Difference {
        int frame;
        std::string field;
        float port, reference;
    };
    struct Metric {
        float maximum = 0;
        std::uint64_t maxUlp = 0;
        int frame = -1;
        std::string field;
    };
    std::optional<Difference> first, firstOutsideTolerance;
    std::map<std::string, Metric> metrics;
    int flagDifferences = 0;
    static constexpr float tolerance = .00002f;
    void Add(const Sample& port, const Sample& reference, int frame) {
        Check(port.values.size() == reference.values.size() &&
                  port.flags.size() == reference.flags.size(),
              "reference trajectory sample shape");
        for (std::size_t i = 0; i < port.flags.size(); ++i)
            if (port.flags[i] != reference.flags[i])
                ++flagDifferences;
        for (std::size_t i = 0; i < port.values.size(); ++i) {
            const float a = port.values[i], b = reference.values[i];
            const float delta = std::fabs(a - b);
            if (delta == 0)
                continue;
            const auto field = port.Field(i);
            if (!first)
                first = Difference{frame, field, a, b};
            if (delta > tolerance && !firstOutsideTolerance)
                firstOutsideTolerance = Difference{frame, field, a, b};
            std::string category = field.substr(0, field.find('['));
            if (category == "rigid") {
                const auto offset = std::stoul(field.substr(field.rfind('+') + 1));
                category = offset < 24
                               ? (offset % 12 < 9 ? "rigidRotation" : "rigidPosition")
                               : (offset < 36 ? "rigidVelocity"
                                              : (offset >= 45 && offset < 54 ? "rigidInertiaWorld"
                                                                             : "rigidProperties"));
            } else if (category == "bone") {
                const auto offset = std::stoul(field.substr(field.rfind('+') + 1));
                category = offset < 16 ? "boneWorld" : (offset < 32 ? "boneLocal" : "boneBackup");
            } else if (category == "jointError") {
                const auto offset = std::stoul(field.substr(field.rfind('+') + 1));
                category = offset < 9 ? "jointRelativeRotation"
                                      : (offset < 12 ? "jointRelativePosition"
                                                     : (offset % 2 ? "jointAngularViolation"
                                                                   : "jointLinearViolation"));
            }
            auto& metric = metrics[category];
            if (delta > metric.maximum)
                metric = {delta, metric.maxUlp, frame, field};
            const auto ordered = [](float value) {
                std::uint32_t bits;
                std::memcpy(&bits, &value, 4);
                return bits & 0x80000000u ? ~bits : bits | 0x80000000u;
            };
            const auto x = ordered(a), y = ordered(b);
            metric.maxUlp = (std::max)(metric.maxUlp, std::uint64_t(x > y ? x - y : y - x));
        }
    }
    bool Passed() const { return !firstOutsideTolerance && flagDifferences == 0; }
    void Report(const std::filesystem::path& path, int frames) const {
        std::ostringstream output;
        output << std::setprecision(9) << "{\n  \"frames\": " << frames
               << ",\n  \"oracle_math\": \"original_vc90\",\n  \"absolute_tolerance\": "
               << tolerance << ",\n  \"within_tolerance\": " << (Passed() ? "true" : "false")
               << ",\n  \"flag_differences\": " << flagDifferences;
        const auto writeFirst = [&](const char* name, const auto& difference) {
            output << ",\n  \"" << name << "\": ";
            if (!difference) {
                output << "null";
                return;
            }
            output << "{\"frame\":" << difference->frame << ",\"field\":\"" << difference->field
                   << "\",\"port\":" << difference->port
                   << ",\"original\":" << difference->reference << "}";
        };
        writeFirst("first_difference", first);
        writeFirst("first_outside_tolerance", firstOutsideTolerance);
        output << ",\n  \"metrics\": {";
        bool comma = false;
        for (const auto& entry : metrics) {
            if (comma)
                output << ",";
            comma = true;
            const auto& m = entry.second;
            output << "\n    \"" << entry.first << "\": {\"max_absolute\":" << m.maximum
                   << ",\"max_ulp\":" << m.maxUlp << ",\"frame\":" << m.frame << ",\"field\":\""
                   << m.field << "\"}";
        }
        output << "\n  }\n}\n";
        std::printf("%s", output.str().c_str());
        if (!path.empty()) {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path);
            file << output.str();
            Check(bool(file), "write trajectory report");
        }
    }
};

#if defined(_M_X64)
struct ContactResult : btDiscreteCollisionDetectorInterface::Result {
    Sample sample;
    void setShapeIdentifiersA(int, int) override {}
    void setShapeIdentifiersB(int, int) override {}
    void addContactPoint(const btVector3& normal, const btVector3& point, float depth) override {
        sample.Vector(normal);
        sample.Vector(point);
        sample.values.push_back(depth);
    }
};
void CheckKernels(const std::filesystem::path& original) {
    // Exercise startup even when a caller previously enabled the modern path.
    _set_FMA3_enable(1);
    InitializeMathRuntime();
    std::unique_ptr<OriginalPhysicsModule> binary;
    if (!original.empty())
        binary = std::make_unique<OriginalPhysicsModule>(original);
    std::uint64_t hash = 14695981039346656037ull, referenceHash = hash;
    const auto hashSample = [&](const Sample& sample, std::uint64_t& hash) {
        const auto mix = [&](std::uint32_t value) {
            for (int byte = 0; byte < 4; ++byte) {
                hash ^= (value >> (byte * 8)) & 255;
                hash *= 1099511628211ull;
            }
        };
        mix(static_cast<std::uint32_t>(sample.values.size()));
        for (float value : sample.values) {
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            mix(bits);
        }
    };
    btSphereShape sphere(.32f);
    btCapsuleShape capsule(.41f, 1.83f);
    btBoxShape box(btVector3(.71f, .84f, .57f));
    btConvexShape* shapes[] = {&sphere, &capsule, &box};
    std::uint32_t seed = 0x93264;
    const auto scalar = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<int>(seed >> 9) - 4194304) / 4194304.0f;
    };
    const auto vector = [&] {
        const float x = scalar(), y = scalar(), z = scalar();
        return btVector3(x, y, z);
    };
    for (int i = 0; i < 1000; ++i) {
        btDiscreteCollisionDetectorInterface::ClosestPointInput input;
        input.m_transformA.setIdentity();
        input.m_transformB.setIdentity();
        // Feed exactly the same already-rounded transforms to both kernels.
        const btVector3 position = vector();
        const btVector3 axis = vector();
        const float qw = scalar();
        input.m_transformB.setOrigin(position);
        input.m_transformB.setRotation(btQuaternion(axis.x(), axis.y(), axis.z(), qw).normalized());
        btGjkEpaPenetrationDepthSolver depth;
        void* nativeDepth = binary ? binary->Function<void*>(0x128CC0) : nullptr;
        btVoronoiSimplexSolver simplex, nativeSimplex;
        auto* a = shapes[i % 3];
        auto* b = shapes[(i / 3) % 3];
        btGjkPairDetector port(a, b, &simplex, &depth);
        btGjkPairDetector native(a, b, &nativeSimplex,
                                 reinterpret_cast<btConvexPenetrationDepthSolver*>(&nativeDepth));
        ContactResult result, expected;
        port.getClosestPointsNonVirtual(input, result, nullptr);
        if (binary) {
            binary->Function<void (*)(
                btGjkPairDetector*, const btDiscreteCollisionDetectorInterface::ClosestPointInput*,
                btDiscreteCollisionDetectorInterface::Result*, btIDebugDraw*)>(0xFB2F0)(
                &native, &input, &expected, nullptr);
            hashSample(expected.sample, referenceHash);
            if (!Compare(result.sample, expected.sample, i, "collision kernel", 0, false)) {
                std::printf("pair=%d/%d method=%d/%d degeneracy=%d/%d iterations=%d/%d\n",
                            a->getShapeType(), b->getShapeType(), port.m_lastUsedMethod,
                            native.m_lastUsedMethod, port.m_degenerateSimplex,
                            native.m_degenerateSimplex, port.m_curIter, native.m_curIter);
                Check(false, "collision kernel differs");
            }
        }
        hashSample(result.sample, hash);
        if (i % 9 == 8) {
            btBoxBoxDetector detector(&box, &box);
            ContactResult contacts, referenceContacts;
            detector.getClosestPoints(input, contacts, nullptr);
            if (binary) {
                binary->Function<void (*)(
                    btBoxBoxDetector*,
                    const btDiscreteCollisionDetectorInterface::ClosestPointInput*,
                    btDiscreteCollisionDetectorInterface::Result*, btIDebugDraw*, bool)>(0xFFF10)(
                    &detector, &input, &referenceContacts, nullptr, false);
                hashSample(referenceContacts.sample, referenceHash);
                Compare(contacts.sample, referenceContacts.sample, i, "box contact kernel", 0);
            }
            hashSample(contacts.sample, hash);
        }
    }
    for (int i = 0; i < 100; ++i) {
        const float mass = .13f + i * .0173f;
        for (int shape = 0; shape < 2; ++shape) {
            btConvexShape* value =
                shape ? static_cast<btConvexShape*>(&box) : static_cast<btConvexShape*>(&sphere);
            btVector3 inertia, expected;
            value->calculateLocalInertia(mass, inertia);
            Sample sample;
            sample.Vector(inertia);
            hashSample(sample, hash);
            if (binary) {
                binary->Function<void (*)(btConvexShape*, float, btVector3*)>(
                    shape ? 0xE81D0 : 0xECA40)(value, mass, &expected);
                Sample reference;
                reference.Vector(expected);
                hashSample(reference, referenceHash);
                Compare(sample, reference, i, "inertia kernel", 0);
            }
        }
    }
    if (binary)
        Check(hash == referenceHash, "reference collision/inertia float words differ");
    if (!binary)
        Check(hash == 0xF9489A9C38A111A4ull, "reference-derived collision/inertia digest");
    std::printf("1000 GJK + 111 box contacts + 200 inertia cases: %016llX%s\n",
                static_cast<unsigned long long>(hash),
                binary ? " (original matched bit for bit)" : "");
    std::uint32_t mathSeed = 932;
    const auto mathScalar = [&] {
        mathSeed = mathSeed * 1664525u + 1013904223u;
        return (static_cast<int>(mathSeed >> 9) - 4194304) / 4194304.0f;
    };
    using Unary = float (*)(float);
    using Binary = float (*)(float, float);
    const auto crt = binary ? GetModuleHandleW(L"msvcr90.dll") : nullptr;
    const auto nativeSine = crt ? reinterpret_cast<Unary>(GetProcAddress(crt, "sinf")) : nullptr;
    const auto nativeCosine = crt ? reinterpret_cast<Unary>(GetProcAddress(crt, "cosf")) : nullptr;
    const auto nativeAsine = crt ? reinterpret_cast<Unary>(GetProcAddress(crt, "asinf")) : nullptr;
    const auto nativeAtan = crt ? reinterpret_cast<Binary>(GetProcAddress(crt, "atan2f")) : nullptr;
    std::uint64_t mathHash = 14695981039346656037ull, nativeMathHash = mathHash;
    for (int i = 0; i < 4096; ++i) {
        const float x = mathScalar(), y = mathScalar();
        Sample sample;
        sample.values = {::sinf(x * 3.0f), ::cosf(x * 3.0f), ::cosf(x * .02f), ::asinf(x),
                         ::atan2f(x, y)};
        hashSample(sample, mathHash);
        if (binary) {
            Sample expected;
            expected.values = {nativeSine(x * 3.0f), nativeCosine(x * 3.0f), nativeCosine(x * .02f),
                               nativeAsine(x), nativeAtan(x, y)};
            hashSample(expected, nativeMathHash);
            Compare(sample, expected, i, "CRT trigonometry", 0);
        }
    }
    Check(mathHash == 0xED441BA962F290DCull, "VC90-derived CRT trigonometry digest");
    if (binary)
        Check(mathHash == nativeMathHash, "VC90 trigonometry float words differ");
    std::printf("4096 VC90 trigonometry cases: %016llX\n",
                static_cast<unsigned long long>(mathHash));
}
struct ConstraintRowProbe {
    btTypedConstraint* constraint;
    void** saved;
    std::array<void*, 7> table;
    int rows = 0;
    std::vector<Sample> samples;
    static inline std::vector<ConstraintRowProbe*> active;
    static ConstraintRowProbe& Find(btTypedConstraint* value) {
        for (auto* probe : active)
            if (probe->constraint == value)
                return *probe;
        std::abort();
    }
    static void Info1(btTypedConstraint* value, btTypedConstraint::btConstraintInfo1* info) {
        auto& probe = Find(value);
        reinterpret_cast<void (*)(btTypedConstraint*, btTypedConstraint::btConstraintInfo1*)>(
            probe.saved[3])(value, info);
        probe.rows = info->m_numConstraintRows;
    }
    static void Info2(btTypedConstraint* value, btTypedConstraint::btConstraintInfo2* info) {
        auto& probe = Find(value);
        reinterpret_cast<void (*)(btTypedConstraint*, btTypedConstraint::btConstraintInfo2*)>(
            probe.saved[4])(value, info);
        Sample sample;
        float* fields[] = {info->m_J1linearAxis,  info->m_J1angularAxis,   info->m_J2linearAxis,
                           info->m_J2angularAxis, info->m_constraintError, info->cfm,
                           info->m_lowerLimit,    info->m_upperLimit};
        for (int row = 0; row < probe.rows; ++row)
            for (int field = 0; field < 8; ++field) {
                const float zero[3]{};
                sample.Floats(fields[field] ? fields[field] + row * info->rowskip : zero,
                              field < 4 ? 3 : 1);
            }
        probe.samples.push_back(std::move(sample));
    }
    explicit ConstraintRowProbe(btTypedConstraint* value)
        : constraint(value), saved(*reinterpret_cast<void***>(value)) {
        std::copy_n(saved, table.size(), table.begin());
        table[3] = reinterpret_cast<void*>(&Info1);
        table[4] = reinterpret_cast<void*>(&Info2);
        *reinterpret_cast<void***>(value) = table.data();
        active.push_back(this);
    }
    ~ConstraintRowProbe() {
        *reinterpret_cast<void***>(constraint) = saved;
        active.erase(std::find(active.begin(), active.end(), this));
    }
};
struct SolverPoolProbe {
    btSequentialImpulseConstraintSolver* solver;
    void** saved;
    std::array<void*, 5> table;
    std::vector<Sample> samples;
    static inline std::vector<SolverPoolProbe*> active;
    using Call = float (*)(btSequentialImpulseConstraintSolver*, btCollisionObject**, int,
                           btPersistentManifold**, int, btTypedConstraint**, int,
                           const btContactSolverInfo&, btIDebugDraw*, btStackAlloc*, btDispatcher*);
    static float Solve(btSequentialImpulseConstraintSolver* solver, btCollisionObject** bodies,
                       int numBodies, btPersistentManifold** manifolds, int numManifolds,
                       btTypedConstraint** constraints, int numConstraints,
                       const btContactSolverInfo& info, btIDebugDraw* debug, btStackAlloc* stack,
                       btDispatcher* dispatcher) {
        auto& probe = **std::find_if(active.begin(), active.end(),
                                     [&](auto* value) { return value->solver == solver; });
        const float result = reinterpret_cast<Call>(probe.saved[2])(
            solver, bodies, numBodies, manifolds, numManifolds, constraints, numConstraints, info,
            debug, stack, dispatcher);
        // solveGroup clears the pool sizes but retains storage. IDA RVA 0x115FB0.
        // The constraint-size and order arrays retain their sizes as well.
        const auto* bytes = reinterpret_cast<const unsigned char*>(solver);
        auto integer = [&](std::size_t offset) {
            int value;
            std::memcpy(&value, bytes + offset, 4);
            return value;
        };
        auto pointer = [&](std::size_t offset) {
            const void* value;
            std::memcpy(&value, bytes + offset, 8);
            return value;
        };
        const auto* sizes = static_cast<const btTypedConstraint::btConstraintInfo1*>(pointer(216));
        int jointRows = 0;
        for (int i = 0; i < integer(204); ++i)
            jointRows += sizes[i].m_numConstraintRows;
        const int counts[] = {integer(140), jointRows, integer(172)};
        const int offsets[] = {56, 88, 120};
        const char* names[] = {"contact", "joint", "friction"};
        Sample sample;
        for (int pool = 0; pool < 3; ++pool) {
            const auto* rows = static_cast<const btSolverConstraint*>(pointer(offsets[pool]));
            for (int i = 0; i < counts[pool]; ++i) {
                const auto& row = rows[i];
                sample.Section(std::string(names[pool]) + "[" + std::to_string(i) + "]");
                sample.Vector(row.m_relpos1CrossNormal);
                sample.Vector(row.m_contactNormal);
                sample.Vector(row.m_relpos2CrossNormal);
                sample.Vector(row.m_angularComponentA);
                sample.Vector(row.m_angularComponentB);
                sample.values.insert(sample.values.end(),
                                     {row.m_friction, row.m_jacDiagABInv, row.m_rhs, row.m_cfm,
                                      row.m_lowerLimit, row.m_upperLimit});
            }
        }
        probe.samples.push_back(std::move(sample));
        return result;
    }
    explicit SolverPoolProbe(btSequentialImpulseConstraintSolver* value)
        : solver(value), saved(*reinterpret_cast<void***>(value)) {
        static_assert(sizeof(btSolverConstraint) == 176, "reference solver row ABI");
        static_assert(sizeof(btSequentialImpulseConstraintSolver) == 240, "reference solver ABI");
        std::copy_n(saved, table.size(), table.begin());
        table[2] = reinterpret_cast<void*>(&Solve);
        *reinterpret_cast<void***>(value) = table.data();
        active.push_back(this);
    }
    ~SolverPoolProbe() {
        *reinterpret_cast<void***>(solver) = saved;
        active.erase(std::find(active.begin(), active.end(), this));
    }
};
struct WorldStageProbe {
    btDiscreteDynamicsWorld* world;
    mdl::ModelRecord& model;
    bool original;
    void** saved;
    std::array<void*, 37> table;
    std::vector<Sample> samples;
    static inline std::vector<WorldStageProbe*> active;
    std::vector<Sample> contacts;
    Sample Contacts() {
        Sample result;
        auto* dispatcher = world->getDispatcher();
        result.flags.push_back(dispatcher->getNumManifolds());
        auto bodyIndex = [&](const void* body) {
            for (unsigned i = 0; i < model.rigidCount; ++i)
                if (model.rigidTable[i].body == body)
                    return static_cast<int>(i);
            return -1;
        };
        for (int i = 0; i < dispatcher->getNumManifolds(); ++i) {
            auto* manifold = dispatcher->getManifoldByIndexInternal(i);
            result.flags.insert(result.flags.end(),
                                {bodyIndex(manifold->getBody0()), bodyIndex(manifold->getBody1()),
                                 manifold->getNumContacts()});
            for (int j = 0; j < manifold->getNumContacts(); ++j) {
                result.Section("contact[" + std::to_string(bodyIndex(manifold->getBody0())) + "," +
                               std::to_string(bodyIndex(manifold->getBody1())) + "][" +
                               std::to_string(j) + "]");
                const auto& point = manifold->getContactPoint(j);
                result.Vector(point.m_localPointA);
                result.Vector(point.m_localPointB);
                result.Vector(point.m_positionWorldOnA);
                result.Vector(point.m_positionWorldOnB);
                result.Vector(point.m_normalWorldOnB);
                result.values.insert(result.values.end(),
                                     {point.m_distance1, point.m_combinedFriction,
                                      point.m_combinedRestitution, point.m_appliedImpulse});
            }
        }
        return result;
    }
    static WorldStageProbe& Find(btDiscreteDynamicsWorld* world) {
        for (auto* probe : active)
            if (probe->world == world)
                return *probe;
        std::abort();
    }
    static void Predict(btDiscreteDynamicsWorld* world, float dt) {
        auto& probe = Find(world);
        reinterpret_cast<void (*)(btDiscreteDynamicsWorld*, float)>(probe.saved[29])(world, dt);
        probe.samples.push_back(Capture(probe.model, probe.original));
    }
    static void Integrate(btDiscreteDynamicsWorld* world, float dt) {
        auto& probe = Find(world);
        reinterpret_cast<void (*)(btDiscreteDynamicsWorld*, float)>(probe.saved[30])(world, dt);
        probe.samples.push_back(Capture(probe.model, probe.original));
    }
    static void Solve(btDiscreteDynamicsWorld* world, btContactSolverInfo& info) {
        auto& probe = Find(world);
        probe.contacts.push_back(probe.Contacts());
        reinterpret_cast<void (*)(btDiscreteDynamicsWorld*, btContactSolverInfo&)>(probe.saved[32])(
            world, info);
        probe.samples.push_back(Capture(probe.model, probe.original));
    }
    WorldStageProbe(mdl::ModelRecord& value, bool ref)
        : world(value.scenePtr->world), model(value), original(ref),
          saved(*reinterpret_cast<void***>(world)) {
        std::copy_n(saved, table.size(), table.begin());
        table[29] = reinterpret_cast<void*>(&Predict);
        table[30] = reinterpret_cast<void*>(&Integrate);
        table[32] = reinterpret_cast<void*>(&Solve);
        *reinterpret_cast<void***>(world) = table.data();
        active.push_back(this);
    }
    ~WorldStageProbe() {
        *reinterpret_cast<void***>(world) = saved;
        active.erase(std::find(active.begin(), active.end(), this));
    }
};
struct ReferenceScene {
    OriginalPhysicsModule& module;
    PhysicsScene scene{};
    std::unique_ptr<D3DRenderer> renderer = std::make_unique<D3DRenderer>();
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    using ModelCall = void (*)(mdl::ModelRecord*);
    using PoseCall = void (*)(mdl::ModelRecord*, int, mdl::ModelRecord**, int);
    using SeekCall = void (*)(mdl::ModelRecord*, std::uint32_t, int);
    bool ready = false;
    explicit ReferenceScene(OriginalPhysicsModule& binary, Fixture& port,
                            const std::filesystem::path& path)
        : module(binary) {
        renderer->device = port.device;
        renderer->postProcessEnabled = 1;
        auto crt = GetModuleHandleW(L"msvcr90.dll");
        const auto locale = reinterpret_cast<_locale_t (*)(int, const char*)>(
            GetProcAddress(crt, "_create_locale"));
        for (auto& value : renderer->localeTable)
            value = locale(LC_ALL, "Japanese_Japan.932");
        Check(
            module.Function<bool (*)(PhysicsScene*, D3DRenderer*)>(0x27B0)(&scene, renderer.get()),
            "construct original physics world");
        module.Function<ModelCall>(0xA9610)(model.get());
        std::array<wchar_t, 32768> paths{};
        using LoadCall = bool (*)(mdl::ModelRecord*, HWND, const wchar_t*, D3DRenderer*,
                                  unsigned char, unsigned char, PhysicsScene*, void*);
        const auto cwd = std::filesystem::current_path();
        ready = module.Function<LoadCall>(0xB2670)(model.get(), port.window, path.c_str(),
                                                   renderer.get(), 0, 1, &scene, paths.data());
        std::filesystem::current_path(cwd);
        Check(ready, "load PMX with original parser");
        std::printf("original PMX loaded: bones=%u rigids=%u joints=%u\n", model->boneCount,
                    model->rigidCount, model->jointCount);
        std::fflush(stdout);
    }
    ~ReferenceScene() {
        // The original allocator owns all of these objects.
        if (ready)
            module.Function<ModelCall>(0xA8D50)(model.get());
        module.Function<void (*)(PhysicsScene*)>(0x2580)(&scene);
        const auto freeLocale = reinterpret_cast<void (*)(_locale_t)>(
            GetProcAddress(GetModuleHandleW(L"msvcr90.dll"), "_free_locale"));
        for (auto value : renderer->localeTable)
            if (value)
                freeLocale(value);
    }
    void Seek(int frame, int mode) {
        module.Function<SeekCall>(0xCBD90)(model.get(), static_cast<std::uint32_t>(frame), mode);
    }
    void Mode(int mode) {
        module.Function<void (*)(mdl::ModelRecord*, int)>(0xC2EE0)(model.get(), mode);
    }
    void CompareRows(mdl::ModelRecord& port, int frame) {
        for (unsigned index = 0; index < model->jointCount; ++index) {
            auto rows = [](btTypedConstraint* constraint, std::size_t size, bool original) {
                std::vector<unsigned char> saved(size);
                std::memcpy(saved.data(), constraint, size);
                btTypedConstraint::btConstraintInfo1 count{};
                constraint->getInfo1(&count);
                Sample geometry;
                if (original) {
                    const auto* data = reinterpret_cast<const unsigned char*>(constraint);
                    geometry.Transform(*reinterpret_cast<const btTransform*>(data + 1136));
                    geometry.Transform(*reinterpret_cast<const btTransform*>(data + 1200));
                    geometry.Floats(reinterpret_cast<const float*>(data + 1264), 3);
                    for (int axis = 0; axis < 3; ++axis)
                        geometry.Vector(
                            *reinterpret_cast<const btVector3*>(data + 1280 + axis * 16));
                } else {
                    auto* generic = static_cast<btGeneric6DofConstraint*>(constraint);
                    geometry.Transform(generic->getCalculatedTransformA());
                    geometry.Transform(generic->getCalculatedTransformB());
                    for (int axis = 0; axis < 3; ++axis)
                        geometry.values.push_back(generic->getAngle(axis));
                    for (int axis = 0; axis < 3; ++axis)
                        geometry.Vector(generic->getAxis(axis));
                }
                std::array<std::array<float, 256>, 8> storage{};
                for (int i = 0; i < 256; ++i) {
                    storage[6][i] = -SIMD_INFINITY;
                    storage[7][i] = SIMD_INFINITY;
                }
                btTypedConstraint::btConstraintInfo2 info{};
                info.fps = 60;
                info.erp = .2f;
                info.rowskip = 16;
                info.m_numIterations = 10;
                info.m_J1linearAxis = storage[0].data();
                info.m_J1angularAxis = storage[1].data();
                info.m_J2linearAxis = storage[2].data();
                info.m_J2angularAxis = storage[3].data();
                info.m_constraintError = storage[4].data();
                info.cfm = storage[5].data();
                info.m_lowerLimit = storage[6].data();
                info.m_upperLimit = storage[7].data();
                constraint->getInfo2(&info);
                Sample sample;
                sample.values = std::move(geometry.values);
                sample.flags.push_back(count.m_numConstraintRows);
                for (auto& field : storage)
                    sample.Floats(field.data(), field.size());
                std::memcpy(constraint, saved.data(), size);
                return sample;
            };
            const auto stage = "constraint[" + std::to_string(index) + "] rows";
            Compare(rows(port.scenePtr->world->getConstraint(index),
                         sizeof(btGeneric6DofSpringConstraint), false),
                    rows(scene.world->getConstraint(index), 1456, true), frame, stage.c_str(), 0);
        }
    }
    void Frame(int mode, bool settle, bool moved,
               const std::function<void(const char*)>& observe = {}) {
        mdl::ModelRecord* models[kModelSlotCount]{};
        models[0] = model.get();
        auto pose = [&](int after, int source) {
            module.Function<ModelCall>(0xBD780)(model.get());
            module.Function<PoseCall>(0xB9F70)(model.get(), after, models, source);
            if (observe)
                observe(after ? "pose1" : "pose0");
        };
        pose(0, settle ? 0 : mode);
        // IDA: pump RVA 0x2C6E5..0x2C85F. The fixture is playing, has
        // deterministic gravity, and has no accessory/physics dialog open.
        if (mode >= 1 && mode <= 3) {
            scene.world->setGravity(FixtureGravity());
            module.Function<ModelCall>(0xC31D0)(model.get());
            if (observe)
                observe("sync");
            scene.world->stepSimulation(kStep, 10, kStep);
            if (observe)
                observe("step");
            if (moved && mode == 3) {
                scene.world->stepSimulation(kStep, 10, kStep);
                if (observe)
                    observe("extra");
            }
            if (settle) {
                for (int i = 0; i < 3; ++i) {
                    module.Function<ModelCall>(0xC45D0)(model.get());
                    if (observe)
                        observe("reseat");
                    scene.world->stepSimulation(kStep, 10, kStep);
                    if (observe)
                        observe("settle step");
                }
            }
            module.Function<ModelCall>(0xC3470)(model.get());
            if (observe)
                observe("readback");
        }
        pose(1, mode);
    }
};
#endif

std::vector<Sample> Run(const std::filesystem::path& externalModel,
                        const std::filesystem::path& original, int frames, bool stages,
                        const std::filesystem::path& report = {}, bool* matched = nullptr) {
    Fixture f(true);
    Check(f.ready, "D3D9 fixture unavailable");
    const auto modelPath = externalModel.empty() ? f.directory / L"physics.pmx" : externalModel;
    if (externalModel.empty())
        WriteComplexModel(modelPath, true);
    auto& model = f.LoadModelFile(modelPath, 0, 0);
    std::printf("port PMX loaded: bones=%u rigids=%u joints=%u\n", model.boneCount,
                model.rigidCount, model.jointCount);
    std::fflush(stdout);
    Check(model.rigidCount > 0 && model.jointCount > 0 && model.ikChainCount > 0,
          "physics PMX must contain rigid bodies, joints and IK");
    auto& app = *f.app;
    app.CameraMode() = 0;
    app.PlaybackActive() = 1;
    app.state.messageSeen = 1;
    app.PhysicsResetPending() = 0;
    app.GravityMagnitude() = 9.8f;
    app.GravityDirection()[0] = app.GravityDirection()[2] = 0;
    app.GravityDirection()[1] = -1;
    SeedMotion(model);
    std::vector<Sample> trajectory;
    TrajectoryComparison comparison;
#if defined(_M_X64)
    std::unique_ptr<OriginalPhysicsModule> binary;
    std::unique_ptr<ReferenceScene> reference;
    if (!original.empty()) {
        binary = std::make_unique<OriginalPhysicsModule>(original);
        reference = std::make_unique<ReferenceScene>(*binary, f, modelPath);
        SeedMotion(*reference->model);
        Compare(Capture(model), Capture(*reference->model, true), -1, "load", stages ? 0 : .00002f);
    }
#else
    Check(original.empty(), "the x64 reference must be run by the x64 fixture");
#endif
    for (int frame = 0; frame < frames; ++frame) {
        const int mode =
            frame < 5 || (frame >= 240 && frame < 245) ? 0 : (frame >= 360 && frame < 420 ? 3 : 1);
        const bool settle = frame == 5 || frame == 180 || frame == 245 || frame == 360;
        const bool moved = frame == 390;
        if (app.PlaybackPhysicsMode() != mode || frame == 0)
            CmdViewMenu(&app, f.window,
                        mode == 0 ? MDS_CMD_PHYSICS_DISABLED
                                  : (mode == 3 ? MDS_CMD_PHYSICS_TRACE : MDS_CMD_PHYSICS_ALWAYS),
                        0);
        const int cursor = frame == 180 ? 48 : frame % 120;
        SeekModelFrame(model, cursor, mode);
        app.PhysicsResetPending() = settle;
        app.state.physicsBodiesMoved = moved;
        g_CatchupDtBudget = kStep;
#if defined(_M_X64)
        if (reference && stages) {
            reference->Mode(mode);
            reference->Seek(cursor, mode);
            Compare(Capture(model), Capture(*reference->model, true), frame, "seek", 0);
            SolverPoolProbe referenceSolver(reference->scene.solver), portSolver(f.physics->solver);
            WorldStageProbe referenceProbe(*reference->model, true), portProbe(model, false);
            std::vector<std::unique_ptr<ConstraintRowProbe>> portRows, referenceRows;
            for (unsigned i = 0; i < model.jointCount; ++i) {
                portRows.push_back(
                    std::make_unique<ConstraintRowProbe>(f.physics->world->getConstraint(i)));
                referenceRows.push_back(
                    std::make_unique<ConstraintRowProbe>(reference->scene.world->getConstraint(i)));
            }
            reference->Frame(mode, settle, moved, [&](const char* stage) {
                if (std::strcmp(stage, "pose0") == 0 || std::strcmp(stage, "pose1") == 0) {
                    const bool after = std::strcmp(stage, "pose1") == 0;
                    ModelApplyMorphs(&model);
                    SetPhysicsMode(&model, after, app.ModelSlots(), !after && settle ? 0 : mode);
                } else if (std::strcmp(stage, "sync") == 0) {
                    f.physics->world->setGravity(FixtureGravity());
                    ModelKinematicSync(&model);
                    if (frame >= 5)
                        reference->CompareRows(model, frame);
                } else if (std::strcmp(stage, "reseat") == 0) {
                    ModelDynamicReseat(&model);
                } else if (std::strcmp(stage, "readback") == 0) {
                    ModelPhysicsReadback(&model);
                } else {
                    f.physics->world->stepSimulation(kStep, 10, kStep);
                    Check(portSolver.samples.size() == referenceSolver.samples.size(),
                          "solver call count");
                    for (std::size_t i = 0; i < portSolver.samples.size(); ++i)
                        Compare(portSolver.samples[i], referenceSolver.samples[i], frame,
                                "solver setup", 0);
                    portSolver.samples.clear();
                    referenceSolver.samples.clear();
                    for (unsigned i = 0; i < model.jointCount; ++i) {
                        Check(portRows[i]->samples.size() == referenceRows[i]->samples.size(),
                              "row call count");
                        for (std::size_t j = 0; j < portRows[i]->samples.size(); ++j)
                            Compare(portRows[i]->samples[j], referenceRows[i]->samples[j], frame,
                                    "solver rows", 0);
                        portRows[i]->samples.clear();
                        referenceRows[i]->samples.clear();
                    }
                    const char* phaseNames[] = {"predict", "solve", "integrate"};
                    for (std::size_t phase = 0; phase < portProbe.samples.size(); ++phase) {
                        if (phase % 3 == 1)
                            Compare(portProbe.contacts[phase / 3],
                                    referenceProbe.contacts[phase / 3], frame, "contacts", 0);
                        Compare(portProbe.samples[phase], referenceProbe.samples[phase], frame,
                                phaseNames[phase % 3], 0);
                    }
                    portProbe.contacts.clear();
                    referenceProbe.contacts.clear();
                    Check(portProbe.samples.size() == referenceProbe.samples.size(),
                          "world phase count");
                    portProbe.samples.clear();
                    referenceProbe.samples.clear();
                }
                Compare(Capture(model), Capture(*reference->model, true), frame, stage, 0);
            });
            app.PhysicsResetPending() = 0;
        } else
#endif
            PhysicsFrame(&app, 0);
        trajectory.push_back(Capture(model));
#if defined(_M_X64)
        if (reference && !stages) {
            reference->Mode(mode);
            reference->Seek(cursor, mode);
            reference->Frame(mode, settle, moved);
            comparison.Add(trajectory.back(), Capture(*reference->model, true), frame);
        }
#endif
        Check(app.PhysicsResetPending() == 0, "settle request was not consumed");
    }
    if (externalModel.empty() && frames >= 20) {
        const auto digest = TrajectoryDigest(trajectory, 20);
#if defined(_M_X64)
        // Frozen after an unmodified VC90 oracle matched all first twenty frames.
        Check(digest == 0xAEC1372BE331E093ull, "reference-derived PMX trajectory prefix");
#endif
        std::printf("generated PMX first 20 frames digest: %016llX\n",
                    static_cast<unsigned long long>(digest));
    }
    Check(f.physics->world->getNumConstraints() == static_cast<int>(model.jointCount),
          "trajectory changed the constraint count");
    std::printf("physics trajectory: %d frames, %zu values/frame\n", frames,
                trajectory.back().values.size());
#if defined(_M_X64)
    if (reference) {
        comparison.Report(report, frames);
        if (matched)
            *matched = comparison.Passed();
    }
#endif
    return trajectory;
}

void CheckLifecycle(const std::filesystem::path& externalModel = {}) {
    Fixture f(true);
    Check(f.ready, "D3D9 fixture unavailable");
    const auto path = externalModel.empty() ? f.directory / L"lifecycle.pmx" : externalModel;
    if (externalModel.empty())
        WriteComplexModel(path, true);
    auto& first = f.LoadModelFile(path, 0, 0);
    auto& second = f.LoadModelFile(path, kModelSlotCount - 1, 1);
    first.comboSelIndex = 1;
    second.comboSelIndex = 2;
    auto& app = *f.app;
    app.CameraMode() = 0;
    app.SetSelectedModelSlot(0);
    app.state.messageSeen = 1;
    app.PlaybackActive() = 1;
    app.GravityMagnitude() = 9.8f;
    app.GravityDirection()[0] = app.GravityDirection()[2] = 0;
    app.GravityDirection()[1] = -1;
    CmdViewMenu(&app, f.window, MDS_CMD_PHYSICS_ALWAYS, 0);
    app.PhysicsResetPending() = 1;
    const auto step = [&] {
        g_CatchupDtBudget = kStep;
        PhysicsFrame(&app, 0);
    };
    for (int frame = 0; frame < 40; ++frame) {
        SeekModelFrame(first, frame, 1);
        SeekModelFrame(second, frame, 1);
        step();
    }
    const int jointCount = first.jointCount + second.jointCount;
    const int bodyCount = first.rigidCount + second.rigidCount + 1;
    auto* world = f.physics->world;
    auto* constraint = FindConstraint(*f.physics, first.jointTable[0].constraint);
    unsigned dynamic = 0;
    while (dynamic < first.rigidCount && first.rigidTable[dynamic].mode == 0)
        ++dynamic;
    Check(dynamic < first.rigidCount, "fixture needs a dynamic rigid body");
    auto* body = first.rigidTable[dynamic].body;
    body->setLinearVelocity(btVector3(1, 2, 3));
    body->setAngularVelocity(btVector3(4, 5, 6));
    body->setInterpolationLinearVelocity(btVector3(7, 8, 9));
    body->setInterpolationAngularVelocity(btVector3(10, 11, 12));
    body->applyCentralForce(btVector3(3, 4, 5));
    const auto interpolation = body->getInterpolationWorldTransform();
    ModelDynamicReseat(&first);
    Check(body->getLinearVelocity() == btVector3(0, 0, 0) &&
              body->getAngularVelocity() == btVector3(0, 0, 0) &&
              body->getInterpolationLinearVelocity() == btVector3(0, 0, 0) &&
              body->getInterpolationAngularVelocity() == btVector3(0, 0, 0),
          "reseat must clear all four velocity vectors");
    Check(body->getInterpolationWorldTransform() == interpolation &&
              body->getTotalForce() == btVector3(3, 4, 5) &&
              FindConstraint(*f.physics, first.jointTable[0].constraint) == constraint,
          "reseat must preserve interpolation pose, force and constraints");
    ModelPhysicsReadback(&first);
    step();

    // Stop requests settling for the next pump; it does not recreate Bullet.
    app.SavedPlaybackPhysicsMode() = 1;
    app.PlaybackActive() = 0;
    app.CurrentFrame() = 48;
    const auto velocity = body->getLinearVelocity();
    StopPlayback(&app);
    Check(app.PhysicsResetPending() == 1 && app.Physics()->world == world &&
              body->getLinearVelocity() == velocity,
          "stop must defer physical reset to the next frame");
    step();
    Check(app.PhysicsResetPending() == 0, "stop settling was not consumed");

    CmdViewMenu(&app, f.window, MDS_CMD_PHYSICS_TRACE, 0);
    app.PhysicsResetPending() = 0;
    app.state.physicsBodiesMoved = 0;
    const auto idle = body->getWorldTransform();
    const auto idleVelocity = body->getLinearVelocity();
    step();
    Check(body->getWorldTransform() == idle && body->getLinearVelocity() == idleVelocity,
          "trace mode must not step while idle");
    app.state.physicsBodiesMoved = 1;
    step();
    Check(app.state.physicsBodiesMoved == 0, "trace drag must consume the moved flag");
    CmdViewMenu(&app, f.window, MDS_CMD_PHYSICS_DISABLED, 0);
    const auto disabled = body->getWorldTransform();
    step();
    Check(body->getWorldTransform() == disabled, "disabled physics must not advance bodies");
    CmdViewMenu(&app, f.window, MDS_CMD_PHYSICS_ALWAYS, 0);
    SeekModelFrame(first, 12, 1);
    SeekModelFrame(second, 12, 1);
    app.PhysicsResetPending() = 1;
    step();
    Check(app.PhysicsResetPending() == 0, "seek must settle before dynamic readback");

    f.Pick(panel::kMainComboModel, 2);
    ApplyModelComboSelection(&app);
    Check(app.SelectedModel() == &second && app.Physics()->world == world &&
              world->getNumConstraints() == jointCount &&
              world->getNumCollisionObjects() == bodyCount,
          "model selection must preserve the shared physics world");
    f.Pick(panel::kMainComboModel, 1);
    ApplyModelComboSelection(&app);
    app.CurrentFrame() = 12;
    f.Save(L"physics-reset.pmm");
    for (int reload = 0; reload < 2; ++reload) {
        f.Reload(L"physics-reset.pmm");
        Check(app.PhysicsResetPending() == 1 && world->getNumConstraints() == jointCount &&
                  world->getNumCollisionObjects() == bodyCount,
              "PMM reload must replace bodies and joints without leaking them");
        Check(app.ModelSlot(0) && app.ModelSlot(kModelSlotCount - 1),
              "PMM reload must preserve sparse model slots");
        for (int slot : {0, kModelSlotCount - 1}) {
            auto& model = *app.ModelSlot(slot);
            for (unsigned i = 0; i < model.rigidCount; ++i) {
                const auto& rigid = *model.rigidTable[i].body;
                Check(rigid.getLinearVelocity() == btVector3(0, 0, 0) &&
                          rigid.getAngularVelocity() == btVector3(0, 0, 0),
                      "PMM reload must create bodies with zero velocity");
            }
        }
        step();
        Check(app.PhysicsResetPending() == 0, "PMM reset was not consumed");
        Capture(*app.ModelSlot(0));
        Capture(*app.ModelSlot(kModelSlotCount - 1));
    }
    ReleaseSceneModels(app);
    Check(world->getNumConstraints() == 0 && world->getNumCollisionObjects() == 1,
          "model release must leave only the ground body");
    std::puts("physics stop/seek/mode/model/PMM lifecycle passed");
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetEnvironmentVariableW(L"OMP_NUM_THREADS", L"1");
    SetWindowsHookExA(WH_CBT, CloseUnexpectedDialog, nullptr, GetCurrentThreadId());
    std::filesystem::path model, original, report;
    int frames = 600;
    bool stages = false, kernels = false;
    try {
        for (int i = 1; i < argc; i += 2) {
            Check(i + 1 < argc, "missing option value");
            if (std::strcmp(argv[i], "--model") == 0)
                model = std::filesystem::absolute(argv[i + 1]);
            else if (std::strcmp(argv[i], "--original") == 0)
                original = std::filesystem::absolute(argv[i + 1]);
            else if (std::strcmp(argv[i], "--report") == 0)
                report = std::filesystem::absolute(argv[i + 1]);
            else if (std::strcmp(argv[i], "--frames") == 0)
                frames = std::stoi(argv[i + 1]);
            else if (std::strcmp(argv[i], "--kernels") == 0)
                kernels = std::stoi(argv[i + 1]) != 0;
            else if (std::strcmp(argv[i], "--stages") == 0)
                stages = std::stoi(argv[i + 1]) != 0;
            else
                Check(false, "unknown physics trajectory option");
        }
#if defined(_M_X64)
        if (kernels) {
            CheckKernels(original);
            return 0;
        }
#else
        Check(!kernels, "collision oracle digest is x64 only");
#endif
        Check(frames > 0, "frame count must be positive");
        Check(!stages || !original.empty(), "stage comparison requires --original");
        Check(report.empty() || !original.empty(), "comparison report requires --original");
        bool matched = true;
        const auto first = Run(model, original, frames, stages, report, &matched);
        const auto second = Run(model, {}, frames, false);
        for (int i = 0; i < frames; ++i)
            Compare(first[i], second[i], i, "fresh reload", 0);
        CheckLifecycle();
        if (!model.empty())
            CheckLifecycle(model);
        Check(matched, "reference trajectory exceeded tolerance; see the complete report");
        Check(dialogs == 0, "unexpected dialog in physics fixture");
        std::puts("physics trajectory and fresh reload passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "physics trajectory: %s\n", error.what());
        return std::strcmp(error.what(), "D3D9 fixture unavailable") == 0 ? 77 : 1;
    }
}
