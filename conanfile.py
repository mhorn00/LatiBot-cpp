import os

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class LatiBotConan(ConanFile):
    name = "latibot"
    version = "0.1.0"
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"

    # FTS5 backs the LLM long-term memory search (plan v4 §14.5).
    default_options = {
        "sqlite3/*:enable_fts5": True,
        # DPP linked into LatiBot.exe rather than beside it as dpp.dll
        # (docs/modules/Module_Plan_Final.md §9.1, I2).
        "dpp/*:shared": False,
        # Only the 8-bit library, for trigger patterns; pcre2grep would bring
        # zlib and bzip2 for nothing.
        "pcre2/*:build_pcre2_16": False,
        "pcre2/*:build_pcre2_32": False,
        "pcre2/*:build_pcre2grep": False,
    }

    def requirements(self):
        # DPP, built from the third_party/DPP submodule by conan/dpp; export it
        # first with `conan export conan/dpp`. It brings zlib and opus.
        self.requires("dpp/10.1.6")
        # The same OpenSSL DPP links; LatiBot hashes emoji images with it.
        self.requires("openssl/3.6.4")
        self.requires("sqlite3/3.53.4")
        # Compile-time regular expressions, used by the URL scanner (plan v4 §9.1).
        self.requires("ctre/3.11.0")
        # Run-time regular expressions, for trigger patterns: unlike std::regex
        # it has match limits and does not recurse on the stack.
        self.requires("pcre2/10.47")

    def build_requirements(self):
        self.test_requires("catch2/3.16.0")

    def layout(self):
        # Every build folder is under build/, and they all share these files
        # (docs/modules/Module_Plan_Final.md §9.2). Debug and Release are
        # installed side by side, for the multi-config generator.
        self.folders.generators = os.path.join("build", "conan")

    def generate(self):
        toolchain = CMakeToolchain(self, generator="Ninja Multi-Config")
        # CMakePresets.json is ours; Conan's presets would only shadow it.
        toolchain.user_presets_path = False
        toolchain.generate()
        CMakeDeps(self).generate()
