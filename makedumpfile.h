/*
 * makedumpfile.h
 *
 * Copyright (C) 2006  NEC Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gelf.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/utsname.h>
#include <zlib.h>
#include <elfutils/libdw.h>
#include <libelf.h>
#include <dwarf.h>
#include "diskdump_mod.h"

/*
 * Result of command
 */
#define COMPLETED	(0)
#define FAILED		(1)
#define WRONG_RELEASE	(2)	/* utsname.release does not match. */
#define ANALYSIS_FAILED	(3)	/* detected illegal page descriptor. */
#define OUTPUT_FAILED	(4)	/* detected an output error. */

/*
 * Type of memory management
 */
enum {
	NOT_FOUND_MEMTYPE,
	SPARSEMEM,
	FLATMEM
};

/*
 * Page flags
 */
#define PG_lru			 (5)
#define PG_private		(11)	/* Has something at ->private */
#define PG_swapcache		(15)	/* Swap page: swp_entry_t in private */

#define PAGE_MAPPING_ANON	(1)

#define LSEEKED_BITMAP	(1)
#define LSEEKED_PDESC	(2)
#define LSEEKED_PDATA	(3)

static inline int
test_bit(int nr, unsigned long addr)
{
	int mask;

	mask = 1 << (nr & 0x1f);
	return ((mask & addr) != 0);
}

#define isLRU(flags)		test_bit(PG_lru, flags)
#define isReserved(flags)	test_bit(PG_reserved, flags)
#define isPrivate(flags)	test_bit(PG_private, flags)
#define isNosave(flags)		test_bit(PG_nosave, flags)
#define isCompound(flags)	test_bit(PG_compound, flags)
#define isSwapCache(flags)	test_bit(PG_swapcache, flags)

static inline int
isAnon(unsigned long mapping)
{
	return ((unsigned long)mapping & PAGE_MAPPING_ANON) != 0;
}

/*
 * for SPARSEMEM
 */
#define SECTION_SIZE_BITS()	(info->section_size_bits)
#define PAGESHIFT()		(ffs(info->page_size) - 1)
#define PFN_SECTION_SHIFT()	(SECTION_SIZE_BITS() - PAGESHIFT())
#define PAGES_PER_SECTION()	(1UL << PFN_SECTION_SHIFT())
#define _SECTIONS_PER_ROOT()	(1)
#define _SECTIONS_PER_ROOT_EXTREME()	(info->page_size / SIZE(mem_section))
#define SECTIONS_PER_ROOT()	(info->sections_per_root)
#define SECTION_ROOT_MASK()	(SECTIONS_PER_ROOT() - 1)
#define SECTION_NR_TO_ROOT(sec)	((sec) / SECTIONS_PER_ROOT())
#define SECTION_MAP_LAST_BIT	(1UL<<2)
#define SECTION_MAP_MASK	(~(SECTION_MAP_LAST_BIT-1))
#define NR_SECTION_ROOTS()	divideup(num_section, SECTIONS_PER_ROOT())
#define SECTION_NR_TO_PFN(sec)	((sec) << PFN_SECTION_SHIFT())

/*
 * Incorrect address
 */
#define NOT_MEMMAP_ADDR	(0x0)
#define NOT_KV_ADDR	(0x0)

/*
 * Dump Level
 */
#define MIN_DUMP_LEVEL		(0)
#define MAX_DUMP_LEVEL		(31)
#define DL_EXCLUDE_ZERO		(0x001) /* Exclude Pages filled with Zeros */
#define DL_EXCLUDE_CACHE	(0x002) /* Exclude Cache Pages
				           without Private Pages */
#define DL_EXCLUDE_CACHE_PRI	(0x004) /* Exclude Cache Pages
				           with Private Pages */
#define DL_EXCLUDE_USER_DATA	(0x008) /* Exclude UserProcessData Pages */
#define DL_EXCLUDE_FREE		(0x010)	/* Exclude Free Pages */

/*
 * For parse_line()
 */
#define NULLCHAR	('\0')
#define MAXARGS		(100)   /* max number of arguments to one function */
#define LASTCHAR(s)	(s[strlen(s)-1])

/*
 * ELF flags
 */
#define ELF32	(1)
#define ELF64	(2)

#define BITPERBYTE		(8)
#define PGMM_CACHED		(512)
#define PFN_EXCLUDED		(256)
#define BUFSIZE_FGETS		(1500)
#define BUFSIZE_BITMAP		(4096)
#define PFN_BUFBITMAP		(BITPERBYTE*BUFSIZE_BITMAP)
#define FILENAME_BITMAP		"/tmp/kdump_bitmap.tmp"
#define FILENAME_3RD_BITMAP	"/tmp/kdump_3rd_bitmap.tmp"

/*
 * Minimam vmcore has 2 ProgramHeaderTables(PT_NOTE and PT_LOAD).
 */
#define MIN_ELF32_HEADER_SIZE \
	sizeof(Elf32_Ehdr)+sizeof(Elf32_Phdr)+sizeof(Elf32_Phdr)
#define MIN_ELF64_HEADER_SIZE \
	sizeof(Elf64_Ehdr)+sizeof(Elf64_Phdr)+sizeof(Elf64_Phdr)
#define MIN_ELF_HEADER_SIZE \
	MAX(MIN_ELF32_HEADER_SIZE, MIN_ELF64_HEADER_SIZE)
#define STRNEQ(A, B)	(A && B && \
	(strncmp((char *)(A), (char *)(B), strlen((char *)(B))) == 0))

#define UINT(ADDR)	*((unsigned int *)(ADDR))
#define ULONG(ADDR)	*((unsigned long *)(ADDR))

/*
 * for symbol
 */
#define NOT_FOUND_SYMBOL	(0)
#define INVALID_SYMBOL_DATA	(ULONG_MAX)
#define SYMBOL(X)		(symbol_table.X)
#define SYMBOL_INIT(symbol, str_symbol) \
do { \
	SYMBOL(symbol) = get_symbol_addr(info, str_symbol, 0); \
} while (0)
#define SYMBOL_INIT_NEXT(symbol, str_symbol) \
do { \
	SYMBOL(symbol) = get_symbol_addr(info, str_symbol, 1); \
} while (0)
#define WRITE_SYMBOL(str_symbol, symbol) \
do { \
	if (SYMBOL(symbol) != NOT_FOUND_SYMBOL) { \
		fprintf(info->file_configfile, "%s%lx\n", \
		    STR_SYMBOL(str_symbol), SYMBOL(symbol)); \
	} \
} while (0)
#define READ_SYMBOL(str_symbol, symbol) \
do { \
	SYMBOL(symbol) = read_config_symbol(info, STR_SYMBOL(str_symbol)); \
	if (SYMBOL(symbol) == INVALID_SYMBOL_DATA) \
		return FALSE; \
} while (0)

/*
 * for structure
 */
#define NOT_FOUND_STRUCTURE	(-1)
#define FAILED_DWARFINFO	(-2)
#define INVALID_STRUCTURE_DATA	(-3)
#define SIZE(X)			(size_table.X)
#define OFFSET(X)		(offset_table.X)
#define GET_STRUCTURE_SIZE	get_structure_size
#define GET_MEMBER_OFFSET	get_member_offset
#define SIZE_INIT(X, Y) \
do { \
	if ((SIZE(X) = GET_STRUCTURE_SIZE(Y)) == FAILED_DWARFINFO) \
		return FALSE; \
} while (0)
#define OFFSET_INIT(X, Y, Z) \
do { \
	if ((OFFSET(X) = GET_MEMBER_OFFSET(Y, Z, DWARF_INFO_GET_MEMBER_OFFSET)) \
	     == FAILED_DWARFINFO) \
		return FALSE; \
} while (0)
#define OFFSET_INIT_NONAME(X, Y, S) \
do { \
	if ((OFFSET(X) = (GET_MEMBER_OFFSET(Y, NULL, DWARF_INFO_GET_NOT_NAMED_UNION_OFFSET) + S)) \
	     == FAILED_DWARFINFO) \
		return FALSE; \
} while (0)
#define WRITE_STRUCTURE_SIZE(str_structure, structure) \
do { \
	if (SIZE(structure) != NOT_FOUND_STRUCTURE) { \
		fprintf(info->file_configfile, "%s%ld\n", \
		    STR_SIZE(str_structure), SIZE(structure)); \
	} \
} while (0)
#define WRITE_MEMBER_OFFSET(str_member, member) \
do { \
	if (OFFSET(member) != NOT_FOUND_STRUCTURE) { \
		fprintf(info->file_configfile, "%s%ld\n", \
		    STR_OFFSET(str_member), OFFSET(member)); \
	} \
} while (0)
#define READ_STRUCTURE_SIZE(str_structure, structure) \
do { \
	SIZE(structure) = read_config_structure(info,STR_SIZE(str_structure)); \
	if (SIZE(structure) == INVALID_STRUCTURE_DATA) \
		return FALSE; \
} while (0)
#define READ_MEMBER_OFFSET(str_member, member) \
do { \
	OFFSET(member) = read_config_structure(info, STR_OFFSET(str_member)); \
	if (OFFSET(member) == INVALID_STRUCTURE_DATA) \
		return FALSE; \
} while (0)

/*
 * kernel version
 */
#define VERSION_2_6_15		(15)
#define VERSION_2_6_16		(16)
#define VERSION_2_6_17		(17)
#define VERSION_2_6_18		(18)

/*
 * field name of config file
 */
#define STR_OSRELEASE	"OSRELEASE="
#define STR_PAGESIZE	"PAGESIZE="
#define STR_SYMBOL(X)	"SYMBOL("X")="
#define STR_SIZE(X)	"SIZE("X")="
#define STR_OFFSET(X)	"OFFSET("X")="

/*
 * vm_table
 */
#define NODES_ONLINE	(1)

/*
 * common value
 */
#define TRUE		(1)
#define FALSE		(0)
#define MAX(a,b)	((a) > (b) ? (a) : (b))
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#define MAX_NR_ZONES	(4)
#define LONG_MAX	((long)(~0UL>>1))
#define ULONG_MAX	(~0UL)
#define ULONGLONG_MAX	(~0ULL)

/*
 * The value of dependence on machine
 */
#ifdef __x86__
#define PAGE_OFFSET		(0xc0000000)
#define __VMALLOC_RESERVE       (128 << 20)
#define MAXMEM                  (-PAGE_OFFSET-__VMALLOC_RESERVE)
#define KVBASE_MASK		(0x7fffff)
#define KVBASE			(SYMBOL(_stext) & ~KVBASE_MASK)
#define _SECTION_SIZE_BITS	(26)
#define _SECTION_SIZE_BITS_PAE	(30)
#define SIZEOF_NODE_ONLINE_MAP	(4)
#define MAX_ORDER		(11)
#endif /* x86 */

#ifdef __x86_64__
#define PAGE_OFFSET		(0xffff810000000000)
#define __START_KERNEL_map	(0xffffffff80000000)
#define VMALLOC_START		(0xffffc20000000000)
#define VMALLOC_END		(0xffffe1ffffffffff)
#define MODULES_VADDR		(0xffffffff88000000)
#define MODULES_END		(0xfffffffffff00000)
#define MAXMEM			(0x3fffffffffffUL)
#define KVBASE			PAGE_OFFSET
#define _SECTION_SIZE_BITS	(27)
#define SIZEOF_NODE_ONLINE_MAP	(8)
#define MAX_ORDER		(11)
#endif /* x86_64 */

#ifdef __powerpc__
#define PAGE_OFFSET		(0xc000000000000000)
#define KERNELBASE		PAGE_OFFSET
#define VMALLOCBASE     	(0xD000000000000000)
#define MAXMEM			(-KERNELBASE-VMALLOCBASE)
#define KVBASE			(SYMBOL(_stext))
#define _SECTION_SIZE_BITS	(24)
#define SIZEOF_NODE_ONLINE_MAP	(8)
#define MAX_ORDER		(9)
#endif

#ifdef __ia64__ /* ia64 */
#define MAXMEM			(0xffffffffffffffff)
#define REGION_SHIFT		(61)

#define KERNEL_CACHED_REGION	(7)
#define KERNEL_UNCACHED_REGION	(6)
#define KERNEL_VMALLOC_REGION	(5)
#define USER_STACK_REGION	(4)
#define USER_DATA_REGION	(3)
#define USER_TEXT_REGION	(2)
#define USER_SHMEM_REGION	(1)
#define USER_IA32_EMUL_REGION	(0)

#define KERNEL_CACHED_BASE	((unsigned long)KERNEL_CACHED_REGION << REGION_SHIFT)
#define KERNEL_UNCACHED_BASE	((unsigned long)KERNEL_UNCACHED_REGION << REGION_SHIFT)
#define KERNEL_VMALLOC_BASE	((unsigned long)KERNEL_VMALLOC_REGION << REGION_SHIFT)

#define KVBASE			KERNEL_VMALLOC_BASE
#define _PAGE_SIZE_64M		(26)
#define KERNEL_TR_PAGE_SIZE	(1 << _PAGE_SIZE_64M)
#define KERNEL_TR_PAGE_MASK	(~(KERNEL_TR_PAGE_SIZE - 1))
#define DEFAULT_PHYS_START	(KERNEL_TR_PAGE_SIZE * 1)
#define _SECTION_SIZE_BITS	(30)
#define SIZEOF_NODE_ONLINE_MAP	(32)
#define MAX_ORDER		(17)
#endif          /* ia64 */

/*
 * The function of dependence on machine
 */
#ifdef __x86__
int get_machdep_info_x86();
#define get_phys_base(X)	TRUE
#define get_machdep_info(X)	get_machdep_info_x86(X)
#define vaddr_to_offset(X, Y)	vaddr_to_offset_general(X,Y)
#endif /* x86 */

#ifdef __x86_64__
int get_phys_base_x86_64();
int get_machdep_info_x86_64();
off_t vaddr_to_offset_x86_64();
#define get_phys_base(X)	get_phys_base_x86_64(X)
#define get_machdep_info(X)	get_machdep_info_x86_64(X)
#define vaddr_to_offset(X, Y)	vaddr_to_offset_x86_64(X, Y)
#endif /* x86_64 */

#ifdef __powerpc__ /* powerpc */
int get_machdep_info_ppc64();
#define get_machdep_info(X)	get_machdep_info_ppc64(X)
#define get_phys_base(X)	TRUE
#define vaddr_to_offset(X, Y)	vaddr_to_offset_general(X, Y)
#endif          /* powerpc */

#ifdef __ia64__ /* ia64 */
int get_phys_base_ia64();
int get_machdep_info_ia64();
#define get_machdep_info(X)	get_machdep_info_ia64(X)
#define get_phys_base(X)	get_phys_base_ia64(X)
#define vaddr_to_offset(X, Y)	vaddr_to_offset_general(X, Y)
#define VADDR_REGION(X)		((X) >> REGION_SHIFT)
#endif          /* ia64 */

#define MSG(x...)	fprintf(stdout, x)
#define ERRMSG(x...)	fprintf(stderr, x)

struct pt_load_segment {
	loff_t			file_offset;
	unsigned long long	phys_start;
	unsigned long long	phys_end;
	unsigned long long	virt_start;
	unsigned long long	virt_end;
};

struct mem_map_data {
	unsigned long long	pfn_start;
	unsigned long long	pfn_end;
	unsigned long	mem_map;
};

struct dump_bitmap {
	int		fd;
	int		no_block;
	char		*file_name;
	char		*buf;
	off_t		offset;
};

struct cache_data {
	int	fd;
	char	*file_name;
	char	*buf;
	size_t	buf_size;
	size_t	cache_size;
	off_t	offset;
};

struct DumpInfo {
	int		kernel_version;      /* version of first kernel */

	/*
	 * General info:
	 */
	int		dump_level;          /* dump level */
	int		flag_compress;       /* flag of compression */
	int		flag_elf;
	int		flag_elf_dumpfile;   /* flag of elf dump file */
	int		flag_vmlinux;	     /* flag of vmlinux */
	int		flag_generate_config;/* flag of generating config file */
	int		flag_read_config;    /* flag of reading config file */
	int		flag_exclude_free;   /* flag of excluding free page */
	int		flag_show_version;   /* flag of showing version */
	long		page_size;           /* size of page */
	unsigned long long	max_mapnr;   /* number of page descriptor */
	unsigned long   section_size_bits;
	unsigned long   sections_per_root;
	unsigned long	phys_base;

	/*
	 * diskdimp info:
	 */
	int		block_order;
	off_t		offset_bitmap1;
	unsigned long	len_bitmap;          /* size of bitmap(1st and 2nd) */
	unsigned long	len_3rd_bitmap;      /* size of bitmap(3rd) */
	struct disk_dump_header		*dump_header; 

	/*
	 * ELF header info:
	 */
	unsigned int		num_load_memory;
	unsigned int		num_load_dumpfile;
	size_t			offset_load_memory;
	size_t			offset_load_dumpfile;
	struct pt_load_segment	*pt_load_segments;

	/*
	 * mem_map info:
	 */
	unsigned int		num_mem_map;
	struct mem_map_data	*mem_map_data;

	/*
	 * Dump memory image info:
	 */
	int			fd_memory;
	char			*name_memory;

	/*
	 * Dump file info:
	 */
	int			fd_dumpfile;
	char			*name_dumpfile;

	/*
	 * bitmap info:
	 */
	int			fd_bitmap;
	int			fd_3rd_bitmap;
	char			*name_bitmap;
	char			*name_3rd_bitmap;
	struct cache_data	*bm3;
	struct vm_table {                /* kernel VM-related data */
		ulong flags;
		int numnodes;
		int nr_free_areas;
		ulong *node_online_map;
		int node_online_map_len;
	} vm_table;

	/*
	 * config file info:
	 */
	FILE			*file_configfile;
	char			*name_configfile;	     /* config file */
	char			release[65]; /*Can I define 65 automatically?*/
};

struct symbol_table {
	unsigned long	mem_map;
	unsigned long	mem_section;
	unsigned long	pkmap_count;
	unsigned long	pkmap_count_next;
	unsigned long	system_utsname;
	unsigned long	_stext;
	unsigned long	phys_base;
	unsigned long	node_online_map;
	unsigned long	node_data;
	unsigned long	pgdat_list;
	unsigned long	contig_page_data;
};

struct size_table {
	long	page;
	long	mem_section;
	long	pglist_data;
	long	zone;
	long	free_area;
	long	list_head;
};

struct offset_table {
	struct page {
		long	flags;
		long	_count;
		long	mapping;
		long	lru;
	} page;
	struct mem_section {
		long	section_mem_map;
	} mem_section;
	struct zone {
		long	free_pages;
		long	free_area;
		long	spanned_pages;
	} zone;
	struct pglist_data {
		long	node_zones;
	} pglist_data;
	struct free_area {
		long	free_list;
	} free_area;
	struct list_head {
		long	next;
		long	prev;
	} list_head;
};

extern struct symbol_table	symbol_table;
extern struct size_table	size_table;
extern struct offset_table	offset_table;

/*
 * Debugging information
 */
#define DWARF_INFO_GET_STRUCT_SIZE		1
#define DWARF_INFO_GET_MEMBER_OFFSET		2
#define DWARF_INFO_GET_NOT_NAMED_UNION_OFFSET	3
#define DWARF_INFO_FOUND_STRUCT			1
#define DWARF_INFO_FOUND_MEMBER			2
#define DWARF_INFO_FOUND_LOCATION		4
#define DWARF_INFO_FOUND_ALL	(DWARF_INFO_FOUND_STRUCT|DWARF_INFO_FOUND_MEMBER|DWARF_INFO_FOUND_LOCATION)

struct dwarf_info {
	uint32_t	status;		/* TEMP */
	unsigned int	cmd;		/* IN */
	char	*vmlinux_name;		/* IN */
	char	*struct_name;		/* IN */
	int	vmlinux_fd;		/* IN */
	int	struct_size;		/* OUT */
	char	*member_name;		/* IN */
	int	member_offset;		/* OUT */
};

struct dwarf_values {
	Dwarf_Die *die;
	uint32_t *found_map;
};

extern struct dwarf_info	dwarf_info;
