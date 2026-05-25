/*
 * kernel_code.c - Project 2, CMPSC 473
 * Copyright 2026 Ruslan Nikolaev <rnikola@psu.edu>
 * Distribution, modification, or usage without explicit author's permission
 * is not allowed.
 */

#include <kernel.h>
#include <types.h>
#include <printf.h>
#include <malloc.h>
#include <string.h>

void *page_table = NULL; /* TODO: Must be initialized to the page table address */
void *user_stack = NULL; /* TODO: Must be initialized to a user stack virtual address */
void *user_program = NULL; /* TODO: Must be initialized to a user program virtual address */

void kernel_init(void *ustack, void *uprogram, void *memory, size_t memorySize)
{
	/* Q1 Answer */
	printf("Hello from sgroup86, KizunaAI0630\n");
	// 'memory' points to the place where memory can be used to create
	// page tables (assume 1:1 initial virtual-to-physical mappings here)
	// 'memorySize' is the maximum allowed size, do not exceed that (given just in case)

	// TODO: Create a page table here

	/* Q3 Answer */
	// 1. Reset memory to 0
	uint64_t entries = 4 * 1024 * 1024 / 4; // Page size
	int pt_size = entries / 512; // 2048 page tables needed, each page table has 512 entries
	int pd_size = pt_size / 512; // 4 page directories needed, each page directory has 512 entries
	int pdpt_size = 1; // 1 page directory pointer table needed, each page directory pointer table has 512 entries
	int pml4_size = 1; // 1 page map level 4 table needed, each page map level 4 table has 512 entries

	int table_size = (pml4_size + pdpt_size + pd_size + pt_size);
	memset(memory, 0, table_size * (4 * 1024)); // Clear the memory for page tables, each entry is 4KB = 4096 bytes

	// 2. Create page tables
	// 4 level page table structure: PML4 -> PDPT -> PD -> PT
	uint64_t *pml4 = (uint64_t *)memory; // PML4 starts at the beginning of the memory
	uint64_t *pdpt = pml4 + 512; // PDPT starts after PML4 (512 entries)
	uint64_t *pd = pdpt + 512; // PD starts after PDPT (512 entries)
	uint64_t *pt = pd + (4 * 512); // PT starts after PD (4 PDs, each with 512 entries)

	// 3. Fill the page tables with appropriate entries using bitwise operations (1|0 = 1; 1|1 = 1; 0|0 = 0)
	pml4[0] = ((uint64_t)pdpt) | 3;
	
	for (int i = 0; i < pd_size; i++) {
		pdpt[i] = ((uint64_t)(pd + (i * 512))) | 3;
	}

	for (int i = 0; i < pt_size; i++) {
		pd[i] = ((uint64_t)(pt + (i * 512))) | 3;
	}

	for (uint64_t i = 0; i < entries; i++)
	{
		pt[i] = (i * 4096) | 3; // Present and Read/Write
	}
	
	page_table = pml4; // Set the page_table pointer to the PML4 address

	/* Q4 Answer */
	uint64_t *user_pdpt = pt + entries; // 1 page directory pointer table needed for user program
	uint64_t *user_pd = user_pdpt + 512; // 1 page directory needed for user program
	uint64_t *user_pt = user_pd + 512; // 1 page table needed for user program

	// VA in x86-64 is Sign extend, pml4, pdpt, pd, pt each has 512 entries, from right to left is 12, 9, 9, 9, 9. https://www.amd.com/system/files/TechDocs/24593.pdf
	/* Target VA is 0xFFFFFFFFFFE00000 */

	pml4[0b111111111] = ((uint64_t)user_pdpt) | 7; // User page directory pointer table, User, Read/Write, Present —》 (2^2 + 2^1 + 2^0)
	user_pdpt[0b111111111] = ((uint64_t)user_pd)  | 7;
	user_pd[0b111111111]  = ((uint64_t)user_pt)  | 7;
	
	user_pt[0] = ((uint64_t)uprogram) | 7; // User program, User, Read/Write, Present
	user_pt[1] = (((uint64_t)ustack) - 4096) | 7; // User stack, User, Read/Write, Present




	// TODO: It is OK for Q1-Q3, but should changed
	// for the final part's Q4 when creating your own user page table
	// Changed to the user stack address, which is user_program + 4kb + 4kb (the user program occupies the first 4kb, and the user stack occupies the second 4kb)
	user_stack = (void *)0xFFFFFFFFFFE00000 + 4096 + 4096;

	// TODO: It is OK for Q1-Q3, but should changed
	// for the final part's Q4 when creating your own user page table
	// changed to the user program address, which is 0xFFFFFFFFFFE00000 allow by document.
	user_program = (void *)0xFFFFFFFFFFE00000;

	// The remaining portion just loads the page table,
	// this does not need to be changed:
	// load 'page_table' into the CR3 register
	const char *err = load_page_table(page_table);
	if (err != NULL) {
		printf("ERROR: %s\n", err);
	}

	// The extra credit assignment
	mem_extra_test();
}

/* The entry point for all system calls */
long syscall_entry(long n, long a1, long a2, long a3, long a4, long a5)
{
	// TODO: the system call handler to print a message (n = 1)
	// the system call number is in 'n', make sure it is valid!

	// Arguments are passed in a1,.., a5 and can be of any type
	// (including pointers, which are casted to 'long')
	// For the project, we only use a1 which will contain the address
	// of a string, cast it to a pointer appropriately 

	// For simplicity, assume that the address supplied by the
	// user program is correct
	//
	// Hint: see how 'printf' is used above, you want something very
	// similar here

	/* Q2 Answer */
	if (n == 1) {
		printf(("%s"), (const char *)a1);
		return 0;
	}

	return -1; /* Success: 0, Failure: -1 */
}
