/* @LICENSE(NICTA_CORE) */

#include <cpio/cpio.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Align 'n' up to the value 'align', which must be a power of two. */
static unsigned long align_up(unsigned long n, unsigned long align)
{
    return (n + align - 1) & (~(align - 1));
}

/* Parse an ASCII hex string into an integer. */
static unsigned long parse_hex_str(char *s, unsigned int max_len)
{
    unsigned long r = 0;
    unsigned long i;

    for (i = 0; i < max_len; i++) {
        r *= 16;
        if (s[i] >= '0' && s[i] <= '9') {
            r += s[i] - '0';
        }  else if (s[i] >= 'a' && s[i] <= 'f') {
            r += s[i] - 'a' + 10;
        }  else if (s[i] >= 'A' && s[i] <= 'F') {
            r += s[i] - 'A' + 10;
        } else {
            return r;
        }
        continue;
    }
    return r;
}

/*
 * Compare up to 'n' characters in a string.
 *
 * We re-implement the wheel to avoid dependencies on 'libc', required for
 * certain environments that are particularly impoverished.
 */
static int cpio_strncmp(const char *a, const char *b, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return a[i] - b[i];
        }
        if (a[i] == 0) {
            return 0;
        }
    }
    return 0;
}

/*
 * Parse the header of the given CPIO entry.
 *
 * Return non-zero if the header is not valid, or is EOF.
 */
int cpio_parse_header(struct cpio_header *archive,
        const char **filename, unsigned long *filesize, void **data,
        struct cpio_header **next)
{
    /* Ensure magic header exists. */
    if (cpio_strncmp(archive->c_magic, CPIO_HEADER_MAGIC,
                sizeof(archive->c_magic)) != 0)
        return 1;

    /* Get filename and file size. */
    *filesize = parse_hex_str(archive->c_filesize, sizeof(archive->c_filesize));
    *filename = ((char *)archive) + sizeof(struct cpio_header);

    /* Ensure filename is not the trailer indicating EOF. */
    if (cpio_strncmp(*filename, CPIO_FOOTER_MAGIC, sizeof(CPIO_FOOTER_MAGIC)) == 0)
        return 1;

    /* Find offset to data. */
    unsigned long filename_length = parse_hex_str(archive->c_namesize,
            sizeof(archive->c_namesize));
    *data = (void *)align_up(((unsigned long)archive)
            + sizeof(struct cpio_header) + filename_length, CPIO_ALIGNMENT);
    *next = (struct cpio_header *)align_up(((unsigned long)*data) + *filesize, CPIO_ALIGNMENT);
    return 0;
}

/*
 * Get the location of the data in the n'th entry in the given archive file.
 *
 * We also return a pointer to the name of the file (not NUL terminated), and
 * the size of the file. The buffer 'name' is assumed to be at least
 * CPIO_FILENAME_BUFF_SIZE bytes long.
 *
 * Return NULL if the n'th entry doesn't exist.
 *
 * Runs in O(n) time.
 */
void *cpio_get_entry(void *archive, int n, const char **name, unsigned long *size)
{
    int i;
    struct cpio_header *header = archive;
    void *result = NULL;

    /* Find n'th entry. */
    for (i = 0; i <= n; i++) {
        struct cpio_header *next;
        int error = cpio_parse_header(header, name, size, &result, &next);
        if (error)
            return NULL;
        header = next;
    }

    return result;
}

/*
 * Find the location and size of the file named "name" in the given 'cpio'
 * archive.
 *
 * Return NULL if the entry doesn't exist.
 *
 * Runs in O(n) time.
 */
void *cpio_get_file(void *archive, const char *name, unsigned long *size)
{
    struct cpio_header *header = archive;

    /* Find n'th entry. */
    while (1) {
        struct cpio_header *next;
        void *result;
        const char *current_filename;

        int error = cpio_parse_header(header, &current_filename,
                size, &result, &next);
        if (error)
            return NULL;
        if (cpio_strncmp(current_filename, name, -1) == 0)
            return result;
        header = next;
    }
}

