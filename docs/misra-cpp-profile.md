# MISRA C++ project assessment

Baseline: MISRA C++:2023, publisher-listed TC1:2026. The guidelines target C++17.
H755Cpp deliberately uses a verified C++26 subset with GCC16.2. This is a project
safety profile informed by applicable MISRA guidance, not a claim of full MISRA
C++:2023 compliance or ISO 26262 certification. The language selection is unchanged.

## Scope and enforcement

Owned C++ in include/, src/ and firmware/cm7/ is in scope. Tests need review for
relevant risks and faithful hardware semantics; they are not delivered firmware.
Pinned CMSIS headers, ST C startup/system code, assembly M4 parking and the compiler
runtime are adopted components with provenance and integration evidence. They are
not silently declared compliant or covered by MISRA C++ alone. C and assembly need
their own appropriate assessment. Python tools remain subject to their existing
checks, outside the C++ guideline language scope.

Compiler warnings, the curated .clang-tidy checks, Cppcheck, project Semgrep rules,
CodeQL and local Sonar continue their current roles. Their combined success does
not establish coverage of all MISRA guidelines. No dedicated MISRA C++:2023 rule
coverage has been verified. Sonar remains manual/local; CI deduplication is retained.
No placeholder MISRA CI job or new service subscription is added.

| Feature/assumption | Actual use | Evidence and remaining assessment |
| --- | --- | --- |
| C++23 std::expected | MDIO/link operations and application startup return explicit errors | Host success/error/timeout tests and target compile; outside C++17, needs separate language-feature assessment |
| C++26 contracts | processor.cpp checks its internal SysTick reload invariant | Target enforce compile/link, explicit fatal hook; target violation/safe-output acceptance remains unverified |
| MMIO and CMSIS intrinsics | Board, processor and typed CMSIS MDIO adapter | Narrow hardware boundary, target build/HIL; exact applicable guidelines and implementation assumptions need review |
| Volatile diagnostic/ISR state | Debugger ABI, SysTick owner/counter | Explicit ownership and host lifecycle tests; not a general multicore/atomicity guarantee |
| Unsigned elapsed-time wrap | MDIO, link/application timing | Boundary/rollover tests; bounded timeout assumptions remain part of the API contract |
| Vendor-only analyzer suppressions | Two exact unused CMSIS linker-boundary comparisons | Narrow locations in security/cppcheck/suppressions.txt; no owned-code suppression; this is not a MISRA deviation approval |

## Rule assessment and deviations

Before a formal assessment, obtain authorized access to the exact guideline and
MISRA Compliance editions, then build the rule matrix from those sources. Record
identifier/category, applicability, checked source/configuration, tool and rule
version or manual review, result and evidence. Unknown and unassessed entries are
not passes. Do not invent rule IDs from memory or map unrelated warnings by name.

For an actual deviation record the exact guideline, narrow locations, necessity,
alternatives, risk, compensating controls/tests, owner/reviewer, disposition and
recheck trigger. A suppression does not approve a deviation; do not waive a
non-deviable requirement. No deviations are represented as approved by this file.
New unresolved applicable findings block acceptance under the selected profile;
a full-compliance release additionally needs the complete guideline/process evidence.

Pending: authoritative rule-by-rule assessment, verified automated/manual guideline
coverage, post-C++17 feature review and adopted-component dispositions. Existing
coverage >80% and zero scanner findings do not close these gaps. Automotive use
additionally requires product hazards/safety goals/ASIL, suitable hardware and tool
confidence, traceable safety requirements and verification/assessment.

Publisher references checked 2026-11-14:
- https://misra.org.uk/product/misra-cpp2023/
- https://misra.org.uk/compliance/
