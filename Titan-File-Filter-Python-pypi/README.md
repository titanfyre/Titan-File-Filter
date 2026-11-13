# Titan-File-Filter Python SDK

`pip install titan-file-filter`

Windows x64 Python3.10+. No driver is installed by this package.

```python
from titan_file_filter import Client
with Client() as client:
    info = client.call("status")
```

[Operation schema and API](docs/API.md). Check errors and the product licensing state before device creation.
