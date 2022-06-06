.PHONY: exec, clean

MOUNT_DIR=/tmp/ssd

exec:
	./make_ssd
	mkdir -p ${MOUNT_DIR}
	./ssd_fuse -d ${MOUNT_DIR}

clean:
	fusermount -u ${MOUNT_DIR}
	#umount ${MOUNT_DIR}
	rm -rf ${MOUNT_DIR}