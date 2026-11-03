/* Normal original card functions only. No path escapes to an internal volume.
 * Original flags are deliberately not described as POSIX flags. */
static int card_path(char out[80], const char name[64]) {
    if (!fx_service_name(name, 64u, 1))
        return 0;
    memcpy(out, "A:\\FENESTRA\\", 12u);
    uint32_t n = 0u;
    while (name[n]) {
        out[12u + n] = name[n];
        ++n;
    }
    out[12u + n] = 0;
    return 1;
}
static uint32_t directory_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static FxResult extended_file_call(FxPlatformOwner *o, Resources *s, const FxServiceRequest *q,
                                   FxServiceReply *a) {
    uint64_t op = q->arg[2];
    if (op == FX_FILE_QUERY) {
        if (q->bytes)
            return FX_ARGUMENT;
        FxFileInfo info = {.abi = 1u,
                           .bytes = sizeof(info),
                           .operations = 0x1eu,
                           .max_chunk = FX_SERVICE_BYTES - sizeof(FxFileRequest),
                           .max_module_bytes = o->module_bytes,
                           .path_bytes = 64u};
        if (native(o, "camera.card.seek64"))
            info.operations |= 1u << FX_FILE_SEEK;
        info.operations |= (1u << FX_FILE_QUERY) | (1u << FX_FILE_READ_MODULE) | (1u << FX_FILE_WRITE_MODULE);
        if (native(o, "camera.card.rename"))
            info.operations |= 1u << FX_FILE_RENAME;
        if (native(o, "camera.card.directory.open") && native(o, "camera.card.directory.next") &&
            native(o, "camera.card.directory.close"))
            info.operations |= (1u << FX_FILE_DIRECTORY_OPEN) | (1u << FX_FILE_DIRECTORY_NEXT) |
                               (1u << FX_FILE_DIRECTORY_CLOSE);
        memcpy(a->data, &info, sizeof(info));
        a->bytes = sizeof(info);
        return FX_OK;
    }
    if (op == FX_FILE_RENAME) {
        if (q->bytes != sizeof(FxFileRename))
            return FX_ARGUMENT;
        FxFileRename p;
        memcpy(&p, q->data, sizeof(p));
        char from[80], to[80];
        if (!card_path(from, p.from) || !card_path(to, p.to))
            return FX_ARGUMENT;
        uintptr_t fn = native(o, "camera.card.rename");
        if (!fn)
            return FX_CAPABILITY;
        uint32_t result = ((uint32_t (*)(const char *, const char *))fn)(from, to);
        a->value[3] = result;
        return result ? FX_IO : FX_OK;
    }
    if (op == FX_FILE_DIRECTORY_OPEN) {
        if (q->bytes || q->arg[3])
            return FX_ARGUMENT;
        uintptr_t fn = native(o, "camera.card.directory.open");
        if (!fn)
            return FX_CAPABILITY;
        void *item = 0;
        uint64_t handle = 0u;
        FxResult r = resource_slot(o, &s->file, &item, &handle);
        if (r != FX_OK)
            return r;
        int32_t fd = -1;
        uint32_t result = ((uint32_t (*)(const char *, int32_t *))fn)("A:\\FENESTRA", &fd);
        if (result || fd < 0) {
            (void)fx_slot_pool_release(&s->file, handle);
            a->value[3] = result;
            return result == UINT32_C(0x80000010) ? FX_EMPTY : FX_IO;
        }
        *(File *)item = (File){.fd = fd, .directory = 1u, .id = handle};
        acquired(s->files, 0u);
        a->value[0] = handle;
        return FX_OK;
    }
    if (op == FX_FILE_DIRECTORY_NEXT || op == FX_FILE_DIRECTORY_CLOSE) {
        if (q->bytes)
            return FX_ARGUMENT;
        File *f = fx_slot_pool_get(&s->file, q->arg[3]);
        if (!f || !f->directory)
            return FX_STALE;
        if (f->uncertain)
            return FX_CLOSE;
        if (op == FX_FILE_DIRECTORY_CLOSE)
            return close_file(o, s, f, a);
        uintptr_t fn = native(o, "camera.card.directory.next");
        if (!fn)
            return FX_CAPABILITY;
        uint8_t raw[0x220] __attribute__((aligned(8))) = {0};
        uint32_t result = ((uint32_t (*)(int32_t, void *))fn)(f->fd, raw);
        a->value[3] = result;
        if (result)
            return result == UINT32_C(0x80000030) ? FX_EMPTY : FX_IO;
        FxFileDirectoryEntry entry = {.bytes = (uint64_t)directory_u32(raw + 0x218u) |
                                               (uint64_t)directory_u32(raw + 0x21cu) << 32,
                                      .attributes = raw[0x20e]};
        for (uint32_t i = 0u; i < 256u; ++i)
            entry.name[i] = (uint16_t)((uint16_t)raw[14u + 2u * i] | (uint16_t)raw[15u + 2u * i] << 8);
        if (!entry.name[0])
            for (uint32_t i = 0u; i < 13u && raw[i]; ++i)
                entry.name[i] = raw[i];
        memcpy(a->data, &entry, sizeof(entry));
        a->bytes = sizeof(entry);
        return FX_OK;
    }
    if (op != FX_FILE_READ_MODULE && op != FX_FILE_WRITE_MODULE)
        return FX_ARGUMENT;
    if (q->bytes != sizeof(FxFileModuleRequest))
        return FX_ARGUMENT;
    FxFileModuleRequest p;
    memcpy(&p, q->data, sizeof(p));
    File *f = fx_slot_pool_get(&s->file, p.handle);
    if (!f || f->directory)
        return FX_STALE;
    if (f->uncertain)
        return FX_CLOSE;
    if (p.reserved || !p.bytes)
        return FX_ARGUMENT;
    uint32_t reading = (uint32_t)(op == FX_FILE_READ_MODULE);
    if ((reading && f->mode == FX_FILE_CREATE_EXCLUSIVE) || (!reading && f->mode == FX_FILE_READ_ONLY))
        return FX_ARGUMENT;
    FxResult r = fx_platform_module_range(o, p.address, p.bytes, reading);
    if (r != FX_OK)
        return r;
    uintptr_t fn = native(o, reading ? "camera.card.read" : "camera.card.write");
    if (!fn)
        return FX_CAPABILITY;
    uint32_t done = 0u,
             status = reading ? ((uint32_t (*)(int32_t, void *, uint32_t, uint32_t *))fn)(
                                    f->fd, (void *)(uintptr_t)p.address, p.bytes, &done)
                              : ((uint32_t (*)(int32_t, const void *, uint32_t, uint32_t *, void *))fn)(
                                    f->fd, (const void *)(uintptr_t)p.address, p.bytes, &done, 0);
    a->value[0] = done;
    a->value[3] = status;
    if (done > p.bytes) {
        f->uncertain = 1u;
        fx_capacity_retain(&f->claim);
        (void)retain_object(&s->file, f->id, s->files, 0u);
        return FX_INTEGRITY;
    }
    return status ? FX_IO : FX_OK;
}
