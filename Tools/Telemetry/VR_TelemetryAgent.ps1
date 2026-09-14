param(
    [switch]$Install,
    [switch]$Uninstall,
    [switch]$RunOnce
)

$ErrorActionPreference = "Stop"

$AgentVersion = "1"
$TelemetryBranch = "telemetry/companion-sessions"
$PollSeconds = 2
$RunKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$RunValueName = "VengeanceTelemetryAgent"

$ScriptPath = $MyInvocation.MyCommand.Path
$ToolsDir = Split-Path -Parent $ScriptPath
$SourceRepo = (Resolve-Path (Join-Path $ToolsDir "..\..")).Path
$GameRoot = Split-Path -Parent $SourceRepo

$StateRoot = Join-Path $env:LOCALAPPDATA "VengeanceTelemetry"
$StateFile = Join-Path $StateRoot "state.json"
$AgentLog = Join-Path $StateRoot "agent.log"
$PendingRoot = Join-Path $StateRoot "pending"
$UploadWorktree = Join-Path $StateRoot "upload-worktree"

$TrackedLogs = @(
    "Campaign Tactical Black Box.tsv",
    "Campaign Tactical Decisions.tsv",
    "Campaign AI Black Box.tsv",
    "Campaign AI Companion.txt"
)

function Ensure-Directory([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

function Write-AgentLog([string]$Message) {
    Ensure-Directory $StateRoot
    $stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    Add-Content -LiteralPath $AgentLog -Value "[$stamp] $Message" -Encoding UTF8
}

function Invoke-Git {
    param(
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [switch]$AllowFailure
    )

    $output = & git @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        throw "git $($Arguments -join ' ') failed ($exitCode): $($output -join [Environment]::NewLine)"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function Get-FileLengths {
    $map = @{}
    foreach ($name in $TrackedLogs) {
        $path = Join-Path $GameRoot $name
        if (Test-Path -LiteralPath $path) {
            $map[$name] = [int64](Get-Item -LiteralPath $path).Length
        } else {
            $map[$name] = [int64]0
        }
    }
    return $map
}

function Save-State($State) {
    Ensure-Directory $StateRoot
    $State | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $StateFile -Encoding UTF8
}

function Load-State {
    Ensure-Directory $StateRoot
    Ensure-Directory $PendingRoot

    if (Test-Path -LiteralPath $StateFile) {
        try {
            return Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
        } catch {
            Write-AgentLog "State file was unreadable; rebuilding offsets from current files."
        }
    }

    $lengths = Get-FileLengths
    $offsets = [ordered]@{}
    foreach ($name in $TrackedLogs) {
        $offsets[$name] = [int64]$lengths[$name]
    }

    $state = [pscustomobject]@{
        version = 1
        initialized_at = (Get-Date).ToString("o")
        offsets = [pscustomobject]$offsets
        active_session = $null
    }
    Save-State $state
    return $state
}

function Get-Offset($State, [string]$Name) {
    $prop = $State.offsets.PSObject.Properties[$Name]
    if ($null -eq $prop) { return [int64]0 }
    return [int64]$prop.Value
}

function Set-Offset($State, [string]$Name, [int64]$Value) {
    $prop = $State.offsets.PSObject.Properties[$Name]
    if ($null -eq $prop) {
        $State.offsets | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    } else {
        $prop.Value = $Value
    }
}

function Get-GameProcesses {
    $rootPrefix = ($GameRoot.TrimEnd('\') + '\').ToLowerInvariant()
    $matches = @()

    try {
        $all = Get-CimInstance Win32_Process -ErrorAction Stop
        foreach ($p in $all) {
            $exe = [string]$p.ExecutablePath
            $name = [string]$p.Name
            if ([string]::IsNullOrWhiteSpace($exe)) { continue }

            $lowerExe = $exe.ToLowerInvariant()
            $lowerName = $name.ToLowerInvariant()

            if (-not $lowerExe.StartsWith($rootPrefix)) { continue }
            if ($lowerName -notmatch "(ja2|vengeance)") { continue }
            if ($lowerName -match "(editor|mapeditor|updater|setup|installer)") { continue }

            $matches += [pscustomobject]@{
                ProcessId = [int]$p.ProcessId
                Name = $name
                ExecutablePath = $exe
                CommandLine = [string]$p.CommandLine
            }
        }
    } catch {
        Write-AgentLog "Process scan failed: $($_.Exception.Message)"
    }

    return @($matches)
}

function Copy-DeltaFile {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Destination,
        [Parameter(Mandatory=$true)][int64]$Offset,
        [switch]$PrependTsvHeader
    )

    $info = Get-Item -LiteralPath $Source
    $start = $Offset
    if ($start -lt 0 -or $start -gt $info.Length) {
        $start = 0
    }

    if ($info.Length -le $start) {
        return $false
    }

    $destDir = Split-Path -Parent $Destination
    Ensure-Directory $destDir

    $out = [System.IO.File]::Open($Destination,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read)

    try {
        if ($PrependTsvHeader -and $start -gt 0) {
            $reader = New-Object System.IO.StreamReader($Source, [System.Text.Encoding]::UTF8, $true)
            try {
                $header = $reader.ReadLine()
            } finally {
                $reader.Dispose()
            }

            if (-not [string]::IsNullOrWhiteSpace($header)) {
                $headerBytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($header + [Environment]::NewLine)
                $out.Write($headerBytes, 0, $headerBytes.Length)
            }
        }

        $input = [System.IO.File]::Open($Source,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)

        try {
            [void]$input.Seek($start, [System.IO.SeekOrigin]::Begin)
            $buffer = New-Object byte[] 65536
            while (($read = $input.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $out.Write($buffer, 0, $read)
            }
        } finally {
            $input.Dispose()
        }
    } finally {
        $out.Dispose()
    }

    return $true
}

function Get-RepoMetadata {
    $branchName = "-"
    $commit = "-"
    $dirty = $false

    if (Get-Command git -ErrorAction SilentlyContinue) {
        $r = Invoke-Git -Arguments @("-C", $SourceRepo, "rev-parse", "--abbrev-ref", "HEAD") -AllowFailure
        if ($r.ExitCode -eq 0) { $branchName = (($r.Output | Select-Object -First 1) -as [string]).Trim() }

        $r = Invoke-Git -Arguments @("-C", $SourceRepo, "rev-parse", "HEAD") -AllowFailure
        if ($r.ExitCode -eq 0) { $commit = (($r.Output | Select-Object -First 1) -as [string]).Trim() }

        $r = Invoke-Git -Arguments @("-C", $SourceRepo, "status", "--porcelain") -AllowFailure
        $dirty = ($r.ExitCode -eq 0 -and @($r.Output).Count -gt 0)
    }

    return [pscustomobject]@{
        source_repo = $SourceRepo
        source_branch = $branchName
        source_commit = $commit
        source_dirty = $dirty
    }
}

function New-SessionPackage {
    param(
        $State,
        [Parameter(Mandatory=$true)]$Session
    )

    $end = Get-Date
    $id = "{0}_{1}" -f $Session.started_at.ToString("yyyyMMdd_HHmmss"), $Session.pid
    $dayFolder = $Session.started_at.ToString("yyyy-MM-dd")
    $sessionDir = Join-Path (Join-Path $PendingRoot $dayFolder) $id
    Ensure-Directory $sessionDir

    $repo = Get-RepoMetadata
    $lengths = Get-FileLengths
    $included = @()

    foreach ($name in $TrackedLogs) {
        $source = Join-Path $GameRoot $name
        if (-not (Test-Path -LiteralPath $source)) { continue }

        $offset = Get-Offset $State $name
        $dest = Join-Path $sessionDir $name
        $isTsv = $name.ToLowerInvariant().EndsWith(".tsv")

        if (Copy-DeltaFile -Source $source -Destination $dest -Offset $offset -PrependTsvHeader:$isTsv) {
            $included += $name
        }

        Set-Offset $State $name ([int64]$lengths[$name])
    }

    $manifest = [ordered]@{
        telemetry_agent_version = $AgentVersion
        framework_expected = "unified-ai-2026-09-14"
        companion_schema_expected = 2
        session_id = $id
        started_at = $Session.started_at.ToString("o")
        ended_at = $end.ToString("o")
        duration_seconds = [math]::Max(0, [int]($end - $Session.started_at).TotalSeconds)
        process_id = $Session.pid
        process_name = $Session.name
        executable = $Session.executable
        game_root = $GameRoot
        telemetry_branch = $TelemetryBranch
        source_branch = $repo.source_branch
        source_commit = $repo.source_commit
        source_dirty = $repo.source_dirty
        included_files = $included
    }

    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $sessionDir "session.json") -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $sessionDir ".pending") -Value $end.ToString("o") -Encoding ASCII

    $State.active_session = $null
    Save-State $State
    Write-AgentLog "Packaged session $id with $($included.Count) telemetry files."
    return $sessionDir
}

function Ensure-UploadWorktree {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "git.exe is not available."
    }

    $fetch = Invoke-Git -Arguments @("-C", $SourceRepo, "fetch", "origin", $TelemetryBranch) -AllowFailure
    if ($fetch.ExitCode -ne 0) {
        throw "Could not fetch $TelemetryBranch from origin."
    }

    if (-not (Test-Path -LiteralPath $UploadWorktree)) {
        Ensure-Directory $StateRoot
        [void](Invoke-Git -Arguments @("-C", $SourceRepo, "worktree", "prune") -AllowFailure)
        $add = Invoke-Git -Arguments @("-C", $SourceRepo, "worktree", "add", "--detach", $UploadWorktree, "origin/$TelemetryBranch") -AllowFailure
        if ($add.ExitCode -ne 0) {
            throw "Could not create telemetry worktree: $($add.Output -join ' ')"
        }
    }

    [void](Invoke-Git -Arguments @("-C", $UploadWorktree, "fetch", "origin", $TelemetryBranch))
    [void](Invoke-Git -Arguments @("-C", $UploadWorktree, "reset", "--hard", "origin/$TelemetryBranch"))
    [void](Invoke-Git -Arguments @("-C", $UploadWorktree, "clean", "-fd"))
}

function Try-UploadPending {
    $pending = @(Get-ChildItem -LiteralPath $PendingRoot -Directory -Recurse -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName ".pending") })

    if ($pending.Count -eq 0) { return }

    try {
        Ensure-UploadWorktree
    } catch {
        Write-AgentLog "Upload deferred: $($_.Exception.Message)"
        return
    }

    foreach ($dir in $pending | Sort-Object FullName) {
        try {
            [void](Invoke-Git -Arguments @("-C", $UploadWorktree, "fetch", "origin", $TelemetryBranch))
            [void](Invoke-Git -Arguments @("-C", $UploadWorktree, "reset", "--hard", "origin/$TelemetryBranch"))
            [void](Invoke-Git -Arguments @("-C", $UploadWorktree, "clean", "-fd"))

            $day = Split-Path -Leaf (Split-Path -Parent $dir.FullName)
            $sessionId = $dir.Name
            $relativeDest = Join-Path (Join-Path "sessions" $day) $sessionId
            $dest = Join-Path $UploadWorktree $relativeDest
            Ensure-Directory (Split-Path -Parent $dest)
            Copy-Item -LiteralPath $dir.FullName -Destination $dest -Recurse -Force
            Remove-Item -LiteralPath (Join-Path $dest ".pending") -Force -ErrorAction SilentlyContinue

            [void](Invoke-Git -Arguments @("-C", $UploadWorktree, "add", "--", $relativeDest))
            $status = Invoke-Git -Arguments @("-C", $UploadWorktree, "status", "--porcelain", "--", $relativeDest) -AllowFailure
            if (@($status.Output).Count -eq 0) {
                Remove-Item -LiteralPath (Join-Path $dir.FullName ".pending") -Force -ErrorAction SilentlyContinue
                continue
            }

            [void](Invoke-Git -Arguments @("-C", $UploadWorktree, "commit", "-m", "telemetry: session $sessionId"))
            $push = Invoke-Git -Arguments @("-C", $UploadWorktree, "push", "origin", "HEAD:refs/heads/$TelemetryBranch") -AllowFailure
            if ($push.ExitCode -ne 0) {
                throw "git push failed: $($push.Output -join ' ')"
            }

            Remove-Item -LiteralPath (Join-Path $dir.FullName ".pending") -Force -ErrorAction SilentlyContinue
            Set-Content -LiteralPath (Join-Path $dir.FullName ".uploaded") -Value (Get-Date).ToString("o") -Encoding ASCII
            Write-AgentLog "Uploaded session $sessionId to $TelemetryBranch."
        } catch {
            Write-AgentLog "Upload failed for $($dir.FullName): $($_.Exception.Message)"
            break
        }
    }
}

function Install-Agent {
    Ensure-Directory $StateRoot
    $command = 'powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + $ScriptPath + '"'
    New-ItemProperty -Path $RunKey -Name $RunValueName -PropertyType String -Value $command -Force | Out-Null

    if (-not (Test-Path -LiteralPath $StateFile)) {
        [void](Load-State)
    }

    Write-AgentLog "Installed auto-start telemetry agent. Game root: $GameRoot"
    Start-Process -FilePath "powershell.exe" -ArgumentList @(
        "-NoProfile", "-WindowStyle", "Hidden", "-ExecutionPolicy", "Bypass",
        "-File", ('"' + $ScriptPath + '"')
    ) | Out-Null
}

function Uninstall-Agent {
    Remove-ItemProperty -Path $RunKey -Name $RunValueName -ErrorAction SilentlyContinue
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -like "*VR_TelemetryAgent.ps1*" -and $_.ProcessId -ne $PID } |
        ForEach-Object {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        }
    Write-AgentLog "Removed auto-start telemetry agent."
}

if ($Install) {
    Install-Agent
    exit 0
}

if ($Uninstall) {
    Uninstall-Agent
    exit 0
}

$state = Load-State
Write-AgentLog "Agent started. Watching $GameRoot"

$active = $null

while ($true) {
    Try-UploadPending

    $processes = @(Get-GameProcesses)
    if ($null -eq $active -and $processes.Count -gt 0) {
        $p = $processes | Select-Object -First 1
        $active = [pscustomobject]@{
            pid = [int]$p.ProcessId
            name = [string]$p.Name
            executable = [string]$p.ExecutablePath
            started_at = Get-Date
        }
        $state.active_session = [pscustomobject]@{
            pid = $active.pid
            name = $active.name
            executable = $active.executable
            started_at = $active.started_at.ToString("o")
        }
        Save-State $state
        Write-AgentLog "Detected game start: $($active.name) pid=$($active.pid)"
    }

    if ($null -ne $active) {
        $stillRunning = $false
        foreach ($p in $processes) {
            if ([int]$p.ProcessId -eq [int]$active.pid) {
                $stillRunning = $true
                break
            }
        }

        if (-not $stillRunning) {
            Write-AgentLog "Detected game exit: $($active.name) pid=$($active.pid)"
            [void](New-SessionPackage -State $state -Session $active)
            Try-UploadPending
            $active = $null
        }
    }

    if ($RunOnce) { break }
    Start-Sleep -Seconds $PollSeconds
}
