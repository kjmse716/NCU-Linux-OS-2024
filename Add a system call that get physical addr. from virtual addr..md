# <font color="#F7A004">Intro</font>

**<font size = 4>2024 Fall NCU Linux OS Project 1</font>**  


* Add a system call that get physical addresses from virtual addresses
* 介紹 `copy_from_user` 及 `copy_to_user` 使用方法  
* 使用Copy on Write 機制來驗證system call 正確呼叫  
* 介紹 Demand Paging 在 memory 中的使用時機  

Demo問題可參考[這篇](https://hackmd.io/@gary7102/ByQDR51M1e)，[github](https://github.com/gary7102/Linux-add-a-system-call.git)，好讀版[hackmd](https://hackmd.io/@gary7102/BkMu4HKk1l)

**<font size = 4>Environment</font>**
```
OS: Ubuntu 22.04
ARCH: X86_64
Kernel Version: 5.15.137
```



# <font color="#F7A004">`copy_from_user` 及 `copy_to_user`</font>

## copy_from_user
根據[bootlin](https://elixir.free-electrons.com/linux/v5.15.137/source/include/linux/uaccess.h#L189)

```c
unsigned long copy_from_user(void *to, const void __user *from, unsigned long n);
```

這個函數的功能是將user space的資料複製到kernel space。其中:  
`to`: 目標位址，是kernel space中的一個指標，用來存放從user space 複製過來的資料。  
`from`:來源位址，是user space中的一個指標，指向需要被複製的資料(ex: point to virtual address)。  
`n`: 要傳送資料的長度  
傳回值: 0 on success, or the number of bytes that could not be copied.  

## copy_to_user

```c
unsigned long copy_to_user(void __user *to, const void *from, unsigned long n);
```

這個函數的功能是將kernel space的資料複製到user space variable。其中:  
`to`: 目標地址(user space)  
`from`: 複製地址(kernel space)  
`n`: 要傳送資料的長度  
傳回值: 0 on success, or the number of bytes that could not be copied.  

## Purpose
* Prevents crashes due to invalid memory access.
* Maintains security by ensuring memory access respects user-space permissions.
* Enables error handling by providing feedback on failed memory operations.

:::success
這兩個 function 都是在 kernel space 中使用
:::

## Example

**<font size = 4>新增一個system call 作為範例</font>**
```c=1
#include <linux/kernel.h>       
#include <linux/syscalls.h>     
#include <linux/uaccess.h>      // For copy_from_user and copy_to_user

SYSCALL_DEFINE2(get_square, int __user *, input, int __user *, output) {
    int kernel_input;
    int result;

    // Copy the input value from user space to kernel space
    if (copy_from_user(&kernel_input, input, sizeof(int))) {
        return -EFAULT; // Return error if copy fails
    }

    // Calculate the square
    result = kernel_input * kernel_input;

    // Copy the result back to user space
    if (copy_to_user(output, &result, sizeof(int))) {
        return -EFAULT; // Return error if copy fails
    }

    return 0; // Return success
}
```

**<font size = 4>User code</font>**
```c=1
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

int main() {
    int input;
    int output;

    printf("Enter an integer: ");
    if (scanf("%d", &input) != 1) {
        fprintf(stderr, "Invalid input\n");
        return 1;
    }

    // Call the system call with pointers to input and output
    long result = syscall(451, &input, &output);

    if (result == -1) {
        perror("syscall failed");
    }else{
        printf("Input: %d, Output (Square): %d\n", input, output);
    }

    return 0;
}
```
line 17傳入`&input`及`&output`，  
分別對應system call 的`int __user *, input`及`int __user *, output`，若正確複製則回傳值為0  
而user space的`output`已經在`copy_to_user()`時寫入新資料。  



**<font size = 4>執行結果 :</font>**  
![image](https://hackmd.io/_uploads/HyF41Ysl1g.png)  

system call 正確呼叫且輸出計算結果



# <font color="#F7A004">實作system call</font>

## Page Table in Linux

Page table 一般來說可以分為兩種結構，32 bit cpu使用4-level(10-10-12)或是 64 bit cpu使用5-level(9-9-9-9-12，加起來只有 48 因為最高的 16 位是sign extension)的架構，但也有3-level的結構，這可以透過 config 內的 `CONFIG_PGTABLE_LEVELS` 設定，基本上是基於處理器架構在設定的

- **Structure of page tables**
    - PGD (Page Global Directory)
    - P4D (Page 4 Directory，<font color="red">5-level 才有</font>)
    - PUD (Page Upper Directory)
    - PMD (Page Middle Directory)
    - PTE （page table entry）
    
使用4-level page table 為例:  

![linux_paging](https://hackmd.io/_uploads/rkIiRAVxJx.jpg)


可以看到Page table的base address 是存放在 CR3（又稱 PDBR，page directory base register）這個register，存放的是**physical address**。但我們需要的是他的virtual address，因此，使用 `task_struct->mm->pgd` 內儲存的則是 Process Global Directory(PGD) 的virtual address，

**補充：**
甚麼是`task_struct`及`mm_struct`可以參考下方 [what is mm_struct?](#mm_struct)

每個process有各自的page table，每當context switch發生時，CR3會載入新的page table base addr.，且CR3寫入時，TLB會被自動刷新，避免用到上一個process之TLB。

因此要從logical address轉換為physical address，需要一層一層下去查表，
順序為: `pgd_t` -> `p4d_t` -> `pud_t` -> `pmd_t` -> `pte_t`

其中舉例，若要查`p4d`的base address則需要`pgd_t + p4d_index`  
``` c=1 
pgd_t *pgd;
p4d_t *p4d;

pgd = pgd_offset(current->mm, vaddr);
p4d = p4d_offset(pgd, vaddr);
```
同理，若要查`pte`的base address則需要`pmd_t + ptd_index`  
```c=1
ptd_t *pte;

pte = pte_offset(pmd, vaddr);
```

我們可以直接到 [bootlin](https://elixir.bootlin.com/linux/v5.15.137/source/include/linux/pgtable.h#L88) 中看到這些offset function 的實作細節  

```c
// include/linux/pgtable.h line 88

#ifndef pte_offset_kernel
static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long address)
{
        return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(address);
}
#define pte_offset_kernel pte_offset_kernel
#endif

//...

// line 106
/* Find an entry in the second-level page table.. */
#ifndef pmd_offset
static inline pmd_t *pmd_offset(pud_t *pud, unsigned long address)
{
        return pud_pgtable(*pud) + pmd_index(address);
}
#define pmd_offset pmd_offset
#endif

#ifndef pud_offset
static inline pud_t *pud_offset(p4d_t *p4d, unsigned long address)
{
        return p4d_pgtable(*p4d) + pud_index(address);
}
#define pud_offset pud_offset
#endif

static inline pgd_t *pgd_offset_pgd(pgd_t *pgd, unsigned long address)
{
        return (pgd + pgd_index(address));
};

/*
 * a shortcut to get a pgd_t in a given mm
 */
#ifndef pgd_offset
#define pgd_offset(mm, address)		pgd_offset_pgd((mm)->pgd, (address))
#endif
```
對應到前一張圖，找到前一層的Directory offset再加上當前Directory的 index，一層一層去找  

不過發現`p4d_offset`的實作細節沒有出現在這，但是`pud_offset`傳入的參數卻是`p4d_t *p4d`，後來在`arch/x86/include/asm/pgtable.h line 926`中找到
```c=926
// arch/x86/include/asm/pgtable.h line 926

/* to find an entry in a page-table-directory. */
static inline p4d_t *p4d_offset(pgd_t *pgd, unsigned long address)
{
        if (!pgtable_l5_enabled())
                return (p4d_t *)pgd;
        return (p4d_t *)pgd_page_vaddr(*pgd) + p4d_index(address);
}
```



根據上述對linux中page table介紹，便可以寫出page table walk 的程式碼

## Page Table walk 實作

新增一個檔案叫 `project1.c`，路徑為 `kernel/project1.c`
:::spoiler <font color = green>範例</font>

```c=1
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
```
:::



## 地址轉換trace code:

### 第一層轉換PGD:
>目標 : 回傳PGD entry的virtual address

**<font size = 4>程式碼:</font>**
```c
pgd = pgd_offset(current->mm, vaddr);
```

**<font size = 4>trace code:</font>**

![image](https://hackmd.io/_uploads/Bku2LGAb1e.png)

![image](https://hackmd.io/_uploads/S11p8fAWJg.png)


由`current->mm->pgd`找出PGD的base address再加上`pgd_index` 計算出pgd entry的虛擬位置，回傳指標。


:::success
### <font color= "#008000">How to get `pgd_index`?</font>
根據 [bootlin](https://elixir.bootlin.com/linux/v5.15.137/source/include/linux/pgtable.h#L85) 
```c
#ifndef pgd_index
/* Must be a compile-time constant, so implement it as a macro */
#define pgd_index(a)        (((a) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#endif
```
其中
* `#define pgd_index(a)`：定義 `pgd_index` Macro，接受一個參數 `a`，代表一個virtual address
* `(((a) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))`：這是用來計算 `a` 在 PGD 中的index的表達式。

**<font size = 4>舉例：</font>**  
在x86_64架構的 `PGDIR_SHIFT` 為 39 (48 - 9)，
且`PTRS_PER_PGD` 為 512，那麼 `pgd_index(a)` 的操作流程如下：

* 將虛擬地址 `a` 右移 39 位，提取出對應 PGD 的高位部分
* 將結果與 `511`（`PTRS_PER_PGD - 1`）做 bitwise `&`，確保index在有效範圍內

得到的結果即為 virtual address `a` 的 `pgd_index`，
並且可以依此類推到 `p4d_index`、`pud_index`、`pmd_index`及`pte_index`的計算方法
:::

### 第二層轉換P4D(p4d僅5 level轉換時啟用，此處會值接回傳傳入的pgd *)
**<font size = 4>程式碼:</font>**
```c
p4d = p4d_offset(pgd, vaddr);
```


**<font size = 4>trace code:</font>**
`//arch/x86/include/asm/pgtable.h line 926)`


![image](https://hackmd.io/_uploads/HyqCLfAWke.png)


其中`pgtable_l5_enabled()` check whether 5-level page table is enabled。因此如果系統使用的是4-level，則無需存取 `p4d_t`，且直接回傳以`(p4d_t*) pgd`，  
也就是說在4-level下 `pgd = p4d`  
相同道理，3-level下 `pgd = p4d = pud`


###  第三層轉換PUD 
>目標 : 使用*pgd與pud index找到之PUD entry的virtual address

**<font size = 4>程式碼:</font>**
```c
pud = pud_offset(p4d, vaddr);
```

**<font size = 4>trace code:</font>**
```c
//arch/x86//include/linux/pgtable.h Line:115
#ifndef pud_offset
static inline pud_t *pud_offset(p4d_t *p4d, unsigned long address)
{
        return p4d_pgtable(*p4d) + pud_index(address);
}
```

![image](https://hackmd.io/_uploads/SJlWDG0bkx.png)


![image](https://hackmd.io/_uploads/BynZPfR-ye.png)

這裡先用macro判斷CONFIG_PGTABLE_LEVELS是否大於4(p4d table是否有真正使用)
在我們情況下使用4 level轉換，故實際function為下方349行而非337行。

 `/ arch / x86 / include / asm / pgtable_types.h`
![image](https://hackmd.io/_uploads/Hkym9MRZJx.png)


![image](https://hackmd.io/_uploads/ry2Q9fAZye.png)


**此處的查詢使用的pgd entry為第一層轉換出來(p4d=pgd)，透過virtual address來指向一個pgd entry的pointer**





### __va() trace code:

`/ arch / x86 / include / asm / page.h`

![image](https://hackmd.io/_uploads/ryNswz0Zyg.png)


透過將physical address加上`PAGE_OFFSET`，也就是加上kernel space virtual address的啟始位置藉此得到透過偏移量轉換的virtual address.

![image](https://hackmd.io/_uploads/BJDTAzA-yx.png)

###  第四層轉換PMD 
>目標 : 使用*pud與pmd index找到之PMD entry的virtual address

**<font size = 4>程式碼:</font>**
```c
pmd = pmd_offset(pud, vaddr);
```



**<font size = 4>trace code:</font>**

```c
//arch/x86//include/linux/pgtable.h line 106
/* Find an entry in the second-level page table.. */
#ifndef pmd_offset
static inline pmd_t *pmd_offset(pud_t *pud, unsigned long address)
{
        return pud_pgtable(*pud) + pmd_index(address);
}
#define pmd_offset pmd_offset
```
![image](https://hackmd.io/_uploads/HyCovzRWke.png)


這裡傳入的pud是透過virtual address指向一個pud entry
![image](https://hackmd.io/_uploads/ryxTvMAZyx.png)

可以看到這裡一樣會檢查判斷CONFIG_PGTABLE_LEVELS是否大於3(pud table是否有啟用)
![image](https://hackmd.io/_uploads/Bk3Tvf0bJg.png)


這裡因為我們`CONFIG_PGTABLE_LEVELS = 4`，故執行的是363行而不是375行的`native_pud_val()`


###  第五層轉換PTE 
>目標 : 使用*pmd與pte index找到之PTE entry的virtual address

**<font size = 4>程式碼:</font>**
```c
pte = pte_offset_kernel(pmd, vaddr);
```

**<font size = 4>trace code:</font>**

```c
// include/linux/pgtable.h line 88

#ifndef pte_offset_kernel
static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long address)
{
        return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(address);
}
#define pte_offset_kernel pte_offset_kernel
#endif
```

`/ arch / x86 / include / asm / pgtable.h`
![image](https://hackmd.io/_uploads/BJ2CvfRb1x.png)






### 由PTE table找到實體記憶體位置
>目標 : 由*pte與pte index找到pte entry中存放的physical address

**<font size = 4>程式碼:</font>**
```c=64
page_addr = pte_val(*pte) & PTE_PFN_MASK;
page_offset = vaddr & ~PAGE_MASK;
paddr = page_addr | page_offset;
```

**<font size = 4>trace code:</font>**

![image](https://hackmd.io/_uploads/Syay_GAZ1e.png)

![image](https://hackmd.io/_uploads/SkQlOGAWJx.png)


這裡透過的`pte_val()`得到pte table entry中的內容。








## 計算physical address

:::success
**<font color = "green">以實際例子介紹line 64~66</font>**

**<font size = 4>新增test.c</font>**

```c=1
#include <stdio.h>
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>

void * my_get_physical_addresses(void *vaddr_of_a){
        unsigned long paddr;

        long result = syscall(450, &vaddr_of_a, &paddr);

        return (void *)paddr;
};

int main()
{
    int a = 10;
    printf("Virtual addr. of arg a = %p\n", &a);
    printf("Physical addr. of arg a = %p\n", my_get_physical_addresses(&a));
}
```

**<font size = 4>結果:</font>**  
![image](https://hackmd.io/_uploads/HyFZkBsZkx.png)


**<font size = 4>使用dmesg來查看kernel內的訊息</font>**  

![image](https://hackmd.io/_uploads/ryaNJriWkl.png)

![image](https://hackmd.io/_uploads/r1iZv-IWJe.png)


可以看到virtual address = `0x7fffd5bd1544`，  
`pte_val(*pte)` = PTE base address = `0x8000000093567867`  
另外，`PTE_PFN_MASK` = `0x0000FFFFFFFFF000`因為page size 為 4KB，且**保留了bit 12 到 51 的部分（總共 40 bit），可參考上圖**，或是最下方[physical memory範圍](##physical_memory範圍)
```c=64
page_addr = pte_val(*pte) & PTE_PFN_MASK;
```
得`page_addr` = `0x8000000093567867 ` & `0x0000FFFFFFFFF000` = `0x93567000`  
`page_addr` 為 **base address of the physical page frame**

```c=65
page_offset = vaddr & ~PAGE_MASK;
````
`page_offset` = `0x7fffd5bd1544` & `0x0000000000000FFF` = `0x544`  
得到 **physical page frame的offset**

```c=66
paddr = page_addr | page_offset;
```
最後 physical address = `0x93567000` | `0x544` = `0x93567544`
    
**<font size = 4>簡單來說，其實就只是需要先算出page frame address再和offset 相加而已，只不過是使用 bitwise`&` 及 `|` 來計算出結果</font>**
:::


:::warning
因為使用 `copy_from_user()`因此必須傳入pointer of of virtual address of `a`，  
所以即使 `my_get_physical_addresses(void *vaddr_of_a)`中的`*vaddr_of_a`已經是pointer，  
但是在呼叫system calls時，`long result = syscall(450, &vaddr_of_a, &paddr);`  
需要傳送的參數是`&vaddr_of_a`(i.e. pointer of of virtual address of `a`)
:::


## Add system call

**<font size = 5>1. Modified Makefile</font>**

修改 `kernel/Makefile`，增加 `project1.o`
```
obj-y     = fork.o exec_domain.o panic.o \
            cpu.o exit.o softirq.o resource.o \
            sysctl.o capability.o ptrace.o user.o \
            signal.o sys.o umh.o workqueue.o pid.o task_work.o \
            extable.o params.o \
            kthread.o sys_ni.o nsproxy.o \
            notifier.o ksysfs.o cred.o reboot.o \
            async.o range.o smpboot.o ucount.o regset.o \
            project1.o \
```
使得在編譯時也會編譯到`project1`這個檔案

**<font size = 5>2. Modified syscall Table</font>**

要新增自己的 system call，打開`arch/x86/entry/syscalls/syscall_64.tbl`
在第 374 行後面新增自己的 system call：
```
450     common  my_get_physical_addresses       sys_my_get_physical_addresses
```
這行有四個部分，每項之間由空白或 tab 隔開，它們代表的意義是：

* `450`
system call number，在使用系統呼叫時要使用這個數字
* `common`
支援的 ABI， 只能是 64、x32 或 common，分別表示「只支援 amd64」、「只支援 x32」或「都支援」
* `my_get_physical_addresses`
system call 的名字
* `sys_my_get_physical_addresses`
system call 對應的實作，kernel 中通常會用 sys 開頭來代表 system call 的實作

`syscall_64.tbl` 這個檔案會在編譯階段被讀取後轉為 header file 檔案位於: `arch/x86/include/generated/asm/syscalls_64.h`：  
![image](https://hackmd.io/_uploads/rJE4StogJl.png)


**<font size = 5>3. Modified `syscalls.h`</font>**

將 syscall 的原型添加進檔案 (`#endif` 之前)
路徑為: `include/linux/syscalls.h`  

![image](https://hackmd.io/_uploads/HyH4IFoeJg.png)

這定義了我們system call的prototype，`asmlinkage`代表我們的參數都可以在stack裡取用，
當 assembly code 呼叫 C function，並且是以 stack 方式傳參數時，在 C function 的 prototype 前面就要加上 `asmlinkage`

# <font color="#F7A004">Compile Kernel</font>

請參考 [add a system call](https://hackmd.io/aist49C9R46-vaBIlP3LDA?view)

# <font color="#F7A004">Copy on Write</font>

* **<font size = 4>Copy on write:</font>** allows multiple processes to share the same physical memory until one intends to modify it.

![螢幕擷取畫面 2024-11-08 154515](https://hackmd.io/_uploads/Hy8Qzrsb1l.png)


可以看到程式執行時，parent process、child process中 `global_a` 的physical memory都是共用的，直到`global_a`被改動之後，os會分配新的physical memory 給改動的process，也因此驗證了system call 確實有正確呼叫



# <font color="#F7A004">Loader</font>

進入這章節前，先快速介紹Linux 中的Demand paging機制，可以對應到老師之前介紹的lazy allocation，不過lazy allocation相對廣義一些，demand paging 單純在memory 中使用

* __Demand Paging__:  pages of a process's memory are loaded into physical memory __only when they are actually needed__(ex: when the process tries to access them)

簡單來說，並不是一開始所有的virtual address都有對應到physical address，而是等到需要使用(access)時才載入到physical memory

因此，以<font color = "red">**process是否access the item**</font>作為區分，可以分為下列幾種情況:

## <font color = "green">case 1:</font> Array store in bss segment 
```c
// global variable
int a[2000000];   // store in bss segment,
                  // same as  int a[2000000] = {0}; 
```
**執行結果:**  
![image](https://hackmd.io/_uploads/Hy2hMHjZJg.png)

可以看到，存放在 bss segment 的 array，
Load到memory中的只有到 `a[1007]`，之後就沒有load 進memory，因此沒有分配physical memory



## <font color = "green">case 2:</font> Array store in data segment
```c
// global variable
int a[2000000] = {1};  // initialized variable, store in Data segment
```
**執行結果:**  
![image](https://hackmd.io/_uploads/H1g4XBsWJe.png)


可以看到，因為第一個element有被預設初始值，因此array `a`會預先載入幾個page至memory中，but only few page store in memory, 剩下尚未存取的需要透過page fault來載入至memory，因此印至 `a[15351]`便停止

**<font size = 5>補充:</font>**   
因為load至`a[15351]`，所以我想試看看預先存取`a[15352]` 產生page fault並將其load入physical memory，看看有甚麼結果
```c
a[15352] = 1;     // occur page fault, load to phy_mem
```
**執行結果:**  
![image](https://hackmd.io/_uploads/SJhooBsWJl.png)

可以看到 load 到`a[16375]`結束，而`a[16376]`尚未存取，
因此可得：
```
16375 - 15351 = 1024    
```

因為page size = 4KB，且一個int 4 bytes，而我們使用64位元架構，
因此page table entries size = 8 bytes(存兩個int element = 8 bytes)，因此：$$\dfrac{4KB}{8B} = \dfrac{2^{12}}{2^3} = 2^9 = 512$$
證明也是64位元架構page table entries 為512個

由此證明老師上課講解的內容


## <font color = "green">case 3:</font> loop through array

```c
// in local 
for(int i=0; i<2000000; i++)
{
    a[i] = 0;    //pre-accessing the array
}
```
**執行結果:**  
![image](https://hackmd.io/_uploads/B1CRwyBg1g.png)

In this particular case，不管是定義在Data segment or BSS segment，透過迴圈存取每個element，會造成page fault 並強迫load into memory，因此陣列中每個element 都有分配到各自的physical address


# <font color="#F7A004">Note</font>

## <font color = "#008000">BSS segment vs Data segment</font>
BSS segment 存放的資料為 **uninitialized global variable (initialized with 0)** 或是 **uninitialized static variable**，而存放在bss segment和data segment的差別可以從[case 1](##case1)及[case 2](##case2)看到，data segement中的資料會在程式載入時會**立即分配頁面**，因此分配到的記憶體更多
```
// global variables

int a[100];               // bss segment
int a[100] = {0};         // bss segment
static int global_var2;   // bss segment
int a[100] = {1};         // Data segment
```


## <font color=" #008000">mm_struct</font>

**<font size = 5>What is `mm_struct`?</font>**

task_struct 被稱為 process descriptor，因為其記錄了這個 process所有的context(ex: PID, scheduling info)，其中有一個被稱為 memory descriptor的結構 `mm_struct`，記錄了Linux視角下管理process address的資訊(ex: page tables)。  
![30528e172c325228bf23dec7772f0c73](https://hackmd.io/_uploads/SkgMiSY1Jg.png)  
圖源: [Linux源码解析-内存描述符（mm_struct）](https://blog.csdn.net/tiankong_/article/details/75676131)

因此 `struct mm_struct *mm = current->mm;` 指的是存取目前process的memory management 資訊 

By assigning `current->mm` to this pointer, now can access to the memory-related information (ex: page tables) for the process that is running the system call.


**<font size = 5>What is `task_struct`?</font>**  

根據 [bootlin](https://elixir.bootlin.com/linux/v5.15.137/source/include/linux/sched.h#L721) 

在 Linux 中，Process Descriptor的data structure是 `task_struct`，每個正在運行或等待的process都對應一個 `task_struct`  

其中比較常見的有:  
```c
struct task_struct {
    pid_t pid;                  // process ID
    pid_t tgid;                 // thread ID
    long state;                 // process state
    struct mm_struct *mm;       // memory descriptor
    struct files_struct *files; // 文件描述符
    struct fs_struct *fs;       // 文件系統信息
    int prio;                   // 優先級
    struct cred *cred;          // 權限信息
    struct signal_struct *signal; // 信號處理
    // ... 
};
```


## <font color=" #008000">SYSCALL_DEFINE</font>

**<font size = 4>What is `SYSCALL_DEFINE2`?</font>**
根據 [bootlin](https://elixir.bootlin.com/linux/v5.15.137/source/include/linux/syscalls.h#L217)定義:

```c
#define SYSCALL_DEFINE1(name, ...) SYSCALL_DEFINEx(1, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE2(name, ...) SYSCALL_DEFINEx(2, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE3(name, ...) SYSCALL_DEFINEx(3, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE4(name, ...) SYSCALL_DEFINEx(4, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE5(name, ...) SYSCALL_DEFINEx(5, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE6(name, ...) SYSCALL_DEFINEx(6, _##name, __VA_ARGS__)

#define SYSCALL_DEFINE_MAXARGS	6
```

其中`SYSCALL_DEFINE1(name, ...)` 中的
* `1`表示system call 參數的個數，依此類推2、3、4、5、6 表示參數個數
* `name` 表示系統呼叫system call的名字

而後面的 `SYSCALL_DEFINEx(1, _##name, __VA_ARGS__)` 中的
* `_##name` 是一個預處理器拼接操作，會將 `_` 和 `name` 組合成一個標識符，  
例如，如果kernel中使用了 
```
SYSCALL_DEFINE1(my_get_physical_addresses, void *ptr)
```
則這個 Macro 會展開為：
```
asmlinkage long sys_my_get_physical_addresses(void *ptr);
```
* `__VA_ARGS__` 代表傳入的參數


## <font color= "#008000">How does the kernel set register `cr3`</font>
refrence: [stackoverflow](https://stackoverflow.com/questions/45239165/how-does-the-kernel-set-register-cr3)


Stackoverflow Reply:

    the page tables are found in kernel address space and the kernel keeps a close track of the virtual->physical mapping there.


    Linux differentiates between two types of virtual addresses in the kernel:

    Kernel virtual addresses - which can map (conceptually) to any physical address; and

    Kernel logical addresses - which are virtual addresses that have a linear mapping to physical addresses



    The kernel places the page tables in logical addresses, so you only need to focus on those for this discussion.

    Mapping a logical address to its corresponding physical one requires only the subtraction of a constant (see e.g. the __pa macro in the Linux source code).

    For example, on x86, physical address 0 corresponds to logical address 0xC0000000, and physical address 0x8000 corresponds to logical address 0xC0008000.

    So once the kernel places the page tables in a particular logical address, it can easily calculate which physical address it corresponds to.




kernel有兩種虛擬記憶體機制:Kernel virtual addresses與Kernel logical addresses，page table存放區域使用的是Kernel logical addresses機制(簡單的偏移量關係可以實現更快速的虛擬位置實體位置轉換)。


透過bootlin trace code:
轉換kernel logical address至physical address是透過__pa()


`/ arch / x86 / include / asm / page.h`

![image](https://hackmd.io/_uploads/B1vQtMR-Jg.png)


繼續進行trace code

![image](https://hackmd.io/_uploads/S1QEKfCWJe.png)


page_32.h中

![image](https://hackmd.io/_uploads/SyBBKG0bJx.png)


在32位元中__pa()透過將physical address減去PAGE_OFFSET 


page_64.h中

![image](https://hackmd.io/_uploads/rJkIYfA-ke.png)



![image](https://hackmd.io/_uploads/Hy48YGAWkg.png)





當 x < y 時，這表示 x 是一個低於內核映射起始地址的虛擬地址。這種情況下，y 會是一個負值（在無符號長整型中，這會導致進位）。為了計算出正確的物理地址，我們需要將 (__START_KERNEL_map - PAGE_OFFSET) 加到 y 上。

__START_KERNEL_map 是內核映射的起始地址，而 PAGE_OFFSET 是內核虛擬地址空間的偏移量。通過將 (__START_KERNEL_map - PAGE_OFFSET) 加到 y 上，我們可以得到一個正確的物理地址，這樣可以確保計算出的物理地址是正確的。

撰寫x = y + (__START_KERNEL_map - PAGE_OFFSET);是為了讓系統可以兼容使用PAGE_OFFSET機制而不是__START_KERNEL_map機制來轉換virtual address的程式



## <font color= "#008000">`CR2`暫存器作用?</font>

CR2 暫存器的作用：

在x86架構下，當發生頁錯（page fault）時，處理器會自動將導致頁錯的虛擬地址寫入 cr2 暫存器。這個虛擬地址就是系統試圖存取但未映射到物理內存的地址。
Page Fault 處理流程：

當頁表條目（page table entry，PTE）中的 present 位（flag）為 0 時，表示該頁未映射到物理內存，因此會觸發頁錯中斷（page fault）。
頁錯處理程式（page fault handler）會讀取 cr2 中的虛擬地址，從而知道是哪個地址引發了頁錯。
內核在頁錯處理過程中可能會在物理內存中找到一個可用的頁框，然後從磁碟（或其他二級存儲）將需要的頁面內容載入到這個頁框中。
最後，內核使用 cr2 中的虛擬地址來更新相應的頁表條目，使該虛擬地址映射到剛載入的物理頁框，並將 present 標誌設為 1，以便未來的訪問不會再觸發頁錯。

# <font color="#F7A004">Problems</font>

## <font color="#008000">physical_memory範圍</font>
假設我們給予8GB 記憶體空間，那麼8GB = 8,589,934,592 Bytes = 0x2 0000 0000   
因此最高能分配到的記憶體位置為 0x1 FFFF FFFF  
![image](https://hackmd.io/_uploads/rJlIAySe1l.png)

以上圖為例，physical address 明顯超出記憶體範圍，原因如下圖所述，並不是所有的bit都為實體記憶體位址，前面0x8000...都是NX bit或是其他功能，所以必須在計算physical address時使用`PTE_PFN_MASK`過濾掉第52 bits以上及後面12 bits，得到的才是實際physical frame number，加上offset 才會是physical address  

![image](https://hackmd.io/_uploads/r1iZv-IWJe.png)





# <font color="#F7A004">Referenced</font>

* [linux系统中copy_to_user()函数和copy_from_user()函数的用法](https://blog.csdn.net/bhniunan/article/details/104088763)
* [where is base register of page table?](https://www.csie.ntu.edu.tw/~wcchen/asm98/asm/proj/b85506061/chap2/paging.html)
* [定址方式](https://www.csie.ntu.edu.tw/~wcchen/asm98/asm/proj/b85506061/chap2/overview.html)
* [實作一個回傳物理位址的系統呼叫](https://hackmd.io/@Mes/make_phy_addr_syscall#%E4%BF%AE%E6%94%B9-syscall_64tbl)
* [add a system call to kernel (v5.15.137)](https://hackmd.io/aist49C9R46-vaBIlP3LDA?view#add-a-system-call-to-kernel-v515137)
* [Kernel 的替換 & syscall 的添加](https://satin-eyebrow-f76.notion.site/Kernel-syscall-3ec38210bb1f4d289850c549def29f9f)
* [關於Linux尋址及page table的一些細節](https://www.cnblogs.com/QiQi-Robotics/p/15630380.html)
* [SYSCALL_DEFINEx宏源码解析](https://blog.csdn.net/qq_41345173/article/details/104071618)
* [Linux Kernel](https://hackmd.io/@eugenechou/H1LGA9AiB#Project-1)