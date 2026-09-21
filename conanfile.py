from conan import ConanFile
from conan.tools.cmake import cmake_layout


class LatiBotConan(ConanFile):
    name = "latibot"
    version = "0.1.0"
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    # FTS5 backs the LLM long-term memory search (plan v4 §14.5).
    default_options = {"sqlite3/*:enable_fts5": True}

    def requirements(self):
        # DPP is vendored in third_party/DPP and built from source; these are its dependencies.
        self.requires("openssl/3.6.4")
        self.requires("zlib/1.3.2")
        self.requires("opus/1.6.1")
        self.requires("sqlite3/3.53.4")
        # Compile-time regular expressions, used by the URL scanner (plan v4 §9.1).
        self.requires("ctre/3.11.0")

    def build_requirements(self):
        self.test_requires("catch2/3.16.0")

    def layout(self):
        cmake_layout(self)
