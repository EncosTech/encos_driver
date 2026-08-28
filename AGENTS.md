# Repository Workflow Rules

- After every code change, run `bash scripts/run_clang_format.sh --check`.
- After every code change, run `bash scripts/run_clang_tidy.sh build`.
- Do not consider a code change complete until both checks finish and any reported issues are addressed.
