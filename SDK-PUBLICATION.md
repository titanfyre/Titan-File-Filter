# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | titan-file-filter |
| CPP/conan | public_recipe_builds_locally |  [Source](packaging/native/) |
| CPP/nuget | paused_pending_owner_email_confirmation | Titan-File-Filter.Native |
| NET/nuget | built_verified_publication_on_hold_by_owner | TitanFileFilter.SDK [Source](Titan-File-Filter-NET-nuget/) |
| Python/pypi | built_fixture_and_production_verified | titan-file-filter [Source](Titan-File-Filter-Python-pypi/) |
| Python/conda-forge | built_tested_requires_source_publication_and_official_review | titan-file-filter [Source](packaging/conda/) |
| Rust/cargo | published_install_verified | [titan-file-filter](https://crates.io/crates/titan-file-filter) [Source](Titan-File-Filter-Rust-cargo/) |
| JavaScript/npm | awaiting_browser_publication_authentication | [titan-file-filter](https://www.npmjs.com/package/titan-file-filter) [Source](Titan-File-Filter-JavaScript-npm/) |
| Java/maven | published_install_verified | [titan-file-filter](https://central.sonatype.com/artifact/io.github.titancsa/titan-file-filter/1.0.0) [Source](Titan-File-Filter-Java-maven/) |
| Java/jitpack | published_install_verified | [titan-file-filter](https://jitpack.io/#titanfyre/Titan-File-Filter/java-sdk-v1.0.0) [Source](Titan-File-Filter-Java-maven/) |
| Go/modules | published_install_verified | [titan-file-filter](https://pkg.go.dev/github.com/titanfyre/Titan-File-Filter/Titan-File-Filter-Go-modules@v1.0.0) [Source](Titan-File-Filter-Go-modules/) |
| Go/pkg-go-dev | indexed_verified | [Documentation](https://pkg.go.dev/github.com/titanfyre/Titan-File-Filter/Titan-File-Filter-Go-modules@v1.0.0) |
| PowerShell/gallery | published_install_verified | [TitanFileFilter](https://www.powershellgallery.com/packages/TitanFileFilter/1.0.0) [Source](Titan-File-Filter-PowerShell-gallery/) |
| Dart/pub-dev | built_fixture_and_production_verified | titan_file_filter [Source](Titan-File-Filter-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [titan-file-filter](https://rubygems.org/gems/titan-file-filter) [Source](Titan-File-Filter-Ruby-rubygems/) |
| PHP/packagist | composer_source_and_tag_published | [titancsa/titan-file-filter](https://packagist.org/packages/titancsa/titan-file-filter) |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
