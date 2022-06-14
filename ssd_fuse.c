/*
  FUSE ssd: FUSE ioctl example
  Copyright (C) 2008       SUSE Linux Products GmbH
  Copyright (C) 2008       Tejun Heo <teheo@suse.de>
  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/
#define FUSE_USE_VERSION 35
#include <errno.h>
#include <fuse.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "debug.h"
#include "ssd_fuse_header.h"
#define SSD_NAME "ssd_file"
enum {
    SSD_NONE,
    SSD_ROOT,
    SSD_FILE,
};

static size_t physic_size;
static size_t logic_size;
static size_t host_write_size;
static size_t nand_write_size;

typedef union pca_rule PCA_RULE;
union pca_rule {
    unsigned int pca;
    struct
    {
        unsigned int lba : 16;
        unsigned int nand : 16;
    } fields;
};

PCA_RULE curr_pca;
static unsigned int get_next_pca();

unsigned int *L2P, *P2L, *valid_count, free_block_number;
unsigned int* invalid_count;
/*
    INVALID_PCA/LBA: can be written
    NOT_USED_PCA:    can't be written & data is not used
    ELSE:            can't be written & data is used
*/

static int ssd_resize(size_t new_size) {
    // set logic size to new_size
    if (new_size > NAND_SIZE_KB * 1024) {
        return -ENOMEM;
    } else {
        logic_size = new_size;
        return 0;
    }
}

static int ssd_expand(size_t new_size) {
    // logic must less logic limit

    if (new_size > logic_size) {
        return ssd_resize(new_size);
    }

    return 0;
}

static int nand_read(char* buf, int pca) {
    char nand_name[100];
    FILE* fptr;

    PCA_RULE my_pca;
    my_pca.pca = pca;
    snprintf(nand_name, 100, "%s/nand_%d", NAND_LOCATION, my_pca.fields.nand);

    // read
    if ((fptr = fopen(nand_name, "r"))) {
        fseek(fptr, my_pca.fields.lba * 512, SEEK_SET);
        fread(buf, 1, 512, fptr);
        fclose(fptr);
    } else {
        printf("open file fail at nand read pca = %d\n", pca);
        return -EINVAL;
    }
    return 512;
}
static int nand_write(const char* buf, int pca) {
    char nand_name[100];
    FILE* fptr;

    PCA_RULE my_pca;
    my_pca.pca = pca;
    snprintf(nand_name, 100, "%s/nand_%d", NAND_LOCATION, my_pca.fields.nand);

    // write
    if ((fptr = fopen(nand_name, "r+"))) {
        fseek(fptr, my_pca.fields.lba * 512, SEEK_SET);
        fwrite(buf, 1, 512, fptr);
        fclose(fptr);
        physic_size++;
        valid_count[my_pca.fields.nand]++;
        invalid_count[my_pca.fields.nand]--;
    } else {
        printf("open file fail at nand (%s) write pca = %d, return %d\n", nand_name, pca, -EINVAL);
        return -EINVAL;
    }

    nand_write_size += 512;
    return 512;
}

static int nand_erase(int block_index) {
    char nand_name[100];
    FILE* fptr;
    snprintf(nand_name, 100, "%s/nand_%d", NAND_LOCATION, block_index);
    fptr = fopen(nand_name, "w");
    if (fptr == NULL) {
        printf("erase nand_%d fail", block_index);
        return 0;
    }
    fclose(fptr);
    valid_count[block_index] = FREE_BLOCK;
    invalid_count[block_index] = PAGE_PER_BLOCK;
    memset(P2L + PAGE_PER_BLOCK * block_index, INVALID_PCA, sizeof(int) * PAGE_PER_BLOCK);
    return 1;
}

static unsigned int get_next_block() {  // linearly find next free block (nand)
    for (int i = 0; i < PHYSICAL_NAND_NUM; i++) {
        if (valid_count[(curr_pca.fields.nand + i) % PHYSICAL_NAND_NUM] == FREE_BLOCK) {
            curr_pca.fields.nand = (curr_pca.fields.nand + i) % PHYSICAL_NAND_NUM;
            curr_pca.fields.lba = 0;
            free_block_number--;
            valid_count[curr_pca.fields.nand] = 0;
            return curr_pca.pca;
        }
    }
    return OUT_OF_BLOCK;
}
static unsigned int get_next_pca() {
    if (curr_pca.pca == INVALID_PCA) {
        // init
        curr_pca.pca = 0;
        valid_count[0] = 0;
        free_block_number--;
        return curr_pca.pca;
    }

    if (curr_pca.fields.lba == 9) {
        int temp = get_next_block();
        if (temp == OUT_OF_BLOCK) {
#ifdef DEBUG
            printf(RED "out of block!!" NC);
#endif
            return OUT_OF_BLOCK;
        } else if (temp == -EINVAL) {
            return -EINVAL;
        } else {
            return temp;
        }
    } else {
        curr_pca.fields.lba += 1;
    }
    return curr_pca.pca;
}

static int ftl_read(char* buf, size_t lba) {
#ifdef DEBUG
    printf(BLUE "ftl_read(0x%x, %d)" NC, buf, lba);
#endif
    // TODO ftl_read
    PCA_RULE pca;
    pca.pca = L2P[lba];
    // printf(GREEN "start nand_read(buf, 0x%x/0x%x)" NC, pca.fields.lba, pca.fields.nand);
    nand_read(buf, pca.pca);
    // printf(GREEN "end nand_read(buf, 0x%x/0x%x)" NC, pca.fields.lba, pca.fields.nand);
}

static int ftl_write(const char* buf, size_t lba_rnage, size_t lba) {
#ifdef DEBUG
    printf(BLUE "ftl_write(0x%x, %d, %d)" NC, buf, lba_rnage, lba);
#endif
    // TODO ftl_write
    PCA_RULE pca;
    // pca.pca = L2P[lba];
    // if (pca.pca == INVALID_PCA) {
    pca.pca = get_next_pca();  // Allocate a new PCA address to write
    if (pca.pca == OUT_OF_BLOCK) {
#ifdef DEBUG
        printf(YELLOW "NO SPACE TO WRITE!!!" NC);
#endif
    }
    L2P[lba] = pca.pca;
    P2L[pca.fields.nand * PAGE_PER_BLOCK + pca.fields.lba] = lba;
    // }
#ifdef DEBUG
    printf(BLUE "nand_write(buf, 0x%x/0x%x)" NC, pca.fields.lba, pca.fields.nand);
#endif
    nand_write(buf, pca.pca);
}

static int ssd_file_type(const char* path) {
    if (strcmp(path, "/") == 0) {
        return SSD_ROOT;
    }
    if (strcmp(path, "/" SSD_NAME) == 0) {
        return SSD_FILE;
    }
    return SSD_NONE;
}
static int ssd_getattr(const char* path, struct stat* stbuf,
                       struct fuse_file_info* fi) {
    (void)fi;
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    stbuf->st_atime = stbuf->st_mtime = time(NULL);
    switch (ssd_file_type(path)) {
        case SSD_ROOT:
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            break;
        case SSD_FILE:
            stbuf->st_mode = S_IFREG | 0644;
            stbuf->st_nlink = 1;
            stbuf->st_size = logic_size;
            break;
        case SSD_NONE:
            return -ENOENT;
    }
    return 0;
}
static int ssd_open(const char* path, struct fuse_file_info* fi) {
    (void)fi;
    if (ssd_file_type(path) != SSD_NONE) {
        return 0;
    }
    return -ENOENT;
}
static int ssd_do_read(char* buf, size_t size, off_t offset) {
#ifdef DEBUG
    printf(BLUE "ssd_do_read(0x%x, %d, 0x%x)" NC, buf, size, offset);
#endif
    int tmp_lba, tmp_lba_range, rst;
    char* tmp_buf;

    // off limit
    if ((offset) >= logic_size) {
        return 0;
    }
    if (size > logic_size - offset) {
        // is valid data section
        size = logic_size - offset;
    }

    tmp_lba = offset / 512;
    tmp_lba_range = (offset + size - 1) / 512 - (tmp_lba) + 1;
    tmp_buf = calloc(tmp_lba_range * 512, sizeof(char));

    for (int i = 0; i < tmp_lba_range; i++) {
        // TODO ssd_do_read
        ftl_read(tmp_buf + i * 512, tmp_lba + i);
    }

    memcpy(buf, tmp_buf + offset % 512, size);

    free(tmp_buf);
    return size;
}
static int ssd_read(const char* path, char* buf, size_t size,
                    off_t offset, struct fuse_file_info* fi) {
    (void)fi;
    if (ssd_file_type(path) != SSD_FILE) {
        return -EINVAL;
    }
    return ssd_do_read(buf, size, offset);
}

static unsigned char gc_page_copy(size_t from_pca, size_t to_pca) {
    // can't merge same block
    if (from_pca == to_pca) return 0;

    int valid_cnt = valid_count[from_pca],
        invalid_cnt = invalid_count[to_pca];

    // unnecessary to merge when invalid count is 0
    if (invalid_cnt == 0 || valid_cnt == PAGE_PER_BLOCK || valid_cnt == FREE_BLOCK) return 0;

    if (valid_cnt != invalid_cnt) return 0;

    // time to copy page
    int from_idx = 0, to_idx = 0;
    int cnt = valid_cnt;
    char* buf = malloc(512);
    unsigned int from_page_idx = from_pca * PAGE_PER_BLOCK + from_idx;
    unsigned int to_page_idx = to_pca * PAGE_PER_BLOCK + to_idx;
    while (cnt--) {
        while (P2L[from_page_idx] == NOT_USED_PCA) from_page_idx += 1;
        while (P2L[to_page_idx] != INVALID_PCA) to_page_idx += 1;

        PCA_RULE temp_from_pca = {
            .fields.nand = from_page_idx / PAGE_PER_BLOCK,
            .fields.lba = from_page_idx % PAGE_PER_BLOCK,
        };
        PCA_RULE temp_to_pca = {
            .fields.nand = to_page_idx / PAGE_PER_BLOCK,
            .fields.lba = to_page_idx % PAGE_PER_BLOCK,
        };

        // copy physical data
        nand_read(buf, temp_from_pca.pca);
        nand_write(buf, temp_to_pca.pca);

        // update L2P & P2L
        L2P[P2L[from_page_idx]] = temp_to_pca.pca;
        P2L[to_page_idx] = P2L[from_page_idx];

        from_page_idx += 1;
        to_page_idx += 1;
    }
    invalid_count[to_pca] = 0;
    valid_count[to_pca] = PAGE_PER_BLOCK;

    // erase the `from` page
    nand_erase(from_pca);

    get_next_block();
}

static int ssd_do_write(const char* buf, size_t size, off_t offset) {
    int tmp_lba, tmp_lba_range, process_size;
    int idx, curr_size, remain_size, rst;
    char* tmp_buf;

#ifdef DEBUG
    printf(BLUE "ssd_do_write(0x%x, %d, 0x%x)" NC, buf, size, offset);
#endif
    host_write_size += size;
    if (ssd_expand(offset + size) != 0) {
        printf(GREEN "WUT" NC);
        return -ENOMEM;
    }

    tmp_lba = offset / 512;
    tmp_lba_range = (offset + size - 1) / 512 - (tmp_lba) + 1;

    process_size = 0;
    remain_size = size;
    curr_size = 0;

    // TODO ssd_do_write
    char *temp_buf = malloc(sizeof(char) * 512), *temp_buf_ptr, *from_buf_ptr;
    int seek_off = offset % 512;
    int temp_size;

    // traverse
    for (idx = 0; idx < tmp_lba_range; idx++) {
        memset(temp_buf, 0, sizeof(char) * 512);

        int curr_lba = tmp_lba + idx;
        // read
        PCA_RULE pca;
        pca.pca = L2P[curr_lba];
        if (pca.pca != INVALID_PCA) {  // if there is data in current lba
            ftl_read(temp_buf, curr_lba);
        }

        // modify
        // if (idx == 0) {  // the first block -> alignment problem
        //     printf(GREEN "memcpy(0x%x, 0x%x, %d)" NC, temp_buf + seek_off, buf, buf_first_size);
        //     memcpy(temp_buf + seek_off, buf, 512 - seek_off);
        // } else {
        //     int curr_idx = buf_first_size + 512 * (idx - 1);
        //     printf(GREEN "memcpy(0x%x, 0x%x, %d)" NC, temp_buf, buf + curr_idx, (size - curr_idx) >= 512 ? 512 : (size - curr_idx));
        //     memcpy(temp_buf, buf + curr_idx, (size - curr_idx) >= 512 ? 512 : (size - curr_idx));
        // }
        if (idx == 0 && idx == tmp_lba_range - 1) {
            temp_buf_ptr = temp_buf + seek_off;
            from_buf_ptr = buf;
            temp_size = size;
        } else if (idx == 0) {
            temp_buf_ptr = temp_buf + seek_off;
            from_buf_ptr = buf;
            temp_size = (512 - seek_off);
        } else if (idx == tmp_lba_range - 1) {
            int curr_idx = (512 - seek_off) + 512 * (idx - 1);
            temp_buf_ptr = temp_buf;
            from_buf_ptr = buf + curr_idx;
            temp_size = size - curr_idx;
        } else {
            int curr_idx = (512 - seek_off) + 512 * (idx - 1);
            temp_buf_ptr = temp_buf;
            from_buf_ptr = buf + curr_idx;
            temp_size = 512;
        }
#ifdef DEBUG
        printf(GREEN "memcpy(0x%x, 0x%x, %d)" NC, temp_buf_ptr, from_buf_ptr, temp_size);
#endif
        memcpy(temp_buf_ptr, from_buf_ptr, temp_size);

        // write
        ftl_write(temp_buf, tmp_lba_range, curr_lba);

        // set INVALID_PCA to NOT_USED_PCA
        if (pca.pca != INVALID_PCA) {
            P2L[pca.fields.nand * PAGE_PER_BLOCK + pca.fields.lba] = NOT_USED_PCA;
            valid_count[pca.fields.nand] -= 1;
        }

#ifdef DEBUG
        printf(GREEN "-------------------" NC);
        // show_L2P();
        // show_P2L();
#endif
        // GC: merge two blocks
        for (int i = 0; i < PHYSICAL_NAND_NUM; i++) {
            for (int j = i + 1; j < PHYSICAL_NAND_NUM; j++) {
                if (gc_page_copy(i, j)) goto end_of_gc_merge;
                if (gc_page_copy(j, i)) goto end_of_gc_merge;
            }
        }
    end_of_gc_merge:

        // GC: if valid_count[i] == 0, simply erase the block
        for (int i = 0; i < PHYSICAL_NAND_NUM; i++) {
            if (valid_count[i] == 0) nand_erase(i);
        }
#ifdef DEBUG
        show_L2P();
        show_P2L();
#endif
    }
    free(temp_buf);
    return size;
}
static int ssd_write(const char* path, const char* buf, size_t size,
                     off_t offset, struct fuse_file_info* fi) {
    (void)fi;
    if (ssd_file_type(path) != SSD_FILE) {
        return -EINVAL;
    }
    return ssd_do_write(buf, size, offset);
}
static int ssd_truncate(const char* path, off_t size,
                        struct fuse_file_info* fi) {
    (void)fi;
    memset(L2P, INVALID_PCA, sizeof(int) * LOGICAL_NAND_NUM * PAGE_PER_BLOCK);
    memset(P2L, INVALID_LBA, sizeof(int) * PHYSICAL_NAND_NUM * PAGE_PER_BLOCK);
    memset(valid_count, FREE_BLOCK, sizeof(int) * PHYSICAL_NAND_NUM);
    curr_pca.pca = INVALID_PCA;
    free_block_number = PHYSICAL_NAND_NUM;
    if (ssd_file_type(path) != SSD_FILE) {
        return -EINVAL;
    }

    return ssd_resize(size);
}
static int ssd_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info* fi,
                       enum fuse_readdir_flags flags) {
    (void)fi;
    (void)offset;
    (void)flags;
    if (ssd_file_type(path) != SSD_ROOT) {
        return -ENOENT;
    }
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, SSD_NAME, NULL, 0, 0);
    return 0;
}
static int ssd_ioctl(const char* path, unsigned int cmd, void* arg,
                     struct fuse_file_info* fi, unsigned int flags, void* data) {
    if (ssd_file_type(path) != SSD_FILE) {
        return -EINVAL;
    }
    if (flags & FUSE_IOCTL_COMPAT) {
        return -ENOSYS;
    }
    switch (cmd) {
        case SSD_GET_LOGIC_SIZE:
            *(size_t*)data = logic_size;
            return 0;
        case SSD_GET_PHYSIC_SIZE:
            *(size_t*)data = physic_size;
            return 0;
        case SSD_GET_WA:
            *(double*)data = (double)nand_write_size / (double)host_write_size;
            return 0;
    }
    return -EINVAL;
}
static const struct fuse_operations ssd_oper =
    {
        .getattr = ssd_getattr,
        .readdir = ssd_readdir,
        .truncate = ssd_truncate,
        .open = ssd_open,
        .read = ssd_read,
        .write = ssd_write,
        .ioctl = ssd_ioctl,
};
int main(int argc, char* argv[]) {
    int idx;
    char nand_name[100];
    physic_size = 0;
    logic_size = 0;
    curr_pca.pca = INVALID_PCA;
    free_block_number = PHYSICAL_NAND_NUM;

    L2P = malloc(LOGICAL_NAND_NUM * PAGE_PER_BLOCK * sizeof(int));
    memset(L2P, INVALID_PCA, sizeof(int) * LOGICAL_NAND_NUM * PAGE_PER_BLOCK);
    P2L = malloc(PHYSICAL_NAND_NUM * PAGE_PER_BLOCK * sizeof(int));
    memset(P2L, INVALID_LBA, sizeof(int) * PHYSICAL_NAND_NUM * PAGE_PER_BLOCK);
    valid_count = malloc(PHYSICAL_NAND_NUM * sizeof(int));
    memset(valid_count, FREE_BLOCK, sizeof(int) * PHYSICAL_NAND_NUM);
    invalid_count = malloc(PHYSICAL_NAND_NUM * sizeof(int));
    // memset(invalid_count, FREE_BLOCK, sizeof(int) * PHYSICAL_NAND_NUM);
    for (int i = 0; i < PHYSICAL_NAND_NUM; i++) invalid_count[i] = PAGE_PER_BLOCK;

    // create nand file
    for (idx = 0; idx < PHYSICAL_NAND_NUM; idx++) {
        FILE* fptr;
        snprintf(nand_name, 100, "%s/nand_%d", NAND_LOCATION, idx);
        fptr = fopen(nand_name, "w");
        if (fptr == NULL) {
            printf("open fail");
        }
        fclose(fptr);
    }
    return fuse_main(argc, argv, &ssd_oper, NULL);
}
