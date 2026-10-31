from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy
import os
import runpy


class Bullet275Conan(ConanFile):
    """Unmodified Bullet 2.75 sources with an external modern build definition."""

    name = "bullet275"
    version = "2.75"
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = ("bullet-src/*", "CMakeLists.txt", "upstream.json", "verify_sources.py")

    def layout(self):
        cmake_layout(self)

    def validate(self):
        # Both sides of the library boundary use C++17 aligned new/delete.
        check_min_cppstd(self, "17")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.cache_variables["BULLET275_ASAN"] = os.environ.get("BULLET275_ASAN") == "1"
        tc.generate()

    def build(self):
        verifier = runpy.run_path(os.path.join(self.source_folder, "verify_sources.py"))
        count = verifier["verify_sources"](
            os.path.join(self.source_folder, "bullet-src", "src"),
            os.path.join(self.source_folder, "upstream.json"))
        self.output.info(f"Verified {count} unmodified upstream Bullet 2.75 files")
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", os.path.join(self.source_folder, "bullet-src", "src"),
             os.path.join(self.package_folder, "include"))
        for component in ("LinearMath", "BulletCollision", "BulletDynamics"):
            for pattern in ("*.lib", "*.a"):
                copy(self, pattern, os.path.join(self.build_folder, component),
                     os.path.join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "BULLET275")
        self.cpp_info.set_property("cmake_target_name", "BULLET275::BULLET275")
        self.cpp_info.libs = ["BulletDynamics", "BulletCollision", "LinearMath"]
        self.cpp_info.includedirs = ["include"]
        if self.settings.compiler == "msvc":
            # Bullet has inline code in its public headers. Consumers need the
            # same floating-point and allocation settings as the library.
            self.cpp_info.cxxflags = ["/fp:precise", "/Zc:alignedNew"]
            if self.settings.arch == "x86":
                self.cpp_info.cxxflags.append("/arch:IA32")
