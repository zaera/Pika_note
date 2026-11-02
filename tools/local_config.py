Import("env")
from pathlib import Path
import json,subprocess
root=Path(env.subst("$PROJECT_DIR"))
path=root/"config/local.json"
cfg=json.loads(path.read_text(encoding="utf-8-sig")) if path.exists() else {}
pin=cfg.get("pin","")
if pin and (not isinstance(pin,str) or not pin.isascii() or not pin.isdigit() or len(pin)!=4):raise ValueError("config/local.json: pin must be a string of exactly 4 digits")
google=cfg.get("google_service_account_file","")
if google:
    candidate=(root/"config"/google).resolve()
    if not candidate.is_relative_to((root/"config").resolve()):raise ValueError("Google key must stay inside config/")
    if candidate.exists():cfg["google_service_json"]=candidate.read_text(encoding="utf-8-sig")
# V7 provisions credentials only after ownership/pairing, never from embedded secrets.
cfg={}
text="#pragma once\n#define PIKA_LOCAL_CONFIG_JSON "+json.dumps(json.dumps(cfg,ensure_ascii=True,separators=(",",":")))+"\n"
target=root/"pika_note/local_config.generated.h"
if not target.exists() or target.read_text()!=text:target.write_text(text)

# Stamp each build from this checkout without committing generated headers.
try:
    commit=subprocess.run(["git","-C",str(root),"rev-parse","--short=7","HEAD"],capture_output=True,text=True,check=True).stdout.strip()[:7]
except (OSError,subprocess.CalledProcessError):
    commit="local"
version="#pragma once\n#define PIKA_GIT_COMMIT "+json.dumps(commit)+"\n"
header=root/"pika_note/build_info.generated.h"
if not header.exists() or header.read_text()!=version:header.write_text(version)

# Always embed the current manager HTML, including Config actions.
import runpy
runpy.run_path(str(root/"tools/pack_manager.py"))
