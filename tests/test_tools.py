import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sonar_local',ROOT/'tools/sonar_local.py')
sonar=importlib.util.module_from_spec(spec)
spec.loader.exec_module(sonar)

class SonarTests(unittest.TestCase):
    def execute(self,args=(),overrides=None,scanner_code=0,env=None,http_error=False,scanner_output="fixture-token scanner log"):
        overrides=overrides or {}
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'tools').mkdir()
            (root/'tools/sonar-scanner-image.txt').write_text('sonarsource/sonar-scanner-cli@sha256:'+'a'*64)
            (root/'build/sonar-trust').mkdir(parents=True)
            (root/'build/sonar-trust/truststore.p12').write_bytes(b'fixture public truststore')
            (root/'token.env').write_text('IGNORED=value\nexport SONAR_TOKEN="fixture-token" # local\n')
            def response(request,timeout):
                endpoint=request.full_url.split('/api/')[-1]
                if http_error: raise urllib.error.HTTPError(request.full_url,403,'forbidden',{},None)
                values={'issues/search?componentKeys=h755cpp&resolved=false&ps=500&p=1':{'issues':[],'paging':{'total':0}},'system/status':{'status':'UP','version':'test'},'languages/list':{'languages':[{'key':'cxx'}]},
                    'qualitygates/project_status?projectKey=h755cpp':{'projectStatus':{'status':'OK'}},
                    'measures/component?component=h755cpp&metricKeys=ncloc,ncloc_language_distribution,files,violations,coverage':{'component':{'measures':[{'metric':'ncloc_language_distribution','value':'cxx=10'}]}}}
                values.update(overrides)
                return io.BytesIO(json.dumps(values[endpoint]).encode())
            (root/'build/coverage').mkdir(parents=True)
            (root/'build/coverage/sonar.xml').write_text('<coverage version="1"/>')
            output=io.StringIO()
            with patch.dict(os.environ,{'SONAR_TOKEN':'fixture-token'} if env is None else env,clear=True),patch.object(sonar.urllib.request,'urlopen',side_effect=response),patch.object(sonar.subprocess,'run',return_value=subprocess.CompletedProcess([],scanner_code,scanner_output)) as docker,contextlib.redirect_stdout(output):
                actual=list(args)
                if "--env-file" in actual:
                    index=actual.index("--env-file")+1
                    actual[index]=str(root/actual[index])
                sonar.main(actual,root)
                self.assertNotIn('fixture-token',output.getvalue())
                self.assertNotIn('fixture-token',(root/'build/sonar/scanner.log').read_text())
                self.assertEqual(docker.call_args.kwargs['env']['SONAR_TOKEN'],'fixture-token')
                self.assertNotIn('fixture-token',docker.call_args.args[0])
    def test_success_and_redaction(self): self.execute()
    def test_authorized_env_file(self): self.execute(['--env-file','token.env'])
    def test_remote_and_credential_urls_rejected(self):
        for url in ['http://example.com:9000','https://127.0.0.1:9000','http://user@127.0.0.1:9000','http://127.0.0.1:9001','http://127.0.0.1:9000/path','http://127.0.0.1:9000/?x=1']:
            with self.subTest(url=url),self.assertRaises(SystemExit):self.execute(['--host-url',url])
    def test_missing_token(self):
        with self.assertRaises(SystemExit):self.execute(env={})
    def test_server_down(self):
        with self.assertRaises(SystemExit):self.execute(overrides={'issues/search?componentKeys=h755cpp&resolved=false&ps=500&p=1':{'issues':[],'paging':{'total':0}},'system/status':{'status':'STARTING'}})
    def test_cxx_missing(self):
        with self.assertRaises(SystemExit):self.execute(overrides={'languages/list':{'languages':[]}})
    def test_unpinned_image(self):
        with self.assertRaises(SystemExit):self.execute(['--scanner-image','untrusted:latest'])
    def test_http_permission_error(self):
        with self.assertRaisesRegex(SystemExit,'HTTP 403'):self.execute(http_error=True)
    def test_scanner_failure(self):
        with self.assertRaises(SystemExit):self.execute(scanner_code=2)
    def test_parser_error_blocks_green_gate(self):
        for message in ['Source code parser: 9 syntax error(s) detected.',
                        'ERROR Unable to parse file: /usr/src/include/phy.hpp',
                        'DEBUG [phy.hpp:4]: skip declaration: class Phy {']:
            with self.subTest(message=message),self.assertRaisesRegex(SystemExit,'parser rejected'):
                self.execute(scanner_output=message)
    def test_zero_parser_errors_allowed(self):
        self.execute(scanner_output='Source code parser: 0 syntax error(s) detected.')
    def test_no_cpp_metrics(self):
        key='measures/component?component=h755cpp&metricKeys=ncloc,ncloc_language_distribution,files,violations,coverage'
        with self.assertRaises(SystemExit):self.execute(overrides={key:{'component':{'measures':[]}}})
    def test_bug_blocks_publish(self):
        key='issues/search?componentKeys=h755cpp&resolved=false&ps=500&p=1'
        with self.assertRaisesRegex(SystemExit,'commit/push'):
            self.execute(overrides={key:{'issues':[{'type':'BUG','severity':'MINOR'}],'paging':{'total':1}}})
    def test_critical_blocks_publish(self):
        key='issues/search?componentKeys=h755cpp&resolved=false&ps=500&p=1'
        with self.assertRaises(SystemExit):
            self.execute(overrides={key:{'issues':[{'type':'VULNERABILITY','severity':'CRITICAL'}],'paging':{'total':1}}})
    def test_paginated_issues(self):
        key='issues/search?componentKeys=h755cpp&resolved=false&ps=500&p='
        self.execute(overrides={key+'1':{'issues':[{'type':'CODE_SMELL','severity':'MINOR'}],'paging':{'total':2}},key+'2':{'issues':[{'type':'CODE_SMELL','severity':'MINOR'}],'paging':{'total':2}}})
    def test_incomplete_pagination(self):
        key='issues/search?componentKeys=h755cpp&resolved=false&ps=500&p=1'
        with self.assertRaisesRegex(SystemExit,'Incomplete'):
            self.execute(overrides={key:{'issues':[],'paging':{'total':1}}})
    def test_failed_gate(self):
        with self.assertRaises(SystemExit):self.execute(overrides={'qualitygates/project_status?projectKey=h755cpp':{'projectStatus':{'status':'ERROR'}}})

class ToolchainTests(unittest.TestCase):
    def execute(self,version='16.2.0',target='arm-none-eabi',header='ARM',failure=False):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'bin').mkdir();(root/'bin/arm-none-eabi-g++').write_bytes(b'fixture compiler')
            def command(args,**kwargs):
                output=''
                if '-dumpfullversion' in args:output=version
                elif '-dumpmachine' in args:output=target
                elif '-dM' in args:output='#define __cplusplus 202600L\n#define OTHER 1\n#define _GLIBCXX_ASSERTIONS 1\ninvalid\n'
                elif '-u' in args:output='std::__glibcxx_assert_fail'
                elif '-h' in args:output=header
                return subprocess.CompletedProcess(args,1 if failure else 0,output,'test compiler failure' if failure else '')
            with patch.object(sys,'argv',['check_toolchain.py','--toolchain',str(root),'--output',str(root/'out')]),patch('subprocess.run',side_effect=command),contextlib.redirect_stdout(io.StringIO()):
                runpy.run_path(str(ROOT/'tools/check_toolchain.py'),run_name='__main__')
            report=json.loads((root/'out/report.json').read_text())
            self.assertTrue(report['library_assertion_reference'])
            self.assertEqual(report['feature_macros']['_GLIBCXX_ASSERTIONS'],'1')
            self.assertEqual(report['target_runtime_execution'],'not run')
    def test_valid_report(self):self.execute()
    def test_wrong_version(self):
        with self.assertRaises(RuntimeError):self.execute(version='10.3')
    def test_wrong_architecture(self):
        with self.assertRaises(RuntimeError):self.execute(target='x86_64')
    def test_wrong_elf(self):
        with self.assertRaises(RuntimeError):self.execute(header='x86')
    def test_command_failure(self):
        with self.assertRaises(RuntimeError):self.execute(failure=True)
