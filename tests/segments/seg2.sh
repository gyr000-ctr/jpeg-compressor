source "$(dirname "$0")/common.sh"
title "段 2/4：校验 SOF 采样系数 (Y=4x1, Cb/Cr=1x1)"
python3 -I - "$OUT/h4v1.jpg" <<'PY'
import sys
d=open(sys.argv[1],"rb").read(); i=2
while i<len(d):
    if d[i]!=0xFF: i+=1; continue
    m=d[i+1]
    if m in (0xC0,0xC2):
        nf=d[i+9]; print(f"SOF 0x{m:02X} components={nf}")
        for c in range(nf):
            s=i+10+c*3; hv=d[s+1]
            print(f"  comp {c}: id={d[s]} H={hv>>4} V={hv&1}")
        break
    i+=2+((d[i+2]<<8)|d[i+3])
PY
sleep 1
