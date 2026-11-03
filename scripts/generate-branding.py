"""Render the independently redrawn supplied V mark. Requires Pillow for developer regeneration only."""
from pathlib import Path
from PIL import Image, ImageDraw
# Cubic outline coordinates in a 512-square viewBox. Preserve the two interlocking V ribbons.
upper = [(45,92),('C',32,92,29,104,36,116),('L',99,221),('C',110,240,122,240,137,227),('C',165,200,178,211,193,239),('L',229,302),('C',242,325,263,325,277,302),('L',316,235),('C',332,207,345,201,371,225),('C',387,239,402,240,413,219),('L',476,113),('C',483,100,478,92,464,92),('L',393,92),('C',359,92,341,105,326,131),('L',272,225),('C',261,244,250,244,239,225),('L',185,132),('C',169,105,150,92,118,92),('Z',)]
lower = [(112,257),('C',140,235,163,232,184,269),('L',224,339),('C',243,374,270,374,289,339),('L',330,268),('C',350,232,374,235,401,257),('L',329,381),('C',292,446,221,446,183,381),('Z',)]
def points(commands):
 out=[commands[0]]
 for c in commands[1:]:
  if c[0]=='L': out.append(c[1:])
  elif c[0]=='C':
   a=out[-1]; b=c[1:3]; d=c[3:5]; e=c[5:7]
   for i in range(1,81):
    t=i/80; u=1-t
    out.append(tuple(u**3*a[j]+3*u*u*t*b[j]+3*u*t*t*d[j]+t**3*e[j] for j in range(2)))
 return out
def svgpath(commands):
 return 'M '+' '.join(map(str,commands[0]))+' '+' '.join(c[0]+' '+' '.join(map(str,c[1:])) for c in commands[1:])
root=Path(__file__).resolve().parents[1]/'assets'
root.mkdir(exist_ok=True)
svg='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512"><defs><linearGradient id="teal" x2="1" y2="1"><stop stop-color="#33e1d1"/><stop offset="1" stop-color="#10bbaa"/></linearGradient></defs>'
svg+=''.join('<path fill="url(#teal)" d="'+svgpath(c)+'"/>' for c in (upper,lower))+'</svg>'
(root/'vibera-symbol.svg').write_text(svg)
im=Image.new('RGBA',(2048,2048)); mask=Image.new('L',im.size); d=ImageDraw.Draw(mask)
for c in (upper,lower): d.polygon([(x*4,y*4) for x,y in points(c)],fill=255)
pix=im.load()
for y in range(2048):
 t=y/2048
 for x in range(2048): pix[x,y]=(int(51-35*t),int(225-38*t),int(209-39*t),255)
im.putalpha(mask)
im=im.resize((512,512),Image.Resampling.LANCZOS)
im.save(root/'vibera-symbol-512.png')
im.resize((64,64),Image.Resampling.LANCZOS).save(root/'vibera-symbol-64.png')
im.save(root/'VibeRa.ico',sizes=[(n,n) for n in (16,24,32,48,64,128,256)])
print('Redrawn transparent symbol, SVG, PNG and 7-resolution ICO generated.')
