"""Compose the README banner from the three reviewed Companion screenshots."""
from pathlib import Path
import argparse
import base64

parser = argparse.ArgumentParser()
parser.add_argument('screenshots', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
parts = ['''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="2560" height="1920" viewBox="0 0 1280 960" role="img" aria-labelledby="title desc">
<title id="title">Pika Companion — The same little world. On your phone.</title><desc id="desc">Real Pika Companion screens: Today, tasks and calendar, in light and dark themes.</desc>
<defs><filter id="shadow" x="-40%" y="-20%" width="180%" height="160%"><feDropShadow dx="0" dy="18" stdDeviation="15" flood-color="#39352b" flood-opacity=".19"/></filter><linearGradient id="edge" x1="0" x2="1"><stop stop-color="#fff"/><stop offset=".45" stop-color="#d9d5cc"/><stop offset="1" stop-color="#fff"/></linearGradient><clipPath id="screen"><rect x="9" y="9" width="242" height="538" rx="29"/></clipPath></defs>
<rect width="1280" height="960" rx="36" fill="#f6f4ef"/>
<circle cx="78" cy="72" r="29" fill="#e9e5dc"/><circle cx="78" cy="72" r="15" fill="#ef573f"/>
<text x="124" y="79" fill="#22211f" font-size="23" font-weight="700" font-family="Arial,Helvetica,sans-serif">pika companion</text>
<text x="1204" y="78" text-anchor="end" fill="#827d73" font-size="15" letter-spacing="2" font-family="Arial,Helvetica,sans-serif">PHONE + PIKA</text>
<text x="64" y="161" fill="#22211f" font-size="51" font-weight="700" letter-spacing="-1.8" font-family="Arial,Helvetica,sans-serif">The same little world. On your phone.</text>
<text x="67" y="199" fill="#7e7a71" font-size="21" font-family="Arial,Helvetica,sans-serif">Your day, your plans, your words. Always close at hand.</text>
<path d="M66 225H1214" stroke="#e1ddd4"/>
<ellipse cx="640" cy="841" rx="471" ry="37" fill="#e5e0d6" opacity=".7"/>
''']
for name, x, y, angle in [('1.png', 180, 277, -7), ('4.png', 846, 277, 7), ('2.png', 510, 254, 0)]:
    data = base64.b64encode((args.screenshots / name).read_bytes()).decode('ascii')
    parts.append(f'''<g transform="translate({x} {y}) rotate({angle} 130 278)"><rect x="-3" y="-3" width="266" height="562" rx="40" fill="url(#edge)" stroke="#d4cfc4" filter="url(#shadow)"/><rect width="260" height="556" rx="36" fill="#fdfcf9"/><rect x="6" y="6" width="248" height="544" rx="32" fill="#22231f"/><image x="9" y="9" width="242" height="538" preserveAspectRatio="none" clip-path="url(#screen)" xlink:href="data:image/png;base64,{data}"/><rect x="9" y="9" width="242" height="538" rx="29" fill="none" stroke="#44423a" stroke-opacity=".14"/><rect x="-5" y="91" width="4" height="43" rx="2" fill="#ccc7bd"/><rect x="-5" y="143" width="4" height="43" rx="2" fill="#ccc7bd"/><rect x="261" y="117" width="4" height="68" rx="2" fill="#ef573f"/></g>''')
parts.append('''<g fill="#22211f" font-family="Arial,Helvetica,sans-serif" font-size="21" font-weight="700" text-anchor="middle"><text x="310" y="887">A little overview.</text><text x="640" y="887">Room for your plans.</text><text x="976" y="887">Every day, together.</text></g><g fill="#878177" font-family="Arial,Helvetica,sans-serif" font-size="15" text-anchor="middle"><text x="310" y="915">TODAY</text><text x="640" y="915">TASKS</text><text x="976" y="915">CALENDAR</text></g></svg>''')
(root / 'docs/assets/pika-companion-hero.svg').write_text(''.join(parts), encoding='utf-8')
print('Built Companion banner SVG')
