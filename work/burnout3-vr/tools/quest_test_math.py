from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
work=root.parent
cc=work/'burnout-toolchain/android-ndk-r28c/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
out=root/'build/quest-camera-math-test'
args=[cc,'--target=aarch64-linux-android33','-O2','-Wall','-Wextra','-Werror',
    '-I'+str(work/'xboxrecomp/src'),root/'quest/test_math.c',work/'xboxrecomp/src/quest/quest_math.c','-lm','-o',out]
subprocess.run(list(map(str,args)),check=True)
adb=work/'android-tools/platform-tools/adb.exe'
for args in [('push',out,'/data/local/tmp/burnout-vr-math-test'),
             ('shell','chmod','755','/data/local/tmp/burnout-vr-math-test'),
             ('shell','/data/local/tmp/burnout-vr-math-test')]:
    subprocess.run([str(adb),*map(str,args)],check=True)
