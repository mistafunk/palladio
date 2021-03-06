import os
from conans import ConanFile


class PalladioConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"

    def requirements(self):
        self.requires("catch2/2.0.1@bincrafters/stable")

        if "PLD_CONAN_HOUDINI_VERSION" in os.environ:
            self.requires("houdini/{}@sidefx/stable".format(os.environ["PLD_CONAN_HOUDINI_VERSION"]))
        else:
            self.requires("houdini/[>17.5.0,<18.0.0]@sidefx/stable")

        if "PLD_CONAN_SKIP_CESDK" not in os.environ:
            cesdk_version = "1.9.3786"
            if "PLD_CONAN_CESDK_VERSION" in os.environ:
                cesdk_version = os.environ["PLD_CONAN_CESDK_VERSION"]
            elif self.settings.os == "Windows":
                cesdk_version = "1.9.3786p1" #88
            self.requires("cesdk/{}@esri-rd-zurich/stable".format(cesdk_version))
