.PHONY: exec, clean

MOUNT_DIR=/tmp/ssd

ifdef DEBUG
	DEBUG_FLAG=-DDEBUG=1 -g
endif

exec:
	gcc -Wall ssd_fuse.c `pkg-config fuse3 --cflags --libs` -D_FILE_OFFSET_BITS=64 ${DEBUG_FLAG} -o ssd_fuse
	gcc -Wall ssd_fuse_dut.c -o ssd_fuse_dut
	mkdir -p ${MOUNT_DIR}
	./ssd_fuse -d ${MOUNT_DIR}

clean:
	-fusermount -u ${MOUNT_DIR}
	-umount ${MOUNT_DIR}
	rm -rf ${MOUNT_DIR}