source "$(dirname "$0")/common.sh"
title "段 3/4：两种上采样解码 + 与 stb 参考对照"
"$BIN" -d "$OUT/h4v1.jpg" "$OUT/h4v1_linear.tga"
"$BIN" -d -box_filtering "$OUT/h4v1.jpg" "$OUT/h4v1_box.tga"
"$BIN" -d -s -box_filtering "$OUT/h4v1.jpg" "$OUT/h4v1_stb_ref.tga"
python3 -I - "$OUT/h4v1_box.tga" "$OUT/h4v1_stb_ref.tga" <<'PY'
import struct,sys
def load(p):
    d=open(p,"rb").read(); w,h=struct.unpack_from("<HH",d,12)
    bpp,desc,typ=d[16],d[17],d[2]; n=bpp//8; off=18+d[0]; rb=w*n
    if typ==2: raw=bytearray(d[off:off+h*rb])
    else:
        raw=bytearray(); pos=off
        while len(raw)<h*rb:
            hd=d[pos]; pos+=1; cnt=(hd&0x7F)+1
            if hd&0x80: raw+=d[pos:pos+n]*cnt; pos+=n
            else: raw+=d[pos:pos+cnt*n]; pos+=cnt*n
    px=bytearray(w*h*n); tl=desc&0x20
    for y in range(h):
        sy=y if tl else h-1-y; px[sy*rb:(sy+1)*rb]=raw[y*rb:(y+1)*rb]
    return w,h,n,px
w,h,n,a=load(sys.argv[1]); _,_,_,b=load(sys.argv[2])
print("box 模式 jpgd vs stb 最大通道差 =", max(abs(a[i]-b[i]) for i in range(w*h*n)))
PY
sleep 1
