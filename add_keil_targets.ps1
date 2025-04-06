$projectFile = "Keil_Projects\Am32G431.uvprojx"
$content = Get-Content $projectFile -Raw

# Find where to insert the new targets
$photonTargetStart = $content.IndexOf("<TargetName>PhotonDrive_G431</TargetName>")
$targetStart = $content.LastIndexOf("<Target>", $photonTargetStart)
$targetEnd = $content.IndexOf("</Target>", $photonTargetStart) + "</Target>".Length

$photonTargetContent = $content.Substring($targetStart, $targetEnd - $targetStart)

# Create the MB1419_G431 target
$mb1419Target = $photonTargetContent -replace "PhotonDrive_G431", "MB1419_G431" -replace "AM32_PROTONDRIVE_G431", "AM32_MB1419_G431"

# Create the MB1419_G431_CAN target
$mb1419CanTarget = $photonTargetContent -replace "PhotonDrive_G431", "MB1419_G431_CAN" -replace "AM32_PROTONDRIVE_G431", "AM32_MB1419_G431_CAN"

# Insert the new targets after the PhotonDrive target
$newContent = $content.Insert($targetEnd, "`n    " + $mb1419Target + "`n    " + $mb1419CanTarget)

# Save the modified content
$newContent | Set-Content $projectFile

Write-Host "Added MB1419_G431 and MB1419_G431_CAN targets to $projectFile" 