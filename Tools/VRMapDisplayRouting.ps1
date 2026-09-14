Set-StrictMode -Version Latest

# Shared display-routing helpers for JA2/Vengeance map tooling.
# Interactive map GUI windows use a non-primary display.
# If no secondary display exists, -RequireSecondary fails closed.
# Non-interactive Windows-service sessions proceed headlessly.

if (-not ('VRMapDisplayRouter' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class VRMapDisplayRouter
{
    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetWindowPos(
        IntPtr hWnd,
        IntPtr hWndInsertAfter,
        int X,
        int Y,
        int cx,
        int cy,
        uint uFlags
    );

    private const uint SWP_NOZORDER = 0x0004;
    private const uint SWP_NOACTIVATE = 0x0010;
    private const uint SWP_NOSENDCHANGING = 0x0400;

    public static int MoveVisibleWindows(
        int processId,
        int workX,
        int workY,
        int workWidth,
        int workHeight,
        int margin
    )
    {
        int moved = 0;
        int cascade = 0;

        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam)
        {
            uint pid;
            GetWindowThreadProcessId(hWnd, out pid);
            if (pid != (uint)processId || !IsWindowVisible(hWnd))
                return true;

            RECT r;
            if (!GetWindowRect(hWnd, out r))
                return true;

            int width = Math.Max(320, r.Right - r.Left);
            int height = Math.Max(200, r.Bottom - r.Top);

            int availableWidth = Math.Max(320, workWidth - (margin * 2));
            int availableHeight = Math.Max(200, workHeight - (margin * 2));
            width = Math.Min(width, availableWidth);
            height = Math.Min(height, availableHeight);

            int offset = (cascade++ % 6) * 18;
            int x = workX + margin + offset;
            int y = workY + margin + offset;

            if (x + width > workX + workWidth - margin)
                x = workX + Math.Max(margin, workWidth - width - margin);
            if (y + height > workY + workHeight - margin)
                y = workY + Math.Max(margin, workHeight - height - margin);

            bool ok = SetWindowPos(
                hWnd,
                IntPtr.Zero,
                x,
                y,
                width,
                height,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING
            );

            if (ok)
                moved++;

            return true;
        }, IntPtr.Zero);

        return moved;
    }
}
'@
}

function Get-VRSecondaryScreen {
    [CmdletBinding()]
    param()

    if (-not [Environment]::UserInteractive) {
        return $null
    }

    Add-Type -AssemblyName System.Windows.Forms

    $secondary = @(
        [System.Windows.Forms.Screen]::AllScreens |
            Where-Object { -not $_.Primary }
    )

    if ($secondary.Count -eq 0) {
        return $null
    }

    return $secondary |
        Sort-Object @{ Expression = { $_.WorkingArea.Width * $_.WorkingArea.Height } }, DeviceName |
        Select-Object -First 1
}

function Test-VRSecondaryScreen {
    [CmdletBinding()]
    param(
        [switch]$RequireSecondary
    )

    if (-not [Environment]::UserInteractive) {
        return [pscustomobject]@{
            UserInteractive = $false
            Available = $false
            DeviceName = $null
            Bounds = $null
            WorkingArea = $null
            Reason = 'Non-interactive Windows session; GUI cannot occupy the user desktop.'
        }
    }

    $screen = Get-VRSecondaryScreen
    if (-not $screen) {
        if ($RequireSecondary) {
            throw 'No secondary monitor is currently available. Map GUI launch cancelled to protect the primary display.'
        }

        return [pscustomobject]@{
            UserInteractive = $true
            Available = $false
            DeviceName = $null
            Bounds = $null
            WorkingArea = $null
            Reason = 'Only the primary monitor is available.'
        }
    }

    return [pscustomobject]@{
        UserInteractive = $true
        Available = $true
        DeviceName = $screen.DeviceName
        Bounds = $screen.Bounds
        WorkingArea = $screen.WorkingArea
        Reason = 'Secondary monitor available.'
    }
}

function Move-VRProcessWindowsToSecondary {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [System.Diagnostics.Process]$Process,

        [switch]$RequireSecondary,

        [int]$Margin = 12
    )

    if (-not [Environment]::UserInteractive) {
        return 0
    }

    $screen = Get-VRSecondaryScreen
    if (-not $screen) {
        if ($RequireSecondary) {
            throw 'Secondary monitor disappeared while map software was running. Refusing to route the window to the primary display.'
        }
        return 0
    }

    try {
        $Process.Refresh()
        if ($Process.HasExited) {
            return 0
        }
    }
    catch {
        return 0
    }

    $wa = $screen.WorkingArea
    return [VRMapDisplayRouter]::MoveVisibleWindows(
        $Process.Id,
        $wa.X,
        $wa.Y,
        $wa.Width,
        $wa.Height,
        [Math]::Max(0, $Margin)
    )
}

function Start-VRProcessOnSecondary {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$FilePath,

        [string[]]$ArgumentList,

        [string]$WorkingDirectory,

        [switch]$RequireSecondary,

        [int]$InitialRouteSeconds = 15
    )

    $display = Test-VRSecondaryScreen -RequireSecondary:$RequireSecondary

    if ($display.UserInteractive -and $display.Available) {
        Write-Host ("Map display target: {0} working area {1}" -f $display.DeviceName, $display.WorkingArea)
    }
    elseif (-not $display.UserInteractive) {
        Write-Host 'Map display target: non-interactive runner session (no user desktop to disturb).'
    }

    $startArgs = @{
        FilePath = $FilePath
        PassThru = $true
    }
    if ($ArgumentList -and $ArgumentList.Count -gt 0) {
        $startArgs.ArgumentList = $ArgumentList
    }
    if ($WorkingDirectory) {
        $startArgs.WorkingDirectory = $WorkingDirectory
    }

    $p = Start-Process @startArgs

    if ([Environment]::UserInteractive -and $display.Available) {
        $deadline = (Get-Date).AddSeconds([Math]::Max(0, $InitialRouteSeconds))
        do {
            $p.Refresh()
            if ($p.HasExited) { break }
            [void](Move-VRProcessWindowsToSecondary -Process $p -RequireSecondary:$RequireSecondary)
            Start-Sleep -Milliseconds 250
        } while ((Get-Date) -lt $deadline)
    }

    return $p
}

function Invoke-VRProcessOnSecondary {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$FilePath,

        [string[]]$ArgumentList,

        [string]$WorkingDirectory,

        [switch]$RequireSecondary,

        [int]$PollMilliseconds = 500
    )

    $p = Start-VRProcessOnSecondary -FilePath $FilePath -ArgumentList $ArgumentList -WorkingDirectory $WorkingDirectory -RequireSecondary:$RequireSecondary

    while ($true) {
        $p.Refresh()
        if ($p.HasExited) { break }

        if ([Environment]::UserInteractive) {
            [void](Move-VRProcessWindowsToSecondary -Process $p -RequireSecondary:$RequireSecondary)
        }

        Start-Sleep -Milliseconds ([Math]::Max(100, $PollMilliseconds))
    }

    $p.WaitForExit()
    return $p.ExitCode
}
