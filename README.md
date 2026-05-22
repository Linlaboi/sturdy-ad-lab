# Wazuh Active Response & Automated Incident Response (IR) Lab

This module focuses on moving beyond simple security detection and telemetry gathering into **Automated Defense and Active Response orchestration**. Operating inside a multi-platform Active Directory environment (`sturdy-ad-lab`), this project demonstrates the implementation of a modern, centralized XDR framework utilizing **Wazuh 5.0**.

Rather than relying on legacy configuration frameworks (`ossec.conf`/`ar.conf` file edits on individual agents), this lab showcases the latest architectural shift towards a **UI-Driven, Centralized Metadata-Driven Execution** model managed via the Wazuh Dashboard Alerting and Notifications ecosystem.

---

## 🏗️ Architectural Overview (Wazuh 5.0 Shift)

In traditional setups, the Wazuh Manager relies on localized analysis configurations to fire scripts. This lab deploys the updated **Wazuh 5.0 unified XDR blueprint**:

1. **The Telemetry Loop**: Edge endpoints (Windows 11 Clients, Domain Controllers, Linux nodes) stream high-fidelity event streams (including Sysmon and Security Logs) to the Wazuh indexer streams (`wazuh-events-v5-*`).
2. **Centralized Detection**: The Wazuh Dashboard Alerting engine evaluates events using custom **Per-Document Monitors**.
3. **Execution Documents**: When a monitor trigger fires, it automatically writes a dedicated execution payload document to the `active-responses-*` index.
4. **Metadata-Driven Routing**: The Wazuh Manager distributes these JSON execution payloads down to the target agent. The agent's `execd` engine parses the execution metadata directly from `stdin` without needing any local configuration lookups.

---

## 🛠️ Implemented Automated Defense Scenarios

### 1. Brute Force Attack Auto-Blocking (`block-ip`)
* **Objective**: Automatically drop malicious connection loops trying to brute force RDP or SSH network interfaces.
* **The Flow**: 
  * The attacker conducts a brute force scan (e.g., via `hydra.exe`).
  * Instead of triggering separate blocks on 20 distinct raw Event ID 4625 documents, the Wazuh Manager aggregates the count and creates a high-fidelity correlation alert (`rule.id: 5712`).
  * A Per-Document monitor picks up the high-level alert document and initiates the `block-ip` response channel.
  * **The Remediation**: The endpoint consumes the JSON metadata via standard input, maps the attacker's `source.ip`, and executes a stateful firewall drop loop via `netsh` (falling back to standard network routing engines if needed).
  * **Stateful Execution**: Configured with a stateful timeout (e.g., 180 seconds) to clear the block automatically, maintaining business uptime and preventing accidental permanent administrator lockouts.

### 2. Orchestrated Identity Isolation (`disable-account.ps1`)
* **Objective**: Defend Active Directory infrastructure against compromised identity vectors (e.g., fast-frequency Kerberoasting or client lateral movement).
* **The Flow**: 
  * An attack or automated script hits a high-frequency credential scanning threshold or triggers a specialized "Bait Account" (e.g., honey token service account with a registered SPN).
  * Telemetry is picked up on a standard domain-joined Windows 11 client machine (`Agent 002`).
  * **Centralized Orchestration**: Instead of trying to run an identity isolation script locally on the low-privilege client machine, the Wazuh 5.0 engine uses a custom **Defined-Agent Route**.
  * **The Remediation**: The Wazuh Manager targets the Active Response block directly to the **Domain Controller (`Agent 001`)**. The DC ingests the attacker's metadata fields using OpenSearch mustache context templates (`{{ctx.alerts.0._source.win.eventdata.targetUserName}}`), executing a seamless `Disable-ADAccount` script loop without touching the edge client.

### 3. Malware Behavioral Killer & Quarantine
* **Objective**: Terminate malicious execution steps and clear active threats verified via intelligence streams before encryption steps (e.g., Ransomware attempting to wipe shadow copies via `vssadmin.exe`) take place.
* **The Flow**:
  * File Integrity Monitoring (FIM) or Sysmon flags an anomalous binary initialization or file drop.
  * The hash context payload routes through the Threat Intelligence integration workflow.
  * **The Remediation**: Active response invokes process termination and immediate target file quarantine/deletion, verifying the atomic signature context to minimize enterprise false positives.

---

## 🛠️ Link to how to create and setup on Wazuh Dashboard
https://github.com/wazuh/wazuh-dashboard-plugins/blob/main/docs/ref/modules/active-response/create.md 

## 💻 Script Specifications & Best Practices

All integration scripts added to `/var/ossec/active-response/bin/` (Linux) or `C:\Program Files (x86)\ossec-agent\active-response\bin\` (Windows) strictly adhere to production-grade engineering constraints:

1. **STDIN Binding**: Scripts do not accept standard positional arguments (`$1`, `$2`); they dynamically swallow and parse structured JSON strings directly from `stdin`.
2. **State Commands**: Implements native support handlers for both `"command": "enable"` and `"command": "disable"` states to safely support stateful lifecycle loops.
3. **Enterprise Unified Logging**: Transacts status events directly to `/var/ossec/logs/active-responses.log` (or Windows equivalent) inside a standardized metadata structure: `Date - ScriptName - Action - Status/Target Payload`.
4. **Clean Exit Codes**: Explicitly terminates loops with exit code `0` on verified operations and exit code `1` if unexpected inputs, missing components, or syntax errors are detected.

---

```bash
echo '{"wazuh":{"active_response":{"name":"block-ip","executable":"block-ip","type":"stateless"}},"source":{"ip":"192.168.1.100"},"command":"enable"}' | /var/ossec/active-response/bin/block-ip
