import argparse
import base64
import json
import os
from pathlib import Path
import shlex
import subprocess
import urllib.parse
import urllib.request
import urllib.error

parser = argparse.ArgumentParser(description='Manual local-only SonarQube CXX scan; no CI scheduling')
parser.add_argument('--env-file', type=Path)
parser.add_argument('--host-url', default='http://127.0.0.1:9000')
parser.add_argument('--scanner-image')
args = parser.parse_args()
project = Path(__file__).resolve().parents[1]
host = urllib.parse.urlparse(args.host_url)
if (host.scheme != 'http' or host.hostname not in {'127.0.0.1', 'localhost'}
        or host.port != 9000 or host.path not in {'', '/'}
        or host.username or host.password or host.query or host.fragment):
    raise SystemExit('This command only authenticates to the local loopback SonarQube')
token = os.environ.get('SONAR_TOKEN', '')
if args.env_file:
    for line in args.env_file.read_text().splitlines():
        line = line.strip().removeprefix('export ')
        if line.startswith('SONAR_TOKEN='):
            fields = shlex.split(line.split('=', 1)[1], comments=True)
            token = fields[0] if len(fields) == 1 else ''
if not token:
    raise SystemExit('Set SONAR_TOKEN or pass an explicitly authorized local --env-file; never commit the token')

def api(path, payload=None):
    body = urllib.parse.urlencode(payload).encode() if payload else None
    request = urllib.request.Request(args.host_url.rstrip('/') + path, data=body)
    request.add_header('Authorization', 'Basic ' + base64.b64encode((token + ':').encode()).decode())
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        raise RuntimeError(f'Sonar API {path.split("?")[0]} returned HTTP {error.code}; check token scope and project permissions') from None

try:
    status = api('/api/system/status')
    if status.get('status') != 'UP':
        raise RuntimeError('SonarQube is not UP')
    languages = api('/api/languages/list')['languages']
    if not any(item['key'] == 'cxx' for item in languages):
        raise RuntimeError('Community CXX plugin is not loaded; this is not a C++ scan')
    image_file = project / 'tools/sonar-scanner-image.txt'
    image = args.scanner_image or image_file.read_text().strip()
    if not image.startswith('sonarsource/sonar-scanner-cli@sha256:'):
        raise RuntimeError('Pin the official scanner image by digest')
    output = project / 'build/sonar'
    output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment['SONAR_TOKEN'] = token
    environment['SONAR_HOST_URL'] = 'http://sonarqube:9000'
    command = [
        'docker', 'run', '--rm', '--platform', 'linux/amd64', '--network', 'mcp-ozon-sonar_default',
        '-e', 'SONAR_TOKEN', '-e', 'SONAR_HOST_URL',
        '--mount', f'type=bind,src={project},dst=/usr/src,readonly',
        '--mount', f'type=bind,src={output},dst=/tmp/h755-sonar',
        image, '-Dsonar.working.directory=/tmp/h755-sonar/.scannerwork',
        '-Dsonar.scm.disabled=true', '-Dsonar.scm.exclusions.disabled=true',
    ]
    result = subprocess.run(command, env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log = result.stdout.replace(token, '[redacted]')
    (output / 'scanner.log').write_text(log)
    print(log)
    if result.returncode:
        raise SystemExit(result.returncode)
    gate = api('/api/qualitygates/project_status?projectKey=h755cpp')
    metrics = api('/api/measures/component?component=h755cpp&metricKeys=ncloc,ncloc_language_distribution,files,violations,coverage')
    report = {'server_version': status['version'], 'quality_gate': gate, 'metrics': metrics,
              'scope': 'Community CXX source analysis; toolchain probes and intentional scanner fixtures excluded; no proof of complete C++26 support or hardware behavior'}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    distribution = next((item['value'] for item in metrics['component'].get('measures', []) if item['metric'] == 'ncloc_language_distribution'), '')
    if 'cxx=' not in distribution:
        raise RuntimeError('No CXX lines reported; analysis coverage is not established')
    if gate['projectStatus']['status'] != 'OK':
        raise SystemExit('Sonar quality gate failed')
    print('Local CXX scan and quality gate verified:', args.host_url + '/dashboard?id=h755cpp')
except Exception as error:
    raise SystemExit(str(error).replace(token, '[redacted]')) from None
