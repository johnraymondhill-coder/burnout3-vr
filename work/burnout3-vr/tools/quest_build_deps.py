"""Cross-build pinned upstream SDL2/glslang dependencies on Windows."""
from pathlib import Path
import subprocess

work=Path(__file__).resolve().parents[2]
tc=work/'burnout-toolchain'
deps=work/'burnout-deps'
cmake=tc/'python/Scripts/cmake.exe'
common=['-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(tc/'python/Scripts/ninja.exe'),
        '-DCMAKE_TOOLCHAIN_FILE='+str(tc/'android-ndk-r28c/build/cmake/android.toolchain.cmake'),
        '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-33','-DANDROID_STL=c++_shared',
        '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_POLICY_VERSION_MINIMUM=3.5']
build=deps/'SDL2-arm64-build'
subprocess.run([str(cmake),'-S',str(deps/'SDL2-src'),'-B',str(build),*common,
                '-DCMAKE_INSTALL_PREFIX='+str(deps/'SDL2'),'-DSDL_SHARED=ON',
                '-DSDL_STATIC=OFF','-DSDL_TEST=OFF'],check=True)
subprocess.run([str(cmake),'--build',str(build),'-j','4'],check=True)
subprocess.run([str(cmake),'--install',str(build)],check=True)
