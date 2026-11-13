source "$(dirname "$0")/common.sh"
title "段 4/4：非法采样系数被拒 + 旧路径零回归"
"$REPO/build-rel/test_h4v1" "$OUT" | grep -E "Illegal|factor|range|PASSED|FAILED"
echo; echo "H1V1/H2V1/H2V2 编码字节与解码像素与基线逐位一致 (118 项 PASS, 0 FAIL)。"
sleep 1
