/* Polled xHCI boot-protocol input for Moscow. DMA uses the runtime's
 * low identity map. Select a keyboard and relative mouse on root ports
 * at boot. */
#include <stdint.h>
#include "hid_mouse.h"

enum {
    USBCMD = 0, USBSTS = 4, PAGESIZE = 8, CRCR = 0x18, DCBAAP = 0x30,
    CONFIG = 0x38, PORTSC = 0x400, PORT_STRIDE = 16,
    RUN = 1, RESET = 2, HALTED = 1, NOT_READY = 1 << 11,
    CONNECTED = 1, ENABLED = 2, PORT_RESET = 1 << 4, PORT_POWER = 1 << 9,
    PORT_WARM_RESET = 1u << 31,
    PORT_PRESERVE = PORT_POWER | (3 << 14) | (7 << 25),
    IMAN = 0, ERSTSZ = 8, ERSTBA = 0x10, ERDP = 0x18,
    CYCLE = 1, TOGGLE = 2, ISP = 4, IOC = 1 << 5, IDT = 1 << 6,
    NORMAL = 1, SETUP = 2, DATA = 3, STATUS = 4, LINK = 6,
    ENABLE_SLOT = 9, DISABLE_SLOT = 10, ADDRESS_DEVICE = 11,
    CONFIGURE_ENDPOINT = 12, EVALUATE_CONTEXT = 13,
    TRANSFER_EVENT = 32, COMMAND_EVENT = 33,
    SUCCESS = 1, SHORT_PACKET = 13,
    RING_COUNT = 32, CONFIG_BYTES = 1024, SCRATCH_COUNT = 32,
    MMIO_BYTES = 1 << 20
};
#define TYPE(n) ((uint32_t)(n) << 10)
#define ALIGNED(n) __attribute__((aligned(n)))

struct trb {
    uint64_t parameter;
    uint32_t status, control;
};

_Static_assert(sizeof(struct trb) == 16, "xHCI TRBs are 16 bytes");

struct ring {
    volatile struct trb trbs[RING_COUNT] ALIGNED(512);
    unsigned next, cycle;
};

enum { KEYBOARD, MOUSE, INPUT_COUNT };

struct input_device {
    struct ring ring;
    volatile struct trb *request;
    volatile uint8_t report[MOUSE_REPORT_BYTES] ALIGNED(64);
    unsigned slot, dci, length, received;
    int disconnected;
    struct controller *controller;
};

struct controller {
    struct ring command_ring, control_rings[INPUT_COUNT];
    volatile struct trb events[RING_COUNT] ALIGNED(512);
    struct {
        uint64_t base;
        uint32_t size, reserved;
    } event_segment ALIGNED(64);
    uint64_t dcbaa[256] ALIGNED(64);
    uint64_t scratch_ptrs[SCRATCH_COUNT] ALIGNED(64);
    uint8_t scratch[SCRATCH_COUNT][4096] ALIGNED(4096);
    volatile uint32_t output_contexts[INPUT_COUNT][32 * 16] ALIGNED(4096);
    uintptr_t operational, doorbells, interrupter;
    unsigned event_next, event_cycle, slots_kept;
};

static struct controller controllers[INPUT_COUNT];
static struct controller *controller;
static unsigned controllers_kept;
static struct ring *control_ring;
static struct input_device inputs[INPUT_COUNT];
static struct mouse_layout mouse_report;
static uint64_t mmio_directory[512] ALIGNED(4096);
static uint64_t mmio_pages[INPUT_COUNT][512] ALIGNED(4096);
static volatile uint32_t *output_context;
static uint32_t input_context[33 * 16] ALIGNED(4096);
static volatile uint8_t descriptor[CONFIG_BYTES] ALIGNED(1024);
static uint8_t previous[6];
static unsigned pending[6], pending_next, pending_count;
static unsigned context_words, slot;
static volatile uint64_t *clock_ticks;
static uint64_t probe_start;
static void (*trace)(const char *, uintptr_t);
static unsigned completion_code;

static void clear(volatile void *ptr, unsigned bytes)
{
    volatile uint8_t *p = ptr;
    while (bytes--)
        *p++ = 0;
}

static void out32(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0,%1" :: "a"(value), "Nd"(port) : "memory");
}

static void out16(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0,%1" :: "a"(value), "Nd"(port) : "memory");
}

static uint32_t pci_read(uint32_t address)
{
    uint32_t value;
    out32(0xcf8, 0x80000000u | address);
    __asm__ volatile("inl %1,%0" : "=a"(value) : "Nd"((uint16_t)0xcfc));
    return value;
}

static void pci_command(uint32_t address, uint16_t command)
{
    out32(0xcf8, 0x80000000u | address | 4);
    out16(0xcfc, command);
}

static uint32_t read32(uintptr_t address)
{
    return *(volatile uint32_t *)address;
}

static void write32(uintptr_t address, uint32_t value)
{
    *(volatile uint32_t *)address = value;
}

static void write64(uintptr_t address, uint64_t value)
{
    write32(address, value);
    write32(address + 4, value >> 32);
}

static int within(uint64_t start, unsigned ticks, unsigned spin)
{
    /* The iteration limit also works if the platform timer is silent. */
    return spin < 16000000 && *clock_ticks - start < ticks;
}

static int wait_bits(uintptr_t address, uint32_t mask, uint32_t value,
                     unsigned ticks)
{
    uint64_t start = *clock_ticks;
    for (unsigned spin = 0; within(start, ticks, spin); ++spin) {
        if ((read32(address) & mask) == value)
            return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static void delay(unsigned ticks)
{
    uint64_t start = *clock_ticks;
    for (unsigned spin = 0; within(start, ticks, spin); ++spin)
        __asm__ volatile("pause");
}

/* The last GiB of the shared kernel slot is above Moscow's existing
 * identity map. Alias the BAR here so firmware can place it anywhere in
 * physical memory, including above the kernel's virtual address range. */
static uintptr_t map_controller(uintptr_t physical)
{
    const uintptr_t window = 511ULL << 30;
    const uint64_t address_mask = 0x000ffffffffff000ULL;
    uintptr_t cr3, page = physical & ~0xfffULL;
    if (physical < 0x100000 || physical > (1ULL << 52) - MMIO_BYTES)
        return 0;
    __asm__ volatile("mov %%cr3,%0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(cr3 & address_mask);
    uint64_t *pdpt = (uint64_t *)(pml4[0] & address_mask);
    uint64_t *pages = mmio_pages[controllers_kept];
    for (unsigned i = 0; i < MMIO_BYTES / 4096 + 1; ++i)
        pages[i] = (page + i * 4096) | 0x1b; /* RW, uncacheable */
    mmio_directory[controllers_kept] = (uintptr_t)pages | 3;
    pdpt[511] = (uintptr_t)mmio_directory | 3;
    __asm__ volatile("mov %0,%%cr3" :: "r"(cr3) : "memory");
    return window + controllers_kept * 0x200000 + (physical & 0xfff);
}

static int claim_controller(uintptr_t base, uint32_t hcc)
{
    unsigned offset = (hcc >> 16) * 4;
    while (offset && offset <= MMIO_BYTES - 8) {
        uint32_t cap = read32(base + offset);
        if ((cap & 255) == 1) {
            uintptr_t legacy = base + offset;
            /* Write the OS-owned byte only, leaving the BIOS semaphore alone. */
            *(volatile uint8_t *)(legacy + 3) = 1;
            if (!wait_bits(legacy, 1 << 16, 0, 50)) {
                *(volatile uint8_t *)(legacy + 3) = 0;
                return 0;
            }
            /* Disable legacy SMI enables and acknowledge their status bits. */
            write32(legacy + 4, 0xe0000000);
            return 1;
        }
        unsigned next = (cap >> 8) & 255;
        if (!next)
            break;
        offset += next * 4;
    }
    return 1;
}

static void ring_init(struct ring *ring)
{
    clear(ring->trbs, sizeof ring->trbs);
    ring->next = 0;
    ring->cycle = 1;
    ring->trbs[RING_COUNT - 1].parameter = (uintptr_t)ring->trbs;
}

static volatile struct trb *enqueue(struct ring *ring, uint64_t parameter,
                                    uint32_t status, uint32_t control)
{
    volatile struct trb *trb = &ring->trbs[ring->next++];
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control ^ ring->cycle;
    if (ring->next == RING_COUNT - 1) {
        ring->trbs[ring->next].control = TYPE(LINK) | TOGGLE | ring->cycle;
        ring->next = 0;
        ring->cycle ^= 1;
    }
    return trb;
}

static void ring_doorbell(const struct controller *host, unsigned target, unsigned endpoint)
{
    __asm__ volatile("mfence" ::: "memory");
    write32(host->doorbells + target * 4, endpoint);
}

static int take_event(struct controller *host, struct trb *event)
{
    volatile struct trb *current = &host->events[host->event_next];
    uint32_t control = current->control;
    if ((control & CYCLE) != host->event_cycle)
        return 0;
    event->control = control;
    event->parameter = current->parameter;
    event->status = current->status;
    if (++host->event_next == RING_COUNT) {
        host->event_next = 0;
        host->event_cycle ^= 1;
    }
    write64(host->interrupter + ERDP, (uintptr_t)&host->events[host->event_next] | 8);
    return 1;
}

static unsigned event_type(const struct trb *event)
{
    return (event->control >> 10) & 63;
}

static int wait_event(unsigned type, volatile struct trb *request,
                      struct trb *result)
{
    uint64_t start = *clock_ticks;
    completion_code = 0;
    for (unsigned spin = 0; within(start, 50, spin); ++spin) {
        if (take_event(controller, result) && event_type(result) == type) {
            unsigned code = result->status >> 24;
            if (result->parameter == (uintptr_t)request) {
                completion_code = code;
                return code == SUCCESS;
            }
            /* An EP0 failure can point at the Setup or Data TRB instead
             * of the Status TRB. There is only one request in flight. */
            if (type == TRANSFER_EVENT && (result->control >> 24) == slot &&
                ((result->control >> 16) & 31) == 1 &&
                code != SUCCESS && code != SHORT_PACKET) {
                completion_code = code;
                return 0;
            }
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static int command(unsigned type, uint64_t parameter, uint32_t flags,
                   struct trb *result)
{
    volatile struct trb *request =
        enqueue(&controller->command_ring, parameter, 0, TYPE(type) | flags);
    ring_doorbell(controller, 0, 0);
    if (wait_event(COMMAND_EVENT, request, result))
        return 1;
    trace("command type/completion", (type << 8) | completion_code);
    return 0;
}

static int control(unsigned request_type, unsigned request, unsigned value,
                   unsigned index, unsigned length)
{
    struct trb event;
    /* Keep a control TD contiguous. A Link TRB between its stages would
     * need to carry the preceding TRB's Chain flag. */
    if (control_ring->next >= RING_COUNT - 3) {
        volatile struct trb *link = &control_ring->trbs[control_ring->next];
        link->parameter = (uintptr_t)control_ring->trbs;
        link->status = 0;
        link->control = TYPE(LINK) | TOGGLE | control_ring->cycle;
        control_ring->next = 0;
        control_ring->cycle ^= 1;
    }
    uint64_t setup = request_type | (request << 8) | ((uint64_t)value << 16) |
                     ((uint64_t)index << 32) | ((uint64_t)length << 48);
    unsigned setup_flags = TYPE(SETUP) | IDT | (length ? 3 << 16 : 0);
    unsigned setup_cycle = control_ring->cycle;
    /* Publish the setup stage only after the whole TD is ready. */
    volatile struct trb *first = enqueue(control_ring, setup, 8, setup_flags | CYCLE);
    if (length)
        enqueue(control_ring, (uintptr_t)descriptor, length, TYPE(DATA) | (1 << 16));
    volatile struct trb *status = enqueue(control_ring, 0, 0,
        TYPE(STATUS) | IOC | (length ? 0 : 1 << 16));
    first->control = setup_flags | setup_cycle;
    ring_doorbell(controller, slot, 1);
    if (wait_event(TRANSFER_EVENT, status, &event))
        return 1;
    trace("USB request/completion", (request << 8) | completion_code);
    return 0;
}

static void endpoint(unsigned dci, unsigned type, unsigned packet,
                     unsigned interval, unsigned burst, unsigned payload,
                     struct ring *ring)
{
    uint32_t *ep = input_context + (1 + dci) * context_words;
    uintptr_t dequeue = (uintptr_t)ring->trbs | ring->cycle;
    ep[0] = (interval << 16) | ((payload >> 16) << 24);
    ep[1] = (3 << 1) | (type << 3) | (burst << 8) | (packet << 16);
    ep[2] = dequeue;
    ep[3] = dequeue >> 32;
    ep[4] = (payload << 16) | (type == 4 ? 8 : packet);
}

static unsigned le16(unsigned offset)
{
    return descriptor[offset] | (descriptor[offset + 1] << 8);
}

struct boot_interface {
    unsigned number, dci, packet, interval, burst, payload, report_bytes;
};

static int read_interrupt_endpoint(struct boot_interface *interface, unsigned kind,
                                   unsigned speed, unsigned offset, unsigned next,
                                   unsigned bytes)
{
    unsigned number = descriptor[offset + 2] & 15;
    unsigned packet = le16(offset + 4), interval = descriptor[offset + 6];
    if (!number || packet < (kind == KEYBOARD ? 8u : 3u) ||
        packet > 1024 || !interval)
        return 0;
    unsigned burst = 0, payload = packet;
    if (speed <= 2) {
        unsigned exponent = 0;
        while ((interval >>= 1) != 0)
            ++exponent;
        interval = exponent + 3;
    } else {
        if (interval > 16)
            return 0;
        --interval;
        if (speed >= 4) {
            if (next + 6 > bytes || descriptor[next] != 6 ||
                descriptor[next + 1] != 48)
                return 0;
            burst = descriptor[next + 2];
            payload = le16(next + 4);
            if (burst > 15 || payload < (kind == KEYBOARD ? 8u : 3u) ||
                payload > packet * (burst + 1))
                return 0;
        }
    }
    interface->dci = number * 2 + 1;
    interface->packet = packet;
    interface->interval = interval;
    interface->burst = burst;
    interface->payload = payload;
    return 1;
}

static unsigned configure_input(unsigned speed)
{
    clear(descriptor, 9);
    if (!control(0x80, 6, 0x200, 0, 9) || descriptor[0] != 9 ||
        descriptor[1] != 2)
        return 0;
    unsigned bytes = le16(2);
    if (bytes < 9 || bytes > CONFIG_BYTES)
        return 0;
    clear(descriptor, bytes);
    if (!control(0x80, 6, 0x200, 0, bytes) || le16(2) != bytes)
        return 0;

    unsigned config = descriptor[5], number = 0, kind = INPUT_COUNT, report_bytes = 0;
    struct boot_interface interfaces[INPUT_COUNT] = {0};
    for (unsigned offset = 9; offset + 2 <= bytes;) {
        unsigned length = descriptor[offset], type = descriptor[offset + 1];
        if (length < 2 || length > bytes - offset)
            return 0;
        if (type == 4) {
            kind = INPUT_COUNT;
            report_bytes = 0;
            if (length >= 9 && descriptor[offset + 3] == 0 &&
                descriptor[offset + 5] == 3 && descriptor[offset + 6] == 1) {
                unsigned protocol = descriptor[offset + 7];
                if (protocol == 1 || protocol == 2) {
                    kind = protocol - 1;
                    number = descriptor[offset + 2];
                }
            }
        } else if (kind == MOUSE && type == 0x21 && length >= 9) {
            for (unsigned i = 6; i + 3 <= length; i += 3) {
                if (descriptor[offset + i] == 0x22) {
                    report_bytes = le16(offset + i + 1);
                    break;
                }
            }
        } else if (type == 5 && length >= 7 && kind < INPUT_COUNT &&
                   !inputs[kind].slot && !interfaces[kind].dci &&
                   (descriptor[offset + 2] & 0x80) &&
                   (descriptor[offset + 3] & 3) == 3 &&
                   read_interrupt_endpoint(&interfaces[kind], kind, speed,
                                           offset, offset + length, bytes)) {
            interfaces[kind].number = number;
            interfaces[kind].report_bytes = report_bytes;
        }
        offset += length;
    }

    unsigned last_dci = 0, flags = 1;
    clear(input_context, sizeof input_context);
    for (kind = 0; kind < INPUT_COUNT; ++kind) {
        const struct boot_interface *it = &interfaces[kind];
        if (!it->dci)
            continue;
        if (flags & (1u << it->dci))
            return 0;
        flags |= 1u << it->dci;
        if (it->dci > last_dci)
            last_dci = it->dci;
        ring_init(&inputs[kind].ring);
        endpoint(it->dci, 7, it->packet, it->interval, it->burst, it->payload,
                 &inputs[kind].ring);
    }
    if (!last_dci)
        return 0;
    uint32_t *slot_context = input_context + context_words;
    input_context[1] = flags;
    slot_context[0] = (output_context[0] & ~(31u << 27)) | (last_dci << 27);
    slot_context[1] = output_context[1];
    slot_context[2] = output_context[2];
    struct trb event;
    if (!command(CONFIGURE_ENDPOINT, (uintptr_t)input_context, slot << 24, &event) ||
        !control(0, 9, config, 0, 0))
        return 0;

    unsigned found = 0;
    for (kind = 0; kind < INPUT_COUNT; ++kind) {
        const struct boot_interface *it = &interfaces[kind];
        if (!it->dci || !control(0x21, 11, 0, it->number, 0))
            continue;
        struct input_device *device = &inputs[kind];
        device->slot = slot;
        device->controller = controller;
        device->dci = it->dci;
        device->length = 8;
        if (kind == MOUSE) {
            device->length = it->packet < 4 ? 3 : 4;
            struct mouse_layout layout = {0};
            clear(descriptor, sizeof descriptor);
            if (it->report_bytes && it->report_bytes <= CONFIG_BYTES &&
                control(0x81, 6, 0x2200, it->number, it->report_bytes) &&
                mouse_layout_read(&layout, descriptor, it->report_bytes) &&
                (layout.bits + 7) / 8 <= it->packet &&
                control(0x21, 11, 1, it->number, 0)) {
                mouse_report = layout;
                device->length = it->packet < MOUSE_REPORT_BYTES ? it->packet : MOUSE_REPORT_BYTES;
            }
        }
        found |= 1u << kind;
    }
    return found;
}

static unsigned enumerate(unsigned port, unsigned speed_id, unsigned speed, unsigned slot_type)
{
    struct trb event;
    if (!command(ENABLE_SLOT, 0, slot_type << 16, &event))
        return 0;
    slot = event.control >> 24;
    if (!slot)
        return 0;

    clear(input_context, sizeof input_context);
    output_context = controller->output_contexts[controller->slots_kept];
    control_ring = &controller->control_rings[controller->slots_kept];
    clear(output_context, sizeof controller->output_contexts[0]);
    controller->dcbaa[slot] = (uintptr_t)output_context;
    ring_init(control_ring);
    input_context[1] = 3;
    uint32_t *slot_context = input_context + context_words;
    slot_context[0] = (speed_id << 20) | (1 << 27);
    slot_context[1] = port << 16;
    unsigned packet = speed >= 4 ? 512 : speed == 3 ? 64 : 8;
    endpoint(1, 4, packet, 0, 0, 0, control_ring);
    if (!command(ADDRESS_DEVICE, (uintptr_t)input_context, slot << 24, &event))
        return 0;

    if (speed == 1) {
        clear(descriptor, 8);
        if (!control(0x80, 6, 0x100, 0, 8) || descriptor[0] != 18 ||
            descriptor[1] != 1)
            return 0;
        packet = descriptor[7];
        if (packet != 8) {
            if (packet != 16 && packet != 32 && packet != 64)
                return 0;
            clear(input_context, sizeof input_context);
            input_context[1] = 2;
            input_context[2 * context_words + 1] = packet << 16;
            if (!command(EVALUATE_CONTEXT, (uintptr_t)input_context, slot << 24, &event))
                return 0;
        }
    }
    return configure_input(speed);
}

static void submit_report(struct input_device *device)
{
    device->request = enqueue(&device->ring, (uintptr_t)device->report,
                              device->length, TYPE(NORMAL) | IOC | ISP);
    ring_doorbell(device->controller, device->slot, device->dci);
}

/* Supported Protocol capabilities supply the port revision, slot type
 * and any controller-specific speed IDs. */
static uintptr_t port_protocol(uintptr_t base, uint32_t hcc, unsigned port)
{
    unsigned offset = (hcc >> 16) * 4;
    while (offset && offset <= MMIO_BYTES - (16 + 15 * 4)) {
        uint32_t cap = read32(base + offset);
        if ((cap & 255) == 2) {
            uint32_t ports = read32(base + offset + 8);
            unsigned first = ports & 255, count = (ports >> 8) & 255;
            if (port >= first && port < first + count)
                return base + offset;
        }
        unsigned next = (cap >> 8) & 255;
        if (!next)
            break;
        offset += next * 4;
    }
    return 0;
}

static unsigned usb_speed(uintptr_t protocol, unsigned speed_id, unsigned major)
{
    if (major >= 3)
        return 4;
    unsigned count = protocol ? read32(protocol + 8) >> 28 : 0;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t speed = read32(protocol + 16 + i * 4);
        if ((speed & 15) == speed_id) {
            uint64_t rate = speed >> 16;
            for (unsigned units = (speed >> 4) & 3; units; --units)
                rate *= 1000;
            if (rate)
                return rate <= 1500000 ? 2 : rate <= 12000000 ? 1 : 3;
        }
    }
    return speed_id;
}

static unsigned reset_port(uintptr_t port, unsigned major)
{
    uint32_t status = read32(port);
    if (!(status & CONNECTED) || (status & (1 << 3)))
        return 0;
    if (major <= 2 || !(status & ENABLED)) {
        unsigned reset = major >= 3 ? PORT_WARM_RESET : PORT_RESET;
        write32(port, (status & PORT_PRESERVE) | reset);
        if (!wait_bits(port, PORT_RESET | ENABLED, ENABLED, 20))
            return 0;
        delay(2);
    }
    status = read32(port);
    return (status & (CONNECTED | ENABLED)) == (CONNECTED | ENABLED) ?
           (status >> 10) & 15 : 0;
}

static unsigned start_controller(uintptr_t base, uint32_t address, uint16_t pci_flags)
{
    uint32_t cap = read32(base), hcs = read32(base + 4);
    uint32_t hcs2 = read32(base + 8), hcc = read32(base + 0x10);
    unsigned ports = hcs >> 24, slots = hcs & 255, op_offset = cap & 255;
    unsigned db_offset = read32(base + 0x14) & ~3u;
    unsigned rt_offset = read32(base + 0x18) & ~31u;
    unsigned scratch_count = ((hcs2 >> 21) & 31) * 32 + (hcs2 >> 27);
    if ((cap >> 24) != 1 || op_offset < 0x20 || !ports || !slots ||
        db_offset < op_offset || db_offset > MMIO_BYTES - (slots + 1) * 4 ||
        rt_offset < op_offset || rt_offset > MMIO_BYTES - 0x40 ||
        scratch_count > SCRATCH_COUNT) {
        trace("unsupported capability/scratch count", ((uint64_t)cap << 32) | scratch_count);
        return 0;
    }
    if (!claim_controller(base, hcc)) {
        trace("firmware did not release controller", address);
        return 0;
    }

    controller->operational = base + op_offset;
    controller->doorbells = base + db_offset;
    controller->interrupter = base + rt_offset + 0x20;
    if (!wait_bits(controller->operational + USBSTS, NOT_READY, 0, 100)) {
        trace("controller not ready", read32(controller->operational + USBSTS));
        return 0;
    }
    write32(controller->operational + USBCMD, 0);
    if (!wait_bits(controller->operational + USBSTS, HALTED, HALTED, 20))
        goto stop;
    write32(controller->operational + USBCMD, RESET);
    /* Give hardware time to begin resetting before touching its MMIO.
     * Some Intel controllers require a delay even before polling. */
    delay(2);
    if (!wait_bits(controller->operational + USBCMD, RESET, 0, 100) ||
        !wait_bits(controller->operational + USBSTS, NOT_READY, 0, 100) ||
        !(read32(controller->operational + PAGESIZE) & 1))
        goto stop;

    clear(controller->dcbaa, sizeof controller->dcbaa);
    if (scratch_count) {
        controller->dcbaa[0] = (uintptr_t)controller->scratch_ptrs;
        for (unsigned i = 0; i < scratch_count; ++i)
            controller->scratch_ptrs[i] = (uintptr_t)controller->scratch[i];
    }
    context_words = (hcc & 4) ? 16 : 8;
    controller->event_next = 0;
    controller->event_cycle = 1;
    clear(controller->events, sizeof controller->events);
    ring_init(&controller->command_ring);
    controller->event_segment.base = (uintptr_t)controller->events;
    controller->event_segment.size = RING_COUNT;
    write64(controller->operational + DCBAAP, (uintptr_t)controller->dcbaa);
    write64(controller->operational + CRCR, (uintptr_t)controller->command_ring.trbs | CYCLE);
    write32(controller->interrupter + IMAN, 1);
    write32(controller->interrupter + ERSTSZ, 1);
    write64(controller->interrupter + ERDP, (uintptr_t)controller->events);
    write64(controller->interrupter + ERSTBA, (uintptr_t)&controller->event_segment);
    write32(controller->operational + CONFIG, slots);
    pci_command(address, pci_flags | 2 | 4 | (1 << 10));
    __asm__ volatile("mfence" ::: "memory");
    write32(controller->operational + USBCMD, RUN);
    if (!wait_bits(controller->operational + USBSTS, HALTED, 0, 20))
        goto stop;

    trace("controller running; root ports", ports);
    int powered = 0;
    uintptr_t first_port = controller->operational + PORTSC;
    for (unsigned i = 0; i < ports; ++i) {
        uintptr_t port = first_port + i * PORT_STRIDE;
        uint32_t status = read32(port);
        if (!(status & PORT_POWER)) {
            write32(port, (status & PORT_PRESERVE) | PORT_POWER);
            powered = 1;
        }
    }
    if (powered)
        delay(10);

    unsigned found = 0;
    controller->slots_kept = 0;
    for (unsigned i = 0; i < ports && *clock_ticks - probe_start < 400; ++i) {
        uintptr_t port = first_port + i * PORT_STRIDE;
        if (!(read32(port) & CONNECTED))
            continue;
        trace("connected root port", i + 1);
        uintptr_t protocol = port_protocol(base, hcc, i + 1);
        unsigned major = protocol ? read32(protocol) >> 24 :
                         ((read32(port) >> 10) & 15) >= 4 ? 3 : 2;
        unsigned speed_id = reset_port(port, major);
        if (!speed_id) {
            trace("port reset failed; PORTSC", read32(port));
            continue;
        }
        unsigned speed = usb_speed(protocol, speed_id, major);
        if (speed < 1 || speed > 4)
            continue;
        unsigned slot_type = protocol ? read32(protocol + 12) & 31 : 0;
        slot = 0;
        unsigned attached = enumerate(i + 1, speed_id, speed, slot_type);
        if (attached) {
            ++controller->slots_kept;
            found |= attached;
            if (inputs[KEYBOARD].slot && inputs[MOUSE].slot)
                break;
            continue;
        }
        if (slot) {
            struct trb event;
            if (!command(DISABLE_SLOT, 0, slot << 24, &event))
                break;
            controller->dcbaa[slot] = 0;
        } else {
            /* The command ring might have stopped responding. */
            break;
        }
    }
    if (found)
        return found;
stop:
    trace("stopping controller; USBSTS", read32(controller->operational + USBSTS));
    write32(controller->operational + USBCMD, 0);
    wait_bits(controller->operational + USBSTS, HALTED, HALTED, 20);
    /* A failed controller must not keep using the shared DMA buffers. */
    pci_command(address, (pci_flags | 2 | (1 << 10)) & ~4u);
    return 0;
}

static unsigned probe(void)
{
    unsigned found = 0;
    probe_start = *clock_ticks;
    for (unsigned dev = 0; dev < (256 << 8); dev += 8) {
        uint32_t device = dev << 8;
        if ((pci_read(device) & 0xffff) == 0xffff)
            continue;
        unsigned functions = (pci_read(device | 0xc) & (1 << 23)) ? 8 : 1;
        for (unsigned fn = 0; fn < functions; ++fn) {
            if (*clock_ticks - probe_start >= 400)
                goto ready;
            uint32_t address = device | (fn << 8);
            if ((pci_read(address | 8) >> 8) != 0x0c0330)
                continue;
            trace("PCI bus/device/function", (address & 0xffffff) >> 8);
            uint32_t low = pci_read(address | 0x10);
            if ((low & 7) != 0 && (low & 7) != 4)
                continue;
            uintptr_t base = low & ~15u;
            if (low & 4)
                base |= (uintptr_t)pci_read(address | 0x14) << 32;
            trace("MMIO BAR", base);
            base = map_controller(base);
            if (!base)
                continue;
            uint16_t flags = pci_read(address | 4);
            pci_command(address, flags | 2);
            controller = &controllers[controllers_kept];
            unsigned attached = start_controller(base, address, flags);
            if (!attached)
                continue;
            ++controllers_kept;
            found |= attached;
            if (found == (1u << INPUT_COUNT) - 1)
                goto ready;
        }
    }
ready:
    for (unsigned kind = 0; kind < INPUT_COUNT; ++kind)
        if (inputs[kind].slot)
            submit_report(&inputs[kind]);
    return found;
}

/* HID usage ID to console scan code. Bit 8 marks the extended keys. */
static const uint16_t scans[] = {
    [4] = 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24,
          0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14,
          0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c,
    [30] = 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    [40] = 0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a, 0x1b, 0x2b,
    [51] = 0x27, 0x28, 0x29, 0x33, 0x34, 0x35,
    [73] = 0x152, 0x147, 0x149, 0x153, 0x14f, 0x151, 0x14d, 0x14b,
           0x150, 0x148,
    [84] = 0x35, 0x37, 0x4a, 0x4e, 0x1c,
    [100] = 0x2b
};

static void decode_report(const volatile uint8_t report[8])
{
    unsigned modifiers = ((report[0] & 0x22) ? 0x200 : 0) |
                         ((report[0] & 0x11) ? 0x400 : 0);
    for (unsigned i = 2; i < 8; ++i)
        if (report[i] >= 1 && report[i] <= 3)
            return; /* Rollover. Keep the last valid set of pressed keys. */

    pending_next = 0;
    pending_count = 0;
    for (unsigned i = 2; i < 8; ++i) {
        unsigned usage = report[i];
        if (usage >= sizeof scans / sizeof scans[0] || !scans[usage])
            continue;
        int held = 0;
        for (unsigned j = 0; j < sizeof previous; ++j)
            held |= usage == previous[j];
        for (unsigned j = 2; j < i; ++j)
            held |= usage == report[j];
        if (!held)
            pending[pending_count++] = scans[usage] | modifiers;
    }
    for (unsigned i = 0; i < sizeof previous; ++i)
        previous[i] = report[i + 2];
}

static void service_events(struct controller *host)
{
    struct trb event;
    for (unsigned i = 0; i < RING_COUNT && take_event(host, &event); ++i) {
        if (event_type(&event) != TRANSFER_EVENT)
            continue;
        for (unsigned kind = 0; kind < INPUT_COUNT; ++kind) {
            struct input_device *device = &inputs[kind];
            if (device->controller != host || !device->slot ||
                (event.control >> 24) != device->slot ||
                ((event.control >> 16) & 31) != device->dci ||
                event.parameter != (uintptr_t)device->request)
                continue;
            unsigned code = event.status >> 24, remaining = event.status & 0xffffff;
            if (code != SUCCESS && code != SHORT_PACKET) {
                device->slot = 0;
                device->disconnected = 1;
            } else if (remaining < device->length) {
                device->received = device->length - remaining;
            } else {
                submit_report(device);
            }
            break;
        }
    }
}

static unsigned poll_keyboard(void)
{
    if (pending_next < pending_count)
        return pending[pending_next++];
    struct input_device *device = &inputs[KEYBOARD];
    if (device->received) {
        if (device->received == 8)
            decode_report(device->report);
        device->received = 0;
        submit_report(device);
    }
    return pending_next < pending_count ? pending[pending_next++] : 0;
}

static uint64_t poll_mouse(void)
{
    struct input_device *device = &inputs[MOUSE];
    const uint64_t valid = 1ULL << 32;
    if (device->disconnected) {
        device->disconnected = 0;
        return valid; /* release any pressed buttons */
    }
    if (!device->received)
        return 0;
    uint64_t packet = 0;
    if (mouse_report.bits) {
        if (device->received * 8 >= mouse_report.bits &&
            (!mouse_report.id || device->report[0] == mouse_report.id)) {
            packet = valid | ((uint64_t)mouse_delta(device->report, mouse_report.x) << 8) |
                     ((uint64_t)mouse_delta(device->report, mouse_report.y) << 16) |
                     ((uint64_t)mouse_delta(device->report, mouse_report.wheel) << 24);
            for (unsigned i = 0; i < 3; ++i)
                if (mouse_field(device->report, mouse_report.buttons[i]))
                    packet |= 1u << i;
        }
    } else if (device->received >= 3) {
        packet = valid | (device->report[0] & 7) |
                 ((uint64_t)device->report[1] << 8) |
                 ((uint64_t)device->report[2] << 16);
        if (device->received >= 4)
            packet |= (uint64_t)device->report[3] << 24;
    }
    device->received = 0;
    submit_report(device);
    return packet;
}

/* Boot returns an input-device mask. A null timer polls the indicated
 * input. The keyboard returns a scan code. The mouse returns buttons,
 * signed X, signed Y, optional signed wheel, and a validity bit (32). */
__attribute__((section(".text.entry")))
uint64_t xhci(volatile uint64_t *ticks, void (*log)(const char *, uintptr_t),
              unsigned kind)
{
    if (ticks) {
        clock_ticks = ticks;
        trace = log;
        return probe();
    }
    for (unsigned i = 0; i < controllers_kept; ++i)
        service_events(&controllers[i]);
    return kind == MOUSE ? poll_mouse() : poll_keyboard();
}
