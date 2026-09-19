// ===========================================================================
// x64 0x7FF7CB44C12E..0x7FF7CB44C656 - Kinect 骨架驱动泵块（行为级移植）
// ===========================================================================
// 位置：x64 泵函数 sub_7FF7CB4474F0（FrameDriver）内，物理帧区段里
// “settle 上升沿请求”（0x7FF7CB44C12E，已移植在 physics_frame.cpp）与
// gate B（0x7FF7CB44C65B，accessoryEditDialogOpen 早退，同在
// physics_frame.cpp）之间。本文件承接其后半段 0x7FF7CB44C141..0x7FF7CB44C656
// （settle 请求本身不重复移植）。
//
// 门（0x7FF7CB44C106..0x7FF7CB44C128）：var_1784(selActive，即泵前部
// 0x7FF7CB44A344 经 [app+0xA1360] = ?OpenNIIsTracking@@YGXPA_N@Z 回填写入的
// 跟踪标志，port 里就是 FrameDriver 的 selActive 局部量)
//   && [app+0x9FC98]==0（frameStepPlayback / x86 0x9ED90）
//   && [app+0x328]==0（optflag[0] 相机模式 / x86 0x2F8）。
// 无 DxOpenNI.dll 时 selActive 恒 0，整块为死代码——与原版一致。
//
// 块内流程（x64 定谳）：
//   0x7FF7CB44C12E  selectionActiveLatch(x64 0xA137C)==0 -> settle=1
//                  （第一置位点；physics_frame.cpp 已移植，此处不重复）
//   0x7FF7CB44C141..0x7FF7CB44C4E7  18~23 次间接调用 [app+0xA1358]
//                  （DxOpenNI 加载器 0x7FF7CB4C6CE1 存入的
//                  ?OpenNIGetSkeltonJointPosition@@YGXHPAUD3DXVECTOR3@@@Z，
//                  即 OniExportSlot(4)）：(ecx=关节号, rdx=模型+关节槽)，
//                  每帧把 Kinect 关节位置抓进 23 个 vec3 “current 槽”。
//                  槽布局与 ModelVertexHistoryPush 的通道表同一套（x86 浮点
//                  下标 3570..3636 = 字节 14280..14556，x64 0x3B70..0x3C84）。
//   0x7FF7CB44C4EF  ModelVertexHistoryPush(模型, [app+0x360]/10)——x64
//                  0x360 就是帧计数器 framesPerSecond（泵首部
//                  0x7FF7CB4475E5..0x7FF7CB447673 的 1 秒 FPS 统计写入同一
//                  字段，29->30/59->60 吸附与 port 帧驱动一致），即平滑窗口
//                  = FPS/10（30fps -> 3 样本，60fps -> 6 样本，内部钳 30）。
//   0x7FF7CB44C52D  模型+0x3CA0(matMisc / x86 14584，SM-Lip 追踪采样计数)
//                  >= 0x3138(12600) 时：CheckMenuItem(菜单 0x124, MF_UNCHECKED)
//                  + [app+0xA1E14]=0（捕获阶段字节 = state.autoRepeat，
//                  x86 0xA0D68——即菜单 0x124/WM_TIMER 0x65 的同一状态机
//                  字节，13 处读写在双架构间一一对应）。
//   0x7FF7CB44C560  ModelStandardPoseSetup(模型, [0xA1E14]==4,
//                  kinectMirrorEnabled(0xA1370/x86 0xA03DC),
//                  kinectInitLostBone(0xA1371/x86 0xA03DD))——x64
//                  sub_7FF7CB4F3A50 的孪生，port 已在 dialog_helpers.cpp；
//                  返回“刚才是否在录制”，为 0 -> 跳到 gate B（块尾）。
//   0x7FF7CB44C5A4  DisableKinect(app)——x64 sub_7FF7CB4C6F00 = x86 0x42A020
//                  的孪生，port 已在 oni_kinect.cpp。
//   0x7FF7CB44C5CC  RegisterKinectPoseCapture(模型, [app+0x1450]=currentFrame)
//                  ——x64 sub_7FF7CB4F2EB0，本文件新移植。
//   0x7FF7CB44C5D4  CurvePanelRepaint(app)——x64 sub_7FF7CB482BB0 = x86
//                  0x4162xx 孪生，port 已在 misc_dialogs.cpp。
//   0x7FF7CB44C604  SeekModelFrame(模型, currentFrame, PlaybackPhysicsMode)
//                  （[app+0xA1D4C]，x86 0xA0CC4）——port 已有。
//   0x7FF7CB44C609  lastRegisteredFrame(x64 0x9F038，x86 pin 647532)
//                  = max(自身, 模型->maxFrame(x64 0x354C/x86 12720))。
//   0x7FF7CB44C632  settle(0x9FCC1/x86 0x9EDB5)=1（第二置位点）、
//                  kinectCaptureActive(0xA1373/x86 0xA03DF)=0、
//                  fpsLimit(x64 0xA1904/x86 0xA08E0)=fpsLimitSaved(0xA1374/
//                  x86 0xA03E0)、PanelPaint(0x7FF7CB480EA0 = x86 0x414610)。
//
// 附带移植的两个孪生（仅本泵块调用）：
//   RegisterKinectPoseCapture  = sub_7FF7CB4F2EB0（x64，2971 字节）：
//     清三张关键帧表的 allocated 标记 -> 开 type-2 undo 槽（30 槽环形、
//     指针快照重建）-> 按骨骼名逐个登记整段姿态追踪缓冲为连续关键帧。
//   RegisterTraceBoneKey       = sub_7FF7CB4F23E0（x64，2761 字节）：
//     单骨骼：把 308 字节/样本的追踪记录逐样本写入 60 字节骨骼关键帧
//     （有序双链插入 + 插值预设 20/20/107/107 + undo 快照）。
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <new>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// 既有孪生（定义见各自文件）。
void ModelVertexHistoryPush(unsigned char* model, int samples);   // dialog_helpers.cpp, x86 0x4B75C0
char ModelStandardPoseSetup(unsigned char* model, bool recordEnable,     // dialog_helpers.cpp, x64 0x4F3A50
                            unsigned char mirrorLeftRight,
                            unsigned char skeletonFlag);
void CurvePanelRepaint(MMDApp* app);                                // misc_dialogs.cpp, x64 0x482BB0

namespace {

// ---- 追踪缓冲（0x3B3760 字节，308 字节/样本，dialog_helpers.cpp 分配）-------
// 指针存模型 +8620（x64 0x21E8），游标为 matMisc（x64 0x3CA0/x86 14584）。
unsigned char* PoseTraceBuffer(unsigned char* model) {
    return static_cast<unsigned char*>(mdl::PoseTraceBuffer(model));
}

// ---- 骨骼名常量（SJIS 字节，.rdata 地址锚点）------------------------------
const unsigned char kCenter[9] =       // 0x551F68 センター
    {0x83, 0x5A, 0x83, 0x93, 0x83, 0x65, 0x81, 0x5B, 0x00};
const unsigned char kUpperBody[7] =    // 0x5524E8 上半身
    {0x8F, 0xE3, 0x94, 0xBC, 0x90, 0x67, 0x00};
const unsigned char kNeck[3] =         // 0x5524F0 首
    {0x8E, 0xF1, 0x00};
const unsigned char kArmL[5] =         // 0x54D70C 左腕
    {0x8D, 0xB6, 0x98, 0x72, 0x00};
const unsigned char kArmR[5] =         // 0x54D724 右腕
    {0x89, 0x45, 0x98, 0x72, 0x00};
const unsigned char kElbowL[7] =       // 0x54D714 左ひじ
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB6, 0x00};
const unsigned char kElbowR[7] =       // 0x54D72C 右ひじ
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB6, 0x00};
const unsigned char kLowerBody[7] =    // 0x5524F4 下半身
    {0x89, 0xBA, 0x94, 0xBC, 0x90, 0x67, 0x00};
const unsigned char kLegL[5] =         // 0x5524FC 左足
    {0x8D, 0xB6, 0x91, 0xAB, 0x00};
const unsigned char kLegR[5] =         // 0x552504 右足
    {0x89, 0x45, 0x91, 0xAB, 0x00};
const unsigned char kLegIkL[9] =       // 0x552510 左足ＩＫ
    {0x8D, 0xB6, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};
const unsigned char kLegIkR[9] =       // 0x552520 右足ＩＫ
    {0x89, 0x45, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};
const unsigned char kKneeL[7] =        // 0x5523CC 左ひざ
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB4, 0x00};
const unsigned char kKneeR[7] =        // 0x5523C4 右ひざ
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB4, 0x00};
const unsigned char kWristL[7] =       // 0x54D71C 左手首
    {0x8D, 0xB6, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};
const unsigned char kWristR[7] =       // 0x54D734 右手首
    {0x89, 0x45, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};
const unsigned char kShoulderL[5] =    // 0x55252C 左肩
    {0x8D, 0xB6, 0x8C, 0xA8, 0x00};
const unsigned char kShoulderR[5] =    // 0x552534 右肩
    {0x89, 0x45, 0x8C, 0xA8, 0x00};

// 按名找骨（0x7FF7CB4F3271 式线性扫描，首个命中）。
int FindBone(unsigned char* model, const void* name, std::size_t bytes) {
    mdl::BoneRecord* bones = mdl::Bones(model);
    const int count = static_cast<int>(mdl::Mdl(model)->boneCount);
    for (int i = 0; i < count; ++i)
        if (std::memcmp(bones[i].name, name, bytes) == 0)
            return i;
    return -1;
}

// x64 0x7FF7CB4F2591 / 0x7FF7CB4F2E35 的满表报错框（沿用 key_registrars /
// model_keyframe_edit 里已定谳的 EN/JP 双分支：模型 physicsFlags 字节选语种）。
void TraceKeyOverflowBox(unsigned char* model) {
    static const char kJpOverflow[] =
        "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67\x90\x94\x82\xaa%d"
        "\x8c\xc2\x82\xf0\x89\x7a\x82\xa6\x82\xdc\x82\xb5\x82\xbd\n"
        "\x82\xb1\x82\xea\x88\xc8\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d"
        "\x73\x82\xa6\x82\xdc\x82\xb9\x82\xf1\n"
        "\x81\x75\xcc\xda\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75\x95"
        "\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x81\x76\x82\xf0\x8e\xfc"
        "\x8d\x73\x82\xb5\x82\xc4\x89\xba\x82\xb3\x82\xa2";
    static const char kJpTitle[] = "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";
    char text[0x100];
    if (mdl::Mdl(model)->physicsFlags != 0) {
        sprintf_s(text, sizeof(text),
                  "You cannot regist over %d point\n"
                  "Please execute 'delete unused frame'",
                  static_cast<int>(mdl::kBoneKeyCapacity));
        MessageBoxA(*reinterpret_cast<HWND*>(model), text, "register frame", 0);
    } else {
        sprintf_s(text, sizeof(text), kJpOverflow,
                  static_cast<int>(mdl::kBoneKeyCapacity));
        MessageBoxA(*reinterpret_cast<HWND*>(model), text, kJpTitle, 0);
    }
}

// 把一条 60 字节骨骼关键帧按 mode 从追踪记录填充（switch a4 的三分支 +
// 插值预设 0x14/0x14/0x6B/0x6B + allocated 置位）。
//   mode 0：仅位置（IK 用），四元数恒等（w=1）。
//   mode 1：仅旋转，位置清零。
//   mode 2：位置取记录 +0..12，四元数取记录 +16..32。
// 记录内组基址 = 4*slot 字节（x64 的 i = 4*a3+8，读取窗 i-8..i+16）。
void FillTraceKey(mdl::BoneKey& record, const unsigned char* traceGroup,
                  int mode) {
    switch (mode) {
    case 0:
        std::memcpy(record.position, traceGroup, sizeof(record.position));
        record.rotation[0] = 0.0f;
        record.rotation[1] = 0.0f;
        record.rotation[2] = 0.0f;
        record.rotation[3] = 1.0f;                                // 0x3F800000
        break;
    case 1:
        record.position[0] = 0.0f;
        record.position[1] = 0.0f;
        record.position[2] = 0.0f;
        std::memcpy(record.rotation, traceGroup, sizeof(record.rotation));
        break;
    default:  // case 2
        std::memcpy(record.position, traceGroup, sizeof(record.position));
        std::memcpy(record.rotation, traceGroup + 16, sizeof(record.rotation));
        break;
    }
    // 0x7FF7CB4F2732..0x7FF7CB4F2FD：字节 +12..27 = 20,20,20,20,
    // 20,20,20,20,107,107,107,107,107,107,107,107（四条插值曲线全
    // {x1=y1=20, x2=y2=107}）。
    for (int lane = 0; lane < 4; ++lane) {
        record.interpolation[4 * lane + 0] = 20;
        record.interpolation[4 * lane + 1] = 20;
        record.interpolation[4 * lane + 2] = 107;
        record.interpolation[4 * lane + 3] = 107;
    }
    record.allocated = 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// x64 sub_7FF7CB4F23E0 - RegisterTraceBoneKey(model, boneIdx, slot, mode,
// frame)：把单条骨骼的整段姿态追踪（308 字节/样本，matMisc 个样本）登记成
// frame 起始的连续骨骼关键帧。返回 false = 满表（报错框已弹）。
// ---------------------------------------------------------------------------
bool RegisterTraceBoneKey(unsigned char* model, unsigned boneIdx, int slot,
                          int mode, unsigned frame) {
    mdl::BoneKey* const keys = mdl::BoneKeys(model);
    const std::int32_t boneCount =
        static_cast<std::int32_t>(mdl::Mdl(model)->boneCount);

    // 0x7FF7CB4F2411..0x7FF7CB4F244F：从链根（记录号 = 骨号）沿 next 走到
    // frame 前驱，与 RegisterBonePoseAtFrame 的游走同型。
    unsigned current = boneIdx;
    if (keys[current].frame < frame) {
        for (;;) {
            const unsigned next = keys[current].next;
            if (next == 0)
                break;
            if (keys[next].frame >= frame)
                break;
            current = next;
        }
    }

    // 0x7FF7CB4F2451..0x7FF7CB4F2499：空闲槽扫描从 boneCount 起步（不是
    // 持久游标——本函数不复位 searchCursor）。
    unsigned freeSlot = static_cast<unsigned>(boneCount);
    if (keys[freeSlot].frame != 0) {
        for (;;) {
            ++freeSlot;
            if (freeSlot >= mdl::kBoneKeyCapacity) {
                TraceKeyOverflowBox(model);
                return false;
            }
            if (keys[freeSlot].frame == 0)
                break;
        }
    }

    unsigned successor = keys[current].next;
    const unsigned sampleCount =
        static_cast<unsigned>(mdl::Mdl(model)->matMisc);
    if (sampleCount == 0)
        return true;                            // 0x7FF7CB4F24C4（无样本）

    const unsigned char* const trace = PoseTraceBuffer(model);
    unsigned sampleFrame = frame;
    for (std::size_t group = 4 * static_cast<std::size_t>(slot);;
         group += 308, ++sampleFrame) {
        // 0x7FF7CB4F24D7..0x7FF7CB4F24EC：先快照三个将被触碰的记录。
        AppendBoneKeyToUndo(model, static_cast<int>(current));      // 0x49D410
        AppendBoneKeyToUndo(model, static_cast<int>(successor));
        AppendBoneKeyToUndo(model, static_cast<int>(freeSlot));

        if (keys[current].frame == sampleFrame) {
            // 0x7FF7CB4F2503：当前记录就是目标帧——脱链后原位覆写（尾部
            // 的 successor 重接会恢复链）。
            keys[current].next = 0;
            FillTraceKey(keys[current], trace + group, mode);
        } else if (keys[successor].frame == sampleFrame) {
            // 0x7FF7CB4F2813：后继记录就是目标帧——接回双链后覆写。
            keys[current].next = successor;
            keys[successor].previous = current;
            FillTraceKey(keys[successor], trace + group, mode);
            current = successor;
            successor = keys[current].next;
        } else {
            // 0x7FF7CB4F2AD3：在空闲槽上开新记录前插，然后为下一轮推进
            // 空闲槽（0x7FF7CB4F2D88..0x7FF7CB4F2DC0；原版的 v12/v15 在
            // 此处始终同值）。
            keys[current].next = freeSlot;
            keys[freeSlot].previous = current;
            keys[freeSlot].frame = sampleFrame;
            keys[freeSlot].next = 0;
            FillTraceKey(keys[freeSlot], trace + group, mode);
            current = freeSlot;
            ++freeSlot;
            if (keys[freeSlot].frame != 0) {
                for (;;) {
                    ++freeSlot;
                    if (freeSlot >= mdl::kBoneKeyCapacity) {
                        TraceKeyOverflowBox(model);
                        return false;
                    }
                    if (keys[freeSlot].frame == 0)
                        break;
                }
            }
        }

        // 0x7FF7CB4F2DC9：maxFrame 逐样本推进（RAW frame 值，原版 quirk）。
        if (sampleFrame > mdl::Mdl(model)->maxFrame)
            mdl::Mdl(model)->maxFrame = sampleFrame;
        if (successor != 0) {
            keys[current].next = successor;        // 0x7FF7CB4F2DE4
            keys[successor].previous = current;    // 0x7FF7CB4F2DF7
        }
        // 0x7FF7CB4F2DFF..0x7FF7CB4F2E15：帧号递增，样本计数满即收工。
        if (sampleFrame + 1 - frame >= sampleCount)
            return true;
    }
}

// ---------------------------------------------------------------------------
// x64 sub_7FF7CB4F2EB0 - RegisterKinectPoseCapture(model, frame)：Kinect
// 捕获收尾——清三张关键帧表的 allocated 标记，开一个 type-2 undo 槽并重建
// 两份快照缓冲，然后按标准骨骼名单把整段追踪落成关键帧；最后释放追踪缓冲。
// 返回值为原版遗留 eax，泵块不消费。
// ---------------------------------------------------------------------------
bool RegisterKinectPoseCapture(unsigned char* model, unsigned frame) {
    HWND hwnd = *reinterpret_cast<HWND*>(model);

    // 0x7FF7CB4F2EC1..0x7FF7CB4F2F5F：三张表全量 allocated 清零
    // （骨骼 600000/x86 300000、表情 20000、表示 1000；原版按
    // “每轮清 2~3 条记录”的展开循环写，效果一致）。
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i)
        mdl::BoneKeys(model)[i].allocated = 0;
    for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i)
        mdl::MorphKeys(model)[i].allocated = 0;
    for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i)
        mdl::DisplayKeys(model)[i].allocated = 0;

    const std::int32_t samples = mdl::Mdl(model)->matMisc;
    if (samples > 0) {                             // 0x7FF7CB4F2F72 反门
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);    // 400
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);   // 401

        // 0x7FF7CB4F2FB2..0x7FF7CB4F2FD8：undo 环游标 +1、30 槽回卷、脏位
        // 置位（word 写 = undoDirty=1 且 redoDirty=0）。
        if (++mdl::Mdl(model)->undoState[0] >= 30u)
            mdl::Mdl(model)->undoState[0] = 0;
        const int cursor = mdl::Mdl(model)->undoState[0];
        mdl::Mdl(model)->undoState[1] = cursor;
        mdl::Mdl(model)->undoDirty = 1;
        mdl::Mdl(model)->redoDirty = 0;
        mdl::UndoRecord& undo = mdl::Mdl(model)->undoRings[0].slots[cursor];
        undo.operation = 2;
        undo.frame = frame;

        // 0x7FF7CB4F300A..0x7FF7CB4F318C：姿态快照（boneCount x 36 字节
        // {骨号, trans, rotQuat, 标志字节}；x64 源为 bone+328..356 = trans/
        // rotQuat，标志字节数组 x64 0x3128——沿用 frame_line_edit.cpp 对
        // 同族快照（x86 0x43A 段）已核定的 boneSelection 选择字节数组）。
        ::operator delete(undo.bonePose);
        undo.bonePose = nullptr;
        const std::int32_t boneCount =
            static_cast<std::int32_t>(mdl::Mdl(model)->boneCount);
        undo.bonePose = static_cast<mdl::BonePoseSnapshot*>(
            ::operator new(sizeof(mdl::BonePoseSnapshot) * boneCount));
        std::memset(undo.bonePose, 0, sizeof(mdl::BonePoseSnapshot) * boneCount);
        mdl::BoneRecord* const bones = mdl::Bones(model);
        unsigned char* const flags = mdl::Mdl(model)->boneSelection;
        for (std::int32_t i = 0; i < boneCount; ++i) {
            undo.bonePose[i].boneIndex = i;
            std::memcpy(undo.bonePose[i].position, bones[i].trans,
                        sizeof(undo.bonePose[i].position));
            std::memcpy(undo.bonePose[i].rotation, bones[i].rotQuat,
                        sizeof(undo.bonePose[i].rotation));
            undo.bonePose[i].physicsDisabled = flags[i];
        }

        // 0x7FF7CB4F319D..0x7FF7CB4F323D：64 字节/条的关键帧快照缓冲
        // （3456 = 54 条 x 64 字节/样本），计数字段清零；keyVisitMap 全清。
        undo.dirty = 0;
        ::operator delete(undo.auxiliaryPose);
        undo.auxiliaryPose = nullptr;
        undo.auxiliaryPose =
            ::operator new(3456 * static_cast<std::size_t>(samples));
        std::memset(undo.auxiliaryPose, 0, 3456 * static_cast<std::size_t>(samples));
        std::memset(mdl::Mdl(model)->keyVisitMap, 0, mdl::kBoneKeyCapacity);

        // 名单驱动的逐骨登记（0x7FF7CB4F3247..0x7FF7CB4F3A16）。任何一骨
        // 满表即刻返回（原版不做清理）。
        const std::uint8_t version = mdl::Mdl(model)->openniVersion;
        int idx;
        if ((idx = FindBone(model, kCenter, 9)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 0, 0, frame))
            return false;                                         // センター
        if ((idx = FindBone(model, kUpperBody, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 3, 1, frame))
            return false;                                         // 上半身
        if (version >= 14 &&
            (idx = FindBone(model, kNeck, 3)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 7, 1, frame))
            return false;                                         // 首（1.40+）
        if ((idx = FindBone(model, kArmL, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 11, 1, frame))
            return false;                                         // 左腕
        if ((idx = FindBone(model, kElbowL, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 15, 1, frame))
            return false;                                         // 左ひじ
        if ((idx = FindBone(model, kArmR, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 19, 1, frame))
            return false;                                         // 右腕
        if ((idx = FindBone(model, kElbowR, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 23, 1, frame))
            return false;                                         // 右ひじ
        if ((idx = FindBone(model, kLowerBody, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 27, 1, frame))
            return false;                                         // 下半身
        if ((idx = FindBone(model, kLegL, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 31, 1, frame))
            return false;                                         // 左足
        if ((idx = FindBone(model, kLegR, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 35, 1, frame))
            return false;                                         // 右足

        // 0x7FF7CB4F361C..0x7FF7CB4F3856：足ＩＫ二选一——IK 链表里名字为
        // 左足ＩＫ/右足ＩＫ 且 enabled 的链存在 -> 登记足ＩＫ骨（mode 2，
        // 带位置）；否则登记ひざ骨（mode 1）。
        bool legIkLeftEnabled = false;
        bool legIkRightEnabled = false;
        mdl::IkChain* const chains = mdl::IkChains(model);
        const std::int32_t chainCount =
            static_cast<std::int32_t>(mdl::Mdl(model)->ikChainCount);
        for (std::int32_t i = 0; i < chainCount; ++i) {
            if (std::memcmp(bones[chains[i].boneIndex].name, kLegIkL, 9) == 0)
                legIkLeftEnabled = chains[i].enabled != 0;
            if (std::memcmp(bones[chains[i].boneIndex].name, kLegIkR, 9) == 0)
                legIkRightEnabled = chains[i].enabled != 0;
        }
        if (legIkLeftEnabled) {
            if ((idx = FindBone(model, kLegIkL, 9)) >= 0 &&
                !RegisterTraceBoneKey(model, idx, 47, 2, frame))
                return false;
        } else {
            if ((idx = FindBone(model, kKneeL, 7)) >= 0 &&
                !RegisterTraceBoneKey(model, idx, 39, 1, frame))
                return false;
        }
        if (legIkRightEnabled) {
            if ((idx = FindBone(model, kLegIkR, 9)) >= 0 &&
                !RegisterTraceBoneKey(model, idx, 54, 2, frame))
                return false;
        } else {
            if ((idx = FindBone(model, kKneeR, 7)) >= 0 &&
                !RegisterTraceBoneKey(model, idx, 43, 1, frame))
                return false;
        }

        if (version >= 14 &&
            (idx = FindBone(model, kWristL, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 61, 1, frame))
            return false;                                         // 左手首（1.40+）
        if (version >= 14 &&
            (idx = FindBone(model, kWristR, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 65, 1, frame))
            return false;                                         // 右手首（1.40+）
        if (version >= 15 &&
            (idx = FindBone(model, kShoulderL, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 69, 1, frame))
            return false;                                         // 左肩（1.50+）
        if (version >= 15 &&
            (idx = FindBone(model, kShoulderR, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 73, 1, frame))
            return false;                                         // 右肩（1.50+）
    }  // samples > 0；满表路径原样直返、不做下面的清理（原版行为）

    // 0x7FF7CB4F3A18..0x7FF7CB4F3A31：释放追踪缓冲、采样计数清零。
    ::operator delete(PoseTraceBuffer(model));
    mdl::PoseTraceBuffer(model) = nullptr;
    mdl::Mdl(model)->matMisc = 0;
    return true;
}

// ---------------------------------------------------------------------------
// x64 0x7FF7CB44C141..0x7FF7CB44C656 - PumpKinectSkeleton：泵块主体。
// 调用点在 frame_driver.cpp（PlaybackCatchup 之后、PhysicsFrame 之前；
// 原版物理帧内部 settle 请求已由 physics_frame.cpp 移植，见文件头注释）。
// ---------------------------------------------------------------------------
void PumpKinectSkeleton(MMDApp* app, unsigned char selActive) {
    auto& s = *app;

    // 0x7FF7CB44C106..0x7FF7CB44C128 三重门。
    if (selActive == 0 ||
        s.state.optflag[0] != 0 ||            // x64 0x328（相机模式）
        s.state.frameStepPlayback != 0)       // x64 0x9FC98（x86 0x9ED90）
        return;

    unsigned char* model = s.SelectedModel();  // 槽号 x64 0x13E0 / 表 0xBE8

    // ① 关节抓取（0x7FF7CB44C141..0x7FF7CB44C4E7）：OpenNI 关节号 -> 槽
    // 浮点下标（字节 = 14280 + 12*槽序，与 ModelVertexHistoryPush 的通道表
    // 同源）。前 18 个无条件；1.40 版补 2 个；1.50 版补 5 个。
    auto getJoint = reinterpret_cast<void (__stdcall*)(int, float*)>(
        s.OniExportSlot(4));  // x64 [app+0xA1358]
    if (model == nullptr || getJoint == nullptr)
        return;
    mdl::CaptureSkeletonJoints(mdl::Mdl(model)->currentJoints,
                               s.state.openniVersion, getJoint);

    // ② 滚动均值平滑（0x7FF7CB44C4EF）：窗口 = framesPerSecond/10。
    ModelVertexHistoryPush(
        model, s.state.framesPerSecond / 10);    // [app+0x360]/10

    // ③ SM-Lip 采样满 12600 -> 关掉自动帧记录菜单并复位捕获阶段
    // （0x7FF7CB44C51C..0x7FF7CB44C558）。
    if (static_cast<std::uint32_t>(mdl::Mdl(model)->matMisc) >= 12600) {
        CheckMenuItem(GetMenu(static_cast<HWND>(s.Hwnd())), 0x124,
                      MF_UNCHECKED);
        s.state.autoRepeat = 0;                  // x64 0xA1E14 / x86 0xA0D68
    }

    // ④ 标准姿态装填 + 录制沿检测（0x7FF7CB44C560..0x7FF7CB44C59B）：
    // 返回 0（本帧没有结束一段录制）-> 直接落到 gate B。
    const char wasRecording = ModelStandardPoseSetup(
        model,
        s.state.autoRepeat == 4 ? 1 : 0,         // dl = (阶段==4)
        s.state.kinectMirrorEnabled,             // r8 = 0xA1370
        s.state.kinectInitLostBone);             // r9 = 0xA1371
    if (wasRecording == 0)
        return;

    // ⑤ 停用 Kinect 并把整段捕获落成关键帧（0x7FF7CB44C5A1..0x7FF7CB44C604）。
    DisableKinect(app);                                          // 0x42A020
    if (model != nullptr)
        RegisterKinectPoseCapture(
            model, static_cast<unsigned>(s.state.currentFrame));  // [0x1450]
    CurvePanelRepaint(app);                            // 0x7FF7CB482BB0
    if (model != nullptr)
        SeekModelFrame(model, s.state.currentFrame,
                      s.PlaybackPhysicsMode());       // 0x7FF7CB4EBD90

    // ⑥ 收尾（0x7FF7CB44C609..0x7FF7CB44C656）。
    if (s.state.lastRegisteredFrame < mdl::Mdl(model)->maxFrame)
        s.state.lastRegisteredFrame = mdl::Mdl(model)->maxFrame;
    s.PhysicsResetPending() = 1;                  // 0x9FCC1（第二置位点）
    s.state.kinectCaptureActive = 0;              // 0xA1373 / x86 0xA03DF
    s.state.fpsLimit = s.state.fpsLimitSaved;     // [0xA1904] = [0xA1374]
    PanelPaint(app);                              // 0x7FF7CB480EA0
}

// ---------------------------------------------------------------------------
// x86 0x46DCCF..0x46DD61 / x64 0x7FF7CB44A34C..0x7FF7CB44A3EC -
// ManageKinectRecordGate：泵序言里 OpenNIIsTracking 探测（frame_driver.cpp
// 的 selActive 块）的紧后处理。调用方已保证门条件（Kinect 已启用且非逐帧
// 回放）；本函数无返回值。
//
//   ① 深度图绘制请求（0x46DCCF/0x7FF7CB44A34C）：
//      OpenNIDrawDepthMap(bool)（导出槽 2，x86 [app+0xA03C8] /
//      x64 [app+0xA1348]）——kinectCaptureActive(0xA03DF)与捕获阶段
//      (0xA0D68/0xA1E14)都为 0（完全闲置）时传 0，否则传 1。
//   ② 菜单 0x124 卫生（0x46DCFE..0x46DD61/0x7FF7CB44A37C..0x7FF7CB44A3E4）：
//      selActive && 非相机模式(app+0x2F8/0x328) -> EnableMenuItem(0x124,
//      MF_ENABLED(0))；否则 EnableMenuItem(0x124, MF_GRAYED(1)) +
//      CheckMenuItem(0x124, MF_UNCHECKED) + 捕获阶段清零（丢失跟踪时把
//      自动帧录制的菜单掐灭并复位状态机；DisableKinect 0x42A044 同款）。
// ---------------------------------------------------------------------------
void ManageKinectRecordGate(MMDApp* app, unsigned char selActive) {
    auto& s = *app;

    // ① 深度图：闲置（既不在捕获文件驱动下、也不在录制状态机的任一
    // 阶段）时不画。
    auto drawDepthMap = reinterpret_cast<void (__stdcall*)(unsigned char)>(
        s.OniExportSlot(2));                      // x64 [app+0xA1348]
    if (drawDepthMap != nullptr)
        drawDepthMap(
            (s.state.kinectCaptureActive == 0 &&   // x86 0xA03DF
             s.state.autoRepeat == 0)              // x86 0xA0D68
                ? 0
                : 1);

    // ② 菜单 0x124：跟踪中且非相机模式才可用。
    HMENU menu = GetMenu(static_cast<HWND>(s.Hwnd()));
    if (selActive != 0 && s.state.optflag[0] == 0) {
        EnableMenuItem(menu, 0x124, 0);           // MF_ENABLED
        return;
    }
    EnableMenuItem(menu, 0x124, 1);               // MF_GRAYED
    CheckMenuItem(menu, 0x124, MF_UNCHECKED);
    s.state.autoRepeat = 0;                       // x64 0xA1E14 / x86 0xA0D68
}

}  // namespace mikudancestudio
