static uint32_t borrow_usb_lock, borrow_tree_lock;
static bool bus_borrowed;
static void (*release_observer)(const char *);
void native_usb_ui_bus_observer(void (*callback)(const char *))
{
    release_observer = callback;
}
bool native_usb_ui_bus_is_leased(void)
{
    return bus_borrowed;
}
unsigned native_usb_ui_bus_lock_stage(void)
{
    return borrow_lock_stage;
}
int native_usb_ui_bus_acquire(void)
{
    unsigned mask = native_critical_enter();
    borrow_lock_stage = 0;
    int result = -60;
    if (bus_borrowed)
        goto done;
    result = -61;
    if (!resolved || !native_usb_ui_active() || !api.bsd_locks || !api.function[UI_SEM_TAKE] ||
        !api.function[UI_SEM_GIVE] || !portable_reader_platform_refresh(&mapping) ||
        !memory(api.bsd_locks, 12))
        goto done;
    uint32_t first = WORD(api.bsd_locks + 4), second = WORD(api.bsd_locks + 8);
    if (first == second || !memory(first, 40) || !memory(second, 40) ||
        WORD(first + 12) != 0x53454d41U || WORD(second + 12) != 0x53454d41U ||
        WORD(first + 24) > 1 || WORD(second + 24) > 1)
        goto done;
    borrow_lock_stage = 1;
    result = ((int (*)(uint32_t, unsigned))(uintptr_t)api.function[UI_SEM_TAKE])(first, 0);
    native_critical_enter();
    if (result)
        goto done;
    borrow_lock_stage = 2;
    result = ((int (*)(uint32_t, unsigned))(uintptr_t)api.function[UI_SEM_TAKE])(second, 0);
    native_critical_enter();
    if (result) {
        ((int (*)(uint32_t))(uintptr_t)api.function[UI_SEM_GIVE])(first);
        native_critical_enter();
        goto done;
    }
    borrow_usb_lock = first;
    borrow_tree_lock = second;
    bus_borrowed = true;
    borrow_lock_stage = 3;
done:
    native_critical_leave(mask);
    return result;
}
void native_usb_ui_bus_release(void)
{
    if (!bus_borrowed)
        return;
    unsigned mask = native_critical_enter();
    /* These objects remain owned by the existing host; the tree lock kept
     * native detach/replacement from freeing them during our schedule lease. */
    if (release_observer)
        release_observer("Native release tree lock");
    ((int (*)(uint32_t))(uintptr_t)api.function[UI_SEM_GIVE])(borrow_tree_lock);
    native_critical_enter();
    if (release_observer)
        release_observer("Native release USB lock");
    ((int (*)(uint32_t))(uintptr_t)api.function[UI_SEM_GIVE])(borrow_usb_lock);
    native_critical_enter();
    bus_borrowed = false;
    native_critical_leave(mask);
    if (release_observer)
        release_observer("Native locks released");
}

void native_usb_ui_bus_snapshot(uint32_t out[8])
{
    for (unsigned i = 0; i < 8; ++i)
        out[i] = 0;
    unsigned mask = native_critical_enter();
    UsbHostTree tree;
    uint32_t resume = 0;
    if (resolved && portable_reader_platform_refresh(&mapping) && resume_tree(&tree, &resume)) {
        out[0] = 1;
        out[1] = WORD(tree.bus + 648);
        out[2] = WORD(tree.bus + 1760);
        out[3] = WORD(tree.handle + 32);
        out[4] = WORD(tree.bus + 604);
        out[5] = WORD(tree.bus + 1772);
        out[6] = *(const volatile uint8_t *)(uintptr_t)(tree.bus + 564);
        for (unsigned slot = 1; slot < 128; ++slot) {
            uint32_t child = WORD(tree.bus + 52 + slot * 4);
            if (child && child != tree.hub)
                ++out[7];
        }
    }
    native_critical_leave(mask);
}

void native_usb_ui_bus_details(uint32_t out[16])
{
    memset(out,0,16*sizeof(*out));
    unsigned mask=native_critical_enter();
    UsbHostTree tree;
    if(resolved && !api.classic_cx && portable_reader_platform_refresh(&mapping) && host_tree(&tree)) {
        out[0]=1;
        out[1]=WORD(tree.bus+592);  /* IRQ events pending for the native worker. */
        out[2]=WORD(tree.bus+1736); /* Cached native interrupt mask. */
        out[3]=WORD(tree.bus+564)&255U;
        out[4]=WORD(tree.bus+1756)&255U;
        out[5]=WORD(tree.bus+1764);
        out[6]=WORD(tree.bus+1740);
        if(memory(out[6],80)) {
            out[7]=WORD(out[6]+76); /* DMA address of the native async head. */
            out[8]=WORD(out[6]);
            out[9]=WORD(out[6]+24);
            out[10]=WORD(out[6]+12);
            out[11]=WORD(out[6]+68); /* Software next, changed by native unlink. */
        }
        out[12]=WORD(tree.bus+1704); /* Retired/free QH list. */
        UsbRegisters r;
        if(usb_registers_for_asic(0x202U,&r) && usb_registers_valid(&r)) {
            out[13]=WORD(r.command+12);
            out[14]=WORD(r.command+24);
            out[15]=WORD(r.command);
        }
    }
    native_critical_leave(mask);
}

void native_usb_ui_bus_phy(uint32_t out[10])
{
    memset(out,0,10*sizeof(*out));
    unsigned mask=native_critical_enter();
    UsbRegisters r;
    if(resolved && !api.classic_cx && usb_registers_for_asic(0x202U,&r) && usb_registers_valid(&r)) {
        uint32_t base=r.capability;
        out[0]=1;
        out[1]=WORD(r.command);
        out[2]=WORD(r.command+4);
        out[3]=WORD(r.port);
        out[4]=WORD(r.otg);
        out[5]=WORD(base+0x40); /* MISC: bit 6 suspends the transceiver. */
        out[6]=WORD(base+0x84); /* OTG interrupt status, read without W1C. */
        out[7]=WORD(base+0x88); /* OTG interrupt enable. */
        out[8]=WORD(base+0xc0); /* Global interrupt status. */
        out[9]=WORD(base+0xc4); /* Global mask: device/OTG/host bits 0/1/2. */
    }
    native_critical_leave(mask);
}

static bool native_irq_shape(uint32_t at, const uint32_t *words, unsigned count)
{
    const PortableView *v = portable_reader_platform_view(&mapping);
    if (!at || !v->allow_span(v->context, at, count * 4, PORTABLE_CODE))
        return false;
    for (unsigned i = 0; i < count; ++i)
        if (WORD(at + i * 4) != words[i])
            return false;
    return true;
}
uint32_t native_usb_ui_bus_irq_mask(void)
{
    unsigned mask = native_critical_enter();
    uint32_t result = 0;
    UsbHostTree tree;
    if (resolved && portable_reader_platform_refresh(&mapping) && host_tree(&tree)) {
        uint32_t irq = WORD(tree.bus + 600);
        if (!memory(irq, 20) || WORD(irq) != 2 ||
            *(const volatile uint8_t *)(uintptr_t)(irq + 4) != 1)
            goto done;
        const PortableView *v = portable_reader_platform_view(&mapping);
        uint32_t registration = code_branch(api.function[UI_HCD_CONTROL] + 124);
        if (!native_usb_ui_role_matches(v, registration, UI_IRQ_REGISTER))
            goto done;
        uint32_t bind = code_branch(registration + 0x90);
        const uint32_t bind_shape[] = {0xe52de004U, 0xe24dd00cU, 0xe59de010U, 0xe1a0c000U,
                                       0xe28cc00cU, 0xe5900008U, 0xe58de000U, 0xe58dc004U};
        if (!native_irq_shape(bind, bind_shape, 8))
            goto done;
        uint32_t bridge = code_branch(bind + 0x20);
        uint32_t enable_thunk = code_branch(bridge + 0x258);
        uint32_t enable = code_branch(enable_thunk);
        if (!enable || !v->allow_span(v->context, enable, 16, PORTABLE_CODE) ||
            WORD(enable) != 0xe92d4010U || WORD(enable + 8) != 0xe8bd4010U)
            goto done;
        uint32_t selector = code_branch(enable + 4), cpu_enable = code_branch(enable + 12);
        const uint32_t selector_shape[] = {0xe3500002U, 0x959f3008U, 0x83a000ffU, 0x97d30000U,
                                           0xe12fff1eU};
        const uint32_t cpu_shape[] = {0xe59f2018U, 0xe3a03337U, 0xe592c000U, 0xe79c1100U,
                                      0xe3a00001U, 0xe1a02110U, 0xe5832010U, 0xe12fff1eU};
        if (!native_irq_shape(selector, selector_shape, 5) ||
            !native_irq_shape(cpu_enable, cpu_shape, 8) ||
            !v->allow_span(v->context, selector, 24, PORTABLE_CODE) ||
            !v->allow_span(v->context, cpu_enable, 36, PORTABLE_CODE))
            goto done;
        /* Native resource index -> logical IRQ -> physical CPU IRQ. The
         * resource index itself is NOT a DC000010 bit number. */
        uint32_t index = WORD(irq + 8), selectors = WORD(selector + 20),
                 table_global = WORD(cpu_enable + 32);
        if (index > 2 || !memory(selectors, 4) || !memory(table_global, 4))
            goto done;
        unsigned logical = *(const volatile uint8_t *)(uintptr_t)(selectors + index);
        uint32_t table = WORD(table_global);
        if (logical >= 32 || !memory(table, (logical + 1) * 4))
            goto done;
        unsigned physical = WORD(table + logical * 4);
        if (physical < 32)
            result = 1U << physical;
    }
done:
    native_critical_leave(mask);
    return result;
}

static bool route_layout_valid(void)
{
    const PortableView *v = portable_reader_platform_view(&mapping);
    uint32_t create = api.function[UI_NEW_DEVICE];
    if (!create || !native_usb_ui_role_matches(v, create, UI_NEW_DEVICE))
        return false;
    uint32_t constructor = code_branch(create + 0x78U);
    if (!constructor || !v->allow_span(v->context, constructor, 228, PORTABLE_CODE))
        return false;
    /* Authenticate the complete witnessed constructor before interpreting
     * device parent/port and inherited TT fields. Branch relocation is ignored,
     * while every field offset/instruction remains checked. */
    uint32_t hash = 2166136261U;
    for (unsigned at = 0; at < 228; at += 4) {
        uint32_t op = WORD(constructor + at);
        if ((op & 0x0e000000U) == 0x0a000000U) {
            uint32_t target = constructor + at + 8 + ((int32_t)(op << 8) >> 6);
            if ((op & 0x01000000U) || target < constructor || target >= constructor + 228)
                op &= 0xff000000U;
        }
        hash = (hash ^ op) * 16777619U;
    }
    return hash == 0xbfe48395U;
}
bool native_usb_ui_bus_route(void *unused, uint8_t address, UsbDeviceRoute *out)
{
    (void)unused;
    *out = (UsbDeviceRoute){0};
    if (!bus_borrowed || !address || address > 127)
        return false;
    unsigned mask = native_critical_enter();
    bool result = false;
    UsbHostTree tree;
    if (!portable_reader_platform_refresh(&mapping) || !host_tree(&tree) || !route_layout_valid())
        goto done;
    uint32_t device = WORD(tree.bus + 52 + address * 4);
    if (!device || device == tree.hub || !memory(device, 112) || WORD(device) != tree.bus)
        goto done;
    const volatile uint8_t *d = (const volatile uint8_t *)(uintptr_t)device;
    if (d[8] != address || d[11] < 1 || d[11] > 3 || d[64] != 18 || d[65] != 1)
        goto done;
    uint32_t parent = WORD(device + 24), port = WORD(device + 20);
    if (!memory(parent, 112) || WORD(parent) != tree.bus || !memory(port, 16) ||
        WORD(port + 8) != device || WORD(port + 12) != parent)
        goto done;
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)parent;
    if (!p[8] || p[8] > 127 || WORD(tree.bus + 52 + p[8] * 4) != parent || p[64] != 18 ||
        p[65] != 1 || p[68] != 9 || p[10] + 1 != d[10] || !d[28] || d[28] > 127)
        goto done;
    UsbDeviceRoute route = {address, d[11], p[8], d[28], 0, 0, false};
    if (d[11] != 3) {
        uint32_t translator = WORD(device + 32);
        if (translator && translator != tree.hub) {
            if (!memory(translator, 112) || WORD(translator) != tree.bus)
                goto done;
            const volatile uint8_t *tt = (const volatile uint8_t *)(uintptr_t)translator;
            if (!tt[8] || tt[8] > 127 || WORD(tree.bus + 52 + tt[8] * 4) != translator ||
                tt[11] != 3 || tt[64] != 18 || tt[65] != 1 || tt[68] != 9 || !d[36] ||
                d[36] > 127 || (tt[70] != 1 && tt[70] != 2))
                goto done;
            route.tt_hub = tt[8];
            route.tt_port = d[36];
            route.tt_multi = tt[70] == 2;
        }
    }
    *out = route;
    result = true;
done:
    native_critical_leave(mask);
    return result;
}

bool native_usb_ui_bus_device_state(void *context, uint8_t address, int interface_number,
                                    UsbDeviceState *out)
{
    *out = (UsbDeviceState){0};
    UsbDeviceRoute route;
    /* Reuse the authenticated device constructor, parent/port/TT checks and
     * live address table. No cached object is used outside the tree lease. */
    if (!native_usb_ui_bus_route(context, address, &route)) return false;
    unsigned mask = native_critical_enter();
    bool result = false;
    UsbHostTree tree;
    if (!bus_borrowed || !host_tree(&tree)) goto done;
    uint32_t device = WORD(tree.bus + 52 + address * 4);
    if (!memory(device, 112) || WORD(device) != tree.bus) goto done;
    const volatile uint8_t *d = (const volatile uint8_t *)(uintptr_t)device;
    /* Same USBdi selected-value/configuration/interface fields already used
     * by configured_storage_interface. Zero is an observed unconfigured state. */
    uint8_t selected = d[9];
    if (!selected) { result = true; goto done; }
    uint32_t config = WORD(device + 84);
    if (!memory(config, 9)) goto done;
    const volatile uint8_t *c = (const volatile uint8_t *)(uintptr_t)config;
    unsigned size = c[2] | (unsigned)c[3] << 8;
    if (c[0] != 9 || c[1] != 2 || c[5] != selected || !c[4] ||
        size < 9 || size > 4096 || !memory(config, size)) goto done;
    for (unsigned at = 9; at < size;) {
        if (size - at < 2 || c[at] < 2 || c[at] > size - at) goto done;
        if (c[at + 1] == 4 && c[at] < 9) goto done;
        at += c[at];
    }
    out->configuration = selected;
    result = true;
    if (interface_number < 0 || interface_number > 255) goto done;
    uint32_t array = WORD(device + 60);
    if (!memory(array, (unsigned)c[4] * 32)) goto done;
    for (unsigned i = 0; i < c[4]; ++i) {
        uint32_t iface = array + i * 32, descriptor = WORD(iface + 8);
        if (WORD(iface) != device) continue;
        for (unsigned at = 9; at < size; at += c[at]) {
            if (config + at != descriptor || c[at + 1] != 4 ||
                c[at + 2] != interface_number) continue;
            /* Only an unclaimed audio-streaming interface with no native
             * open pipes may substitute for a stalled GET_INTERFACE. */
            if (c[at + 5] == 1 && c[at + 6] == 2 && !WORD(iface + 4) && !WORD(iface + 28)) {
                out->alternate = c[at + 3];
                out->alternate_known = true;
            }
            goto done;
        }
    }
done:
    native_critical_leave(mask);
    return result;
}

static bool running_native_bus(void)
{
    UsbHostTree tree;
    uint32_t resume = 0;
    return portable_reader_platform_refresh(&mapping) && resume_tree(&tree, &resume) &&
           WORD(tree.bus + 604) && WORD(tree.bus + 1772) == 1 &&
           !WORD(tree.bus + 1760) && !WORD(tree.handle + 32);
}

static bool retain_bus_lease(uint32_t lock, uint32_t tree_lock)
{
    UsbRegisters r;
    if (bus_borrowed || !running_native_bus() ||
        !usb_registers_for_asic(api.classic_cx ? 0x101U : 0x202U, &r) ||
        !usb_registers_valid(&r) || (WORD(r.otg) & r.role_mask) || (WORD(r.port) & 0x85U) != 5U ||
        !(WORD(r.command) & 1U) || (WORD(r.command) & (2U | 64U)) ||
        (WORD(r.command + 4) & (0x1000U | 16U)))
        return false;
    borrow_usb_lock = lock;
    borrow_tree_lock = tree_lock;
    bus_borrowed = true;
    borrow_lock_stage = 3;
    return true;
}
