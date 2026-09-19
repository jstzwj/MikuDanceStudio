param([string]$ExeName = "MikuMikuDanceE.exe", [int]$Enable = 1)
$k = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\$ExeName"
if ($Enable -eq 1) {
    New-Item -Path $k -Force | Out-Null
    Set-ItemProperty -Path $k -Name GlobalFlag -Value 0x02000000 -Type DWord
    Set-ItemProperty -Path $k -Name PageHeapFlags -Value 0x3 -Type DWord
    Write-Output "pageheap ENABLED for $ExeName"
} else {
    if (Test-Path $k) {
        Remove-Item -Path $k -Recurse -Force
        Write-Output "pageheap key REMOVED for $ExeName"
    } else {
        Write-Output "no key for $ExeName"
    }
}
