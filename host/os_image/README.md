# Millennium Raspberry Pi OS image

This directory defines the minimal production operating-system image for the
Raspberry Pi Zero 2 W. It uses Raspberry Pi's official `rpi-image-gen`, pinned
by `rpi-image-gen.lock`, and adapts its rotational A/B layout to the MBR boot
scheme required by Zero 2 W firmware.

The image contains runtime dependencies and deployed application artifacts
only. Repository sources, compilers, QEMU state, package caches, and temporary
build files do not belong in the output.

## Layout

| Number | Purpose | Format/type |
| --- | --- | --- |
| 1 | boot control (`bootcode.bin`, `start.elf`, `autoboot.txt`) | FAT32 primary |
| 2 | boot A | FAT32 primary |
| 3 | boot B | FAT32 primary |
| 4 | logical-partition container | MBR extended |
| 5 | system A | ext4 logical |
| 6 | system B | ext4 logical |
| 7 | persistent device state | ext4 logical |

The static slot map is `a.boot=::2`, `a.system=::5`, `b.boot=::3`, and
`b.system=::6`. Persistent data uses the upstream `PERSISTENT` filesystem and
`/persistent` mount contract.

Because MBR partitions do not provide GPT partition labels, the image layer
installs that static map at `/boot/slot.map` before regenerating every
initramfs, then proves each generated archive contains it. The copy on each
boot FAT partition is retained for runtime inspection; the embedded copy is
what lets early boot create `/dev/disk/by-slot/{active,other}` before mounting
the root filesystem.

The image also embeds `89-millennium-mbr-slots` immediately before the
upstream root safety check. It reads the firmware-selected boot partition and
creates all four active/other aliases deterministically for partitions 2/5 and
3/6. Any other partition or missing block device still fails closed.

Zero 2 W's BCM2837 boot ROM loads `bootcode.bin` from the first FAT partition
before the second-stage firmware can process `autoboot.txt`. The preparation
transform therefore copies the pinned boot payload's real `bootcode.bin` and
`start.elf` into BOOTCONFIG. The selected A/B partition contains the same GPU
firmware plus the kernel and initramfs. Missing or empty firmware fails the
build; the independent verifier also proves the selector and selected-slot
copies match.

## Build

Build on native arm64 Debian Bookworm or Trixie. Clone the exact commit in the
lock file, prepare an isolated builder tree, then build with the project source
layer:

```sh
git clone https://github.com/raspberrypi/rpi-image-gen.git rpi-image-gen
git -C rpi-image-gen checkout 3f2c916086ad70197945bfc50ef953c1f6035f10
python3 tools/prepare_zero2w_image_builder.py \
  --upstream rpi-image-gen --output rpi-image-gen-zero2w
mkdir image-work
cd rpi-image-gen-zero2w
PATH=/usr/sbin:/sbin:$PATH ./rpi-image-gen build \
  -S ../host/os_image -c millennium-zero2w-ab.yaml -B ../image-work
```

The preparation script verifies the upstream commit and every transformed
source fragment. It refuses unknown upstream content instead of applying a
best-effort patch.

## Verify

Run the independent MBR/EBR parser against the raw image before publishing or
writing removable media:

```sh
python3 tools/verify_zero2w_image.py \
  --boot-content \
  image-work/image-millennium-zero2w-ab/millennium-zero2w-ab.img
```

Also inspect `autoboot.txt`, `slot.map`, the NetworkManager profile, enabled
units, account name, SSH policy, and deployed application payload. A successful
partition check alone is not a production release gate.

The built root filesystem must contain the project-owned
`/usr/lib/systemd/system-generators/slot-shared-generator`. Its regression test
proves that every `Path=` declaration creates both a mount unit and a
`local-fs.target.requires` link:

```sh
python3 -m unittest tools.tests.test_slot_shared_generator
```

After booting the exact recovery image on a Zero 2 W, verify the runtime mount
topology before accepting any application health result:

```sh
findmnt -no SOURCE,OPTIONS /
findmnt -no SOURCE,OPTIONS /etc/millennium
findmnt -no SOURCE,OPTIONS /etc/ssh
findmnt -no SOURCE,OPTIONS /etc/NetworkManager/system-connections
findmnt -no SOURCE,OPTIONS /var/lib/millennium
```

The root source must be the active system slot and include `ro`. Every shared
path must be backed by the persistent partition and include `rw`. QEMU's
generic `virt` guest does not satisfy this physical-image gate.

Run `tools/audit_zero2w_rootfs.py` against the generated root and package
manifest. It rejects compiler/binutils, EEPROM and device-tree tooling that is
irrelevant to Zero 2 W, VCS/source residue, package caches, and temporary files.

For a new physical phone, generate all per-device secrets in a memory-backed
staging directory and run `tools/factory_seed_zero2w.py` on a copy of the raw
image. It seeds both system slots and the official shared/persistent paths,
checks the declared sharing contract, applies fixed ownership and modes, and
writes a privacy-safe provisioning record. Never publish or retain the seeded
whole-disk image: it contains the device's private host and tunnel keys.

The optional upstream Image Description Provisioning document is omitted for
this target because its schema rejects DOS logical partitions. The raw image is
still constructed by `genimage`, checked by `sfdisk`, and independently checked
by `verify_zero2w_image.py`.
