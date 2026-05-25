# Final Submission Work

## Note
    This program is modified on a VM using VirtualBox on a M1 Mac.

## Q1
    Print a welcome message as project instruction requires.

## Q2
    When a user program wants to print a message, it stores the string in memory and puts its memory address into register a1. Then, when the system call number n is set to 1, the kernel reads the address stored in a1 and prints the target string, else return error.

## Q3
    Calculate the number of entries needed for 4GB memory, then allocate memory for the 4-level page tables (PML4, PDPT, PD, PT).

    1 page table have 512 entries
    1 page directory have 512 entries
    1 page dir pointer have 512 entries
    1 pml4 have 512 entries
    
    We reset this memory to 0 to clean out old data. Then, We connect the tables layer by layer. For each connection, we add `| 3` to the address. This gives the kernel the necessary permissions to access the memory. We map the lowest page table to the actual physical  addresses, and then save the top table's address to the page_table variable.

## Q4
    We want to give the user program its own memory space at a specific address; we picked 0xFFFFFFFFFFE00000 here. Then, allocate 3 new memory pages to act as the user's PDPT, PD, and PT. Add | 7 to these user entries, which give the program the required access permission. In the user's lowest table, put the program code at index 0, and the stack at index 1, but must subtract 4096 because the stack grows downward and we need the starting address. Last, we update the uprogram and ustack to their new virtual addresses.

## Extra Credit
    Simply Copy and Paste from P1.