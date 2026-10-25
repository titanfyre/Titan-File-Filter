# Deployment prerequisites

Windows requires an appropriate unique altitude for a minifilter. Microsoft must
allocate the company's first altitude in the relevant load-order group. An
existing Activity Monitor allocation is not automatically an access-control
allocation in another group.

For this file-access policy component, the draft requests **FSFilter Security
Enhancer**, subject to Microsoft's assessment. The corresponding setup class is
SecurityEnhancer, GUID `{D02BC3DA-0C8E-4945-9BD5-F1883C226C8C}`. No sample,
invented or borrowed altitude is substituted for the allocation.

The owner has a private request draft with product identity, repository URL,
intended behavior and placeholders for the legal company/contact information.
No request has been sent by this project. Microsoft may request more information
or assign a different group; adjust and revalidate the package accordingly.

After allocation:

1. Record Microsoft's actual altitude/group allocation.
2. Generate the INF/catalog/CAB with the private gated build. The template alone
   is not installable. CAB generation is refused while no allocation is confirmed.
3. Submit through the owner's Microsoft driver-signing workflow.
4. Verify the returned SYS/INF/catalog, catalog membership and preserved image.
5. Build the self-contained UI/console installers with that opaque package.
6. Qualify in disposable Windows 10/11 VMs, then representative physical systems.
   Exercise installation/removal/restart/recovery, active-client exit, concurrent
   rules/I/O, pending I/O/unload, low-memory conditions and targeted Verifier.
7. Qualify filesystem naming, reparse/alias/mapped/offload semantics for the exact
   commercial scope. Do not advertise broader protection than those tests establish.

The INF template includes demand-start, the FltMgr dependency, driver-store
installation and both instance-registry layouts needed across the intended
Windows generations. It does not modify unrelated filters or boot policies.
No test-signing, Secure Boot or HVCI settings are changed by the installers.

References: [Microsoft altitude allocation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/load-order-groups-and-altitudes-for-minifilter-drivers),
[minifilter INF requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/creating-an-inf-file-for-a-minifilter-driver),
[filter classes](https://learn.microsoft.com/en-us/previous-versions/windows/drivers/ifs/file-system-filter-driver-classes-and-class-guids).
