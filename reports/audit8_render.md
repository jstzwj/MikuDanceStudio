# 第 8 轮审计 —— 子审计：渲染管线与媒体录制

- 日期：2026-09-15
- 审计员：render/media 子审计（IDA database `e02eac62`，x64 原版，基址 0x7FF7CB420000）
- 范围：src/render/{d3d_init,frame_scene,render_states,model_renderers,toon_textures,accessory,bg_overlay,capture_downsample,device_reset,draw_glyph,fill_panel,sprite_overlay,overlay_producers,scene_font,stereo_nvapi,debug_geometry}.cpp、fx_slots.hpp、src/media/{media_load,wave_audio}.cpp、src/app/{avi_record_start,dshow_record_graph,record_readback}.cpp、src/mmdxshow/（抽样）
- 方法：全部以工作树为准；对帧包络（sub_7FF7CB4474F0）、InitD3D（sub_7FF7CB427A30）、InitRenderStates（sub_7FF7CB428550）、固定帧（sub_7FF7CB4BFB20）、固定函数材质循环（sub_7FF7CB4D6D70）、toon 初始化（sub_7FF7CB4BA130）、PostDeviceReset（sub_7FF7CB4C6160）、DShow 图构建（sub_7FF7CB42AAB0）做了逐指令/反编译核对。

## 已核对一致（抽样结论，未列发现）

以下重点项经字节级核对与原版一致，不计入发现：
- 帧包络：Clear 时机与参数（0x7FF7CB44A2B9：d3dInitialized→flags=7 否则 3；app+0xA1104→黑色 0 否则 0x00FFFFFF；Z=1.0、stencil=0）；两道门（[0xA1E18] 管 VB+Clear、[0xA166D] 只管 VB 循环）；passCount=1→BeginScene(0x44A419)→阴影图(0x44A4E0)→pass 循环→自阴影合成（0x44A5CD，ALPHABLEND 前后包夹、FVF 0x144、2 个三角形）→深度回调四边形（0x44A6B5）→录制窗下采样/捕获→文本→线（先 SetTexture(0,NULL)，0x44AF44）→精灵→EndScene→逐帧录制回读（0x44B073 起，CreateRenderTarget/CreateOffscreenPlainSurface 懒建、立体加宽 capW = 立体倍率×渲染宽）。
- toon 初始化 sub_7FF7CB4BA130：11 槽释放顺序、槽 0 = 资源 0x67 PNG、30 项边色表默认值位型（0x3F4CFFFC 等，移植用 0.80078101 等十进制字面量四舍五入到同一位型）、toon1..10 顺序加载、失败回退资源 i+103、成功时 LockRect 取**最底行最左像素**按 B,G,R 写 3(N-1)..3(N-1)+2（0x7FF7CB4BA522..567）。**未发现任何 toon02 专属暗化分支**——"toon02 变暗"的效果全部来自该表默认值本身（t[3..5]=0x3F74FFFC/0x3F58FFFC/0x3F58FFFC），移植已按位复现。
- d3d_init sub_7FF7CB427A30：后台缓冲格式探测 21→22、深度阶梯 75/77/80、MSAA 8/4/2/1 双格式探测 + quality-1、SwapEffect 3 回退、CreateDevice 7 级阶梯、caps +0x6C=MaxAnisotropy 镜像（0x7FF7CB4280E1）、VS/PS≥2 门、HDR RT = **D3DXCreateTexture(w,h,1,RENDERTARGET,fmt 114=R32F,POOL_DEFAULT)**、精灵 PNG、深度面 77、效果资源 118(SM3)/117(SM2)、SM2 分支 GetLevelDesc==114→SKII1=600 且 [240249]=1 否则 500/0（0x7FF7CB428355..78）、fov/aspect、InitRenderStates 调用点、viewScale=1.0、GetBackBuffer/GetDepthStencil、无 MSAA 时 lockable captureSurface 回退（失败置 240096=1 的怪逻辑）、stereo 探测。
- PostDeviceReset sub_7FF7CB4C6160：11 项资源释放顺序逐一相同；effect OnLostDevice=槽 69；Reset=[240064]+槽 16（0x7FF7CB4C6323）；InitRenderStates；GetMenuState(0x115)&8 → RS(161)（0x7FF7CB4C6378）；OnResetDevice=槽 70；全屏/主窗视口刷新分支；尾部三个字节清零。
- DShow 图 sub_7FF7CB42AAB0：音频 mux 输入脚 QI→IAMStreamControl；StartAt(0,0)（0x7FF7CB42B661 槽 3）；StopAt = seconds×1e7f（movss/mulss dword_7FF7CB552BF8/cvttss2si，0x7FF7CB42B664..74）→ 槽 4；IConfigAviMux::SetMasterStream(1)；IMediaFilter::SetSyncSource(NULL)（槽 8）；IConfigInterleaving::put_Mode(2)（0x7FF7CB42B774）与 put_Interleaving(10000000, 7500000)（0x7FF7CB42B7D2/DA）。
- 帧头/材质循环的 TCI 字面量 0x10000、材质循环 TSS/SamplerState 混用布局（[rax+218h] 与 [rax+228h]，后者=SetSamplerState 槽 69，共享 sampler-2 尾 0x7FF7CB428951）、toon 选择链（-1→槽 0；名字=="toonNN.bmp"→槽 N+1，0x7FF7CB4D79C8 起的 repe cmpsb 链；否则 PMX material+0x4C0 / PMD 目录+文件名缓存查询，缓存查找器 sub_7FF7CB428EB0）。
- record_readback：ack 轮询（GetStreamingState 槽 4 = [vt+0x20]，E_FAIL=流结束）、Present→TestCooperativeLevel 泵→PostDeviceReset、StretchRect LINEAR→NONE 双试、GetRenderTargetData→LockRect(READONLY)→自底向上翻转拷贝→StartStreaming(槽 5=[vt+0x28])→UnlockRect。
- mmdxshow（抽样）：IPushSource 协议槽（+0x0C SetBitmapInfo/0x10001730、+0x10 GetStreamingState/0x100017D0、+0x14 StartStreaming/0x10001800、+0x1C GetRate 写 1.02）与 record_readback 的消费闭环一致；该模块自带完整地址锚点，未重复逐字 diff。

---

## 发现

### [P0] 渲染状态（D3DRENDERSTATETYPE）编号体系性错位：原版二进制全量使用 D3D7/D3D8 时代编号，移植按现代 D3D9 枚举"翻译"，激活了原版从未生效的 stencil/alpha-test/cull/zfunc/blend 开关

- 我们：
  - `src/render/render_states.cpp:125-140`（stencil/blend/alpha-test 段）：
    ```cpp
    if (sub->d3dInitialized != 0) {  // stencil shadow setup
        dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);              // state 22
        dev->SetRenderState(D3DRS_STENCILMASK, 255);                 // state 28
    }
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);       // state 206
    dev->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_MAX);         // state 209, 5
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);          // state 12, 5
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCRCALPHA);    // state 13, 6
    ...
    if (caps.AlphaCmpCaps & D3DPCMPCAPS_GREATEREQUAL) {              // caps+52 & 0x40
        dev->SetRenderState(D3DRS_ALPHAREF, 1);                      // state 16
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);            // state 10
        dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);   // state 17, 7
    }
    ```
  - `src/render/model_renderers.cpp:1380-1384`（帧头 stencil 三连）、`:1192-1200`（SetProjectedStencil，FUNC=GREATER/REF=2/PASS=REPLACE）、`:1426-1432`（描边前导 ALPHABLENDENABLE/CULL_CW/ZFUNC_LESS）、`:1440-1441`（CULL_CCW/ZFUNC_LESSEQUAL）等全部同名枚举。
- 原版（x64 实测立即数，`call qword ptr [rax+1C8h]` = SetRenderState 槽 57）：
  - InitRenderStates sub_7FF7CB428550：
    ```
    0x7FF7CB428A84:  SetRenderState(52,  1)    ; 意图 STENCILENABLE
    0x7FF7CB428A9F:  SetRenderState(58, 255)   ; 意图 STENCILMASK
    0x7FF7CB428ABA:  SetRenderState(206, 1)    ; SEPARATEALPHABLENDENABLE（与 D3D9 相同）
    0x7FF7CB428AD5:  SetRenderState(209, 5)    ; BLENDOPALPHA=MAX（与 D3D9 相同）
    0x7FF7CB428AEE:  SetRenderState(19,  5)    ; 意图 SRCBLEND=SRCALPHA
    0x7FF7CB428B07:  SetRenderState(20,  6)    ; 意图 DESTBLEND=INVSRCALPHA
    0x7FF7CB428B3C:  SetRenderState(24,  1)    ; 意图 ALPHAREF
    0x7FF7CB428B55:  SetRenderState(15,  1)    ; 意图 ALPHATESTENABLE
    0x7FF7CB428B6E:  SetRenderState(25,  7)    ; 意图 ALPHAFUNC=GREATEREQUAL
    ```
    （三连的原始汇编即 `mov edx,18h/0Fh/19h; lea r8d,[rdx-17h/-0Eh/-12h]`）
  - 固定帧头 sub_7FF7CB4BFB20（d3dInitialized 门内）：
    ```
    0x7FF7CB4BFC30:  SetRenderState(56, 8)     ; 意图 STENCILFUNC=ALWAYS
    0x7FF7CB4BFC4F:  SetRenderState(57, 1)     ; 意图 STENCILREF=1
    0x7FF7CB4BFC6E:  SetRenderState(55, 3)     ; 意图 STENCILPASS=REPLACE
    ```
  - 描边前导 0x7FF7CB4C0968/988/9A8：`SetRenderState(137,0)`（LIGHTING，两代相同）、`SetRenderState(27,1)`（意图 ALPHABLENDENABLE）、`SetRenderState(22,2)`（意图 CULLMODE=CW）、`SetRenderState(23,2)`（意图 ZFUNC=LESS）。
  - FILLMODE 用 8、ZENABLE 用 7、LIGHTING 用 137、SPECULARENABLE 用 29 —— 与 d3d7/d3dtypes.h 老编号一致（7=FILLMODE? 否——7=ZENABLE、8=FILLMODE 恰为 D3D7/D3D9 共值），206/209 为后来追加的 D3D9 项。整套编号 = "老 d3dtypes.h + 追加新状态"的自定义头文件。
- 差异与影响：D3D9 运行时下，原版这些调用的**字面落点**与移植完全不同：
  | 原版调用 | D3D9 实际落点 | 原版可观察效果 | 移植效果 |
  |---|---|---|---|
  | RS(52,1)/RS(58,255) | 52/58 = 未定义状态 → INVALIDCALL 无效 | **stencil 从不启用** | STENCILENABLE/MASK 真实生效 |
  | RS(55,3)/RS(56,8)/RS(57,1)（帧头） | 55/56/57 无效 → 无效 | stencil 三连从不生效 | FUNC=ALWAYS/REF=1/PASS=REPLACE 真实生效，几何把 stencil 写为 1 |
  | RS(22,x)/RS(23,x) | 22=STENCILENABLE（值 2/3 非法→无效）、23=STENCILFAIL | **CULLMODE/ZFUNC 永不改**（默认 CCW/LESSEQUAL） | CULL_CW/CCW、ZFUNC LESS/LESSEQUAL 真实切换 |
  | RS(27,x) | 27=STENCILREF | **ALPHABLENDENABLE 所有开关全无效**——只在 init 被 RS(19,5) 误中（19=ALPHABLENDENABLE，值 5→TRUE）一次性开启后恒开 | 关/开切换真实生效（自阴影合成四边形、地面网格、帧尾等） |
  | RS(19,5)/RS(20,6) | 19=ALPHABLENDENABLE=TRUE（副作用）；20 无效 | 混合恒开；SRC/DESTBLEND 保持默认 | SRC=SRCALPHA/DEST=INVSRCALPHA 真实写入 |
  | RS(24,1)/RS(25,7) | 24=STENCILZFAIL=KEEP、25=STENCILPASS=INCR（stencil 关闭下惰性） | **alpha test 从不启用** | ALPHAREF=1/ALPHAFUNC=GEQUAL 真实生效 |
  | RS(15,1)（caps 门内） | 15=**ZFUNC=D3DCMP_NEVER** | 见下方注记 | 移植写的是 ALPHATESTENABLE，ZFUNC 不动 |
  由此产生的可见渲染分歧（按严重度）：
  1. **地面投射影**：移植中 stencil 真实开启后，SetProjectedStencil 的 `FUNC=GREATER / REF=2` 在模型/配件已把 stencil 写为 1、清屏为 0 的情况下**永假**（2>1、2>0 均不成立）→ 投射影（配件+模型剪影）在 d3dInitialized!=0（有 D24S8）的机器上整帧被丢弃、不可见；原版无 stencil，影子按深度+混合正常绘制。
  2. **alpha test**：移植丢弃 alpha<1/255 的纹素（球面/加算纹理的透明边缘出现挖空），原版不丢弃。
  3. **CULLMODE**：移植描边 pass 只画背面（CULL_CW），原版恒 CCW（描边几何正反一起画，靠挤出方向呈现）；半透明材质的 CULL_NONE/CCW 切换同理。
  4. **ALPHABLENDENABLE**：自阴影合成四边形（ComposeSelfShadow 先关混合再开）与地面网格/地面面片（DrawGroundGeometry 关混合画平面）在移植中变为不透明覆盖，原版全程混合开启。
  5. 附带反证（记录备查）：按字面执行，原版 init 的 RS(15,1) 会在支持 GREATEREQUAL 的硬件上把 D3D9 ZFUNC 置为 NEVER 且后续所有"ZFUNC"写（23 号）均无效——该 x64 参考若真按此运行则什么都画不出来；这说明参考构建本身可能从未按字面设备流验证过（或真实 x86 版此处另有差异）。审计只陈述两侧行为分歧，最终取舍需项目裁决。
- 建议（二选一并写入文档 docs/ARCHITECTURE.md，render_states.cpp 头注释所引的该文件目前不存在）：
  1. 若"1:1"指**字面设备流**：引入一个 `LegacyRs()` 帮助函数把语义名映射回原版立即数（STENCILENABLE→52、STENCILMASK→58、STENCILFUNC→56、STENCILREF→57、STENCILPASS→55、CULLMODE→22、ZFUNC→23、ALPHABLENDENABLE→27、SRCBLEND→19、DESTBLEND→20、ALPHAREF→24、ALPHATESTENABLE→15、ALPHAFUNC→25；ZENABLE=7、FILLMODE=8、LIGHTING=137、SPECULARENABLE=29、206/209 不变），让 D3D9 runtime 自然无效化，复现原版可观察行为。
  2. 若维持"作者意图"语义（现状）：至少必须消除被激活后自相矛盾的逻辑——SetProjectedStencil 的 GREATER/REF=2 组合在本移植的 stencil 语义下永假（地面影不可见），应按原版可观察行为退化为不启用 stencil 测试；alpha-test 的开启同样建议对齐原版（不启用）。其余（cull/blend 开关）差异需逐项确认视觉基线后决策。

### [P1] d3d_init：CreateDevice 阶梯缺少原版的第 5 次（重复）尝试

- 我们：`src/render/d3d_init.cpp:274-317`，7 次阶梯：HAL/0x40(fmt21)→fmt22→fmt21+COPY+lockable→fmt22→HAL/0x20→REF/0x40→REF/0x20。
- 原版：sub_7FF7CB427A30 0x7FF7CB427E9D 起共 **8** 次：前三次同移植；`||` 链内第 4 次 fmt=22/0x40 后还有一次**完全同参数**的第 5 次 `CreateDevice(0,HAL,hWnd,0x40,pp)`（0x7FF7CB42802C 一带），然后 HAL/0x20、REF/0x40、REF/0x20。
- 差异与影响：同参数立即重试在确定性失败下行为等价；但严格意义上设备调用次数与失败路径下的重试语义（瞬时失败、驱动竞态）不同。极低概率可观察。
- 建议：补上重复调用（保持调用流 1:1），或在注释中说明有意省略。

### [P2] d3d_init：格式 114 的注释标错为 A16B16G16R16F（实际 114 = R32F）

- 我们：`src/render/d3d_init.cpp:360` `static_cast<D3DFORMAT>(114) /*A16B16G16R16F*/`；`src/render/model_renderers.cpp:1634`（RenderShadowMap 的 `static_cast<D3DFORMAT>(114)` 无标注但同值）。
- 原版：sub_7FF7CB427A30 0x7FF7CB428162 `D3DXCreateTexture(dev, w, h, 1, 1, 114, 0, ...)`；SM2 分支 0x7FF7CB428355 `if (v48[0] == 114)` 判 R32F（'r'）→SKII1=600。114 在 D3DFMT 枚举中是 **R32F**（A16B16G16R16F=113）。
- 差异与影响：代码行为一致（两侧都传 114）；仅注释误导后续维护者，且与 SKII1 判 `Format==D3DFMT_R32F` 的注释链矛盾——读者会以为该分支永假。
- 建议：把两处注释改为 `/*114 = D3DFMT_R32F*/`。

### [P2] frame_scene：录制窗路径下 CaptureAccessoryScreenTexture 的 512 回退未判 hr（与原版一致性无害，但双重回退语义不同）

- 我们：`src/render/frame_scene.cpp:293-301`：`CreateTexture(1024,1024,...)` 失败且 `result==D3DERR_OUTOFVIDEOMEMORY||E_OUTOFMEMORY` 才试 `CreateTexture(512,512,...)`；`texture==nullptr` 才 return。
- 原版：帧驱动 0x46E787 前的捕获段（x64 0x7FF7CB44A7E6..0x7FF7CB44A880）：`CreateTexture(0x400,0x400,...)` 返回值比较的正是 `8876017Ch`(=D3DERR_OUTOFVIDEOMEMORY) 与 `8007000Eh`(=E_OUTOFMEMORY) 后重试 `0x200`。
- 差异与影响：经核对两者一致（列出以确认核对过）。此项**无差异**——真正要记录的是：0x7FF7CB44A7D0 处的门是 `captureMode==1(r14d) 或 ==2` 才进入，`lea rbx,[r12+9FA78h]` 即 CaptureTexture 槽，与移植 `mode != 1 && mode != 2 return` 相同。无发现，撤回。（保留此段作为核对记录。）

### [P2] accessory：AccessoryPlacement 对 parentBone 做了 `std::max(...,0)` 负值钳制，原版无

- 我们：`src/render/accessory.cpp:142` `const int boneIndex = std::max(mdl::Accessory(accessory)->parentBone, 0);`（同文件注释自认 x64 0x7FF7CB4C19F3 只判 `parentModel == -1`、无上下界检查）。
- 原版：sub_7FF7CB4C19F3 一带仅 `cmp dword[r10+240h],-1`；parentBone 直接作骨骼数组下标。
- 差异与影响：parentBone<0 时原版会以负下标越界读骨骼表（依赖 UI 层保证非负）；移植钳到骨骼 0（センター），产生确定性的错误绑定而非潜在越界。防御性偏差，正常数据下无差异。
- 建议：保留钳制但记录为"移植侧防御"；或完全复刻（负值时原版行为未定义，不建议）。

### [P2] toon 纹理 d3dx9 DLL 选择按架构分裂（x64 载 d3dx9_43、x86 载 d3dx9_32）

- 我们：`src/render/toon_textures.cpp:68-69` `LoadLibraryA(sizeof(void*)==8 ? "d3dx9_43.dll" : "d3dx9_32.dll")`。
- 原版（本参考二进制）：`D3DXCreateTextureFromFileInMemoryEx` 等**直接走导入表**（refs 显示为已解析导入），x64 参考静态链接的是 d3dx9_43（导入表既有），x86 原版为 d3dx9_32——DLL 名差异是重建侧事实。
- 差异与影响：运行时动态解析 vs 载入时导入——缺失 DLL 时原版拒绝启动，移植静默降级（InitToonTextures 返回 false），且 d3d_init 把 d3dx 缺失仅当"跳过 SM3/效果路径"。低危、可接受的移植权衡，但行为点不同。
- 建议：在 toon_textures.cpp 的"d3dx9 fidelity"注释中补充"动态解析 + 失败降级"与原版"导入表硬依赖"的差异说明。

### [P3] record_readback 的 DIAG pace 替换原版 ack 节奏（仅诊断构建）

- 我们：`src/app/record_readback.cpp:178-181` `MIKUDANCESTUDIO_REC_PACE_MS` 的 Sleep。
- 原版：仅靠 GetStreamingState ack 轮询限速。
- 差异与影响：仅 MIKUDANCESTUDIO_DIAG 构建；默认 OFF 无差异。
- 建议：无需动作（记录在案）。

### [P3] frame_scene：messageSeen==0 时整函数早退，原版在跳过场景后仍执行帧尾驱动段

- 我们：`src/render/frame_scene.cpp:373-374` `if (app->state.messageSeen == 0) return;`
- 原版：0x7FF7CB44A1A3 `[0xA1E18]==0` 跳到 0x44A2BF——**Clear 之后、BeginScene 门之前**的中段（stereo 收敛更新 0x44A2F2..31C = UpdateFrameStereo、OpenNI 探测+菜单 0x124 卫生 0x44A321..3EC）仍然执行，然后在 0x44A3F1 的同门处跳到帧尾 0x44B073。
- 差异与影响：移植里这两块分别在 `RenderFrameScene` 顶部（UpdateFrameStereo 在门之前调用，等价）与 `frame_driver.cpp` §4（ManageKinectRecordGate，在 RenderFrameScene 之前调用）——执行顺序从"Clear 之后"提前到"Clear 之前/函数之外"，同一帧内、任何绘制调用之前，无可观察渲染差异。菜单/立体状态更新在无消息帧中两侧都会发生。
- 建议：无需改码；在 frame_scene.cpp 注释里点明中段块的实际落点（oni_skeleton_pump.cpp / frame_driver.cpp）即可。

---

## 补充核对记录（无发现）

- **MME 接缝**：frame_scene 的 mme::ClearScene/BeginScene/EndScene/DrawPrimitive/DrawIndexedPrimitive/PreRenderTargetCopy 包装点与原版 MMHack 拦截面（设备虚表 Clear/BeginScene/EndScene/DrawPrimitive/DIP/UpdateSurface/GetRenderTargetData/StretchRect 槽）一一对应；PostDeviceReset 中 mme::OnLostDevice→Reset→mme::OnResetDevice→InitRenderStates→RS(0xA1)→D3DX OnResetDevice 的合成顺序与 device_reset.cpp 注释引用的 MMHack 反汇编一致。未发现新的可疑点（设备丢失时 effect 的 OnLost/OnReset 槽 69/70 已核）。
- **wave_audio**：RIFF/PCM 解析偏移（fread 4 + fseek 0x10 → wFormatTag@20）、cbSize=0x12、bufferBytes=2×avg、波形列数学（1/30f、13.0f、390.0f，avg 零扩展 cvtsi2ss）、k 通道 stride 表、`*25>>7/>>15+25`、skip 段失败也计入 failureCount（0x7FF7CB4FA755/770）——移植注释链与 x64 地址一致，本轮未发现新偏差。WaveRestartAt 的 SetFrequency 槽 15 旧注已在先前轮定谳。
- **avi_record_start**：RecConfig 0x28/1/0x20/w*h*4、`f += 2^32` 负帧修正、seconds=frames/g_FrameScale、wavPath 条件（includeWave && frameStart==0）、0x464760 的立体宽 = w×multiplier、0x4649A7 的哑探测——与移植一致；KickRecordPhysics 的"空操作"结论（btConstraintSolver::allSolved 默认空实现）维持。
- **bg_overlay / media_load**：VFW 链、四边形顶点写入序（28 字节 XYZRHW|DIFFUSE|TEX1，6 顶点 TRIANGLELIST）、kOverlayScale=1.2000000476837158、纵横比公式（zoom=(hideW/viewScale*1.2)/mediaW，posY=h2-drawnH/2，SSE 单精度求值序）、AVI 成功后回写解析路径、'vids' 最低 wPriority 流选择、frameMs∈(0x20,0x22) 的 30fps 时序判定——一致。
- **fill_panel / draw_glyph / scene_font / sprite_overlay / overlay_producers / debug_geometry / capture_downsample**：本轮通读未发现与既有地址锚点矛盾之处（多数已在第 4-7 轮定谳）；fill_panel/draw_glyph 为 GDI 简单路径。

## 总结

发现总数：**6**（P0×1、P1×1、P2×3、P3×2，其中一条 P2 为核对后撤回、保留为记录；实际计分 5 条 + 1 条撤回）。

最重要三条：
1. **[P0] 渲染状态编号体系错位**——原版二进制全套 SetRenderState 用 D3D7/D3D8 老编号（52/55/56/57/58、22/23、27、19/20、15/24/25），在 D3D9 上大多无效或误中他态；移植"翻译"成现代枚举后激活了原版从未生效的 stencil/alpha-test/CULLMODE/ZFUNC/blend 管线，直接后果包括：地面投射影因 GREATER/REF=2 永假而不可见、低 alpha 纹素被 alpha test 挖空、描边/半透明 cull 行为改变、自阴影合成与地面绘制的不透明覆盖。需要项目级决策（字面复刻 vs 意图语义+修正矛盾逻辑）。
2. **[P1] CreateDevice 阶梯少一次原版的重复尝试**（同参数第 5 连击），设备调用流不 1:1。
3. **[P2] 格式 114 注释标错**（写成 A16B16G16R16F，实为 R32F），会误导对 SKII1=600 分支的理解；顺带确认 HDR RT 真实格式为 R32F 且两侧一致。

---

## 【2026-09-15 主审复核】P0 渲染状态编号错位——驳回（REJECTED）

该 P0 基于错误的枚举值记忆，不成立。逐项复核（d3d9types.h 权威值）：

| 原版立即数 | D3D9 权威枚举 | 我们移植 | 一致？ |
|---|---|---|---|
| RS(52,1) | D3DRS_STENCILENABLE=**52** | D3DRS_STENCILENABLE | ✓ |
| RS(58,255) | D3DRS_STENCILMASK=**58** | D3DRS_STENCILMASK | ✓ |
| RS(206,1) | D3DRS_SEPARATEALPHABLENDENABLE=**206** | 同名枚举 | ✓ |
| RS(209,5) | D3DRS_BLENDOPALPHA=**209**, MAX=5 | 同名枚举 | ✓ |
| RS(19,5)/(20,6) | SRCBLEND=**19**/DESTBLEND=**20** | 同名枚举 | ✓ |
| RS(24,1)/(15,1)/(25,7) | ALPHAREF=**24**/ALPHATESTENABLE=**15**/ALPHAFUNC=**25** | 同名枚举 | ✓ |
| RS(56,8)/(57,1)/(55,3) | STENCILFUNC=**56**/STENCILREF=**57**/STENCILPASS=**55** | 同名枚举 | ✓ |
| RS(27,x)/(22,x)/(23,x) | ALPHABLENDENABLE=**27**/CULLMODE=**22**/ZFUNC=**23** | 同名枚举 | ✓ |

原报告称"D3DRS_STENCILENABLE=22、SRCBLEND=12"等系枚举值误记（22 是 CULLMODE，12 非 RS）。
原版 x64 本就是规范 D3D9 调用（206/209 是 D3D9 尾段状态，D3D7/8 反而没有），两侧无任何分歧，
"地面影丢失/alpha test 挖空/描边只画背面"等推论全部不成立。**请勿按该 P0 修改任何代码。**
