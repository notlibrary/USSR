/* USB HID relative mouse input layouts. The boot report remains usable
 * when a device's report descriptor is outside the subset we support. */
enum { MOUSE_REPORT_BYTES = 64, HID_REPORTS = 8, HID_USAGES = 16 };

struct hid_field {
    unsigned bit, size;
};

struct mouse_layout {
    unsigned id, bits;
    struct hid_field x, y, wheel, buttons[3];
};

static void clear(volatile void *ptr, unsigned bytes);

struct hid_globals {
    unsigned page, size, count, id;
};

static uint32_t hid_usage(const struct hid_globals *state, uint32_t value,
                          unsigned size)
{
    return size == 4 ? value : (state->page << 16) | value;
}

static int mouse_layout_read(struct mouse_layout *layout,
                             const volatile uint8_t *data, unsigned length)
{
    struct mouse_layout reports[HID_REPORTS];
    clear(reports, sizeof reports);
    struct hid_globals state = {0}, stack[4];
    uint32_t usages[HID_USAGES], first = 0, last = 0;
    unsigned depth = 0, count = 0, report_count = 0;
    for (unsigned at = 0; at < length;) {
        unsigned prefix = data[at++];
        unsigned size = prefix & 3;
        if (size == 3)
            size = 4;
        if (prefix == 0xfe || size > length - at)
            return 0;
        uint32_t value = 0;
        for (unsigned i = 0; i < size; ++i)
            value |= (uint32_t)data[at++] << (i * 8);

        switch (prefix & 0xfc) {
        case 0x04: state.page = value; break;
        case 0x74: state.size = value; break;
        case 0x84:
            if (!value || value > 255)
                return 0;
            state.id = value;
            break;
        case 0x94: state.count = value; break;
        case 0xa4:
            if (depth == sizeof stack / sizeof stack[0])
                return 0;
            stack[depth++] = state;
            break;
        case 0xb4:
            if (!depth)
                return 0;
            state = stack[--depth];
            break;
        case 0x08:
            if (count < HID_USAGES)
                usages[count++] = hid_usage(&state, value, size);
            break;
        case 0x18: first = hid_usage(&state, value, size); break;
        case 0x28: last = hid_usage(&state, value, size); break;
        }

        if ((prefix & 12) != 0)  /* Only main items consume local usages. */
            continue;
        if ((prefix & 0xfc) == 0x80) { /* Input */
            unsigned i = 0;
            while (i < report_count && reports[i].id != state.id)
                ++i;
            if (i == HID_REPORTS || state.size > MOUSE_REPORT_BYTES * 8 ||
                state.count > MOUSE_REPORT_BYTES * 8)
                return 0;
            struct mouse_layout *report = &reports[i];
            if (i == report_count) {
                ++report_count;
                report->id = state.id;
                report->bits = state.id ? 8 : 0;
            }
            unsigned bits = state.size * state.count;
            if (bits > MOUSE_REPORT_BYTES * 8 - report->bits)
                return 0;
            if (state.size > 0 && state.size <= 16 && (value & 3) == 2) {
                for (unsigned field = 0; field < state.count; ++field) {
                    uint32_t usage = 0;
                    if (field < count)
                        usage = usages[field];
                    else if (first && first <= last && field - count <= last - first)
                        usage = first + field - count;
                    struct hid_field item = {report->bits + field * state.size, state.size};
                    if (usage >= 0x90001 && usage <= 0x90003)
                        report->buttons[usage - 0x90001] = item;
                    else if (value & 4) {
                        if (usage == 0x10030)
                            report->x = item;
                        else if (usage == 0x10031)
                            report->y = item;
                        else if (usage == 0x10038)
                            report->wheel = item;
                    }
                }
            }
            report->bits += bits;
        }
        count = first = last = 0;
    }

    for (unsigned i = 0; i < report_count; ++i) {
        if (reports[i].x.size && reports[i].y.size && reports[i].buttons[0].size &&
            (!layout->bits || reports[i].wheel.size))
            *layout = reports[i];
    }
    return layout->bits != 0;
}

static uint32_t mouse_field(const volatile uint8_t *report, struct hid_field field)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < field.size; ++i) {
        unsigned bit = field.bit + i;
        value |= (uint32_t)((report[bit / 8] >> (bit % 8)) & 1) << i;
    }
    return value;
}

static uint8_t mouse_delta(const volatile uint8_t *report, struct hid_field field)
{
    if (!field.size)
        return 0;
    int value = mouse_field(report, field);
    if (value & (1 << (field.size - 1)))
        value -= 1 << field.size;
    if (value < -127)
        value = -127;
    if (value > 127)
        value = 127;
    return (uint8_t)value;
}
