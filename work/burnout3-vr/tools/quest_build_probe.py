"""Build the local Android/OpenXR compatibility check without Gradle."""
from pathlib import Path
import subprocess
import zipfile
import shutil

root=Path(__file__).resolve().parents[1]
work=root.parent
tc=work/'burnout-toolchain'
ndk=tc/'android-ndk-r28c/toolchains/llvm/prebuilt/windows-x86_64'
bt=tc/'build-tools/android-15'
jdk=tc/'jdk-21.0.13+9'
loader=work/'burnout-deps/openxr-loader'
out=root/'build/quest-probe'
out.mkdir(parents=True,exist_ok=True)

def run(*args):
    subprocess.run(list(map(str,args)),check=True)

run(ndk/'bin/clang.exe','--target=aarch64-linux-android29','-shared','-fPIC','-O2','-Wall','-Wextra',
    '-Wno-missing-field-initializers','-I'+str(loader/'prefab/modules/headers/include'),
    root/'quest/probe/probe.c','-L'+str(loader/'jni/arm64-v8a'),
    '-lopenxr_loader','-lvulkan','-landroid','-llog','-Wl,-z,max-page-size=16384',
    '-Wl,-soname,libbo3probe.so','-o',out/'libbo3probe.so')
run(bt/'aapt2.exe','link','-I',tc/'platforms/android-35/android.jar',
    '--manifest',root/'quest/probe/AndroidManifest.xml',
    '--version-code','1','--version-name','0.1-preflight','-o',out/'resources.apk')
with zipfile.ZipFile(out/'resources.apk','a',zipfile.ZIP_DEFLATED) as apk:
    apk.write(out/'libbo3probe.so','lib/arm64-v8a/libbo3probe.so')
    apk.write(loader/'jni/arm64-v8a/libopenxr_loader.so','lib/arm64-v8a/libopenxr_loader.so')
run(bt/'zipalign.exe','-f','-p','4',out/'resources.apk',out/'aligned.apk')
key=tc/'burnout-vr-development.keystore'
if not key.exists():
    run(jdk/'bin/keytool.exe','-genkeypair','-noprompt','-keystore',key,'-storepass','android',
        '-keypass','android','-alias','androiddebugkey','-keyalg','RSA','-keysize','2048',
        '-validity','10000','-dname','CN=Local Burnout VR Development,O=Local,C=US')
run(jdk/'bin/java.exe','-jar',bt/'lib/apksigner.jar','sign','--ks',key,'--ks-pass','pass:android',
    '--key-pass','pass:android','--out',out/'burnout-vr-graphics-check.apk',out/'aligned.apk')
run(jdk/'bin/java.exe','-jar',bt/'lib/apksigner.jar','verify',out/'burnout-vr-graphics-check.apk')
print(out/'burnout-vr-graphics-check.apk')
