# --- 1. Configuration Section ---
$serverListPath = "C:\temp\servers.txt"  # File with hostnames (one per line)
$wazuhManager   = "YOUR_WAZUH_SERVER_IP"
$regPort        = 1515
$msiUrl         = "https://packages-staging.xdrsiem.wazuh.info/pre-release/5.x/windows/wazuh-agent-5.0.0-beta1.msi"

# Local paths to the files on the machine running this script
$localMsiPath  = "C:\deployment\wazuh-agent.msi"
$localCertPath = "C:\deployment\agent.cert"
$localKeyPath  = "C:\deployment\agent.key"

# --- 2. Prerequisites ---
# Download the installer locally once if it doesn't exist
if (!(Test-Path $localMsiPath)) {
    Write-Host "Downloading installer..." -ForegroundColor Yellow
    Invoke-WebRequest -Uri $msiUrl -OutFile $localMsiPath
}

# Get admin credentials for the remote VMs
$cred = Get-Credential 

# --- 3. Deployment Loop ---
$servers = Get-Content -Path $serverListPath
foreach ($server in $servers) {
    Write-Host "`n[+] Starting deployment for $server" -ForegroundColor Cyan
    
    try {
        # Create remote temp directory via Administrative Share (C$)
        $remotePath = "\\$server\C$\windows\temp\wazuh_install"
        if (!(Test-Path $remotePath)) { 
            New-Item -ItemType Directory -Path $remotePath -Force | Out-Null 
        }

        # Copy the 3 files to the target VM
        Write-Host "    - Copying files to $server..."
        Copy-Item -Path $localMsiPath, $localCertPath, $localKeyPath -Destination $remotePath -Force

        # Run the installation remotely
        $ScriptBlock = {
            param($ManagerIP, $Port)
            
            # Paths relative to the target VM itself
            $msi    = "C:\windows\temp\wazuh_install\wazuh-agent.msi"
            $cert   = "C:\windows\temp\wazuh_install\agent.cert"
            $key    = "C:\windows\temp\wazuh_install\agent.key"

            # Execute silent MSI installation
            # Note: CA is omitted as per your request
            $args = "/i `"$msi`" /qn WAZUH_MANAGER='$ManagerIP' WAZUH_REGISTRATION_PORT=$Port " +
                    "WAZUH_REGISTRATION_CERTIFICATE='$cert' WAZUH_REGISTRATION_KEY='$key'"

            $process = Start-Process "msiexec.exe" -ArgumentList $args -Wait -PassThru
            
            if ($process.ExitCode -eq 0) {
                return "Success: Wazuh Agent installed."
            } else {
                throw "Installation failed with Exit Code: $($process.ExitCode)"
            }
        }

        Invoke-Command -ComputerName $server -Credential $cred -ScriptBlock $ScriptBlock -ArgumentList $wazuhManager, $regPort
        Write-Host "    - Installation triggered successfully on $server" -ForegroundColor Green

    } catch {
        Write-Host "    - ERROR on $server : $($_.Exception.Message)" -ForegroundColor Red
    }
}