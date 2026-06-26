"""Conan recipe for SoaTable, a header-only C++23 sparse Structure-of-Arrays table."""

import os

from conan import ConanFile
from conan.tools.cmake import cmake_layout
from conan.tools.files import copy


class SoaTableConan(ConanFile):
    name = "soatable"
    version = "0.3.0"
    license = "MIT"
    url = "https://github.com/bbalouki/soatable"
    homepage = "https://github.com/bbalouki/soatable"
    description = "Header-only C++23 sparse Structure-of-Arrays table with generational handles."
    topics = ("soa", "ecs", "columnar", "header-only", "cpp23")

    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True
    exports_sources = (
        "include/*",
        "LICENSE",
        "tools/visualizers/*.natvis",
        "tools/visualizers/*.py",
        "tools/visualizers/README.md",
    )

    def package_id(self):
        # Header-only: the package is identical across configurations.
        self.info.clear()

    def layout(self):
        cmake_layout(self)

    def package(self):
        copy(
            self,
            "*.hpp",
            src=os.path.join(self.source_folder, "include"),
            dst=os.path.join(self.package_folder, "include"),
        )
        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )
        # Ship the debugger visualizers so Conan users receive them too; they are opt-in developer
        # aids that the consumer wires into their debugger (see tools/visualizers/README.md).
        copy(
            self,
            "*",
            src=os.path.join(self.source_folder, "tools", "visualizers"),
            dst=os.path.join(self.package_folder, "res", "soatable", "visualizers"),
            excludes=("__pycache__/*",),
        )

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.resdirs = ["res"]
        self.cpp_info.set_property("cmake_file_name", "soatable")
        self.cpp_info.set_property("cmake_target_name", "soatable::soatable")
        self.cpp_info.cppstd = "23"
