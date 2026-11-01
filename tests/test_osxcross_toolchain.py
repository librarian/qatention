"""Check application and vcpkg dependency configuration without a macOS SDK."""
from pathlib import Path
import subprocess
import tempfile
import unittest

TOOLCHAIN = Path(__file__).resolve().parent.parent / 'cmake/toolchains/osxcross.cmake'


class OsxcrossToolchainTests(unittest.TestCase):
    def test_application_and_dependency_architectures(self):
        for variable, architecture, processor in (
            ('VCPKG_TARGET_ARCHITECTURE', 'arm64', 'arm64'),
            ('VCPKG_TARGET_ARCHITECTURE', 'x64', 'x86_64'),
            ('CMAKE_OSX_ARCHITECTURES', 'arm64', 'arm64'),
            ('CMAKE_OSX_ARCHITECTURES', 'x86_64', 'x86_64'),
        ):
            with self.subTest(variable=variable, architecture=architecture):
                self.run_cmake(f'''
set({variable} {architecture})
set(VCPKG_LINKER_FLAGS "-Wl,-headerpad,0x1000")
include("{TOOLCHAIN.as_posix()}")
# CMake loads toolchains more than once during configuration.
include("{TOOLCHAIN.as_posix()}")
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "{processor}" OR
   NOT CMAKE_OSX_ARCHITECTURES STREQUAL "{processor}")
    message(FATAL_ERROR "Incorrect target architecture")
endif()
if(NOT CMAKE_CXX_COMPILER MATCHES "/{processor}-apple-darwin23.5-clang[+][+]$")
    message(FATAL_ERROR "Incorrect compiler: ${{CMAKE_CXX_COMPILER}}")
endif()
foreach(kind EXE SHARED MODULE)
    if(NOT CMAKE_${{kind}}_LINKER_FLAGS_INIT STREQUAL " -Wl,-headerpad,0x1000")
        message(FATAL_ERROR "Missing or duplicated linker flags for ${{kind}}")
    endif()
endforeach()
''')

    def test_requires_an_explicit_target_architecture(self):
        result = self.run_cmake(f'include("{TOOLCHAIN.as_posix()}")', check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Set CMAKE_OSX_ARCHITECTURES', result.stderr)

    def run_cmake(self, contents, check=True):
        with tempfile.TemporaryDirectory() as temporary:
            script = Path(temporary) / 'check.cmake'
            script.write_text(contents)
            return subprocess.run(['cmake', '-P', str(script)],
                                  check=check, capture_output=True, text=True)


if __name__ == '__main__':
    unittest.main()
