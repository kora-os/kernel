# Track B filesystem delivery

Work in the dedicated Track B worktree. Each step branches from current upstream
main, is reviewed independently, passes host sanitizers and QEMU/CI checks, then
is merged upstream and followed by fork synchronization. No real cards are used.

The heap prerequisite was merged in upstream PR #45 on 2026-10-03. Scheduler
milestone 7 is not a dependency: filesystem operations run serially under the
big kernel lock when it arrives. Track B uses syscall slots 32 through 47.

## Delivery ledger

- [x] 8.1 (PR #46): physical block registry, bounded primary MBR partition views, bare
  FAT32 compatibility, backend registration and MBR QEMU coverage.
- [x] 8.2 (PR #47): heap-backed FAT volumes, labels, namespace resolver, inherited task
  cwd, chdir/getcwd (32/33), multi-volume and relative-path coverage.
- [x] 8.3 (PR #49): single-target assigns, sys/c boot defaults, spawn through c, shell
  navigation and volumes listing, boot and inheritance regression coverage.
- [x] 9.1 (PR #50): write-back sector cache, FAT allocation/free, existing-file write,
  append/truncate and FSInfo accounting, scratch-image tests.
- [ ] 9.2: create/unlink/mkdir/rmdir/rename, UTF-8 LFN encoding and unique 8.3
  aliases, namespace and malformed-input host coverage.
- [ ] 9.3: explicit sync and dirty state, file syscall write flags and namespace
  calls, cp/rm/mkdir/mv, virt write smoke with host fsck and mtools verification.

## Namespace decisions

Device names identify mountable partition slots; volume labels follow media.
All prefixes compare case-insensitively. A leading slash means the current
volume root; name:path is absolute in the named root. Initial cwd is the boot
volume root, inherited by children. No additional filesystem locks or blocking
waits are introduced. FAT32_NAME_MAX remains 765 bytes.

The user selected one df0/df1/... device-slot sequence across all backend types.
No synthetic unified tree is provided. getcwd uses a unique volume label with
device fallback, and the listing command is volumes.

## Verification

Existing required CI check names stay stable. New suites join existing host
sanitizer and virt jobs, which run on PRs and main pushes. Every write test uses
a disposable image copied from the shared immutable userfs; the final CI stage
checks fsck.fat -n and mtools content after QEMU exits. Pi kernel builds and
raspi3b ramdisk smoke remain part of the regression gate.
