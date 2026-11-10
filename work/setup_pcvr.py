from pathlib import Path
import urllib.request,zipfile,tarfile,subprocess,os,re,shutil
w=Path(__file__).resolve().parent
d=w/'pcvr-deps';d.mkdir(exist_ok=True)
tc=w/'burnout-toolchain/python/Scripts'
def run(*args):subprocess.run(list(map(str,args)),check=True)
def fetch(url,name):
 p=d/name
 if not p.exists():
  print('Downloading',name,flush=True);urllib.request.urlretrieve(url,p)
 return p
if not (d/'llvm-mingw').exists():
 p=fetch('https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip','llvm.zip')
 with zipfile.ZipFile(p) as z:z.extractall(d/'llvm-unpack')
 next((d/'llvm-unpack').iterdir()).rename(d/'llvm-mingw')
if not (d/'SDL2').exists():
 p=fetch('https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz','sdl.tar.gz')
 with tarfile.open(p) as t:t.extractall(d/'sdl-unpack',filter='data')
 next((d/'sdl-unpack').iterdir()).rename(d/'SDL2')
env=os.environ.copy();env['PATH']=str(tc)+os.pathsep+str(d/'llvm-mingw/bin')+os.pathsep+env['PATH'];os.environ.update(env)
common=['-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(tc/'ninja.exe'),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_TOOLCHAIN_FILE='+str(w/'burnout3-vr/cmake/llvm-mingw-x86_64.cmake'),'-DLLVM_MINGW_ROOT='+str(d/'llvm-mingw'),'-DCMAKE_POLICY_VERSION_MINIMUM=3.5']
cm=tc/'cmake.exe'
for name,src,opts in [('glslang',w/'burnout-deps/glslang-src',['-DBUILD_SHARED_LIBS=OFF','-DENABLE_OPT=OFF','-DENABLE_GLSLANG_BINARIES=OFF','-DENABLE_CTEST=OFF','-DBUILD_EXTERNAL=OFF']),('openxr',w/'burnout-deps/OpenXR-SDK',['-DBUILD_TESTS=OFF','-DBUILD_API_LAYERS=OFF','-DBUILD_CONFORMANCE_TESTS=OFF','-DDYNAMIC_LOADER=ON'])]:
 if not (d/name/'lib').exists():
  run(cm,'-S',src,'-B',d/(name+'-build'),*common,'-DCMAKE_INSTALL_PREFIX='+str(d/name),*opts)
  run(cm,'--build',d/(name+'-build'),'-j','4');run(cm,'--install',d/(name+'-build'))
vk=d/'vulkan';(vk/'lib').mkdir(parents=True,exist_ok=True)
headers=w/'burnout-toolchain/android-ndk-r28c/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for name in ['vulkan','vk_video']:shutil.copytree(headers/name,vk/'include'/name,dirs_exist_ok=True)
names=set()
for h in ['vulkan_core.h','vulkan_win32.h']:names.update(re.findall(r'VKAPI_CALL\s+(vk\w+)',(headers/'vulkan'/h).read_text()))
(vk/'lib/vulkan.def').write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+'\n'.join(sorted(names)))
run(d/'llvm-mingw/bin/x86_64-w64-mingw32-dlltool.exe','-m','i386:x86-64','-d',vk/'lib/vulkan.def','-l',vk/'lib/libvulkan-1.dll.a')
print('Windows dependencies ready',flush=True)
