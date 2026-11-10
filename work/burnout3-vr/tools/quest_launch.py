"""Install/launch a one-session Quest test without replacing saved preferences."""
from pathlib import Path
import argparse
import subprocess
parser=argparse.ArgumentParser()
parser.add_argument('--stereo',action='store_true')
parser.add_argument('--install',action='store_true')
parser.add_argument('--capture',action='store_true')
parser.add_argument('--serial',help='USB serial or authorized wireless ADB address')
parser.add_argument('--foveation',choices=['on','off'])
parser.add_argument('--scale',type=float,choices=[1.5,2.0,2.5,3.0,3.5])
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
adb=root.parent/'android-tools/platform-tools/adb.exe'
devices=subprocess.check_output([str(adb),'devices'],text=True)
serials=[line.split()[0] for line in devices.splitlines()[1:] if line.strip().endswith('\tdevice')]
if args.serial:
    if args.serial not in serials:raise SystemExit('The selected authorized Quest connection is not available.')
    serial=args.serial
elif len(serials)==1:serial=serials[0]
else:raise SystemExit('Select one authorized Quest connection with --serial (USB and Wi-Fi may show the same headset twice).')
def run(*cmd):subprocess.run([str(adb),'-s',serial,*map(str,cmd)],check=True)
if args.install:run('install','-r',root/'build/quest-apk/burnout3-quest-development.apk')
folder='/sdcard/Android/data/io.github.xenophobentx.takedawn/files'
env=[f'BO3_QUEST_STEREO={int(args.stereo)}','RECOMP_INPUT_CONTROL_SCHEME=xbox',
    'RECOMP_GFX_MOTION_BLUR=0','RECOMP_GFX_HIGH_REFRESH=0','RECOMP_GFX_HDR=0',
    'RECOMP_PRESENT_SPIN_LEAD=0',f'BO3_CTL_LOG={folder}/quest-controls.log',
    f'RECOMP_TRACE=frametime={folder}/quest-frametime.csv,vk_frame={folder}/quest-vulkan-frames.csv,post_gpu']
if args.scale:env.append(f'RECOMP_RENDER_SCALE={args.scale:g}')
if args.foveation:env.append(f'BO3_QUEST_FOVEATION={int(args.foveation=="on")}')
if args.capture:env.append(f'BO3_QUEST_STEREO_CAPTURE={folder}/quest-stereo')
config=root/'build/quest-apk/scene-env.txt'
config.write_text('\n'.join(env)+'\n',encoding='utf-8')
run('push',config,folder+'/scene-env.txt')
run('shell','am','force-stop','io.github.xenophobentx.takedawn')
run('shell','input','keyevent','KEYCODE_WAKEUP')
run('shell','am','start','-n','io.github.xenophobentx.takedawn/.QuestStartActivity')
