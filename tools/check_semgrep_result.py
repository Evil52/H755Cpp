import json
from pathlib import Path
import sys

report = json.loads(Path(sys.argv[1]).read_text())
if report.get('errors'):
    raise SystemExit('Semgrep reported parse/analysis errors; scan is incomplete')
paths = report.get('paths', {}).get('scanned', [])
if not paths:
    raise SystemExit('Semgrep scanned no source files')
required = {'include/h755/ethernet/mdio.hpp', 'src/ethernet/cmsis_mdio.cpp'}
normalized = {Path(path).as_posix().removeprefix('./') for path in paths}
if not required.issubset(normalized):
    raise SystemExit(f'Missing expected Semgrep source coverage: {sorted(required - normalized)}')
if report.get('results'):
    raise SystemExit('Semgrep findings require review')
print(f'Semgrep analyzed {len(paths)} files without parser errors or rule findings')
