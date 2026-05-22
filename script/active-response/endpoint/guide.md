# Wazuh 5.0 Active Response — C-Based Script Implementation Guide

A concise reference for building, deploying, and testing native C-based Active Response executables on Wazuh 5.0 endpoints.

---

## Overview

Active responses in Wazuh 5.0 are triggered through the Dashboard's **Per-Document Monitors** and pushed as structured JSON payloads to target agents. The agent-side daemon responsible for handling these is `wazuh-execd`, which forks a child process and pipes the full alert metadata directly to the script via `stdin`.

---

## How `wazuh-execd` Works

- Forks a child process and opens a `stdin` pipe to pass the full JSON alert payload.
- Positional CLI arguments (`argv[1]`, `argv[2]`) are **ignored entirely** — all input must be read from `stdin`.
- Scripts must read and act on a `command` field in the JSON payload, which declares the intended action: `"enable"` (apply mitigation) or `"disable"` (rollback mitigation).

---

## Core Requirements

Every production-grade C active response binary must satisfy the following:

### Windows DLL Hardening
On any `WIN32` build, the very **first line of logic inside `main()`** must call `enable_dll_verification()`. This protects elevated system processes from untrusted DLL injection.

### JSON Parsing
Input must be parsed using a structured tokenizer — the standard within Wazuh is **cJSON** — following the Elastic Common Schema (ECS) field path hierarchy.

### Exit Codes
`wazuh-execd` evaluates exit codes strictly:
- `0` (OS_SUCCESS) — fully completed execution
- `1` (OS_INVALID / failure) — parsing errors, unauthorized inputs, or system-level failures

### Logging
All operational events must be appended to the platform-specific `active-responses.log` using direct file handlers (unbuffered, asynchronous). Log paths:
- **Linux:** `/var/ossec/logs/active-responses.log`
- **Windows:** `C:\Program Files (x86)\ossec-agent\active-response\active-responses.log`

---

## Program Structure Summary

A compliant C active response binary should follow this logical flow:

1. On Windows, call `enable_dll_verification()` immediately.
2. Read the entire JSON payload from `stdin` using `fread`.
3. Parse the JSON with cJSON; log and exit `1` on any parse failure.
4. Extract and validate the `command` field — reject anything other than `"enable"` or `"disable"`.
5. Traverse the JSON hierarchy to extract the target IP, following the ECS path: `wazuh → active_response → parameters → alert → _source → source → ip`. Fall back to the root-level `source.ip` field if the nested path is absent.
6. If no valid target IP is found, log an error and exit `1`.
7. Execute the remediation or rollback logic based on the `command` value.
8. Log the outcome and exit `0` on success, `1` on failure.

---

## Build & Deployment

### Linux
- Install dependencies: `libcjson-dev` and `gcc`.
- Compile the binary and place it in `/var/ossec/active-response/bin/`.
- Set permissions to `750` with ownership `root:wazuh`.

### Windows (Cross-compiled from Linux)
- Install the `mingw-w64` cross-compilation toolchain.
- Compile targeting `x86_64-w64-mingw32-gcc` with the `WIN32` preprocessor flag.
- Deploy the resulting `.exe` to `C:\Program Files (x86)\ossec-agent\active-response\bin\`.

---

## Local Testing

Before wiring the script to the Wazuh Indexer Dashboard, validate it locally by simulating the `stdin` pipe directly from the terminal.

Pipe a JSON payload with `command` set to `"enable"` or `"disable"` and a `source.ip` value directly into the compiled binary. After execution, inspect `active-responses.log` to confirm the expected log entries appear with correct timestamps and action labels.

Both the nested ECS path and the flat root-level fallback path for `source.ip` should be tested separately to confirm graceful handling of both schema variants.

---

## Key File Paths Reference

| Purpose | Linux Path | Windows Path |
|---|---|---|
| Active response binaries | `/var/ossec/active-response/bin/` | `C:\Program Files (x86)\ossec-agent\active-response\bin\` |
| Active response log | `/var/ossec/logs/active-responses.log` | `C:\Program Files (x86)\ossec-agent\active-response\active-responses.log` |

---

*This guide reflects Wazuh 5.0 architectural standards for C-based active response development. For script-level implementation details, refer to the accompanying `custom_active_response.c` source template.*
