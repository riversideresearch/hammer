# Cppcheck Usage

This folder contains configuration for running **cppcheck** on the project.

## Run Analysis
From the project root:
```bash
./tools/cppcheck/run.sh
```
or this directory:
```bash
./run.sh
```

## Suppressions
- Suppressions are listed in `suppressions.txt`.
- Add entries here if a warning is a false positive.

## Notes
- Reports are outputted to `tools/cppcheck/report.txt`.
