from pathlib import Path
import os,subprocess
w=Path(__file__).resolve().parent;d=w/'pcvr-deps';tc=w/'burnout-toolchain/python/Scripts'
os.environ['PATH']=str(tc)+os.pathsep+str(d/'llvm-mingw/bin')+os.pathsep+os.environ['PATH']
root=w/'burnout3-vr';b=root/'build-pcvr';out=root/'build/pcvr'
def run(*args):subprocess.run(list(map(str,args)),check=True)
headers=d/'vulkan/include'
run(tc/'cmake.exe','-S',root,'-B',b,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(tc/'ninja.exe'),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_TOOLCHAIN_FILE='+str(root/'cmake/llvm-mingw-x86_64.cmake'),'-DLLVM_MINGW_ROOT='+str(d/'llvm-mingw'),'-DCMAKE_PREFIX_PATH='+str(d/'SDL2/x86_64-w64-mingw32')+';'+str(d/'glslang')+';'+str(d/'openxr'),'-DVulkan_INCLUDE_DIR='+str(headers),'-DVulkan_LIBRARY='+str(d/'vulkan/lib/libvulkan-1.dll.a'),'-DBO3_PC_VR=ON','-DBO3_BIN_DIR='+str(out),'-DBO3_GLYPHS_PYTHON='+str(tc/'python.exe'),'-DPython3_EXECUTABLE='+str(tc/'python.exe'),'-DCMAKE_POLICY_VERSION_MINIMUM=3.5')
run(tc/'cmake.exe','--build',b,'--target','burnout3','-j','4')
