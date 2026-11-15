import argparse
import base64
import hashlib
import json
import os
import re
from pathlib import Path
import shlex
import subprocess
import urllib.parse
import urllib.request
import urllib.error


def validate_coverage(project):
    summary=project/'build/coverage/summary.json'
    xml=project/'build/coverage/sonar.xml'
    if not summary.is_file() or not xml.is_file():
        raise RuntimeError('Fresh whole-owned-code coverage report is required before Sonar')
    report=json.loads(summary.read_text())
    groups=report.get('groups',{})
    if (not report.get('passed') or report.get('unmeasured') or
        any(groups.get(kind,{}).get('percent',0)<=80 for kind in ('c-cpp','python')) or
        any(item.get('kind')=='assembly-instructions' and
            (item.get('total',0)<=0 or item.get('covered')!=item.get('total'))
            for item in report.get('files',{}).values())):
        raise RuntimeError('Whole-owned-code coverage gate has not passed')
    expected=report.get('source_digests')
    actual={path.relative_to(project).as_posix():hashlib.sha256(path.read_bytes()).hexdigest()
            for directory in ('include','src','firmware','tools')
            for path in sorted((project/directory).rglob('*'))
            if path.suffix in {'.hpp','.h','.cpp','.c','.py','.s'} and 'probes' not in path.parts}
    if expected!=actual:
        raise RuntimeError('Coverage report is stale for the current owned sources')


def validate_host(url):
    host=urllib.parse.urlparse(url)
    if (host.scheme!='http' or host.hostname not in {'127.0.0.1','localhost'}
        or host.port!=9000 or host.path not in {'','/'} or host.username
        or host.password or host.query or host.fragment):
        raise SystemExit('This command only authenticates to the local loopback SonarQube')


def load_token(env_file):
    token=os.environ.get('SONAR_TOKEN','')
    if env_file:
        for line in env_file.read_text().splitlines():
            line=line.strip().removeprefix('export ')
            if line.startswith('SONAR_TOKEN='):
                fields=shlex.split(line.split('=',1)[1],comments=True)
                token=fields[0] if len(fields)==1 else ''
    if not token: raise SystemExit('Set SONAR_TOKEN or pass an explicitly authorized local --env-file; never commit the token')
    return token


class LocalApi:
    def __init__(self,url,token):
        self.url=url.rstrip('/')
        self.authorization='Basic '+base64.b64encode((token+':').encode()).decode()
    def get(self,path):
        request=urllib.request.Request(self.url+path)
        request.add_header('Authorization',self.authorization)
        try:
            with urllib.request.urlopen(request,timeout=30) as response: return json.load(response)
        except urllib.error.HTTPError as error:
            error.close()
            raise RuntimeError(f'Sonar API {path.split("?")[0]} returned HTTP {error.code}; check token scope and project permissions') from None


def check_server(api):
    status=api.get('/api/system/status')
    if status.get('status')!='UP': raise RuntimeError('SonarQube is not UP')
    if not any(item['key']=='cxx' for item in api.get('/api/languages/list')['languages']):
        raise RuntimeError('Community CXX plugin is not loaded; this is not a C++ scan')
    return status


def target_toolchain(project):
    files=list((project/'build/cm7-ethernet-coverage/CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    compilers=set()
    for file in files:
        match=re.search(r'^set\(CMAKE_CXX_COMPILER "([^"]+)"\)$',file.read_text(),re.M)
        if match: compilers.add(Path(match.group(1)))
    if len(compilers)!=1:
        raise RuntimeError('The instrumented target build must identify exactly one C++ compiler')
    compiler=compilers.pop()
    if compiler.name!='arm-none-eabi-g++' or not compiler.is_file():
        raise RuntimeError('The instrumented target compiler is missing')
    manifest=json.loads((project/'docs/toolchain-validation.json').read_text())
    if hashlib.sha256(compiler.read_bytes()).hexdigest()!=manifest['compiler_sha256']:
        raise RuntimeError('Instrumented target compiler differs from the pinned toolchain')
    if subprocess.check_output([compiler,'-dumpfullversion'],text=True).strip()!='16.2.0':
        raise RuntimeError('Local Sonar requires the pinned GCC 16.2 target headers')
    toolchain=compiler.parent.parent
    includes=('arm-none-eabi/include/c++/16.2.0',
              'arm-none-eabi/include/c++/16.2.0/arm-none-eabi',
              'arm-none-eabi/include/c++/16.2.0/backward',
              'lib/gcc/arm-none-eabi/16.2.0/include',
              'lib/gcc/arm-none-eabi/16.2.0/include-fixed',
              'arm-none-eabi/include')
    if any(not (toolchain/path).is_dir() for path in includes):
        raise RuntimeError('Pinned GCC 16.2 target include tree is incomplete')
    return toolchain


def scanner_command(project,image,output,toolchain):
    if not image.startswith('sonarsource/sonar-scanner-cli@sha256:'):
        raise RuntimeError('Pin the official scanner image by digest')
    truststore=project/'build/sonar-trust/truststore.p12'
    if not truststore.is_file(): raise RuntimeError('Local TLS truststore is missing; configure the local Sonar TLS front first')
    command=['docker','run','--rm','--platform','linux/amd64','--network','mcp-ozon-sonar_default',
        '-e','SONAR_TOKEN','-e','SONAR_HOST_URL',
        '--mount',f'type=bind,src={truststore},dst=/tls/truststore.p12,readonly',
        '--mount',f'type=bind,src={project},dst=/usr/src,readonly',
        '--mount',f'type=bind,src={toolchain},dst=/opt/h755-arm,readonly',
        '--mount',f'type=bind,src={output},dst=/tmp/h755-sonar',image,
        '-Dsonar.working.directory=/tmp/h755-sonar/.scannerwork',
        '-Dsonar.scm.disabled=true','-Dsonar.scm.exclusions.disabled=true',
        '-Dsonar.scanner.truststorePath=/tls/truststore.p12']
    command.append('-Dsonar.coverageReportPaths=build/coverage/sonar.xml')
    return command


def scan(project,args,token,output):
    image=args.scanner_image or (project/'tools/sonar-scanner-image.txt').read_text().strip()
    toolchain=target_toolchain(project)
    environment=os.environ.copy()
    environment.update(SONAR_TOKEN=token,SONAR_HOST_URL='https://sonarqube:9443')
    result=subprocess.run(scanner_command(project,image,output,toolchain),env=environment,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    log=result.stdout.replace(token,'[redacted]')
    (output/'scanner.log').write_text(log)
    print(log)
    if result.returncode: raise SystemExit(result.returncode)
    if re.search(r'Source code parser:\s*[1-9]\d*\s+syntax error|Unable to parse file:|skip declaration:',log):
        raise RuntimeError('Sonar CXX parser rejected source; analysis incomplete, commit/push is not allowed')
    if re.search(r'Preprocessor:\s*[1-9]\d* include directive error\(s\)',log):
        raise RuntimeError('Sonar CXX could not resolve target headers; analysis incomplete')


def fetch_issues(api):
    issues=[];page=1
    while True:
        batch=api.get(f'/api/issues/search?componentKeys=h755cpp&resolved=false&ps=500&p={page}')
        issues.extend(batch['issues'])
        if len(issues)>=batch['paging']['total']: return issues
        if not batch['issues']: raise RuntimeError('Incomplete Sonar issue pagination')
        page+=1


def read_report(api,status):
    gate=api.get('/api/qualitygates/project_status?projectKey=h755cpp')
    metrics=api.get('/api/measures/component?component=h755cpp&metricKeys=ncloc,ncloc_language_distribution,files,violations,coverage')
    issues=fetch_issues(api)
    return {'issues':issues,'blocking_issues':[i for i in issues if i.get('type')=='BUG' or i.get('severity') in {'BLOCKER','CRITICAL'}],
        'server_version':status['version'],'quality_gate':gate,'metrics':metrics,
        'scope':'Community CXX source analysis plus imported reports; no proof of complete C++26 support or absence of vulnerabilities'}


def validate_report(report):
    measures=report['metrics']['component'].get('measures',[])
    distribution=next((i['value'] for i in measures if i['metric']=='ncloc_language_distribution'),'')
    if 'cxx=' not in distribution: raise RuntimeError('No CXX lines reported; analysis coverage is not established')
    if report['blocking_issues']:
        raise RuntimeError(f"Sonar found {len(report['blocking_issues'])} bugs or critical/blocker issues; commit/push is not allowed")
    if report['quality_gate']['projectStatus']['status']!='OK': raise SystemExit('Sonar quality gate failed')


def main(argv=None,project=None):
    parser=argparse.ArgumentParser(description='Manual local-only SonarQube CXX scan; no CI scheduling')
    parser.add_argument('--env-file',type=Path)
    parser.add_argument('--host-url',default='http://127.0.0.1:9000')
    parser.add_argument('--scanner-image')
    args=parser.parse_args(argv)
    project=project or Path(__file__).resolve().parents[1]
    validate_host(args.host_url)
    token=load_token(args.env_file)
    try:
        validate_coverage(project)
        api=LocalApi(args.host_url,token)
        status=check_server(api)
        output=project/'build/sonar';output.mkdir(parents=True,exist_ok=True)
        scan(project,args,token,output)
        report=read_report(api,status)
        (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        validate_report(report)
        print('Local CXX scan and quality gate verified:',args.host_url+'/dashboard?id=h755cpp')
    except Exception as error:
        raise SystemExit(str(error).replace(token,'[redacted]')) from None


if __name__=='__main__': main()
