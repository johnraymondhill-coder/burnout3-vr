from pathlib import Path
import subprocess,re,shutil
w=Path(__file__).resolve().parent;d=w/'pcvr-deps';root=w/'burnout3-vr'
out=w.parent/'outputs/Burnout 3 PCVR';out.mkdir(parents=True,exist_ok=True)
shutil.copy2(root/'build/pcvr/burnout3.exe',out/'Burnout3PCVR.exe')
search=[d/'SDL2/x86_64-w64-mingw32/bin',d/'openxr/bin',d/'llvm-mingw/bin',d/'llvm-mingw/x86_64-w64-mingw32/bin']
todo=[out/'Burnout3PCVR.exe'];seen=set();missing=[]
while todo:
 p=todo.pop();seen.add(p.name.lower())
 listing=subprocess.check_output([str(d/'llvm-mingw/bin/llvm-objdump.exe'),'-p',str(p)],text=True)
 for name in re.findall(r'DLL Name:\s*(\S+)',listing):
  if name.lower() in seen:continue
  source=next((f/name for f in search if (f/name).exists()),None)
  if source:
   dest=out/name;shutil.copy2(source,dest);todo.append(dest)
  elif not (Path('C:/Windows/System32')/name).exists() and not name.lower().startswith(('api-ms-','ext-ms-')):missing.append(name)
if missing:raise SystemExit('Unresolved DLLs: '+str(missing))
data=w/'pcvr-game'
script='''param([ValidateSet('Balanced','Performance','High')][string]$Quality='Balanced')
$ErrorActionPreference='Stop'
$base=$PSScriptRoot
$exe=Join-Path $base 'Burnout3PCVR.exe'
$game='''+"'"+str(data).replace("'","''")+"'"+'''
if (!(Test-Path -LiteralPath (Join-Path $game 'Burnout 3 Takedown/default.xbe'))) { throw 'Burnout game files were not found at the configured location.' }
if (Get-Process -Name Burnout3PCVR -ErrorAction SilentlyContinue) { throw 'Burnout PCVR is already running. Close it before launching again.' }
foreach($folder in @('saves','cache','logs')) { [IO.Directory]::CreateDirectory((Join-Path $base $folder)) | Out-Null }
$env:TAKEDAWN_DATA=$game
$env:RECOMP_SAVE_DIR=Join-Path $base 'saves'
$env:RECOMP_RENDER_SHADER_CACHE=Join-Path $base 'cache'
$env:RECOMP_STDIO_LOG=Join-Path $base 'logs/latest.log'
$env:RECOMP_PB_BACKEND='vulkan'
$env:BO3_QUEST_STEREO='1'
$env:BO3_QUEST_FOVEATION='0'
$env:BO3_QUEST_HUD_SCALE='0.62'
$env:RECOMP_INPUT_CONTROL_SCHEME='xbox'
$env:RECOMP_GFX_MOTION_BLUR='0'
$env:RECOMP_GFX_HIGH_REFRESH='0'
$env:RECOMP_GFX_HDR='0'
$env:RECOMP_GFX_FILM_UPSCALE='off'
$env:RECOMP_GFX_TEXTURE_UPSCALE='off'
$env:RECOMP_PRESENT_SPIN_LEAD='0'
$env:RECOMP_GFX_AA='smaa'
$env:RECOMP_GFX_ANISO='8'
$env:RECOMP_GFX_SHARPEN='0.12'
$env:RECOMP_RENDER_SCALE= switch($Quality) { 'Performance' {'3'} 'High' {'5'} default {'4'} }
$env:BO3_QUEST_STEREO_CAPTURE=Join-Path $base 'logs/pcvr-stereo'
$env:BO3_CTL_LOG=Join-Path $base 'logs/controls.log'
$env:RECOMP_TRACE='frametime='+ (Join-Path $base 'logs/frametime.csv') + ',post_gpu'
Start-Process -FilePath $exe -WorkingDirectory $base -WindowStyle Hidden
'''
(out/'Launch PCVR.ps1').write_text(script,encoding='utf-8-sig')
for label,preset in [('Launch Burnout PCVR','Balanced'),('Launch Performance','Performance'),('Launch High Resolution','High')]:
 (out/(label+'.cmd')).write_text('@echo off\npowershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0Launch PCVR.ps1" -Quality '+preset+'\n',encoding='utf-8')
(out/'README.txt').write_text('''Burnout 3 PCVR — development build

Connect the Quest to this PC with Virtual Desktop, keep it awake, then run
Launch Burnout PCVR.cmd. The PC renders the game and streams VR to the headset.
The normal launch uses the existing active Windows OpenXR runtime.

Balanced: 2560x1920 per eye. Performance: 1920x1440. High: 3200x2400.
These are rendered eye-image dimensions; perceived sharpness also depends on
Virtual Desktop streaming quality. Balanced has been tested on this PC's
RTX 3060 Ti with Quest 3 / Virtual Desktop; the player confirmed smoother
gameplay and clear stereo depth. High resolution is an optional untested preset.
First-time shader compilation can cause hitches.

The game targets 60 FPS. For even presentation, select 120 FPS in Virtual
Desktop's headset VR streaming settings if offered. The app uses the refresh
rates Virtual Desktop exposes; the present loop repeats the latest completed
game image between updates so headset tracking can continue at headset cadence.

Menus and briefing appear on a floating screen. Racing enters stereo VR at GO.
Left stick steers; right trigger accelerates; left trigger brakes/reverses.
A confirms/boosts, B returns, Y changes view. Tap left Menu to pause;
hold it to recenter, or click the right thumbstick to recenter immediately.

PC saves are in saves/. Logs and eye captures are in logs/. The Quest app and
its saves are separate. This local launcher references your extracted game
folder; original game files are not bundled in this folder.

This development build does not add steering-wheel or force-feedback support.
''',encoding='utf-8')
lic=out/'licenses';lic.mkdir(exist_ok=True)
for name,src in [('Takedawn',root/'LICENSE'),('xboxrecomp',w/'xboxrecomp/LICENSE'),('OpenXR',w/'burnout-deps/OpenXR-SDK/LICENSE'),('SDL2',w/'burnout-deps/SDL2-src/LICENSE.txt'),('glslang',w/'burnout-deps/glslang-src/LICENSE.txt')]:shutil.copy2(src,lic/(name+'.txt'))
print(out)
