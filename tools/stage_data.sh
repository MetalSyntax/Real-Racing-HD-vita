#!/bin/sh
# Prepara ux0_data/<slug>/ (= ux0:data/<slug>/ en la PS Vita) desde el app bundle (game_app).
# Imita el sandbox de iOS para que las rutas del juego se resuelvan directamente:
#   <AppName>.app/   bundle limpio (ejecutable + recursos), sin firmas de Apple ni basura
#   rebase.bin       tabla de rebase generada por tools/macho_prep.py
#   saves/           partidas (Documents/ de iOS); deploy nunca sobreescribe ni borra su contenido
#   logs/            <slug>_001.log .. <slug>_999.log rotativos escritos por el loader
#   controls.txt     mapeo de botones (extras/controls.txt; nunca se pisa si ya existe)
#
# Uso: tools/stage_data.sh [ruta/a/App.app]

set -e
cd "$(dirname "$0")/.."

APP="${1:-game_app}"
if [ ! -d "$APP" ]; then
    echo "[-] Error: No se encuentra el bundle en $APP (¿falta crear el symlink game_app?)"
    exit 1
fi

# Detectar ejecutable e identificador desde Info.plist
EXEC_NAME=$(python3 -c "
import plistlib, sys
try:
    with open('$APP/Info.plist', 'rb') as f:
        pl = plistlib.load(f)
    print(pl.get('CFBundleExecutable', ''))
except Exception:
    print('')
")

if [ -z "$EXEC_NAME" ] || [ ! -f "$APP/$EXEC_NAME" ]; then
    # Fallback: buscar el binario ejecutable más grande en la raíz del .app
    EXEC_NAME=$(find "$APP" -maxdepth 1 -type f -perm +111 -exec ls -1S {} + 2>/dev/null | head -n 1 | xargs basename 2>/dev/null || echo "")
fi

if [ -z "$EXEC_NAME" ] || [ ! -f "$APP/$EXEC_NAME" ]; then
    echo "[-] Error: No se pudo identificar el ejecutable Mach-O en $APP"
    exit 1
fi

# Detectar slug
SLUG=$(python3 -c "
import json, re, os
slug = ''
if os.path.exists('.psvita-toolkit.json'):
    try:
        with open('.psvita-toolkit.json') as f:
            slug = json.load(f).get('slug', '')
    except Exception:
        pass
if not slug and os.path.exists('CMakeLists.txt'):
    m = re.search(r'ux0:data/([^/]+)/?', open('CMakeLists.txt').read())
    if m:
        slug = m.group(1)
print(slug or 'ios_game')
")

# Nombre fijo del bundle stagiado: el loader resuelve BUNDLE_PATH (hle.h) como ux0:data/<slug>/game_app/
APP_DIR_NAME="game_app"

OUT="ux0_data/$SLUG"
echo "[*] Preparando datos para '$SLUG' desde '$APP/$EXEC_NAME'..."
mkdir -p "$OUT/$APP_DIR_NAME" "$OUT/saves" "$OUT/logs" "docs"

# Copiar recursos del bundle excluyendo firmas y metadatos innecesarios.
# Los archivos sueltos de res/ (2149) van a rb/XX/ (abajo): exFAT busca nombres en forma lineal en la
# Vita (~250 ms por apertura en res/, logs 004/012); en cubetas de ~34 la apertura es casi gratis.
rsync -a --delete \
  --exclude '/rb/' \
  --include '/res/*/' --exclude '/res/*' \
  --exclude '._*' \
  --exclude '.DS_Store' \
  --exclude '_CodeSignature' \
  --exclude 'SC_Info' \
  --exclude 'embedded.mobileprovision' \
  --exclude 'CodeResources' \
  "$APP/" "$OUT/$APP_DIR_NAME/"

# res/<archivo> -> rb/<FNV-1a(minúsculas) % 64>/<archivo> (= res_redirect en source/darwin.c); rb/.done al final
python3 -I - "$APP/res" "$OUT/$APP_DIR_NAME/rb" <<'EOF'
import os, shutil, sys
sys.path.insert(0, "tools")
from deploy_data import res_bucket
src, rb = sys.argv[1], sys.argv[2]
names = [n for n in os.listdir(src) if not n.startswith("._") and n != ".DS_Store" and os.path.isfile(os.path.join(src, n))]
for b in range(64):
    os.makedirs(f"{rb}/{b:02x}", exist_ok=True)
for n in names:
    s, d = os.path.join(src, n), f"{rb}/{res_bucket(n):02x}/{n}"
    old = os.path.join(rb, "..", "res", n)  # staging previo con res/ plano
    if os.path.isfile(old):
        os.replace(old, d)
    if not os.path.exists(d) or os.path.getsize(d) != os.path.getsize(s):
        shutil.copy2(s, d)
open(f"{rb}/.done", "w").write("1")
print(f"[*] res/ -> rb/XX/: {len(names)} archivos en 64 cubetas")
EOF

# exFAT en macOS crea AppleDouble (._*); deploy_data.py ya los salta, pero se limpian aquí
command -v dot_clean >/dev/null 2>&1 && dot_clean -m "$OUT" || true

# Generar rebase.bin y reporte de imports
echo "[*] Analizando Mach-O y generando rebase.bin..."
python3 -I tools/macho_prep.py "$OUT/$APP_DIR_NAME/$EXEC_NAME" "$OUT/rebase.bin" --report docs/macho_report.txt

# Mapeo de controles editable por el usuario: solo si no existe (no pisar su calibración)
[ -f "$OUT/controls.txt" ] || cp extras/controls.txt "$OUT/controls.txt"

# Audio/video: Real Racing HD solo usa WAV PCM (SFX, OpenAL) y MP3 (música, AudioQueue), ambos se
# decodifican en la consola (coreaudio.c + libmpg123): no hay conversión offline. Se verifica aquí.
python3 -I tools/media_check.py "$OUT/$APP_DIR_NAME"

echo "[+] Staging completado en $OUT/"
echo "    - $OUT/$APP_DIR_NAME/ ($EXEC_NAME y recursos)"
echo "    - $OUT/rebase.bin"
echo "    - $OUT/saves/ (directorio de guardado)"
echo "    - $OUT/logs/ (directorio de logs)"
echo "    - $OUT/controls.txt"
echo "[+] Listo para desplegar con: python3 -I tools/deploy_data.py o 'psvita-toolkit deploy --data'"
