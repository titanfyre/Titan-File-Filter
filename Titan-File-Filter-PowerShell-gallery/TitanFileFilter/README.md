# Titan-File-Filter PowerShell SDK

PowerShell7.4+ Windows x64. `Install-Module TitanFileFilter -Scope CurrentUser`.

```powershell
$client=Connect-TitanFileFilterSDK
try { Invoke-TitanFileFilterSDK -Client $client -Operation status } finally { Disconnect-TitanFileFilterSDK -Client $client }
```

Use -Arguments with a hashtable matching [schema](docs/API.md). Native errors retain their Windows code through the inner exception. Matching driver installed separately.
