int boot_thread (void) {
    /* boot up device filesystem & mount it */
    assert (thread_create_from_cpio ("fs_dev", rootserver_syscall_cap));

    /* boot up core services */
    printf ("Starting core services (VFS, device FS)...\n");
    assert (thread_create_from_cpio ("svc_vfs", rootserver_syscall_cap));
    assert (thread_create_from_cpio ("svc_dev", rootserver_syscall_cap));

    ep_addr = ut_alloc (seL4_EndpointBits);
    conditional_panic (!ep_addr, "no memory for badgemap endpoint");
    err = cspace_ut_retype_addr (ep_addr, 
                                seL4_EndpointObject, seL4_EndpointBits,
                                cur_cspace, &_badgemap_ep);
    conditional_panic (err, "failed to retype badgemap endpoint");
    assert (thread_create_internal ("fs_cpio", rootserver_syscall_cap));

    /* Network, NFS, and other devices get pulled up from svc_init */
    assert (thread_create_from_cpio ("svc_net", rootserver_syscall_cap));

    assert (thread_create_from_cpio ("fs_nfs", rootserver_syscall_cap));

    /* start any devices services inside the CPIO archive */
    dprintf (1, "Looking for device services linked into CPIO...\n");
    unsigned long size;
    char *name;
    for (int i = 0; cpio_get_entry (_cpio_archive, i, (const char**)&name, &size); i++) {
        if (strstr (name, "dev_") == name) {
            thread_create_from_cpio (name, rootserver_syscall_cap);
        }
    }

    /* finally, start the boot app */
    dprintf (1, "Starting boot application \"%s\"...\n", CONFIG_SOS_STARTUP_APP);
    thread_t boot_thread = thread_create_from_cpio (CONFIG_SOS_STARTUP_APP, rootserver_syscall_cap);
    assert (boot_thread);
    dprintf (1, "  started with PID %d\n", boot_thread->pid);
}