from pathlib import Path
import gzip,re
root=Path.cwd();header=(root/'pika_note/src/app/manager_assets.h').read_text();payload=bytes(map(int,re.findall(r'\d+',header.split('{',1)[1].split('}',1)[0])));html=gzip.decompress(payload).decode('utf-8');source=(root/'tools/manager.html').read_text(encoding='utf-8');tag=re.search(r'kPortalCssTag = "([^"]+)"',(root/'pika_note/src/app/portal_assets.h').read_text()).group(1)
assert html==source.replace('href="portal.css"','href="/portal.css?v='+tag+'"')
for token in ['Factory reset','open-factory-reset','confirm-factory-reset','factoryReset','Backup & restore','Undo last change']:assert token in html,token
assert 'tools/pack_manager.py' in (root/'tools/local_config.py').read_text()
print('Embedded manager matches source; factory reset UI, handler, backup, undo and automatic packing: PASS')
