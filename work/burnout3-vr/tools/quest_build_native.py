"""Configure/build the private NTSC-U Android game library on Windows."""
from pathlib import Path
import subprocess
import os
import sys

root=Path(__file__).resolve().parents[1]
work=root.parent
tc=work/'burnout-toolchain'
deps=work/'burnout-deps'
ndk=tc/'android-ndk-r28c'
cmake=tc/'python/Scripts/cmake.exe'
build=root/'build-quest-baseline'
env=os.environ.copy()
env['PATH']=str(tc/'python/Scripts')+os.pathsep+env['PATH']
args=['-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(tc/'python/Scripts/ninja.exe'),
      '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
      '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-33','-DANDROID_STL=c++_shared',
      '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
      '-DXBOXRECOMP_DIR='+str(work/'xboxrecomp'),
      '-DCMAKE_FIND_ROOT_PATH='+str(deps/'SDL2')+';'+str(deps/'glslang'),
      '-DCMAKE_PREFIX_PATH='+str(deps/'SDL2')+';'+str(deps/'glslang'),
      '-DVulkan_INCLUDE_DIR='+str(ndk/'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'),
      '-DVulkan_LIBRARY='+str(ndk/'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/33/libvulkan.so'),
      '-DBO3_BIN_DIR='+str(build/'lib'),'-DBO3_GLYPHS_PYTHON='+str(tc/'python/Scripts/python.exe'),
      '-DPython3_EXECUTABLE='+str(tc/'python/Scripts/python.exe'),'-DBUILD_TESTING=OFF']
if '--vr' in sys.argv:
    args+=['-DBO3_QUEST_VR=ON','-DBO3_OPENXR_AAR='+str(deps/'openxr-loader')]
else:
    args+=['-DBO3_QUEST_VR=OFF']
subprocess.run([str(cmake),'-S',str(root),'-B',str(build),*args],check=True,env=env)
targets=['xbox_d3d8','xbox_video','xbox_kernel'] if '--runtime-only' in sys.argv else ['burnout3']
subprocess.run([str(cmake),'--build',str(build),'--target',*targets,'-j','4'],check=True,env=env)
