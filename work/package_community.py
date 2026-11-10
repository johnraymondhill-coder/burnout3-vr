from pathlib import Path
import subprocess,shutil,json,zipfile,hashlib,os,sys
w=Path(__file__).resolve().parent
out=w.parent/'outputs/Burnout3VR-Community';out.mkdir(exist_ok=True)
def cp(src,dst):dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
def tracked(repo,dest,extra=()):
    names=subprocess.check_output(['git','-C',str(repo),'ls-files','-z'],text=True).split('\0')
    for name in names:
        if name and (repo/name).is_file():cp(repo/name,dest/name)
    for name in extra:
        p=repo/name
        for f in ([p] if p.is_file() else p.rglob('*')):
            if f.is_file() and '__pycache__' not in str(f) and f.name not in ('STATUS.md','PCVR-STATUS.md'):cp(f,dest/f.relative_to(repo))
root=w/'burnout3-vr';runtime=w/'xboxrecomp'
source=out/'repository'
tracked(root,source/'work/burnout3-vr',['src/game/quest_scene.c','src/game/quest_scene.h','quest','tools/quest_build_apk.py','tools/quest_build_deps.py','tools/quest_build_native.py','tools/quest_build_probe.py','tools/quest_launch.py','tools/quest_regen_windows.py','tools/quest_test_math.py'])
tracked(runtime,source/'work/xboxrecomp',['src/quest'])
for name in ['community_launcher.py','package_community.py','setup_pcvr.py','build_pcvr.py','package_pcvr.py']:
    cp(w/name,source/'work'/name)
cp(root/'LICENSE',source/'LICENSE')
cp(root/'THIRD_PARTY_NOTICES.md',source/'THIRD_PARTY_NOTICES.md')
test=source/'work/burnout3-vr/tools/quest_test_math.py'
test.write_text(test.read_text().replace("'-s','2G0YC5ZG7L05KB',",''))
versions={name:subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip() for name,repo in [('takedawn',root),('xboxrecomp',runtime)]}
(source/'UPSTREAM.json').write_text(json.dumps(versions,indent=2))
(source/'.gitignore').write_text('releases/\n**/Game/\n**/Burnout 3 Takedown/\n**/build*/\n**/__pycache__/\n**/*.iso\n**/*.xbe\n**/*.apk\n**/*.exe\n**/*.dll\n**/*.so\n**/*.o\n**/*.obj\n**/saves/\n**/cache/\n**/logs/\n')
for mode,label in [('PCVR','Burnout3VR-PCVR-Windows'),('Quest 3','Burnout3VR-Quest3-Setup-Windows')]:
    dest=out/label;dest.mkdir(exist_ok=True)
    cp(w/'community-dist/Burnout 3 VR Setup.exe',dest/'Burnout 3 VR Setup.exe')
    (dest/'launcher.json').write_text(json.dumps({'mode':mode}))
    for name in ['disc.py','xbe.py','xdvdfs.py','supported.json']:cp(root/'tools/install'/name,dest/'installer'/name)
    cp(root/'config/disc/ntsc-u-retail.manifest',dest/'installer/disc.manifest')
    if mode=='PCVR':
        original=w.parent/'outputs/Burnout 3 PCVR'
        for f in original.iterdir():
            if f.is_file() and f.suffix in ('.exe','.dll'):cp(f,dest/f.name)
        script=(original/'Launch PCVR.ps1').read_text(encoding='utf-8-sig').replace("$game='"+str(w/'pcvr-game')+"'","$game=Join-Path $base 'Game'")
        assert str(w/'pcvr-game') not in script
        # Disable development capture/telemetry in public launcher.
        script='\n'.join(line for line in script.splitlines() if not any(x in line for x in ['$env:BO3_QUEST_STEREO_CAPTURE=','$env:BO3_CTL_LOG=','$env:RECOMP_TRACE=']))+'\n'
        (dest/'Launch PCVR.ps1').write_text(script,encoding='utf-8-sig')
        cp(original/'README.txt',dest/'CONTROLS.txt')
    else:
        cp(root/'build/quest-apk/burnout3-quest-development.apk',dest/'Burnout3VR.apk')
        for name in ['adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','NOTICE.txt']:cp(w/'android-tools/platform-tools'/name,dest/'adb'/name)
    for f in (w.parent/'outputs/Burnout 3 PCVR/licenses').iterdir():
        if f.is_file():cp(f,dest/'licenses'/f.name)
    cp(root/'THIRD_PARTY_NOTICES.md',dest/'licenses/THIRD_PARTY_NOTICES.md')
    cp(Path(sys.executable).parent/'LICENSE.txt',dest/'licenses/Python.txt')
    cp(Path(sys.executable).parent/'tcl/tk8.6/license.terms',dest/'licenses/Tk.txt')
    cp(w/'release-build-tools/pyinstaller-6.22.3.dist-info/licenses/COPYING.txt',dest/'licenses/PyInstaller.txt')
    for f in runtime.rglob('*'):
        if f.is_file() and f.name.startswith(('LICENSE','COPYING','NOTICE')) and not any(x in f.parts for x in ['.git','build']):cp(f,dest/'licenses/runtime'/f.relative_to(runtime))
    (dest/'licenses/SOURCE.txt').write_text('Corresponding modified source is included in Burnout3VR-Source.zip with this release.\nUpstream commits (plus local VR modifications):\n'+json.dumps(versions,indent=2)+'\nBuild instructions: repository/BUILDING.md.\n')
    (dest/'README.txt').write_text('Extract this entire ZIP into a writable folder, then open Burnout 3 VR Setup.exe.\nChoose your original Xbox NTSC-U retail ISO and install/validate it. PS2 is unsupported.\nNo game files are included. No Python installation is needed.\n'+('Connect Quest through Virtual Desktop first, then choose quality and Launch VR.\n' if mode=='PCVR' else 'Enable Quest developer mode. Connect USB and accept USB debugging in the headset.\nRefresh devices, choose your Quest, then install your ISO. Launch VR or open the app in Unknown Sources.\nFor wireless ADB use an already authorized IP:port; USB installation is simplest.\nAfter installation the game runs entirely on Quest and USB can be disconnected.\nGraphics presets are in the Burnout VR Graphics app.\n')+'This is an experimental community preview. High PCVR quality is unbenchmarked.\nNo wheel/force-feedback support.\n')
    shutil.make_archive(str(out/label),'zip',dest)
shutil.make_archive(str(out/'Burnout3VR-Source'),'zip',source)
sums=[]
for f in sorted(out.glob('*.zip')):sums.append(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name)
(out/'SHA256SUMS.txt').write_text('\n'.join(sums)+'\n')
print(out)
