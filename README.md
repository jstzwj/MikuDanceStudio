# MikuDanceStudio

Product version: [VERSION](VERSION). Release instructions: [RELEASES.md](docs/RELEASES.md).

[English](#english) | [中文](#中文) | [日本語](#日本語)

MME is compiled directly into the EXE; see [integration](third_party/mmeffect/README.md).
MME 已静态内置于 EXE；当前对齐状态、修复清单和验证边界见 [PORTING_STATUS](docs/PORTING_STATUS.md)。
All builds require Python 3 / 所有构建均需要 Python 3 / Python 3 が必要です。

---

## English

MikuDanceStudio is a Windows desktop 3D dance animation studio written in C++:
load PMD/PMX models and VMD/VPD motion data, edit bones / morphs / camera /
lighting / accessories in a DirectX 9 viewport, with built-in Bullet 2.75 rigid
body physics, AVI video recording, VSQ audio alignment, and animation data
export.

This project is a continuation of a behavior-level port of
[MikuMikuDance](https://learnmmd.com/downloads/) v932: the observable behavior
of the original program (UI, file formats, rendering, physics solve order) is
the alignment target, rebuilt with a modern toolchain
(MSVC v143 / CMake / Conan 2).

### Features

* **Models**: PMD / PMX loading (incl. BDEF4/SDEF weights), bone trees, IK,
  morphs, toon rendering
* **Animation**: VMD load/save, VPD export, frame editor (bone / morph /
  camera / lighting / self-made expression / accessory tracks), frame playback
  with physics preview
* **Physics**: Bullet 2.75 rigid bodies + constraints (6DOF spring),
  deterministic solve order
* **Rendering**: DirectX 9 fixed pipeline + optional SM2/SM3 effect chain
  (HDR RT), toon textures, ground shadows, stereoscopic 3D (NVIDIA 3D Vision)
* **Multimedia**: Wave/AVI recording (DirectShow, incl. the MMDxShow push
  source filter), VSQ score import, Kinect skeleton input
* **UI**: EN/JP bilingual, 168-control main window, timeline, accessory
  editing, undo/redo

### Building from Source

Dependencies: Windows 10/11, Visual Studio 2022 (v143 toolset + Desktop
development with C++ workload), [CMake](https://cmake.org/) ≥ 3.21,
[Conan](https://conan.io/) 2.x, and Python 3 (embedded MME icon resources).

```powershell
conan profile detect --force
conan create recipes/bullet275 --profile:all profiles/x64 -s compiler.cppstd=17 -s build_type=Release --build=missing --lockfile=""
conan install . --profile:all profiles/x64 -s compiler.cppstd=17 -s build_type=Release --build=missing --lockfile="" --output-folder=out/ci
cmake -S . -B out/ci/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="$PWD/out/ci/build/generators/conan_toolchain.cmake"
cmake --build out/ci/build --config Release
```

Artifacts: `out/ci/build/Release/MikuDanceStudio.exe` (the GUI application) and
`out/ci/build/Release/MMDxShow.dll` (DirectShow push source filter). Place the DLL
under `Data/` next to the EXE, as the release packager does. Default toon textures
are embedded; optional `Data/toon01.bmp..toon10.bmp` override them.

> Note: the Conan profiles under `profiles/` assume a Windows + MSVC host; if
> your default profile is already set up that way, you can simply
> `include(default)`.

### Acknowledgments

* [MikuMikuDance](https://learnmmd.com/downloads/) (Yu Higuchi) — the program
  whose behavior this project aligns to; thank you for two decades of
  contribution to the creative community
* [Bullet Physics](https://github.com/bulletphysics/bullet3) (zlib license)
* The MikuMikuEffect (MME) community effect ecosystem — the 37 `Exp*` APIs in
  `exports/` are compatible with it

### License

This project is licensed under the same freeware terms as MikuMikuDance —
see [LICENSE](LICENSE). Third-party components follow their own licenses:
Bullet Physics (zlib), DirectX SDK / Windows SDK (Microsoft EULA).

---

## 中文

MikuDanceStudio 是一个用 C++ 编写的 Windows 桌面 3D 舞蹈动画工作室：加载 PMD/PMX 模型、
VMD/VPD 动作数据，在 DirectX 9 视口中编辑骨骼/形态/相机/照明/附件，内建 Bullet 2.75
刚体物理、AVI 视频录制与 VSQ 音频对齐，并可导出动画数据。

本工程是 [MikuMikuDance](https://learnmmd.com/downloads/) v932 行为级移植的延续：
以原版程序的可观测行为（UI、文件格式、渲染、物理求解序列）为对齐目标，在现代工具链
（MSVC v143 / CMake / Conan 2）下重建整个应用程序。

### 功能

* **模型**：PMD / PMX（含 BDEF4/SDEF 权重）加载，骨骼树、IK、形态（morph）、toon 渲染
* **动画**：VMD 加载/保存、VPD 导出、帧编辑器（骨骼/形态/相机/照明/自作表情/附件轨道）、
  帧回放与物理预览
* **物理**：Bullet 2.75 刚体 + 约束（6DOF spring），确定性求解顺序
* **渲染**：DirectX 9 固定管线 + 可选 SM2/SM3 特效链（HDR RT）、toon 纹理、地面阴影、
  立体视觉（NVIDIA 3D Vision）
* **多媒体**：Wave/AVI 录制（DirectShow，含 MMDxShow 推源过滤器）、VSQ 乐谱导入、
  Kinect 骨骼输入
* **UI**：EN/JP 双语、168 控件主窗口、时间轴、附件编辑、撤销/重做

### 从源码构建

依赖：Windows 10/11、Visual Studio 2022（v143 工具集 + C++ 桌面开发工作负载）、
[CMake](https://cmake.org/) ≥ 3.21、[Conan](https://conan.io/) 2.x。

```powershell
conan profile detect --force
conan create recipes/bullet275 --profile:all profiles/x64 -s compiler.cppstd=17 -s build_type=Release --build=missing --lockfile=""
conan install . --profile:all profiles/x64 -s compiler.cppstd=17 -s build_type=Release --build=missing --lockfile="" --output-folder=out/ci
cmake -S . -B out/ci/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="$PWD/out/ci/build/generators/conan_toolchain.cmake"
cmake --build out/ci/build --config Release
```

产物：`out/ci/build/Release/MikuDanceStudio.exe`（GUI 主程序）、`out/ci/build/Release/MMDxShow.dll`
（DirectShow 推源过滤器）。将 DLL 放入 EXE 旁的 `Data/` 目录；发布打包会自动安排。
默认 toon 贴图已内嵌，也可以用 `Data/toon01.bmp..toon10.bmp` 覆盖。

> 说明：`profiles/` 下的 Conan profile 假定主机为 Windows + MSVC；若你的默认 profile
> 已如此设置，可直接 `include(default)`。

### 致谢

* [MikuMikuDance](https://learnmmd.com/downloads/)（樋口優）——
  本工程对齐行为的目标程序，感谢二十年来对创作社区的贡献
* [Bullet Physics](https://github.com/bulletphysics/bullet3)（zlib 许可）
* MikuMikuEffect（MME）社区特效生态 —— `exports/` 的 37 个 `Exp*` API 与之兼容

### 许可

本工程采用与 MikuMikuDance 相同的免费软件条款，详见 [LICENSE](LICENSE)。
第三方组件遵循各自许可：Bullet Physics（zlib）、
DirectX SDK / Windows SDK（微软 EULA）。

---

## 日本語

MikuDanceStudio は C++ で書かれた Windows デスクトップ向け 3D ダンスアニメーション
スタジオです。PMD/PMX モデルと VMD/VPD モーションデータを読み込み、DirectX 9
ビューポート上でボーン／モーフ／カメラ／照明／アクセサリを編集できます。
Bullet 2.75 剛体物理、AVI 動画録画、VSQ 音源との同期、アニメーションデータの
エクスポートを内蔵しています。

本プロジェクトは [MikuMikuDance](https://learnmmd.com/downloads/) v932 の
動作レベルでの移植を継続するものです。オリジナルの観測可能な動作（UI、ファイル
フォーマット、レンダリング、物理求解の順序）を合わせ込みの目標とし、現代的な
ツールチェーン（MSVC v143 / CMake / Conan 2）でアプリケーション全体を再構築します。

### 機能

* **モデル**：PMD / PMX（BDEF4/SDEF ウェイト対応）読み込み、ボーンツリー、IK、
  モーフ、トゥーンレンダリング
* **アニメーション**：VMD 読み込み／保存、VPD エクスポート、フレームエディタ
  （ボーン／モーフ／カメラ／照明／自作表情／アクセサリトラック）、物理プレビュー付き
  フレーム再生
* **物理**：Bullet 2.75 剛体＋コンストレイント（6DOF スプリング）、決定論的な
  求解順序
* **レンダリング**：DirectX 9 固定機能パイプライン＋オプションの SM2/SM3
  エフェクトチェーン（HDR RT）、トゥーンテクスチャ、地面影、立体視
  （NVIDIA 3D Vision）
* **マルチメディア**：Wave/AVI 録画（DirectShow、MMDxShow プッシュソースフィルタ
  含む）、VSQ 楽譜インポート、Kinect スケルトン入力
* **UI**：英／日バイリンガル、168 コントロールのメインウィンドウ、タイムライン、
  アクセサリ編集、元に戻す／やり直し

### ソースからのビルド

必要環境：Windows 10/11、Visual Studio 2022（v143 ツールセット＋「C++ による
デスクトップ開発」ワークロード）、[CMake](https://cmake.org/) ≥ 3.21、
[Conan](https://conan.io/) 2.x。

```powershell
conan profile detect --force
conan create recipes/bullet275 --profile:all profiles/x64 -s compiler.cppstd=17 -s build_type=Release --build=missing --lockfile=""
conan install . --profile:all profiles/x64 -s compiler.cppstd=17 -s build_type=Release --build=missing --lockfile="" --output-folder=out/ci
cmake -S . -B out/ci/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="$PWD/out/ci/build/generators/conan_toolchain.cmake"
cmake --build out/ci/build --config Release
```

成果物：`out/ci/build/Release/MikuDanceStudio.exe`（GUI アプリケーション）、
`out/ci/build/Release/MMDxShow.dll`（DirectShow プッシュソースフィルタ）。DLL は EXE の
隣の `Data/` に配置します。リリース ZIP にはこの配置で含まれます。
標準 toon は内蔵されており、`Data/toon01.bmp..toon10.bmp` で上書きできます。

> 注：`profiles/` 配下の Conan プロファイルはホストが Windows + MSVC であることを
> 前提としています。デフォルトプロファイルが既にその設定であれば、そのまま
> `include(default)` できます。

### 謝辞

* [MikuMikuDance](https://learnmmd.com/downloads/)（樋口優）——
  本プロジェクトが動作を合わせ込む対象のプログラム。20 年にわたる創作コミュニティへの
  貢献に感謝します
* [Bullet Physics](https://github.com/bulletphysics/bullet3)（zlib ライセンス）
* MikuMikuEffect（MME）コミュニティのエフェクトエコシステム —— `exports/` の
  37 個の `Exp*` API は互換性を保っています

### ライセンス

本プロジェクトは MikuMikuDance と同じフリーウェア条項の下で配布されます。
詳細は [LICENSE](LICENSE) を参照してください。サードパーティコンポーネントは
それぞれのライセンスに従います：Bullet Physics（zlib）、
DirectX SDK / Windows SDK（マイクロソフト EULA）。
