"""DPP, built from LatiBot's third_party/DPP submodule.

A Conan package rather than add_subdirectory, so DPP is built once per
configuration and every build folder, and CI, reuses it
(docs/modules/Module_Plan_Final.md §9.1, I1). The build itself is what the
root CMakeLists.txt did before: see CMakeLists.txt beside this file.

Export it before installing LatiBot's dependencies:

    conan export conan/dpp

To move to another DPP: check out its tag in the submodule, and change
`version` below to match.
"""
import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy


class DppConan(ConanFile):
    name = "dpp"
    version = "10.1.6"
    license = "Apache-2.0"
    description = "D++, the C++ Discord library, as LatiBot builds it"
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False]}
    default_options = {"shared": True}

    def export_sources(self):
        copy(self, "CMakeLists.txt", src=self.recipe_folder, dst=self.export_sources_folder)
        dpp = os.path.join(self.recipe_folder, "..", "..", "third_party", "DPP")
        if not os.path.isfile(os.path.join(dpp, "CMakeLists.txt")):
            raise RuntimeError("third_party/DPP is empty. Run: git submodule update --init third_party/DPP")
        # Everything the build reads; not the docs, the tests' data, or the
        # Windows binaries DPP bundles, which LatiBot replaces with Conan's.
        copy(self, "*", src=dpp, dst=os.path.join(self.export_sources_folder, "DPP"),
             excludes=(".git*", "docpages/*", "doxygen-awesome-css/*", "testdata/*", "win32/*", "library-vcpkg/*", "vcpkg/*"))

    def requirements(self):
        # The same versions LatiBot's own conanfile.py asks for.
        self.requires("openssl/3.6.4", transitive_headers=True)
        self.requires("zlib/1.3.2")
        self.requires("opus/1.6.1")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeToolchain(self).generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        dpp = os.path.join(self.source_folder, "DPP")
        copy(self, "LICENSE", src=dpp, dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "*", src=os.path.join(dpp, "include"), dst=os.path.join(self.package_folder, "include"))
        # DPP's build puts them under DPP/library/<config>.
        lib = os.path.join(self.package_folder, "lib")
        bin = os.path.join(self.package_folder, "bin")
        copy(self, "*/dpp.lib", src=self.build_folder, dst=lib, keep_path=False)
        copy(self, "*/dpp.dll", src=self.build_folder, dst=bin, keep_path=False)
        copy(self, "*/dpp.pdb", src=self.build_folder, dst=bin, keep_path=False)
        if not os.path.isfile(os.path.join(lib, "dpp.lib")):
            raise RuntimeError(f"no dpp.lib under {self.build_folder}: DPP's build put it somewhere unexpected")

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "dpp")
        self.cpp_info.set_property("cmake_target_name", "dpp::dpp")
        self.cpp_info.libs = ["dpp"]
        # DPP's headers include nlohmann/json from include/dpp.
        self.cpp_info.includedirs = ["include", os.path.join("include", "dpp")]
        self.cpp_info.defines = ["DPP_FORMATTERS"]
        self.cpp_info.requires = ["openssl::openssl", "zlib::zlib", "opus::opus"]
