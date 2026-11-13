# CI and local SonarQube

## Event ownership: avoid duplicate feature/PR runs

One CI workflow owns host tests and security checks. It runs on pull requests into
main, pushes to main, and explicit manual dispatch. It does not run on feature-branch
pushes: an open PR receives one run per synchronization, not an additional push run.
New PR runs cancel obsolete PR runs; main runs are not cancelled by subsequent pushes.

A main push after merge runs only the three CodeQL analyses to update the
default-branch security baseline. Host tests/sanitizers, Cppcheck, Clang analysis,
Semgrep, Actionlint and Gitleaks run in PRs or an explicit manual dispatch only;
they do not repeat after merge. CodeQL still compiles the portable C++ under tracing,
but does not execute its tests. The aggregate Quality gate also runs only in PRs
and manual dispatches, where every dependency must succeed; on main each CodeQL job
reports its own result. Skipped PR-only jobs on main are expected, not test results.
Direct main pushes therefore receive CodeQL analysis only; use a PR for full validation.
Do not enable CodeQL default setup alongside this advanced workflow. There is no
second Sonar/cloud workflow or automatic local Sonar schedule.

## Checks and scope

- Host build, warnings as errors, unit and mocked-register integration, ASan/UBSan.
- Cppcheck and Clang static analysis of portable MDIO code via its host test translation unit.
  Clang 18 uses its own CMake compilation database and libc++ 18: GCC module-scanning
  flags and GCC 14's standard-library feature gates are incompatible with this analyzer.
  C++26 remains required; this is a second host library, not target-runtime analysis.
- Semgrep CE with pinned CLI, versioned local rules, rule fixtures, strict parse/error
  handling, and explicit nonempty expected-file coverage checks. These project policy
  rules are not a comprehensive security ruleset or a lifetime proof.
- CodeQL security-extended for C/C++, Python helpers, and GitHub Actions. C++ extraction
  uses a separately traced host build; this rebuild is required by the analyzer rather
  than an accidental duplicate test job. Inspect extractor diagnostics before declaring coverage.
- Actionlint and redacted Gitleaks history scan. GitHub secret scanning/push protection
  remains independently enabled.
- In PRs/manual dispatches, Quality gate fails if any required configured job fails,
  is cancelled, or is skipped. It is skipped on main pushes along with PR-only checks.

CodeQL's published language support currently lists through C++23, with GNU compiler
extensions through GCC 16. This does not establish full C++26 contracts support.
The host extraction covers portable MDIO logic and tests, not the target-only CMSIS
adapter or native-contract toolchain probe. Cppcheck uses its supported C++23 parser
for the same compatible source subset; compilation remains C++26. No source is
rewritten or silently compiled under an older requested standard to make analysis green.

M7 GCC 16.2 Debug/Release checks are currently local. Target CI, fuzzing, hardware/e2e,
runtime stack/deadline measurements and release-image verification remain separate
unmet gates. No firmware deployment or release workflow is enabled: there is no complete
firmware image yet. Future CD must consume verified immutable CI artifacts rather than
rebuild the same release independently.

## Sonar: manual local workflow only

Local server: loopback port 9000, Docker Community Build 26.7. C++ support is supplied
by SonarOpenCommunity sonar-cxx 2.3.1.1656, not commercial CFamily. Its release matrix
tests 26.1/25.8; this 26.7 instance successfully loaded the plugin and registered its CXX profile.
Plugin SHA256: `b64327bf680a9fe4a04bb04f13ecbe679f4611c6908ec9eb0df027229d183a8a`.
Before installation, the database and plugin directory were backed up outside this repository.
Only the SonarQube container was restarted; PostgreSQL was preserved.

Run manually when useful (for example once a development day):

```sh
python3 tools/sonar_local.py --env-file /path/to/authorized-local-sonar.env
```

Alternatively supply SONAR_TOKEN through the process environment. Do not put credentials
in GitHub or committed files for this local workflow. The command accepts only a loopback
server, activates CXX for this project, submits local project `h755cpp` (requiring analysis
and project-creation permission if it does not exist),
and runs the pinned official scanner image in the existing Sonar Docker network.
The scanner uses https://sonarqube:9443 with a mounted PKCS#12 truststore.
The TLS front shares the Sonar container network namespace and proxies only to
127.0.0.1:9000; it publishes no host port. The helper API remains restricted to host
loopback 9000. No certificate-verification bypass or insecure fallback is used. Reports
are local under ignored `build/sonar/`. The source mount is read-only; tokens are passed
via environment and redacted from saved logs. Nothing is uploaded to an external Sonar service.
SCM blame and Git-ignore filtering are disabled for this local scan so Docker can analyze
Git worktrees without accessing their external Git metadata. Only the explicit source/test
directories are scanned; Git blame and new-code attribution are not established by this run.

Source exclusions are explicit: toolchain probes use native C++26 syntax outside established
plugin support; Semgrep fixtures deliberately contain forbidden operations. Actual application
files remain in scope. A quality gate result alone is not evidence of complete parsing,
test completeness, commercial CFamily checks, or target behavior. Read scanner diagnostics
and verify CXX file/line metrics. External analyzer reports and coverage are not yet imported.
The initial source-only scan had unresolved include warnings and a local Docker-HTTP
finding. The link/coverage work removed that finding with a real local TLS front, fixed
critical Python-complexity findings and imported host/MCU/Python coverage. See
[link monitor and coverage](link-monitor-and-coverage.md). No Sonar rule or gate was
relaxed to obtain the passing result. Community CXX remains limited by its parser and
installed rules; zero findings are not proof of absence of all vulnerabilities.

TLS image: nginx@sha256:901e593c95dc85f5a729f5765516321366ef4ac401821431d408897e95287d0a.
Configuration: security/sonar/nginx.conf. The local container is h755-sonar-tls, with
network mode container:mcp-ozon-sonar-sonarqube-1, no published ports and read-only
configuration/cert mounts. It can be stopped/started manually. Recreate it if the Sonar
container is recreated. Existing Sonar/PostgreSQL containers were not restarted.
The certificate SAN is sonarqube; the private key is local mode 600 and never committed.
The scanner truststore contains the public certificate only and uses its documented
standard password; it contains no private key or token. No Mac system trust settings
were modified. Copy the public truststore to ignored build/sonar-trust/truststore.p12
in each checkout. Certificate renewal/recreation is a local administrator operation.
See [Sonar TLS documentation](https://docs.sonarsource.com/sonarqube-server/analyzing-source-code/scanners/scanner-environment/manage-tls-certificates).
