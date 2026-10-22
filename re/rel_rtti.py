import struct,sys
d=open('re/MoviesSE.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[]
for i in range(n):
    o=pe+24+opt+40*i
    vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8);secs.append((va,rs,ro))
def off2va(off):
    for va,rs,ro in secs:
        if ro<=off<ro+rs: return 0x400000+va+off-ro
def va2off(a):
    rva=a-0x400000
    for va,rs,ro in secs:
        if va<=rva<va+rs: return ro+rva-va
def findall(b):
    r=[];i=d.find(b)
    while i>=0: r.append(i);i=d.find(b,i+1)
    return r
for name in sys.argv[1:]:
    so=d.find(name.encode()+b'\0')
    td=off2va(so)-8
    for c in findall(struct.pack('<I',td)):
        col=off2va(c)-12
        if struct.unpack_from('<I',d,c-12)[0]!=0: continue
        for v in findall(struct.pack('<I',col)):
            vt=off2va(v)+4
            ents=[hex(struct.unpack_from('<I',d,va2off(vt)+4*k)[0]) for k in range(24)]
            print(name,'COL',hex(col),'vtable',hex(vt),' '.join(ents))
