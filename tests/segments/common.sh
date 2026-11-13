REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$REPO/bin/jpge"
TD="$REPO/output/testdata"
OUT="$REPO/output/demo"
mkdir -p "$OUT"
export TERM=xterm-256color
title() {
	printf '\n\033[1;36m================================================================\n'
	printf '  %s\n' "$1"
	printf '================================================================\033[0m\n'
	sleep 1
}
