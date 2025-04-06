# This script sets up MB1419 targets in the Keil project file

$projectFile = "Keil_Projects\Am32G431.uvprojx"

# First, check if the file exists and is readable
if (-not (Test-Path $projectFile)) {
    Write-Host "ERROR: Project file not found: $projectFile"
    exit 1
}

try {
    # Create a backup with timestamp
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $backupFile = "${projectFile}.${timestamp}.bak"
    Copy-Item -Path $projectFile -Destination $backupFile
    Write-Host "Created backup: $backupFile"

    # Try to load the XML
    $xmlContent = [xml](Get-Content $projectFile)
    
    # Get the first target from the project (XML structure should have Project -> Targets -> Target)
    $firstTarget = $xmlContent.Project.Targets.Target[0]
    
    # Verify we can access the target
    if ($null -eq $firstTarget) {
        throw "Could not find Target element in project file"
    }
    
    # Make a clone of the first target for our MB1419_G431 target
    $stdTarget = $xmlContent.CreateElement("Target")
    $stdTarget.InnerXml = $firstTarget.InnerXml
    
    # Update the standard target
    $stdTarget.TargetName = "MB1419_G431"
    $stdTarget.SelectSingleNode("TargetOption/TargetCommonOption/OutputName").InnerText = "AM32_MB1419_G431"
    
    # Update compiler version if needed
    $pArmCC = $stdTarget.SelectSingleNode("pArmCC")
    if ($pArmCC -ne $null) {
        $pArmCC.InnerText = "6230000::V6.23::ARMCLANG"
    }
    
    $pCCUsed = $stdTarget.SelectSingleNode("pCCUsed")
    if ($pCCUsed -ne $null) {
        $pCCUsed.InnerText = "6230000::V6.23::ARMCLANG"
    }
    
    # Make a clone for the DroneCAN target
    $canTarget = $xmlContent.CreateElement("Target")
    $canTarget.InnerXml = $stdTarget.InnerXml
    
    # Update the CAN target
    $canTarget.TargetName = "MB1419_G431_CAN"
    $canTarget.SelectSingleNode("TargetOption/TargetCommonOption/OutputName").InnerText = "AM32_MB1419_G431_CAN"
    
    # Add both targets to the project, preserving the original
    $xmlContent.Project.Targets.AppendChild($stdTarget)
    $xmlContent.Project.Targets.AppendChild($canTarget)
    
    # Save the file with pretty formatting
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.IndentChars = "  "
    $writer = [System.Xml.XmlWriter]::Create($projectFile, $settings)
    $xmlContent.Save($writer)
    $writer.Close()
    
    Write-Host "Successfully added MB1419_G431 and MB1419_G431_CAN targets to project"
    Write-Host "You can now open the project in Keil and select one of these targets"
    
} catch {
    Write-Host "ERROR: Failed to modify project file: $_"
    Write-Host "Restoring from backup..."
    Copy-Item -Path $backupFile -Destination $projectFile -Force
    Write-Host "Project file restored from backup"
    exit 1
} 