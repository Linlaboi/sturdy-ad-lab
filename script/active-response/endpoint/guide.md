# Wazuh 5.0 Custom Active Response: Automatic User Logoff

## Overview

This guide walks you through building and deploying a **production-grade Windows active response script** that automatically logs off users when brute-force attacks are detected by Wazuh.

**Problem Solved**: In Wazuh 5.0, PowerShell-to-exe converted scripts don't execute immediately due to broken STDIN/STDOUT pipes with `wazuh-execd`. This native C implementation follows the official Wazuh architecture and works identically to built-in scripts like `block-ip.exe`.

---

## Prerequisites

### Required Software
- **Windows Client** with Wazuh Agent 5.0 installed
- **C Compiler** (choose one):
  - **Visual Studio 2019/2022** (recommended) - [Download Community Edition](https://visualstudio.microsoft.com/downloads/)
  - **MinGW-w64** - [Download](https://www.mingw-w64.org/)

### Required Libraries
- **cJSON** - Lightweight JSON parser (we'll download this)

---

## Part 1: Download Dependencies

### Step 1.1: Get cJSON Library

Download the cJSON source files (just 2 files):

**Option A: Using PowerShell**
```powershell
# Create working directory
New-Item -ItemType Directory -Force -Path C:\Temp\wazuh-ar
cd C:\Temp\wazuh-ar

# Download cJSON files
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c" -OutFile "cJSON.c"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h" -OutFile "cJSON.h"
```

**Option B: Manual Download**
1. Go to https://github.com/DaveGamble/cJSON
2. Download `cJSON.c` and `cJSON.h`
3. Save both files to `C:\Temp\wazuh-ar\`

### Step 1.2: Get the Active Response Script

Save the `log-off.c` file (provided separately) to the same directory:
```
C:\Temp\wazuh-ar\
├── cJSON.c
├── cJSON.h
└── log-off.c
```

---

## Part 2: Compilation

### Option A: Using Visual Studio (Recommended)

**Step 2.1**: Open **Developer Command Prompt for VS 2022** (or your VS version)
- Search for "Developer Command Prompt" in Start menu
- Run as Administrator

**Step 2.2**: Navigate to your working directory
```cmd
cd C:\Temp\wazuh-ar
```

**Step 2.3**: Compile the script
```cmd
cl /O2 /W3 log-off.c cJSON.c /Fe:log-off.exe wtsapi32.lib
```

**Expected Output**:
```
Microsoft (R) C/C++ Optimizing Compiler Version...
log-off.c
cJSON.c
Generating Code...
Microsoft (R) Incremental Linker Version...
```

---

### Option B: Using MinGW

**Step 2.1**: Open Command Prompt as Administrator

**Step 2.2**: Navigate to your working directory
```cmd
cd C:\Temp\wazuh-ar
```

**Step 2.3**: Compile the script
```cmd
gcc -O2 -Wall log-off.c cJSON.c -o log-off.exe -lwtsapi32
```

**Expected Output**:
```
(No output means success)
```

---

### Verify Compilation

Check that `log-off.exe` was created:
```cmd
dir log-off.exe
```

You should see:
```
05/22/2026  10:30 AM           45,056 log-off.exe
```

---

## Part 3: Deployment to Wazuh Agent

### Step 3.1: Stop the Wazuh Agent Service

```powershell
Stop-Service -Name WazuhSvc
```

### Step 3.2: Backup Old Script (if exists)

```powershell
# Backup your old PS2EXE version
$oldScript = "C:\Program Files (x86)\ossec-agent\active-response\bin\log-off.exe"
if (Test-Path $oldScript) {
    Copy-Item $oldScript "$oldScript.backup"
}
```

### Step 3.3: Deploy the New Executable

```powershell
# Copy the new native C executable
Copy-Item "C:\Temp\wazuh-ar\log-off.exe" "C:\Program Files (x86)\ossec-agent\active-response\bin\log-off.exe" -Force
```

### Step 3.4: Set Permissions

```powershell
# Verify SYSTEM account has execute permissions
$acl = Get-Acl "C:\Program Files (x86)\ossec-agent\active-response\bin\log-off.exe"
$permission = "NT AUTHORITY\SYSTEM","FullControl","Allow"
$accessRule = New-Object System.Security.AccessControl.FileSystemAccessRule $permission
$acl.SetAccessRule($accessRule)
Set-Acl "C:\Program Files (x86)\ossec-agent\active-response\bin\log-off.exe" $acl
```

### Step 3.5: Start the Wazuh Agent Service

```powershell
Start-Service -Name WazuhSvc
```

---

## Part 4: Configure Active Response on Wazuh Manager

### Step 4.1: Define the Command

On your **Wazuh Manager (Ubuntu)**, edit the configuration:

```bash
sudo nano /var/ossec/etc/ossec.conf
```

Add this command definition inside the `<ossec_config>` block:

```xml
<command>
  <name>logoff-command</name>
  <executable>log-off.exe</executable>
  <timeout_allowed>no</timeout_allowed>
</command>
```

### Step 4.2: Bind the Command to Your Detection Rule

Add this active response binding (replace `100002` with your actual Hydra detection rule ID):

```xml
<active-response>
  <command>logoff-command</command>
  <location>local</location>
  <rules_id>100002</rules_id>
</active-response>
```

### Step 4.3: Restart Wazuh Manager

```bash
sudo systemctl restart wazuh-manager
```

---

## Part 5: Testing

### Step 5.1: Clear Old Logs

On your **Windows Client**:

```powershell
# Clear the debug log
Remove-Item "C:\Program Files (x86)\ossec-agent\active-response\bin\logoff-debug.log" -ErrorAction SilentlyContinue
```

### Step 5.2: Trigger the Attack

From your **attacker machine** (e.g., Kali Linux):

```bash
hydra -l johndoe -P /usr/share/wordlists/rockyou.txt rdp://192.168.1.100
```

**Important**: Make sure `johndoe` is actually logged into the Windows client.

### Step 5.3: Observe Real-Time Response

**Within 30 seconds**, you should see:

1. ✅ **Windows Client**: User `johndoe` gets immediately logged off (no restart needed!)
2. ✅ **Debug Log**: `logoff-debug.log` shows execution logs
3. ✅ **OpenSearch Dashboard**: Alert fires and shows active response triggered

---

## Part 6: Verification

### Check the Debug Log

On **Windows Client**:

```powershell
Get-Content "C:\Program Files (x86)\ossec-agent\active-response\bin\logoff-debug.log" -Tail 20
```

**Expected Output** (should appear within 30 seconds of attack):

```
2026-05-22 14:23:15 log-off: Starting
2026-05-22 14:23:15 log-off: {"command":"enable","user":{"name":"johndoe"},...}
2026-05-22 14:23:15 log-off: Target user: johndoe
2026-05-22 14:23:15 log-off: {"version":1,"origin":{"name":"log-off","module":"active-response"},"command":"check_keys","parameters":{"keys":["johndoe"]}}
2026-05-22 14:23:15 log-off: {"command":"continue"}
2026-05-22 14:23:15 log-off: Found session ID: 2
2026-05-22 14:23:15 log-off: SUCCESS: Logged off session 2
```

### Check OpenSearch Alert

Go to **OpenSearch Dashboards** → **Wazuh** → **Security Events**

Search for:
```
rule.id: 100002
```

You should see:
- Alert with `user.name: johndoe`
- `rule.description` matching your Hydra detection
- Timestamp matching the attack time

---

## Troubleshooting

### Issue 1: Script Not Executing

**Symptom**: No entries in `logoff-debug.log` after attack

**Check**:
```powershell
# Verify the executable exists
Test-Path "C:\Program Files (x86)\ossec-agent\active-response\bin\log-off.exe"

# Check Wazuh agent is running
Get-Service WazuhSvc
```

**Fix**:
- Restart the agent: `Restart-Service WazuhSvc`
- Check manager configuration matches the command name exactly

---

### Issue 2: "Cannot Read Response from Execd"

**Symptom**: Log shows `"Cannot read response from execd"`

**Cause**: The deduplication handshake failed

**Check Manager Logs**:
```bash
sudo tail -f /var/ossec/logs/ossec.log | grep active-response
```

**Fix**:
- Verify your Wazuh Manager is version 5.0+
- Ensure `wazuh-manager` service is running

--->

Ensure Sysmon or Windows Event Logs provide the target username in the event.

---

### Issue 3: "No Active Session"

**Symptom**: Log shows `"No active session for johndoe"`

**Cause**: The user isn't actually logged in

**Fix**:
- Log into Windows as `johndoe` before testing
- Use `query user` to verify active sessions:
  ```cmd
  query user
  ```

---

## Architecture Comparison

### Your Old PS2EXE Script vs. Native C Implementation

| Component | PS2EXE Wrapper | Native C (This Guide) |
|-----------|----------------|----------------------|
| **STDIN Read** | ✅ Works | ✅ Works |
| **Deduplication Protocol** | ❌ Missing | ✅ Full implementation |
| **Bidirectional Pipe** | ❌ Broken | ✅ Native stdio |
| **stdout Flush** | ❌ Not in wrapper | ✅ Explicit `fflush()` |
| **Execution Timing** | ❌ Queued until restart | ✅ Immediate (<5 seconds) |
| **Version Metadata** | ❌ Missing | ✅ Version 1 + origin tracking |
| **Log File** | ⚠️ Shared (locked) | ✅ Separate debug log |

---
