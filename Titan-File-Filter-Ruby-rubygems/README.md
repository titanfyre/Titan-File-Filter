# Titan-File-Filter Ruby SDK

`gem install titan-file-filter`

Windows x64 Ruby3.1+.

```ruby
require "titan_file_filter"
TitanFileFilterSDK::Client.open { |client| p client.call("status") }
```

[API and operation schema](docs/API.md). Matching driver installed separately.
