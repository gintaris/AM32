# This script adds a DroneCAN target to the Keil project

$projectFile = "Keil_Projects\Am32G431.uvprojx"
$xmlContent = [xml](Get-Content $projectFile)

# Make a backup
Copy-Item -Path $projectFile -Destination "${projectFile}.with_mb1419"
Write-Host "Backup created at ${projectFile}.with_mb1419"

# Get the first target as our template
$template = $xmlContent.Project.Targets.Target[0]

# Clone the target by serializing to XML and deserializing
$targetXml = $template.OuterXml
$stringReader = New-Object System.IO.StringReader($targetXml)
$xmlReader = [System.Xml.XmlReader]::Create($stringReader)
$newTarget = $xmlContent.ReadNode($xmlReader)

# Update the new target for DroneCAN
$newTarget.TargetName = "MB1419_G431_CAN"
$newTarget.TargetOption.TargetCommonOption.OutputName = "AM32_MB1419_G431_CAN"

# Add the new target to the project
$xmlContent.Project.Targets.AppendChild($newTarget)

# Save the modified XML
$xmlContent.Save($projectFile)

Write-Host "Added MB1419_G431_CAN target to project"
Write-Host "Please open the project in Keil and build it" 