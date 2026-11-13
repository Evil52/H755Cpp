# CI and local SonarQube

## Event ownership: avoid duplicate feature/PR runs

One CI workflow owns host tests and security checks. It runs on pull requests into
main, pushes to main, and explicit manual dispatch. It does not run on feature-branch
pushes: an open PR receives one run per synchronization, not an additional push run.
New PR runs cancel obsolete PR runs; main runs are not cancelled by subsequent pushes.

A main run after merge is intentional verification of the integrated commit and
updates the default-branch security baseline. It is separate from the PR candidate.
Do not enable CodeQL default setup alongside this advanced workflow. There is no
second Sonar/cloud workflow or automatic local Sonar schedule.

## Checks and scope

- Host build, warnings as errors, unit and mocked-register integration, ASan/UBSan.
- Cppcheck and Clang static analysis of portable MDIO code via its host test translation unit.
- Semgrep CE with pinned CLI, versioned local rules, rule fixtures, strict parse/error
  handling, and explicit nonempty expected-file coverage checks. These project policy
  rules are not a comprehensive security ruleset or a lifetime proof.
- CodeQL security-extended for C/C++, Python helpers, and GitHub Actions. C++ extraction
  uses a separately traced host build; this rebuild is required by the analyzer rather
  than an accidental duplicate test job. Inspect extractor diagnostics before declaring coverage.
- Actionlint and redacted Gitleaks history scan. GitHub secret scanning/push protection
  remains independently enabled.
- Quality gate fails if any required configured job fails, is cancelled, or is skipped.

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
and runs the pinned official scanner image in the existing Sonar Docker network. Reports
are local under ignored `build/sonar/`. The source mount is read-only; tokens are passed
via environment and redacted from saved logs. Nothing is uploaded to an external Sonar service.
SCM blame and Git-ignore filtering are disabled for this local scan so Docker can analyze
Git worktrees without accessing their external Git metadata. Only the explicit source/test
directories are scanned; Git blame and new-code attribution are not established by this run.

Source exclusions are explicit: toolchain probes use native C++26 syntax outside established
plugin support; Semgrep fixtures deliberately contain forbidden operations. Actual application
files remain in scope. A quality gate result alone is not evidence of complete parsing,
test coverage, commercial CFamily checks, or target behavior. Read scanner diagnostics
and verify CXX file/line metrics. External analyzer reports and coverage are not yet imported.
The initial scan passed its quality gate and reported CXX lines, but warned about
unresolved includes (including standard-library headers). Its source metrics and rules
must not be presented as complete semantic C++ analysis.
The first baseline returned no evaluated new-code gate conditions. It also reported
`python:S5332` for HTTP inside the existing local Docker bridge; that finding remains
visible. This helper intentionally targets the local server requested by the user,
not a remote endpoint. A remote/shared deployment needs TLS and a separate credential policy.
