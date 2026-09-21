# Versions and releases / 版本与发布

The root `VERSION` file is the single editable product version, starting at
`10.00`. CMake generates the C++ version header and Windows EXE version resource;
the About dialog, Conan metadata and ZIP filenames read the same value.
MMD 9.32 remains the behavioral reference, not this project's release number.

版本只修改根目录 `VERSION`，格式为 `主版本.两位次版本`，例如 `10.00`、
`10.01`、`10.08`、`11.00`。不要在源码里单独修改版本，不使用浮点数表示版本。
Windows 数值版本的 `10.00` 为 `10.0.0.0`，显示文本仍为 `10.00`。

## Publish / 发布

Commit the version and changes first, then push the matching tag:

```sh
git tag -a v10.00 -m "MikuDanceStudio 10.00"
git push origin v10.00
```

`.github/workflows/release.yml` checks that the tag exactly matches `VERSION`,
builds and runs CTest for x86 and x64 on Windows, and publishes a GitHub Release only
after both architectures pass. It uploads two ZIPs and their SHA-256 files. A failed
build can be rerun for the same tag; publishing replaces its matching assets.
Normal branch pushes do not publish releases. The built-in `GITHUB_TOKEN` is used;
If a tag push does not start a run, use Actions → Windows release → Run workflow
on `main` and enter the existing tag. This checks out that tag without moving it.
no personal access token is needed. Repository/organization policy must permit
the publish job's `contents: write` permission.

推送标签后自动构建并发布两个架构；标签与 `VERSION` 不一致时立即失败。
以后发布新版本：修改 `VERSION`、提交，再推送同名 `vxx.xx` 标签即可。

## Package and runtime / 压缩包与运行环境

Each ZIP contains `MikuDanceStudio.exe`, `Data/MMDxShow.dll`, version/readme and
license files. MME and the default toon textures are embedded in the executable.
External models, motions and effects are not included. Extract the entire folder
and keep the `Data` subdirectory next to the EXE.

Install the Microsoft Visual C++ 2015–2022 Redistributable for the chosen
architecture, the legacy Visual C++ 2008 runtime required by the retained
manifest, and the DirectX End-User Runtimes (June 2010). x64 imports `d3dx9_43.dll`;
x86 imports `d3dx9_32.dll`. These Microsoft runtime installers are not bundled.
The release workflow installs the checksum-pinned Microsoft runtime on its runner
before CTest and saves JUnit results plus CTest logs, including on failure. GPU
tests may explicitly skip with code 77 when the required device is unavailable;
a skip is not a GPU parity result. Optional local `ray.fx` tests are enabled only
when `MIKUDANCESTUDIO_RAY_TEST_EFFECT` is configured; CI does not supply that asset.

压缩包需完整解压，运行库未打包。CI 构建不依赖本机安装的 MMD 或任何模型。

## Reproduce the release build

The modified Bullet 2.75 source is versioned under `recipes/bullet275/bullet-src`.
Do not replace it with an upstream ZIP: it contains the project's physics parity
fixes. CI creates that local recipe before installing application dependencies.
`--lockfile=""` deliberately bypasses the historical workstation lock, whose
Bullet recipe revision predates these versioned sources.

```powershell
python -m pip install conan==2.27.1
conan profile detect --force
conan create recipes/bullet275 --profile:all profiles/x64 -s compiler.cppstd=17 -s build_type=Release --build=missing --lockfile=""
conan install . --profile:all profiles/x64 -s compiler.cppstd=17 -s build_type=Release --build=missing --lockfile="" --output-folder=out/ci
cmake -S . -B out/ci/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="$PWD/out/ci/build/generators/conan_toolchain.cmake" -DBUILD_TESTING=ON -DMIKUDANCESTUDIO_DIAG=OFF
cmake --build out/ci/build --config Release --parallel 4
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir out/ci/build -C Release --output-on-failure --timeout 90 --no-tests=error
if ($LASTEXITCODE) { exit $LASTEXITCODE }
python scripts/release.py --tag v10.00 --build-dir out/ci/build --arch x64
```

For x86 use `profiles/x86`, `-A Win32`, `--arch x86` and a separate output folder.
The GitHub release publishing commands follow the [GitHub CLI release API](https://cli.github.com/manual/gh_release_create).
