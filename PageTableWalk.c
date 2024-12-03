#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/mm_types.h>
#include <asm/pgtable.h>

SYSCALL_DEFINE2(my_get_physical_addresses,
                void *, user_vaddr, 
                unsigned long *, user_paddr) {
    
    unsigned long vaddr;
    unsigned long paddr = 0;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long page_addr = 0;
    unsigned long page_offset = 0;

    // Copy the virtual address from user space to kernel space
    if (copy_from_user(&vaddr, user_vaddr, sizeof(unsigned long))) {
        printk("Error: Failed to copy virtual address from user space\n");
        return -EFAULT;
    }

    // Get the PGD (Page Global Directory) for the current process
    pgd = pgd_offset(current->mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        printk("PGD entry not valid or not present\n");
        return -EFAULT;    // #define	EFAULT		14	 /*Bad address*/
    }

    // Get the P4D (Page 4 Directory)
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        printk("P4D entry not valid or not present\n");
        return -EFAULT;
    }
    // Get the PUD (Page Upper Directory)
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        printk("PUD entry not valid or not present\n");
        return -EFAULT;
    }

    // Get the PMD (Page Middle Directory)
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        printk("PMD entry not valid or not present\n");
        return -EFAULT;
    }

    // Get the PTE (Page Table Entry)
    pte = pte_offset_kernel(pmd, vaddr);
    if (!pte_present(*pte)) {
        printk("Page not present in memory\n");
        return -EFAULT;
    }

    // Compute physical address from PTE
    page_addr = pte_val(*pte) & PTE_PFN_MASK;
    page_offset = vaddr & ~PAGE_MASK;
    paddr = page_addr | page_offset;

    // Copy the result back to user space
    if (copy_to_user(user_paddr, &paddr, sizeof(unsigned long))) {
        printk("Error: Failed to copy physical address to user space\n");
        return -EFAULT;
    }

    return 0;
}