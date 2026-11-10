"""Windows community setup and launch UI. Game data is always supplied locally."""
import hashlib,json,os,queue,subprocess,sys,threading
from pathlib import Path
import tkinter as tk
from tkinter import ttk,filedialog,messagebox
BASE=Path(sys.executable).parent if getattr(sys,'frozen',False) else Path(__file__).parent
sys.path.insert(0,str(BASE/'installer'))
import disc

def verify(folder):
    entries=[]
    manifest=(BASE/'installer/disc.manifest').read_text()
    for line in manifest.splitlines():
        if line and not line.startswith('#'):
            digest,size,name=line.split(' ',2); entries.append((digest,int(size),name))
    for digest,size,name in entries:
        p=folder/name
        if not p.is_file() or p.stat().st_size!=size: raise RuntimeError('Missing or wrong size: '+name)
        h=hashlib.sha256()
        with p.open('rb') as f:
            for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
        if h.hexdigest()!=digest:raise RuntimeError('Disc validation failed: '+name)
    mid=next(x[5:] for x in manifest.splitlines() if x.startswith('# id '))
    return f'xr-install 1\nmanifest {mid}\nversion 0.3.1\nfiles {len(entries)}\nbytes {sum(x[1] for x in entries)}\n'

def call(args,**kw):
    return subprocess.run(list(map(str,args)),check=True,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                          creationflags=subprocess.CREATE_NO_WINDOW,**kw).stdout

class App:
    def __init__(self):
        self.mode=json.loads((BASE/'launcher.json').read_text())['mode']
        self.root=tk.Tk();self.root.title('Burnout 3 VR — '+self.mode);self.root.geometry('700x440')
        self.events=queue.Queue();self.busy=False;self.source=tk.StringVar();self.quality=tk.StringVar(value='Balanced');self.device=tk.StringVar();self.ip=tk.StringVar()
        body=ttk.Frame(self.root,padding=18);body.pack(fill='both',expand=True)
        ttk.Label(body,text='Burnout 3 VR • '+self.mode,font=('Segoe UI',18,'bold')).pack(anchor='w')
        ttk.Label(body,text='Requires your original Xbox NTSC-U retail ISO. PS2 and other regions are unsupported.').pack(anchor='w',pady=8)
        row=ttk.Frame(body);row.pack(fill='x');ttk.Entry(row,textvariable=self.source).pack(side='left',fill='x',expand=True)
        ttk.Button(row,text='Choose ISO',command=self.choose).pack(side='right')
        self.buttons=[]
        self.button(body,'Install / validate my game',self.install)
        if self.mode=='Quest 3':
            row=ttk.Frame(body);row.pack(fill='x',pady=5)
            ttk.Combobox(row,textvariable=self.device,state='readonly',width=31).pack(side='left');self.devices=row.winfo_children()[0]
            self.button(row,'Refresh devices',self.refresh,side='left')
            row=ttk.Frame(body);row.pack(fill='x');ttk.Entry(row,textvariable=self.ip,width=25).pack(side='left')
            self.button(row,'Connect Wi-Fi IP:port',self.connect,side='left')
            ttk.Label(body,text='Enable developer mode; connect USB and accept the headset debugging prompt first.').pack(anchor='w')
        else:
            ttk.Label(body,text='Resolution per eye: Performance 1920×1440 • Balanced 2560×1920 • High 3200×2400').pack(anchor='w',pady=12)
            ttk.Combobox(body,textvariable=self.quality,values=['Performance','Balanced','High'],state='readonly').pack(anchor='w')
        self.button(body,'Launch VR',self.launch)
        self.status=tk.StringVar(value='Choose your ISO to begin. Your disc files stay on your own devices.')
        ttk.Label(body,textvariable=self.status,wraplength=650).pack(anchor='w',pady=10)
        self.root.after(100,self.poll)
    def button(self,parent,text,fn,side=None):
        b=ttk.Button(parent,text=text,command=lambda:self.work(fn));b.pack(**({'side':side} if side else {'anchor':'w','pady':6}));self.buttons.append(b)
    def choose(self):
        p=filedialog.askopenfilename(filetypes=[('Xbox disc image','*.iso'),('All files','*.*')])
        if p:self.source.set(p)
    def work(self,fn):
        if self.busy:return
        # Read Tk values on the UI thread only.
        self.input=(self.source.get(),self.device.get(),self.ip.get(),self.quality.get())
        self.busy=True
        for b in self.buttons:b.configure(state='disabled')
        def job():
            try:fn();self.events.put(('done','Ready.'))
            except Exception as e:self.events.put(('error',str(e)))
        threading.Thread(target=job,daemon=True).start()
    def poll(self):
        while not self.events.empty():
            kind,value=self.events.get()
            if kind=='devices':self.devices['values']=value;self.device.set(value[0] if value else '')
            else:self.status.set(value)
            if kind in ('done','error'):
                self.busy=False
                for b in self.buttons:b.configure(state='normal')
                if kind=='error':messagebox.showerror('Burnout 3 VR',value)
        self.root.after(100,self.poll)
    def adb(self,*args,selected=False):
        prefix=['-s',self.input[1]] if selected and self.input[1] else []
        if selected and not prefix:raise RuntimeError('Refresh devices and choose an authorized Quest first.')
        return call([BASE/'adb/adb.exe',*prefix,*args])
    def refresh(self):
        lines=self.adb('devices').splitlines()[1:]
        self.events.put(('devices',[x.split()[0] for x in lines if x.strip().endswith('\tdevice')]))
    def connect(self):
        import re
        if not re.fullmatch(r'[0-9.]+:[0-9]+',self.input[2]):raise RuntimeError('Enter the headset IP and authorized ADB port, for example 192.168.1.100:5555.')
        self.events.put(('status',self.adb('connect',self.input[2])));self.refresh()
    def install(self):
        if not self.input[0]:raise RuntimeError('Choose your ISO first.')
        builds=disc.load_supported(str(BASE/'installer/supported.json'))[:1]
        disc.probe(self.input[0],builds)
        game=BASE/'Game/Burnout 3 Takedown'
        (game.parent/'.complete').unlink(missing_ok=True)
        self.events.put(('status','Extracting your ISO. This can take several minutes…'))
        disc.install(self.input[0],str(game),builds,lambda f,n:self.events.put(('status',f'Extracting: {f:.0%} — {n}')))
        self.events.put(('status','Checking every file against the supported disc manifest…'))
        marker=verify(game);(game.parent/'.complete').write_text(marker)
        if self.mode=='Quest 3':
            self.events.put(('status','Installing app and transferring your game to Quest. Keep it connected…'))
            self.adb('install','-r',BASE/'Burnout3VR.apk',selected=True)
            remote='/sdcard/Android/data/io.github.xenophobentx.takedawn/files'
            self.adb('shell','mkdir','-p',remote,selected=True)
            self.adb('push',game,remote+'/',selected=True)
            self.adb('push',game.parent/'.complete',remote+'/.complete',selected=True)
    def launch(self):
        if self.mode=='Quest 3':
            self.adb('shell','am','start','-n','io.github.xenophobentx.takedawn/.QuestStartActivity',selected=True)
        else:
            if not (BASE/'Game/.complete').is_file():raise RuntimeError('Install and validate your ISO first.')
            call(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',BASE/'Launch PCVR.ps1','-Quality',self.input[3]])

if __name__=='__main__':App().root.mainloop()
