#!/bin/sh
# Aplica o overlay fork/ sobre a árvore de build (cópia do xerpi/play-switch).
# Uso: sh scripts/apply-fork.sh [dir-da-arvore]  (padrão: upstream/play-switch)
set -e
cd "$(dirname "$0")/.."
DST="${1:-upstream/play-switch}"
if [ ! -d "$DST/Source" ]; then
	echo "[ERRO] arvore de build ausente: $DST"
	echo "Clone antes: git clone --recurse-submodules https://github.com/xerpi/play-switch.git $DST"
	exit 1
fi
cp -r fork/. "$DST/"
echo "[OK] overlay Fase 2 aplicado em $DST"
