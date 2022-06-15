# OSC2022 Final - SSD
## Environment
- page size: 512 bytes
- 1 nand = 1 `Block` = 10 `Page`
- logical block count: 10
- physical block count: 13

## Build & Execute
build and execute

```
make exec
```

with debug symbol and debug message
```
make exec DEBUG = 1
```

clean, umount and remove `/tmp/ssd`
```
make clean
```

## Implementation
### # Read
> `ssd_read` -> `ssd_do_read` -> `ftl_read` -> `nand_read`

### # Write
> `ssd_write` -> `ssd_do_write` -> `ftl_write` -> `nand_write`
- When doing `read-modify-write` and there is no modification. Simply skip writing this page.

### # Garbage Collection (GC)
- Once all the pages in one block are not `INVALID_PCA` and none of them is logged in `L2P`. Erase the block.
- Lazy strategy. Never do GC if there still exists `INVALID_PCA` to write.
