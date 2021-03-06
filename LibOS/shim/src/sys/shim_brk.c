/* Copyright (C) 2014 Stony Brook University
   Copyright (C) 2020 Invisible Things Lab
   This file is part of Graphene Library OS.

   Graphene Library OS is free software: you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   Graphene Library OS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/*
 * shim_brk.c
 *
 * Implementation of system call "brk".
 */

#include <sys/mman.h>

#include "pal.h"
#include "shim_checkpoint.h"
#include "shim_internal.h"
#include "shim_table.h"
#include "shim_utils.h"
#include "shim_vma.h"

static struct {
    size_t data_segment_size;
    char* brk_start;
    char* brk_current;
    char* brk_end;
} brk_region;

static struct shim_lock brk_lock;

int init_brk_region(void* brk_start, size_t data_segment_size) {
    size_t brk_max_size = DEFAULT_BRK_MAX_SIZE;
    data_segment_size = ALLOC_ALIGN_UP(data_segment_size);

    if (root_config) {
        char brk_cfg[CONFIG_MAX];
        if (get_config(root_config, "sys.brk.max_size", brk_cfg, sizeof(brk_cfg)) > 0)
            brk_max_size = parse_int(brk_cfg);
    }

    if (brk_start && !IS_ALLOC_ALIGNED_PTR(brk_start)) {
        debug("Starting brk address is not aligned!\n");
        return -EINVAL;
    }
    if (!IS_ALLOC_ALIGNED(brk_max_size)) {
        debug("Max brk size is not aligned!\n");
        return -EINVAL;
    }

    if (brk_start && brk_start <= PAL_CB(user_address.end)
            && brk_max_size <= (uintptr_t)PAL_CB(user_address.end)
            && (uintptr_t)brk_start < (uintptr_t)PAL_CB(user_address.end) - brk_max_size) {
        size_t offset = 0;
#if ENABLE_ASLR == 1
        int ret = DkRandomBitsRead(&offset, sizeof(offset));
        if (ret < 0) {
            return -convert_pal_errno(-ret);
        }
        /* Linux randomizes brk at offset from 0 to 0x2000000 from main executable data section
         * https://elixir.bootlin.com/linux/v5.6.3/source/arch/x86/kernel/process.c#L914 */
        offset %= MIN((size_t)0x2000000,
                      (size_t)((char*)PAL_CB(user_address.end) - brk_max_size - (char*)brk_start));
        offset = ALLOC_ALIGN_DOWN(offset);
#endif
        brk_start = (char*)brk_start + offset;

        ret = bkeep_mmap_fixed(brk_start, brk_max_size, PROT_NONE,
                               MAP_FIXED_NOREPLACE | VMA_UNMAPPED, NULL, 0, "heap");
        if (ret == -EEXIST) {
            /* Let's try mapping brk anywhere. */
            brk_start = NULL;
            ret = 0;
        }
        if (ret < 0) {
            return ret;
        }
    } else {
        /* Let's try mapping brk anywhere. */
        brk_start = NULL;
    }

    if (!brk_start) {
        int ret;
#if ENABLE_ASLR == 1
        ret = bkeep_mmap_any_aslr
#else
        ret = bkeep_mmap_any
#endif
                            (brk_max_size, PROT_NONE, VMA_UNMAPPED, NULL, 0, "heap", &brk_start);
        if (ret < 0) {
            return ret;
        }
    }

    brk_region.brk_start = brk_start;
    brk_region.brk_current = brk_region.brk_start;
    brk_region.brk_end = (char*)brk_start + brk_max_size;
    brk_region.data_segment_size = data_segment_size;

    set_rlimit_cur(RLIMIT_DATA, brk_max_size + data_segment_size);

    if (!create_lock(&brk_lock)) {
        debug("Creating brk_lock failed!\n");
        return -ENOMEM;
    }

    return 0;
}

void reset_brk(void) {
    lock(&brk_lock);

    void* tmp_vma = NULL;
    size_t allocated_size = ALLOC_ALIGN_UP_PTR(brk_region.brk_current) - brk_region.brk_start;
    if (bkeep_munmap(brk_region.brk_start, brk_region.brk_end - brk_region.brk_start,
                     /*is_internal=*/false, &tmp_vma) < 0) {
        BUG();
    }

    DkVirtualMemoryFree(brk_region.brk_start, allocated_size);
    bkeep_remove_tmp_vma(tmp_vma);

    brk_region.brk_start = NULL;
    brk_region.brk_current = NULL;
    brk_region.brk_end = NULL;
    brk_region.data_segment_size = 0;
    unlock(&brk_lock);

    destroy_lock(&brk_lock);
}

void* shim_do_brk(void* _brk) {
    char* brk = _brk;
    size_t size = 0;
    char* brk_aligned = ALLOC_ALIGN_UP_PTR(brk);

    lock(&brk_lock);

    char* brk_current = ALLOC_ALIGN_UP_PTR(brk_region.brk_current);

    if (brk < brk_region.brk_start) {
        goto out;
    } else if (brk <= brk_current) {
        size = brk_current - brk_aligned;

        if (size) {
            if (bkeep_mmap_fixed(brk_aligned, brk_region.brk_end - brk_aligned, PROT_NONE,
                                 MAP_FIXED | VMA_UNMAPPED, NULL, 0, "heap")) {
                goto out;
            }

            DkVirtualMemoryFree(brk_aligned, size);
        }

        brk_region.brk_current = brk;
        goto out;
    } else if (brk > brk_region.brk_end) {
        goto out;
    }

    uint64_t rlim_data = get_rlimit_cur(RLIMIT_DATA);
    size = brk_aligned - brk_region.brk_start;

    if (rlim_data < brk_region.data_segment_size
            || rlim_data - brk_region.data_segment_size < size) {
        goto out;
    }

    size = brk_aligned - brk_current;
    /* brk_aligned >= brk > brk_current */
    assert(size);

    if (bkeep_mmap_fixed(brk_current, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                         NULL, 0, "heap") < 0) {
        goto out;
    }

    if (!DkVirtualMemoryAlloc(brk_current, size, 0, PAL_PROT_READ | PAL_PROT_WRITE)) {
        if (bkeep_mmap_fixed(brk_current, brk_region.brk_end - brk_current, PROT_NONE,
                             MAP_FIXED | VMA_UNMAPPED, NULL, 0, "heap") < 0) {
            BUG();
        }
        goto out;
    }

    brk_region.brk_current = brk;

out:
    brk = brk_region.brk_current;
    unlock(&brk_lock);
    return brk;
}

BEGIN_CP_FUNC(brk) {
    __UNUSED(obj);
    __UNUSED(size);
    __UNUSED(objp);
    ADD_CP_FUNC_ENTRY((ptr_t)brk_region.brk_start);
    ADD_CP_ENTRY(SIZE, brk_region.brk_current - brk_region.brk_start);
    ADD_CP_ENTRY(SIZE, brk_region.brk_end - brk_region.brk_start);
    ADD_CP_ENTRY(SIZE, brk_region.data_segment_size);
}
END_CP_FUNC(brk)

BEGIN_RS_FUNC(brk) {
    __UNUSED(rebase);
    brk_region.brk_start         = (char*)GET_CP_FUNC_ENTRY();
    brk_region.brk_current       = brk_region.brk_start + GET_CP_ENTRY(SIZE);
    brk_region.brk_end           = brk_region.brk_start + GET_CP_ENTRY(SIZE);
    brk_region.data_segment_size = GET_CP_ENTRY(SIZE);
}
END_RS_FUNC(brk)
