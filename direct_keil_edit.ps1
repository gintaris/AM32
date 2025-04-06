# This script modifies an existing Keil target and sets up support for MB1419 boards

$projectFile = "Keil_Projects\Am32G431.uvprojx"
$xmlContent = [xml](Get-Content $projectFile)

# First, make a backup
Copy-Item -Path $projectFile -Destination "${projectFile}.original"
Write-Host "Backup created at ${projectFile}.original"

# Find the first target (typically the default/test one)
$firstTarget = $xmlContent.Project.Targets.Target[0]

# Update the target name to MB1419_G431
$firstTarget.TargetName = "MB1419_G431"

# Update the output name
$firstTarget.TargetOption.TargetCommonOption.OutputName = "AM32_MB1419_G431"

# Update compiler version to V6.23
$firstTarget.ToolsetNumber = "0x4"
$firstTarget.ToolsetName = "ARM-ADS"
$firstTarget.pArmCC = "6230000::V6.23::ARMCLANG"
$firstTarget.pCCUsed = "6230000::V6.23::ARMCLANG"
$firstTarget.uAC6 = "1"

# Save the modified XML
$xmlContent.Save($projectFile)

Write-Host "Modified project file to use MB1419_G431 target with V6.23 compiler"
Write-Host "Please open the project in Keil and build it" 