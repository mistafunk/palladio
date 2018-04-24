from conans import ConanFile

class PalladioConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"

    def requirements(self):
        if self.settings.os == "Windows":
            self.requires("cesdk/1.9.3786p1@esri-rd-zurich/stable")  # see issue #88
        else:
            self.requires("cesdk/1.9.3786@esri-rd-zurich/stable")
