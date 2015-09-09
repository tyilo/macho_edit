macho_edit
==========

Command line utility for modifying [Mach-O](https://en.wikipedia.org/wiki/Mach-O) binaries in various ways.

Supported functionality
----

- Extracting and removing archs from a fat binary.
- Inserting an arch from another binary into a fat binary.
- Making a fat binary thin or a thin binary fat.
- Moving around and removing load commands.
- Inserting new load commands. Currently only `LC_LOAD_DYLIB`, `LC_LOAD_WEAK_DYLIB` and `LC_RPATH` is supported.
- Removing code signature (`LC_CODE_SIGNATURE`).


Removing code signature
----

To remove the code signature it is enough to delete the `LC_CODE_SIGNATURE` load command and fixup the mach header's `ncmds` and `sizeofcmds`, assuming it is the last load command.

However if you just do this `codesign_allocate` (used by `codesign` and `ldid`) will fail with the error:

```
.../codesign_allocate: file not in an order that can be processed (link edit information does not fill the __LINKEDIT segment):
```

To fix this `macho_edit` assumes that the code signature that `LC_CODE_SIGNATURE` is in the end of the `__LINKEDIT` segment and the that the segment is in the end of the architectures slice.

It then truncate that slice to remove the code signature part of the `__LINKEDIT` segment. It also updates the `LC_SEGMENT` (or `LC_SEGMENT64`) load command for the `__LINKEDIT` segment from the new file size. If the binary is fat we also update the size and we might also move the slice and so the offset should also be updated.

After removing the code signature from the `__LINKEDIT` segment, the last thing in that segment is typically the string table. As the code signature seems to be aligned by `0x10`, and so after removing the code signature, nothing points to the padding at the end of the segment, which `codesign_allocate` doesn't like either. To fix this we just trim the file so the string table in the `LC_SYMTAB` command is at the end of the slice.

Todo
----

- Option to modify mach header flags
