import struct,sys
d=open('re/MoviesSE.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[]
for i in range(n):
    o=pe+24+opt+40*i
    name=d[o:o+8].rstrip(b'\0');vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8)
    secs.append((name,va,rs,ro))
text=[s for s in secs if s[0]==b'.text'][0]
_,va,rs,ro=text
t=d[ro:ro+rs]
addrs=[int(a,16) for a in sys.argv[1:]]
for a in addrs:
    b=struct.pack('<I',a);res=[];i=t.find(b)
    while i>=0:
        res.append(hex(0x400000+va+i));i=t.find(b,i+1)
    print(hex(a),' '.join(res))
