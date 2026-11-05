# Versions and releases / 版本与发布

The root `VERSION` file is the single editable product version, starting at
`10.00`. CMake generates the C++ version header and Windows EXE version resource;
the About dialog, Conan metadata and ZIP filenames read the same value.
MMD 9.32 remains the behavioral reference, not this project's release number.

版本只修改根目录 `VERSION`，格式为 `主版本.两位次版本`，例如 `10.00`、
`10.01`、`10.08`、`11.00`。不要在源码里单独修改版本，不使用浮点数表示版本。
Windows 数值版本的 `10.00` 为 `10.0.0.0`，显示文本仍为 `10.00`。

## Release requirements / 发布规范

- Public GitHub Release titles and descriptions must be written in English,
  including change summaries, validation results and installation instructions.
  Use the title `MikuDanceStudio vMAJOR.MINOR`.
- Release both Windows architectures together: x64 and x86, C++17, `Release`,
  `BUILD_TESTING=ON` and `MIKUDANCESTUDIO_DIAG=OFF`. The release workflow is the
  authoritative build and test gate; both architecture jobs must succeed before
  publication.
- Record actual passed, failed and skipped test counts per architecture, and link
  the release workflow run. Report local environment failures separately from CI
  results. Skipped GPU tests do not establish rendering parity; passing the
  configured suite does not establish full GUI or community-effect equivalence.
- Commit the intended source and `VERSION`, then create an annotated tag matching
  `VERSION` exactly. Published tags stay fixed. Rerun the workflow for the same
  commit when necessary; source changes require a new version and tag.
- Publish exactly four release assets: an x64 ZIP, an x86 ZIP and one `.zip.sha256`
  file for each. Package contents and runtime requirements follow the contract
  below; external models, motions, effects and local diagnostic files stay out.
- A release is complete after CI succeeds, the public assets are downloaded and
  verified, and the English release notes have been reviewed. Version commits
  follow the repository's [commit rules](../AGENTS.md), including no AI attribution
  trailers.

## Publish / 发布

Check the working tree and changes since the previous release. Update only the
root `VERSION` for the product version, validate it with
`python scripts/release.py --tag v10.00`, and commit the intended changes. Replace
`10.00` in these examples with the version being released. Push the source commit
and its matching annotated tag:

```sh
git tag -a v10.00 -m "MikuDanceStudio 10.00"
git push origin main v10.00
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

For a regression-only run, enable `validation_only` and set `tag` to the branch or
commit to test. This uses the same x86/x64 build and test steps, preserves the test
artifacts, and skips packaging and publishing. Existing version tags remain fixed.

```sh
gh workflow run release.yml -f tag=main -f validation_only=true
```

## English release notes / 英文发布说明

Describe the final changes since the preceding version in plain English. Lead
with user-visible fixes, then relevant maintenance changes. Include a link to
the successful release workflow, test counts for each architecture, any skips or
remaining verification limits, and a short installation reminder.

The workflow currently uses `--generate-notes` when creating a new release. Review
that generated text and replace it with the English summary before considering
the release complete; commit titles alone are not the release description. Save
the reviewed text as a UTF-8 file and update the existing release with:

```sh
gh release edit v10.00 --title "MikuDanceStudio v10.00" --notes-file out/release-notes-v10.00.md
```

Example description (replace the changes, test counts and run link with the
actual release evidence):

```text
This release fixes ...

- Fix ...
- Update ...

Validation: x64 N/N and x86 N/N tests passed, with no failures or skips.
CI: <release workflow run URL>

Windows x64 and x86 ZIPs and their SHA-256 checksums are attached. Extract the
entire folder and keep Data next to the executable. See RELEASES.md in the ZIP
for the required Microsoft runtimes.
```

## Verify the published release / 发布后核验

1. Wait for both architecture jobs and the publish job to finish successfully.
   Download their JUnit reports and check the actual results, including skips.
2. Confirm that the release is public, has the intended tag and title, and is
   marked as the latest stable release for a normal version update. Check that
   the remote tag resolves to the intended source commit.
3. Download all four public release assets. Verify each ZIP against its SHA-256
   file, check archive integrity and the packaged file list, and confirm that
   `VERSION` and the EXE version resource match the tag. Check the PE architecture
   of both `MikuDanceStudio.exe` and `Data/MMDxShow.dll` in each ZIP.
4. Apply and read back the English release notes, then share the release page
   link and the verified build/test result.

## Package and runtime / 压缩包与运行环境

Each ZIP contains `MikuDanceStudio.exe`, `Data/MMDxShow.dll`, version/readme and
license files. MME and the default toon textures are embedded in the executable.
`LICENSE` contains the MIT license for MikuDanceStudio's own code.
`licenses/MikuMikuDance.txt` preserves the original MikuMikuDance permission
statement and disclaimer; the extracted resources described in `res/README.md`
retain their original terms. `licenses/Bullet.txt` contains the Bullet license.
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

The unmodified Bullet 2.75 `src` tree from `bullet-2.75.zip` is versioned under
`recipes/bullet275/bullet-src`. `upstream.json` records the archive SHA-256 and
every source file's checksum; each recipe build verifies the tree. The modern
CMake build stays outside the upstream tree and builds the three rigid-body
libraries used by the application. Numerical alignment uses compiler settings,
with no Bullet algorithm, expression-order, class-layout or diagnostic patches.
This does not guarantee bitwise equivalence with MMD's historical compiler.
CI creates that local recipe before installing application dependencies.
`--lockfile=""` prevents locally generated lockfiles from pinning an older
Bullet recipe revision instead of the recipe just exported from these sources.

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
