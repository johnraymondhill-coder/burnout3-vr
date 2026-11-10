"""Package the local Quest development build with the portable Windows SDK."""
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
work = root.parent
tc = work / 'burnout-toolchain'
deps = work / 'burnout-deps'
bt = tc / 'build-tools/android-15'
jdk = tc / 'jdk-21.0.13+9/bin'
ndk = tc / 'android-ndk-r28c/toolchains/llvm/prebuilt/windows-x86_64'
jar = tc / 'platforms/android-35/android.jar'
out = root / 'build/quest-apk'
for d in ['gen', 'classes', 'dex', 'assets/licenses', 'native']:
    (out / d).mkdir(parents=True, exist_ok=True)

def run(*args):
    subprocess.run([str(a) for a in args], check=True)

android = 'http://schemas.android.com/apk/res/android'
ET.register_namespace('android', android)
def a(key): return '{' + android + '}' + key
tree = ET.parse(root / 'android/AndroidManifest.xml')
manifest = tree.getroot()
for permission in ['org.khronos.openxr.permission.OPENXR', 'org.khronos.openxr.permission.OPENXR_SYSTEM']:
    ET.SubElement(manifest, 'uses-permission', {a('name'): permission})
ET.SubElement(manifest, 'uses-feature', {a('name'): 'android.hardware.vr.headtracking', a('version'): '1', a('required'): 'true'})
queries = ET.SubElement(manifest, 'queries')
ET.SubElement(queries, 'provider', {a('authorities'): 'org.khronos.openxr.runtime_broker;org.khronos.openxr.system_runtime_broker'})
for name in ['OpenXRRuntimeService', 'OpenXRApiLayerService']:
    intent = ET.SubElement(queries, 'intent')
    ET.SubElement(intent, 'action', {a('name'): 'org.khronos.openxr.' + name})
app = manifest.find('application')
ET.SubElement(app, 'meta-data', {a('name'): 'com.oculus.supportedDevices', a('value'): 'quest3'})
ET.SubElement(app, 'meta-data', {a('name'): 'com.oculus.vr.focusaware', a('value'): 'true'})
for activity in app.findall('activity'):
    name = activity.get(a('name'))
    if name.endswith('.StartActivity'):
        activity.set(a('name'), 'io.github.xenophobentx.takedawn.QuestStartActivity')
        ET.SubElement(activity.find('intent-filter'), 'category', {a('name'): 'com.oculus.intent.category.VR'})
    if name.endswith('.GameActivity'):
        activity.set(a('exported'), 'true')
        filt = ET.SubElement(activity, 'intent-filter')
        ET.SubElement(filt, 'action', {a('name'): 'android.intent.action.MAIN'})
        ET.SubElement(filt, 'category', {a('name'): 'com.oculus.intent.category.VR'})
    if name.endswith('.DriverActivity'):
        for filt in list(activity.findall('intent-filter')):
            activity.remove(filt)
settings_activity=ET.SubElement(app,'activity',{a('name'):'io.github.xenophobentx.takedawn.QuestSettingsActivity',
    a('label'):'Burnout VR Graphics',a('screenOrientation'):'sensorLandscape',a('exported'):'true'})
settings_filter=ET.SubElement(settings_activity,'intent-filter')
ET.SubElement(settings_filter,'action',{a('name'):'android.intent.action.MAIN'})
ET.SubElement(settings_filter,'category',{a('name'):'android.intent.category.LAUNCHER'})
ET.SubElement(app,'activity-alias',{a('name'):'io.github.xenophobentx.takedawn.StartActivity',
    a('targetActivity'):'io.github.xenophobentx.takedawn.QuestStartActivity',a('exported'):'false'})
tree.write(out / 'AndroidManifest.xml', encoding='utf-8', xml_declaration=True)

overlay = out / 'overlay/values'
overlay.mkdir(parents=True, exist_ok=True)
(overlay / 'strings.xml').write_text('<resources><string name="app_name">Burnout 3 VR Development</string><string name="driver_name">Burnout VR: graphics driver</string></resources>', encoding='utf-8')
run(bt/'aapt2.exe', 'compile', '--dir', root/'android/res', '-o', out/'res.zip')
run(bt/'aapt2.exe', 'compile', '--dir', overlay.parent, '-o', out/'overlay.zip')
manifest_id = re.search(r'^# id (.+)$', (root/'config/disc/ntsc-u-retail.manifest').read_text(), re.M).group(1)
info = out/'gen/io/github/xenophobentx/takedawn/BuildInfo.java'
info.parent.mkdir(parents=True, exist_ok=True)
info.write_text('package io.github.xenophobentx.takedawn;\nfinal class BuildInfo {\n'
    'static final String VERSION="0.5-quest-dev";\n'
    'static final String[] BUILDS={"ntsc-u-retail"};\n'
    f'static final String[] MANIFEST_IDS={{"{manifest_id}"}};\n'
    'static final String[] LIBRARIES={"main"};\n}\n', encoding='utf-8')
licenses = {'Takedawn': root/'LICENSE', 'xboxrecomp': work/'xboxrecomp/LICENSE',
    'SDL2': deps/'SDL2-src/LICENSE.txt', 'glslang': deps/'glslang-src/LICENSE.txt', 'OpenXR': deps/'OpenXR-SDK/LICENSE'}
for name, src in licenses.items():
    shutil.copy2(src, out/f'assets/licenses/{name}.txt')
run(bt/'aapt2.exe', 'link', '-o', out/'base.apk', '-I', jar, '--manifest', out/'AndroidManifest.xml',
    '--custom-package', 'io.github.xenophobentx.takedawn', '-R', out/'overlay.zip', '--auto-add-overlay',
    '--java', out/'gen', '--version-code', '6', '--version-name', '0.5-quest-dev',
    '--min-sdk-version', '33', '--target-sdk-version', '34', '-A', out/'assets', out/'res.zip')
sources = list((root/'android/java').rglob('*.java')) + list((deps/'SDL2-src/android-project/app/src/main/java').rglob('*.java')) + list((root/'quest/java').rglob('*.java'))
# The mobile activity asks Android for its fastest 60 Hz multiple. The XR runtime
# owns the headset display mode; keep that phone-only request out of this build.
game_java = root/'android/java/io/github/xenophobentx/takedawn/GameActivity.java'
copy_java = out/'gen/io/github/xenophobentx/takedawn/GameActivity.java'
game_source=game_java.read_text().replace('        requestDisplayMode();',
    '        // Quest display timing is owned by OpenXR.')
game_source=game_source.replace('            for (java.util.Map.Entry<String, String> e : env.entrySet()) {',
    '            QuestSettingsActivity.apply(this, env);\n            for (java.util.Map.Entry<String, String> e : env.entrySet()) {')
assert 'QuestSettingsActivity.apply(this, env)' in game_source
copy_java.write_text(game_source, encoding='utf-8')
sources.remove(game_java)
sources += list((out/'gen').rglob('*.java'))
(out/'sources.txt').write_text('\n'.join('"'+p.as_posix()+'"' for p in sources), encoding='utf-8')
run(jdk/'javac.exe', '-nowarn', '-Xlint:none', '-source', '17', '-target', '17', '-encoding', 'UTF-8',
    '-classpath', jar, '-d', out/'classes', '@'+str(out/'sources.txt'))
(out/'classes.txt').write_text('\n'.join(p.as_posix() for p in (out/'classes').rglob('*.class')), encoding='utf-8')
run(jdk/'java.exe', '-cp', bt/'lib/d8.jar', 'com.android.tools.r8.D8', '--release', '--min-api', '33',
    '--lib', jar, '--output', out/'dex', '@'+str(out/'classes.txt'))
libs = [root/'build-quest-baseline/lib/libmain.so', deps/'SDL2/lib/libSDL2.so',
    ndk/'sysroot/usr/lib/aarch64-linux-android/libc++_shared.so', deps/'openxr-loader/jni/arm64-v8a/libopenxr_loader.so']
for lib in libs:
    dest = out/'native'/lib.name
    shutil.copy2(lib, dest)
    run(ndk/'bin/llvm-strip.exe', '--strip-unneeded', dest)
with zipfile.ZipFile(out/'base.apk', 'a', zipfile.ZIP_DEFLATED) as apk:
    for lib in (out/'native').glob('*.so'):
        apk.write(lib, 'lib/arm64-v8a/'+lib.name)
    apk.write(out/'dex/classes.dex', 'classes.dex')
run(bt/'zipalign.exe', '-f', '-p', '4', out/'base.apk', out/'aligned.apk')
run(jdk/'java.exe', '-jar', bt/'lib/apksigner.jar', 'sign', '--ks', tc/'burnout-vr-development.keystore',
    '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', out/'burnout3-quest-development.apk', out/'aligned.apk')
run(jdk/'java.exe', '-jar', bt/'lib/apksigner.jar', 'verify', out/'burnout3-quest-development.apk')
print(out/'burnout3-quest-development.apk')
