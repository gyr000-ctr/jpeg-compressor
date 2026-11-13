source "$(dirname "$0")/common.sh"
title "段 1/4：用仓库自带入口生成 4:1:1 (H4V1) 素材"
echo "\$ jpge -h4v1 colorbars.tga h4v1.jpg 95"; sleep 1
"$BIN" -h4v1 "$TD/colorbars.tga" "$OUT/h4v1.jpg" 95
sleep 1
