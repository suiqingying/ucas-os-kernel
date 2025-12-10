#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_FILE "./image"
#define ARGS "[--extended] [--vm] <bootblock> <executable-file> ..."

#define SECTOR_SIZE 512
#define BOOT_LOADER_SIG_OFFSET 0x1fe
#define OS_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 2)
#define APP_INFO_ADDR_LOC (BOOT_LOADER_SIG_OFFSET - 10)
// Reserve 8 bytes for swap info (start sector, max pages) below app info
#define SWAP_INFO_LOC (BOOT_LOADER_SIG_OFFSET - 18)
#define BATCH_FILE_SECTOR 50
#define BOOT_LOADER_SIG_1 0x55
#define BOOT_LOADER_SIG_2 0xaa

// Keep these values synchronized with include/os/mm.h
#define MAX_SWAP_PAGES 131072 / 64 // Maximum pages that can be swapped (512MB)
#define SWAP_SECTORS_PER_PAGE 8

#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* design your own task_info_t */
typedef struct {
    char task_name[16];
    uint32_t start_addr;
    uint32_t block_nums;
    uint64_t p_filesz;
    uint64_t p_memsz;
} task_info_t;

#define TASK_MAXNUM 16
static task_info_t taskinfo[TASK_MAXNUM];

/* structure to store command line options */
static struct {
    int vm;
    int extended;
} options;

/* prototypes of local functions */
static void create_image(int nfiles, char *files[]);
static void error(char *fmt, ...);
static void read_ehdr(Elf64_Ehdr *ehdr, FILE *fp);
static void read_phdr(Elf64_Phdr *phdr, FILE *fp, int ph, Elf64_Ehdr ehdr);
static uint64_t get_entrypoint(Elf64_Ehdr ehdr);
static uint32_t get_filesz(Elf64_Phdr phdr);
static uint32_t get_memsz(Elf64_Phdr phdr);
static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr);
static void write_padding(FILE *img, int *phyaddr, int new_phyaddr);
static void write_img_info(int nbytes_kernel, task_info_t *taskinfo,
                           short tasknum, FILE *img, int *taskinfo_addr);
static void reserve_swap_space(FILE *img, int *phyaddr, int *swap_start_sector);
static void write_swap_info(FILE *img, int swap_start_sector);

int main(int argc, char **argv)
{
    char *progname = argv[0];

    /* process command line options */
    options.vm = 0;
    options.extended = 0;
    while ((argc > 1) && (argv[1][0] == '-') && (argv[1][1] == '-')) {
        char *option = &argv[1][2];

        if (strcmp(option, "vm") == 0) {
            options.vm = 1;
        } else if (strcmp(option, "extended") == 0) {
            options.extended = 1;
        } else {
            error("%s: invalid option\nusage: %s %s\n", progname,
                  progname, ARGS);
        }
        argc--;
        argv++;
    }
    if (options.vm == 1) {
        error("%s: option --vm not implemented\n", progname);
    }
    if (argc < 3) {
        /* at least 3 args (createimage bootblock main) */
        error("usage: %s %s\n", progname, ARGS);
    }
    create_image(argc - 1, argv + 1);
    return 0;
}

/* assign your task_info_t somewhere in 'create_image' */
static void create_image(int nfiles, char *files[])
{
    int tasknum = nfiles - 2;
    int nbytes_kernel = 0;
    int phyaddr = 0;
    FILE *fp = NULL, *img = NULL;
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;

    /* open the image file */
    img = fopen(IMAGE_FILE, "w");
    assert(img != NULL);

    /* for each input file */
    for (int fidx = 0; fidx < nfiles; ++fidx) {

        int taskidx = fidx - 2;
        int start_addr = phyaddr;
        uint64_t p_filesz = 0;
        uint64_t p_memsz = 0;

        /* open input file */
        fp = fopen(*files, "r");
        assert(fp != NULL);

        /* read ELF header */
        read_ehdr(&ehdr, fp);
        printf("0x%04lx: %s\n", ehdr.e_entry, *files);

        /* for each program header */
        for (int ph = 0; ph < ehdr.e_phnum; ph++) {

            /* read program header */
            read_phdr(&phdr, fp, ph, ehdr);

            if (phdr.p_type != PT_LOAD) continue;

            /* write segment to the image */
            write_segment(phdr, fp, img, &phyaddr);

            /* update nbytes_kernel */
            if (strcmp(*files, "main") == 0) {
                nbytes_kernel += get_filesz(phdr);
            }

            /* accumulate file size and mem size */
            if(phdr.p_type == PT_LOAD){
                p_filesz += phdr.p_filesz;
                p_memsz += phdr.p_memsz;
            }
        }

        /* write padding bytes */
        /* only padding bootblock is allowed!
         */
        if (strcmp(*files, "bootblock") == 0) {
            write_padding(img, &phyaddr, SECTOR_SIZE);
        } else {
            write_padding(img, &phyaddr, phyaddr + (phyaddr & 1));
            
            if (taskidx >= 0) {
                strcpy(taskinfo[taskidx].task_name, *files);
                taskinfo[taskidx].start_addr = start_addr;
                taskinfo[taskidx].block_nums = NBYTES2SEC(phyaddr) - start_addr / SECTOR_SIZE;
                taskinfo[taskidx].p_filesz = p_filesz;
                taskinfo[taskidx].p_memsz = p_memsz;
                
                printf("Info: %s: start=0x%x, blocks=%d, size=0x%lx, memsz=0x%lx\n", 
                       *files, start_addr, taskinfo[taskidx].block_nums, p_filesz, p_memsz);
            }
        }
        fclose(fp);
        files++;
    }
    write_img_info(nbytes_kernel, taskinfo, tasknum, img, &phyaddr);
    printf("current phyaddr:%x\n", phyaddr);
    /*
     * Ensure the image is padded to sector boundary and then reserve
     * exactly one sector at the end for the batch file. Instead of
     * using a hard-coded absolute sector (like 50), record the batch
     * sector dynamically (the first free sector after current content)
     * and write that value into the boot info area so the loader can
     * find it at runtime.
     */
    fseek(img, phyaddr, SEEK_SET);
    /* pad to current sector boundary */
    write_padding(img, &phyaddr, NBYTES2SEC(phyaddr) * SECTOR_SIZE);
    printf("current phyaddr:%x\n", phyaddr);

    /* reserve one sector for batch file at the end */
    int batch_sector = NBYTES2SEC(phyaddr); /* batch will live at this sector */
    write_padding(img, &phyaddr, (batch_sector + 1) * SECTOR_SIZE);
    printf("Reserved one sector for batch file at sector %d, current phyaddr:%x\n", batch_sector, phyaddr);

    /* finally ensure swap region exists inside the SD image and record its start */
    int swap_start_sector = 0;
    reserve_swap_space(img, &phyaddr, &swap_start_sector);
    write_swap_info(img, swap_start_sector);
    fclose(img);
}

static void read_ehdr(Elf64_Ehdr * ehdr, FILE * fp)
{
    int ret;

    ret = fread(ehdr, sizeof(*ehdr), 1, fp);
    assert(ret == 1);
    assert(ehdr->e_ident[EI_MAG1] == 'E');
    assert(ehdr->e_ident[EI_MAG2] == 'L');
    assert(ehdr->e_ident[EI_MAG3] == 'F');
}

static void read_phdr(Elf64_Phdr * phdr, FILE * fp, int ph,
                      Elf64_Ehdr ehdr)
{
    int ret;

    fseek(fp, ehdr.e_phoff + ph * ehdr.e_phentsize, SEEK_SET);
    ret = fread(phdr, sizeof(*phdr), 1, fp);
    assert(ret == 1);
    if (options.extended == 1) {
        printf("\tsegment %d\n", ph);
        printf("\t\toffset 0x%04lx", phdr->p_offset);
        printf("\t\tvaddr 0x%04lx\n", phdr->p_vaddr);
        printf("\t\tfilesz 0x%04lx", phdr->p_filesz);
        printf("\t\tmemsz 0x%04lx\n", phdr->p_memsz);
    }
}

static uint64_t get_entrypoint(Elf64_Ehdr ehdr)
{
    return ehdr.e_entry;
}

static uint32_t get_filesz(Elf64_Phdr phdr)
{
    return phdr.p_filesz;
}

static uint32_t get_memsz(Elf64_Phdr phdr)
{
    return phdr.p_memsz;
}

static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr)
{
    if (phdr.p_memsz != 0 && phdr.p_type == PT_LOAD) {
        /* write the segment itself */
        /* NOTE: expansion of .bss should be done by kernel or runtime env! */
        if (options.extended == 1) {
            printf("\t\twriting 0x%04lx bytes\n", phdr.p_filesz);
        }
        fseek(fp, phdr.p_offset, SEEK_SET);
        while (phdr.p_filesz-- > 0) {
            fputc(fgetc(fp), img);
            (*phyaddr)++;
        }
    }
}

static void write_padding(FILE *img, int *phyaddr, int new_phyaddr)
{
    if (options.extended == 1 && *phyaddr < new_phyaddr) {
        printf("\t\twrite 0x%04x bytes for padding\n", new_phyaddr - *phyaddr);
    }

    while (*phyaddr < new_phyaddr) {
        fputc(0, img);
        (*phyaddr)++;
    }
}

static void write_img_info(int nbytes_kernel, task_info_t *taskinfo,
                           short tasknum, FILE * img, int *taskinfo_addr)
{
    // write image info to some certain places
    // NOTE: os size, infomation about app-info sector(s) ...
     // 计算 kernel 所占扇区数
    short kernel_sectors = NBYTES2SEC(nbytes_kernel);

    // 跳转到 OS_SIZE_LOC 位置
    fseek(img, OS_SIZE_LOC, SEEK_SET);

    // 写入 kernel 所占扇区数（2字节）
    fwrite(&kernel_sectors, sizeof(short), 1, img);
    printf("kernel sectors: %d\n", kernel_sectors); 

    // 写入用户程序数目（2字节，紧跟在 kernel_sectors 后面）
    fwrite(&tasknum, sizeof(short), 1, img);
    printf("user program count: %d\n", tasknum);

        // 将taskinfo的size写进bootloader的末尾几个字节
    int info_size = sizeof(task_info_t) * tasknum;
    // 将定位信息写进bootloader的末尾几个字节
    fseek(img, APP_INFO_ADDR_LOC, SEEK_SET);  // 文件指针指到 APP_INFO_ADDR_LOC
    fwrite(taskinfo_addr, sizeof(int), 1, img);
    printf("Task info address: %x\n", *taskinfo_addr);
    fwrite(&info_size, sizeof(int), 1, img);    
    printf("Task info size: %d\n", info_size);
    fseek(img, *taskinfo_addr, SEEK_SET);  
    fwrite(taskinfo, sizeof(task_info_t), tasknum, img);
    printf("Task info written at: %x\n", *taskinfo_addr);
    *taskinfo_addr += info_size;
}

static void reserve_swap_space(FILE *img, int *phyaddr, int *swap_start_sector)
{
    // Place swap right after current payload (aligned to sector boundary)
    int start_sector = NBYTES2SEC(*phyaddr);
    long long swap_start = (long long)start_sector * SECTOR_SIZE;
    long long swap_end =
        swap_start + (long long)MAX_SWAP_PAGES * SWAP_SECTORS_PER_PAGE * SECTOR_SIZE;
    int swap_end_sector = start_sector + MAX_SWAP_PAGES * SWAP_SECTORS_PER_PAGE;

    if (options.extended) {
        printf("Placing swap after payload at sector 0x%x (%lld bytes)\n",
               start_sector, swap_start);
    }

    // Pad to swap end so the image file actually contains the reserved space
    fseek(img, swap_end - 1, SEEK_SET);
    fputc(0, img);
    *phyaddr = (int)swap_end;
    *swap_start_sector = start_sector;
    printf("Reserved swap space: sectors [0x%x, 0x%x), size=%lld bytes (~%lld MB)\n",
           start_sector, swap_end_sector,
           swap_end - swap_start, (swap_end - swap_start) / (1024 * 1024));
}

static void write_swap_info(FILE *img, int swap_start_sector)
{
    // Write swap start sector and max pages into bootblock (below app-info)
    fseek(img, SWAP_INFO_LOC, SEEK_SET);
    fwrite(&swap_start_sector, sizeof(int), 1, img);
    fwrite(&(int){MAX_SWAP_PAGES}, sizeof(int), 1, img);
    if (options.extended) {
        printf("swap_start_sector=%d written at offset 0x%x\n",
               swap_start_sector, SWAP_INFO_LOC);
    }
}

/* print an error message and exit */
static void error(char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    if (errno != 0) {
        perror(NULL);
    }
    exit(EXIT_FAILURE);
}
