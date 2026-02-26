from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy
import os


class MyLobsterPPConan(ConanFile):
    name = "mylobsterpp"
    version = "2026.2.25"
    description = "MyLobster++ C++23 AI Assistant Platform"
    license = "Proprietary"
    url = "https://github.com/mylobster-ai/personal-assistant"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "build_executable": [True, False],
    }
    default_options = {
        "shared": False,
        "build_executable": True,
    }
    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "src/*",
        "include/*",
        "tests/*",
    )

    # Dependencies are fetched via CMake FetchContent — no Conan requires needed.
    # This conanfile packages the built output for distribution.

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["MYLOBSTER_BUILD_TESTS"] = False
        tc.variables["MYLOBSTER_BUILD_SHARED"] = self.options.shared
        tc.variables["MYLOBSTER_BUILD_EXECUTABLE"] = self.options.build_executable
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["mylobster_lib"]
        self.cpp_info.set_property("cmake_file_name", "mylobster")
        self.cpp_info.set_property("cmake_target_name", "mylobster::mylobster")
        self.cpp_info.requires = []
        # System deps that consumers need to link
        self.cpp_info.system_libs = ["pthread"]
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["ws2_32"]
