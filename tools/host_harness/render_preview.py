import sys, os
from PIL import Image, ImageDraw, ImageFont
# Usage: render_preview.py <dir-containing-cmds.txt>   (writes prev_<frame>.png beside it)
S=sys.argv[1]
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import glob
FONT=(glob.glob('/usr/share/fonts/**/DejaVuSans.ttf',recursive=True) or glob.glob('/usr/share/fonts/**/*Sans*Regular*.ttf',recursive=True))[0]
order=[l.strip() for l in open(os.path.join(ROOT,'assets/icons/order.txt'))]
icons=[Image.open(os.path.join(ROOT,f'build/icons/{n}.png')).convert('RGBA') for n in order]
def rgba(u): return (u&255,(u>>8)&255,(u>>16)&255,(u>>24)&255)
frames={}; cur=None
for line in open(S+'/cmds.txt'):
    line=line.rstrip('\n')
    if line.startswith('F '): cur=line[2:]; frames[cur]=[]; continue
    frames[cur].append(line)
SC=2
for name,lines in frames.items():
    screens={0:Image.new('RGBA',(400*SC,240*SC)),1:Image.new('RGBA',(320*SC,240*SC))}
    cmds={0:[],1:[]}; tgt=0; clear={}
    for ln in lines:
        p=ln.split(' ',1)
        if p[0]=='S': tgt=int(p[1]); continue
        if p[0]=='C': a=p[1].split(); clear[int(a[0])]=rgba(int(a[1])); continue
        cmds[int(ln.split()[1])].append(ln)
    for t in (0,1):
        im=screens[t]; d=ImageDraw.Draw(im,'RGBA'); d.rectangle([0,0,im.width,im.height],fill=clear.get(t,(0,0,0,255)))
        items=[]
        for i,ln in enumerate(cmds[t]):
            a=ln.split(' ',8); kind=a[0]
            z=float(a[4]) if kind in 'TRGOIK' else float(a[7]) if kind=='L' else 0
            items.append((z,i,ln))
        items.sort()
        for z,i,ln in items:
            k=ln[0]
            if k=='R':
                _,_,x,y,z_,w,h,c=ln.split(); x,y,w,h=map(float,(x,y,w,h))
                if w>0 and h>0: d.rectangle([x*SC,y*SC,max(x*SC,(x+w)*SC-1),max(y*SC,(y+h)*SC-1)],fill=rgba(int(c)))
            elif k=='G':
                _,_,x,y,z_,w,h,a1,b1,c1,d1=ln.split(); x,y,w,h=map(float,(x,y,w,h)); top=rgba(int(a1)); bot=rgba(int(c1))
                for r in range(int(h*SC)):
                    f=r/(h*SC); col=tuple(int(top[j]+(bot[j]-top[j])*f) for j in range(4)); d.line([x*SC,y*SC+r,(x+w)*SC,y*SC+r],fill=col)
            elif k=='O':
                _,_,x,y,z_,r,c=ln.split(); x,y,r=map(float,(x,y,r)); d.ellipse([(x-r)*SC,(y-r)*SC,(x+r)*SC-1,(y+r)*SC-1],fill=rgba(int(c)))
            elif k=='L':
                _,_,x0,y0,x1,y1,th,z_,c=ln.split(); x0,y0,x1,y1,th=map(float,(x0,y0,x1,y1,th)); d.line([x0*SC,y0*SC,x1*SC,y1*SC],fill=rgba(int(c)),width=max(1,int(th*SC)))
            elif k=='T':
                a=ln.split(' ',7); x,y,z_,sx,c,txt=float(a[2]),float(a[3]),a[4],float(a[5]),int(a[6]),a[7]
                f=ImageFont.truetype(FONT,int(26*sx*SC)); d.text((x*SC,y*SC),txt,font=f,fill=rgba(c),anchor='ls')
            elif k=='K':
                _,_,x,y,z_,sc=ln.split(); x,y,sc=float(x),float(y),float(sc); sz=int(64*sc*SC)
                d.rectangle([x*SC,y*SC,x*SC+sz,y*SC+sz],fill=(70,130,170,255)); d.rectangle([x*SC,y*SC,x*SC+sz//2,y*SC+sz//2],fill=(200,90,70,255))
            elif k=='I':
                _,_,x,y,z_,sc,idx,c=ln.split(); x,y,sc=float(x),float(y),float(sc); ic=icons[int(idx)]
                sz=int(32*sc*SC); ic=ic.resize((sz,sz),Image.LANCZOS); col=rgba(int(c)); tint=Image.new('RGBA',ic.size,col); tint.putalpha(ic.split()[3]); im.alpha_composite(tint,(int(x*SC),int(y*SC)))
    w=screens[0].width+screens[1].width+20
    sheet=Image.new('RGBA',(w,240*SC),(60,60,60,255)); sheet.paste(screens[0],(0,0)); sheet.paste(screens[1],(screens[0].width+20,0))
    sheet.convert('RGB').save(f'{S}/prev_{name}.png')
print('ok')
