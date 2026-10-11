from setuptools import setup, Extension
import os
import glob

# Source files from Pation core
pation_src = glob.glob("../src/**/*.c", recursive=True) + glob.glob("../src/**/*.cpp", recursive=True)
# Exclude main.cpp since it has the main() function for the CLI app
pation_src = [f for f in pation_src if "main.cpp" not in f]

# Add wrapper
src_files = pation_src + [
    "wrapper-python.cpp",
    "../third-party/miniz/miniz.c",
    "../third-party/jpge/jpge.cpp",
    "../third-party/ccitt/ccitt.c"
]

# Includes
includes = [
    "../include",
    "../third-party",
    "../third-party/libjpeg-turbo/src",
    "../third-party/libjpeg-turbo/build_ucrt"
]

extra_objs = [
    "../third-party/libjpeg-turbo/build_ucrt/libturbojpeg.a"
]

module = Extension(
    "pation",
    sources=src_files,
    include_dirs=includes,
    extra_objects=extra_objs,
    extra_compile_args=["-O3", "-std=c++17"]
)

setup(
    name="pation",
    version="1.0",
    description="Python wrapper for Pation PDF compressor",
    ext_modules=[module]
)
