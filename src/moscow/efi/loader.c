/* loader.c - BOOTX64.EFI, Moscow's boot loader for UEFI: what boot.nasm
 * does on a BIOS PC, from UEFI's side.
 *
 *     clang --target=x86_64-unknown-windows -ffreestanding ... -c loader.c
 *     lld-link /subsystem:efi_application /entry:efi_main ... -out:BOOTX64.EFI
 *
 * It finds the Moscow image - the disk image whose sector 16 holds the
 * file table ("MOSCOWFS"), as ussr.img starts - through the firmware's
 * block devices: on a disk at its start, on a CD (the ISO) where its El
 * Torito BIOS entry, /boot/ussr.img, lies. Read through UEFI, the CD may
 * be on any controller. It loads the files the table lists (the NASM
 * USSR runtime first) from 1 MiB up, as boot.nasm does; from a CD the
 * whole image too, wherever the firmware has room below 4 GiB, a RAM disk
 * (/dev/ram) the kernel takes as its root. What the loader allocated
 * (EfiLoaderData: the files, the RAM disk, the low pages) is reserved in
 * the memory map the kernel gets, so its heap stays off it. Then the GOP framebuffer (the console draws on it: UEFI has no
 * VGA text mode), ACPI's RSDP, and the memory map in BIOS e820 form go
 * into the boot information block at 0x500; boot services end; the
 * identity page tables for the first 4 GiB go to 0x1000 as boot.nasm
 * builds them, and the runtime starts with the block's address in rdi.
 *
 * What it says goes to the screen and to COM1. */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u64 UINTN;
typedef u64 EFI_STATUS;
typedef void *EFI_HANDLE;
typedef u16 CHAR16;

#define EFIAPI __attribute__((ms_abi))
#define EFI_ERROR(s) (((long long)(s)) < 0)

/* layout.inc's */
#define BOOTINFO        0x500
#define PAGE_PML4       0x1000
#define PAGE_PDPT       0x2000
#define PAGE_PD         0x3000
#define RT_BASE         0x100000
#define RT_STACK_TOP    0x80000
#define TOC_LBA         16
#define TOC_MAX         15
#define FILE_ALIGN      0x1000
#define E820_MAX        64
#define BI_MAGIC        0
#define BI_NFILES       4
#define BI_NE820        8
#define BI_DRIVE        12
#define BI_FILES        16
#define BIF_SIZE        32
#define BI_FILES_END    0x200
#define BI_E820         0x208
#define BI_CD_DRIVE     0x808
#define BI_RSDP         0x820
#define BI_FB_BASE      0x828
#define BI_FB_WIDTH     0x830
#define BI_FB_HEIGHT    0x838
#define BI_FB_STRIDE    0x840
#define BI_FB_FORMAT    0x848
#define BI_RAMDISK_BASE 0x850
#define BI_RAMDISK_BYTES 0x858
#define BI_END          0x860

typedef struct { u32 a; u16 b, c; u8 d[8]; } EFI_GUID;

typedef struct
{
    u64 Signature;
    u32 Revision, HeaderSize, CRC32, Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
{
    void *Reset;
    EFI_STATUS (EFIAPI *OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct
{
    u32 Type, Pad;
    u64 PhysicalStart, VirtualStart, NumberOfPages, Attribute;
} EFI_MEMORY_DESCRIPTOR;

enum { AllocateAnyPages, AllocateMaxAddress, AllocateAddress };
enum { EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData, EfiBootServicesCode,
       EfiBootServicesData, EfiRuntimeServicesCode, EfiRuntimeServicesData,
       EfiConventionalMemory, EfiUnusableMemory, EfiACPIReclaimMemory,
       EfiACPIMemoryNVS, EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace,
       EfiPalCode, EfiPersistentMemory };
enum { AllHandles, ByRegisterNotify, ByProtocol };

typedef struct
{
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL, *RestoreTPL;
    EFI_STATUS (EFIAPI *AllocatePages)(int, int, UINTN, u64 *);
    EFI_STATUS (EFIAPI *FreePages)(u64, UINTN);
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, u32 *);
    EFI_STATUS (EFIAPI *AllocatePool)(int, UINTN, void **);
    EFI_STATUS (EFIAPI *FreePool)(void *);
    void *CreateEvent, *SetTimer, *WaitForEvent, *SignalEvent, *CloseEvent, *CheckEvent;
    void *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
    void *Reserved, *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath;
    void *InstallConfigurationTable, *LoadImage, *StartImage, *Exit, *UnloadImage;
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE, UINTN);
    void *GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(UINTN);
    EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN, u64, UINTN, CHAR16 *);
    void *ConnectController, *DisconnectController, *OpenProtocol, *CloseProtocol;
    void *OpenProtocolInformation, *ProtocolsPerHandle;
    EFI_STATUS (EFIAPI *LocateHandleBuffer)(int, EFI_GUID *, void *, UINTN *, EFI_HANDLE **);
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *, void *, void **);
} EFI_BOOT_SERVICES;

typedef struct { EFI_GUID VendorGuid; void *VendorTable; } EFI_CONFIGURATION_TABLE;

typedef struct
{
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    u32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    void *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct
{
    u32 MediaId;
    u8 RemovableMedia, MediaPresent, LogicalPartition, ReadOnly, WriteCaching;
    u32 BlockSize, IoAlign;
    u64 LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL
{
    u64 Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    void *Reset;
    EFI_STATUS (EFIAPI *ReadBlocks)(struct EFI_BLOCK_IO_PROTOCOL *, u32, u64, UINTN, void *);
} EFI_BLOCK_IO_PROTOCOL;

typedef struct
{
    u32 Version, HorizontalResolution, VerticalResolution, PixelFormat;
    u32 PixelInformation[4];
    u32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct
{
    u32 MaxMode, Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    u64 FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct
{
    void *QueryMode, *SetMode, *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

static EFI_GUID block_io_guid = { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static EFI_GUID gop_guid = { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };
static EFI_GUID acpi20_guid = { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } };
static EFI_GUID acpi10_guid = { 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };

static EFI_SYSTEM_TABLE *st;
static EFI_BOOT_SERVICES *bs;

/* --- what clang may call, and small helpers */

void *memcpy(void *d, const void *s, UINTN n)
{
    u8 *dd = d;
    const u8 *ss = s;
    while (n--)
        *dd++ = *ss++;
    return d;
}

void *memset(void *d, int c, UINTN n)
{
    u8 *dd = d;
    while (n--)
        *dd++ = (u8)c;
    return d;
}

static int same(const void *a, const void *b, UINTN n)
{
    const u8 *x = a, *y = b;
    while (n--)
        if (*x++ != *y++)
            return 0;
    return 1;
}

static int guid_is(const EFI_GUID *a, const EFI_GUID *b)
{
    return same(a, b, sizeof(*a));
}

static inline void outb(u16 port, u8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void serial_putc(char c)
{
    int spin;
    for (spin = 0; spin < 100000 && !(inb(0x3fd) & 0x20); ++spin)
        ;
    outb(0x3f8, (u8)c);
}

/* text to the firmware's console while boot services last (it has COM1
 * too, when there is one), else straight to COM1 */
static void say(const char *text)
{
    CHAR16 buf[160];
    int n = 0;
    const char *p;
    if (st == 0 || bs == 0)
    {
        for (p = text; *p; ++p)
        {
            if (*p == '\n')
                serial_putc('\r');
            serial_putc(*p);
        }
        return;
    }
    for (p = text; *p && n < 156; ++p)
    {
        if (*p == '\n')
            buf[n++] = '\r';
        buf[n++] = (CHAR16)(u8)*p;
    }
    buf[n] = 0;
    st->ConOut->OutputString(st->ConOut, buf);
}

static void say_hex(const char *label, u64 v)
{
    char buf[64];
    int i = 0, k;
    while (*label && i < 40)
        buf[i++] = *label++;
    buf[i++] = '0';
    buf[i++] = 'x';
    for (k = 60; k >= 0; k -= 4)
        buf[i++] = "0123456789abcdef"[(v >> k) & 15];
    buf[i++] = '\n';
    buf[i] = 0;
    say(buf);
}

static EFI_STATUS fail(const char *why)
{
    say("Moscow UEFI: ");
    say(why);
    say("\n");
    for (;;)
        __asm__ volatile("hlt");
}

/* --- the Moscow image */

typedef struct
{
    EFI_BLOCK_IO_PROTOCOL *io;
    u64 base;               /* its first byte on the device */
    int cd;                 /* on a CD: loaded whole, the RAM disk */
} image_t;

static u8 *scratch;         /* 64 KiB, page aligned */

/* bytes at offset on the device (any alignment, at most 32 KiB) to dst */
static int read_bytes(EFI_BLOCK_IO_PROTOCOL *io, u64 offset, UINTN n, void *dst)
{
    u32 bs_ = io->Media->BlockSize;
    u64 first = offset / bs_;
    u64 last = (offset + n + bs_ - 1) / bs_;
    UINTN bytes = (UINTN)(last - first) * bs_;
    if (bytes > 65536)
        return 0;
    if (EFI_ERROR(io->ReadBlocks(io, io->Media->MediaId, first, bytes, scratch)))
        return 0;
    memcpy(dst, scratch + (offset - first * bs_), n);
    return 1;
}

/* a device holding the image: at its start (a disk), or where its El
 * Torito BIOS entry lies (a CD) */
static int image_on(EFI_BLOCK_IO_PROTOCOL *io, image_t *img)
{
    u8 sector[2048];
    u32 bs_ = io->Media->BlockSize;
    if (!io->Media->MediaPresent || io->Media->LogicalPartition)
        return 0;
    img->io = io;
    if (bs_ == 512 && read_bytes(io, (u64)TOC_LBA * 512, 512, sector) &&
        same(sector, "MOSCOWFS", 8))
    {
        img->base = 0;
        img->cd = 0;
        return 1;
    }
    if (bs_ == 2048 && read_bytes(io, 17 * 2048, 2048, sector) && sector[0] == 0 &&
        same(sector + 1, "CD001", 5) && same(sector + 7, "EL TORITO SPECIFICATION", 23))
    {
        u32 catalog = *(u32 *)(sector + 71);
        u32 rba;
        if (!read_bytes(io, (u64)catalog * 2048, 2048, sector) || sector[32] != 0x88)
            return 0;
        rba = *(u32 *)(sector + 32 + 8);
        if (!read_bytes(io, (u64)rba * 2048 + TOC_LBA * 512, 512, sector) ||
            !same(sector, "MOSCOWFS", 8))
            return 0;
        img->base = (u64)rba * 2048;
        img->cd = 1;
        return 1;
    }
    return 0;
}

static void find_image(image_t *img)
{
    EFI_HANDLE *handles;
    UINTN n, i;
    image_t disk = { 0 }, cd = { 0 };
    if (EFI_ERROR(bs->LocateHandleBuffer(ByProtocol, &block_io_guid, 0, &n, &handles)))
        fail("no block devices");
    for (i = 0; i < n; ++i)
    {
        EFI_BLOCK_IO_PROTOCOL *io;
        image_t found;
        if (EFI_ERROR(bs->HandleProtocol(handles[i], &block_io_guid, (void **)&io)))
            continue;
        if (!image_on(io, &found))
            continue;
        if (found.cd && cd.io == 0)
            cd = found;
        if (!found.cd && disk.io == 0)
            disk = found;
    }
    /* booted from the ISO, the CD's; else a disk's */
    if (cd.io)
        *img = cd;
    else if (disk.io)
        *img = disk;
    else
        fail("no Moscow image (MOSCOWFS) on the disks or CDs");
}

/* --- after boot services: the boot information, the tables, the jump */

static u64 e820[E820_MAX][3];       /* base, length, type */
static u32 ne820;

static void e820_add(u64 base, u64 len, u32 type)
{
    if (ne820 > 0)
    {
        u64 *last = e820[ne820 - 1];
        if (last[2] == type && last[0] + last[1] == base)
        {
            last[1] += len;
            return;
        }
    }
    if (ne820 < E820_MAX)
    {
        e820[ne820][0] = base;
        e820[ne820][1] = len;
        e820[ne820][2] = type;
        ++ne820;
    }
}

static u32 e820_type(u32 efi)
{
    switch (efi)
    {
    case EfiLoaderCode: case EfiBootServicesCode:
    case EfiBootServicesData: case EfiConventionalMemory:
        return 1;                   /* the kernel's, boot services over */
    case EfiLoaderData:
        return 2;                   /* the files, the RAM disk: kept */
    case EfiACPIReclaimMemory:
        return 3;
    case EfiACPIMemoryNVS:
        return 4;
    case EfiUnusableMemory:
        return 5;
    default:
        return 2;
    }
}

static u8 bootinfo[BI_END];

static void __attribute__((noreturn)) start_runtime(void)
{
    u64 *pml4 = (u64 *)PAGE_PML4, *pdpt = (u64 *)PAGE_PDPT, *pd = (u64 *)PAGE_PD;
    u64 i;
    memcpy((void *)BOOTINFO, bootinfo, sizeof(bootinfo));
    /* the first 4 GiB, identity, 2 MiB pages: boot.nasm's tables */
    memset(pml4, 0, 6 * 4096);
    pml4[0] = PAGE_PDPT | 3;
    for (i = 0; i < 4; ++i)
        pdpt[i] = (PAGE_PD + i * 4096) | 3;
    for (i = 0; i < 2048; ++i)
        pd[i] = (i << 21) | 0x83;
    __asm__ volatile(
        "cli\n"
        "mov %0, %%cr3\n"
        "mov %1, %%rsp\n"
        "jmp *%2\n"
        : : "r"((u64)PAGE_PML4), "r"((u64)RT_STACK_TOP), "r"((u64)RT_BASE), "D"((u64)BOOTINFO)
        : "memory");
    for (;;)
        ;
}

/* --- efi_main */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system)
{
    image_t img;
    u8 toc[512];
    u32 nfiles, i;
    u64 at, files_end, image_bytes = 0, ram = 0;
    u64 low = 0x1000, region, pages;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    UINTN map_size, map_key, desc_size, k;
    u32 desc_version;
    u8 *map;
    u64 scratch_at;

    st = system;
    bs = system->BootServices;
    bs->SetWatchdogTimer(0, 0, 0, 0);
    say("Moscow UEFI: loader\n");

    scratch_at = 0xffffffff;
    if (EFI_ERROR(bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, 16, &scratch_at)))
        fail("no memory for a buffer");
    scratch = (u8 *)scratch_at;
    find_image(&img);
    say(img.cd ? "Moscow UEFI: the image on the CD\n" : "Moscow UEFI: the image on a disk\n");

    /* the file table */
    if (!read_bytes(img.io, img.base + (u64)TOC_LBA * 512, 512, toc))
        fail("cannot read the file table");
    nfiles = *(u32 *)(toc + 8);
    if (nfiles == 0 || nfiles > TOC_MAX)
        fail("a bad file table");
    /* how far the files go, and the image (its one partition's end) */
    files_end = RT_BASE;
    for (i = 0; i < nfiles; ++i)
    {
        u8 *e = toc + 16 + i * 32;
        files_end += ((u64) * (u32 *)(e + 24) * 512 + FILE_ALIGN - 1) & ~(u64)(FILE_ALIGN - 1);
    }
    if (img.cd)
    {
        u8 mbr[512];
        if (!read_bytes(img.io, img.base, 512, mbr) || mbr[510] != 0x55 || mbr[511] != 0xaa)
            fail("the image has no partition table");
        image_bytes = ((u64) * (u32 *)(mbr + 446 + 8) + *(u32 *)(mbr + 446 + 12)) * 512;
    }
    /* memory the runtime takes: its tables and variables low, the files
     * from 1 MiB, where boot.nasm puts them; the RAM disk anywhere */
    region = files_end;
    if (image_bytes)
    {
        ram = 0xffffffff;
        if (EFI_ERROR(bs->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                        (image_bytes + 4095) / 4096, &ram)))
            fail("no memory for the image");
    }
    if (EFI_ERROR(bs->AllocatePages(AllocateAddress, EfiLoaderData, (RT_STACK_TOP - 0x1000) / 4096, &low)))
        fail("the memory below 512 KiB is taken");
    at = RT_BASE;
    pages = (region - RT_BASE + 4095) / 4096;
    if (EFI_ERROR(bs->AllocatePages(AllocateAddress, EfiLoaderData, pages, &at)))
        fail("the memory from 1 MiB is taken");

    memset(bootinfo, 0, sizeof(bootinfo));
    *(u32 *)(bootinfo + BI_MAGIC) = 0x43534f4d;      /* 'MOSC' */
    *(u32 *)(bootinfo + BI_NFILES) = nfiles;
    at = RT_BASE;
    for (i = 0; i < nfiles; ++i)
    {
        u8 *e = toc + 16 + i * 32;
        u8 *b = bootinfo + BI_FILES + i * BIF_SIZE;
        u32 lba = *(u32 *)(e + 16), bytes = *(u32 *)(e + 20), sectors = *(u32 *)(e + 24);
        u64 off = 0, total = (u64)sectors * 512;
        memcpy(b, e, 16);
        *(u64 *)(b + 16) = at;
        *(u64 *)(b + 24) = bytes;
        while (off < total)
        {
            UINTN n = total - off > 32768 ? 32768 : (UINTN)(total - off);
            if (!read_bytes(img.io, img.base + (u64)lba * 512 + off, n, (u8 *)(at + off)))
                fail("cannot read a file");
            off += n;
        }
        at += (total + FILE_ALIGN - 1) & ~(u64)(FILE_ALIGN - 1);
    }
    if (img.cd)
    {
        /* the whole image, a RAM disk; 2048-byte blocks straight in */
        u64 off = 0;
        say("Moscow UEFI: the image into memory\n");
        while (off < image_bytes)
        {
            UINTN n = image_bytes - off > 32768 ? 32768 : (UINTN)(image_bytes - off);
            if (!read_bytes(img.io, img.base + off, n, (u8 *)(ram + off)))
                fail("cannot read the image");
            off += n;
        }
        *(u64 *)(bootinfo + BI_RAMDISK_BASE) = ram;
        *(u64 *)(bootinfo + BI_RAMDISK_BYTES) = image_bytes;
    }
    *(u64 *)(bootinfo + BI_FILES_END) = at;

    /* the screen: GOP's framebuffer, 32 bits a pixel */
    if (!EFI_ERROR(bs->LocateProtocol(&gop_guid, 0, (void **)&gop)) && gop && gop->Mode &&
        gop->Mode->Info && gop->Mode->Info->PixelFormat <= 1 && gop->Mode->FrameBufferBase < 0x100000000ULL)
    {
        *(u64 *)(bootinfo + BI_FB_BASE) = gop->Mode->FrameBufferBase;
        *(u64 *)(bootinfo + BI_FB_WIDTH) = gop->Mode->Info->HorizontalResolution;
        *(u64 *)(bootinfo + BI_FB_HEIGHT) = gop->Mode->Info->VerticalResolution;
        *(u64 *)(bootinfo + BI_FB_STRIDE) = gop->Mode->Info->PixelsPerScanLine;
        *(u64 *)(bootinfo + BI_FB_FORMAT) = gop->Mode->Info->PixelFormat;   /* 1: BGRX */
    }
    else
        say("Moscow UEFI: no framebuffer: the console is COM1 only\n");
    /* ACPI: the 2.0 table first */
    for (k = 0; k < st->NumberOfTableEntries; ++k)
        if (guid_is(&st->ConfigurationTable[k].VendorGuid, &acpi20_guid))
            *(u64 *)(bootinfo + BI_RSDP) = (u64)st->ConfigurationTable[k].VendorTable;
    if (*(u64 *)(bootinfo + BI_RSDP) == 0)
        for (k = 0; k < st->NumberOfTableEntries; ++k)
            if (guid_is(&st->ConfigurationTable[k].VendorGuid, &acpi10_guid))
                *(u64 *)(bootinfo + BI_RSDP) = (u64)st->ConfigurationTable[k].VendorTable;
    say_hex("Moscow UEFI: files loaded from 1 MiB to ", at);
    say("Moscow UEFI: starting the runtime\n");

    /* the memory map, then no more boot services (and no more say on the
     * screen: COM1 only) */
    map_size = 0;
    bs->GetMemoryMap(&map_size, 0, &map_key, &desc_size, &desc_version);
    map_size += 16 * desc_size;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, map_size, (void **)&map)))
        fail("no memory for the memory map");
    for (k = 0; k < 4; ++k)
    {
        UINTN size = map_size;
        if (EFI_ERROR(bs->GetMemoryMap(&size, (EFI_MEMORY_DESCRIPTOR *)map, &map_key, &desc_size, &desc_version)))
            fail("no memory map");
        if (!EFI_ERROR(bs->ExitBootServices(image_handle, map_key)))
        {
            UINTN j;
            bs = 0;
            st = 0;
            for (j = 0; j + desc_size <= size; j += desc_size)
            {
                EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(map + j);
                e820_add(d->PhysicalStart, d->NumberOfPages * 4096, e820_type(d->Type));
            }
            memcpy(bootinfo + BI_E820, e820, sizeof(e820[0]) * ne820);
            *(u32 *)(bootinfo + BI_NE820) = ne820;
            start_runtime();
        }
    }
    return fail("ExitBootServices refused");
}
