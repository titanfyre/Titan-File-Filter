# Validation status

Engineering preview. No altitude has yet been supplied, and this minifilter has
not been installed, attached or exercised in a live kernel. No Driver Verifier,
Windows certification or production throughput result is claimed for it.

Implemented offline validation:

- Native C++ Release build, MSVC W4/WX, CFG, ASLR and NX.
- AddressSanitizer on the application/SDK/offline tests.
- Pointer-free wire protocol, sizes/counts/flags, unique rule IDs, malformed
  responses, denied-result consistency and bounded rule/path cases.
- Existing-directory normalization on a local NTFS volume, and rejection of
  nonexistent scopes and missing/untrusted package inputs.
- The exact private kernel rule selector: first-match priority, process-reference
  identity model, operation masks, directory boundaries, alternate-stream boundary
  and rename/hardlink destination matching over two million iterations.
- Private WDK 26100 driver compilation, CFG and PREfast with zero reported defects.
- Refusal to create an INF/catalog/CAB without a confirmed genuine altitude.

These tests do not establish runtime correctness of the Filter Manager callbacks,
actual process/PID lifetime behavior, low-resource handling, paging/mapped I/O,
namespace alias handling or coexistence. Short user-mode matcher timings are not
live I/O measurements. Static analysis is not proof that no defect exists.

The post-allocation qualification plan is in [DEPLOYMENT.md](DEPLOYMENT.md).

The portable user-mode protocol/directory-normalization checks, exact matcher
test and CLI sample also passed in an isolated Windows 10 build 19045 x64 VM.
No driver was installed or loaded. Its baseline and all nine historical snapshots
were restored; these remain offline checks, not a live minifilter campaign.
