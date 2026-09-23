from pathlib import Path

Import('env')
root = Path(env['PROJECT_DIR'])
html = (root / 'src/web.html').read_text(encoding='utf-8')
script = (root / 'src/web.js').read_text(encoding='utf-8')
html = html.replace('/*LEPUS_SCRIPT*/', script)
target = root / 'include/WebPage.h'
content = '#pragma once\ninline String htmlPage() { return R"lepus(' + html + ')lepus"; }\n'
if not target.exists() or target.read_text(encoding='utf-8') != content:
    target.write_text(content, encoding='utf-8')
