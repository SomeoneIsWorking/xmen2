---
id: C046
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/xboxrecomp.lock
---

## Claim

X-Men Legends II validates the cluster size of \Device\Harddisk0\partition1 at boot and reboots to the dashboard unless BytesPerSector * SectorsPerAllocationUnit == 0x4000. The toolkit reported 512*8 = 4 KB; a real Xbox HDD partition is FATX with 16 KB clusters.

## Evidence

sub_002264C1 computes MEM32(ebp-28) * MEM32(ebp-24) -- the SectorsPerAllocationUnit and BytesPerSector fields of the 0x18-byte FileFsSizeInformation buffer it just requested -- and returns 0xC000014F unless the product equals its 0x4000 argument. Setting SectorsPerAllocationUnit to 32 makes the run proceed past that check to IoCreateSymbolicLink, a call it never previously reached.

## What would falsify it

the DVD volume (D:) has different geometry (2048-byte sectors); one implementation serving both volumes will be wrong for whichever it does not match
