/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <l4/bootinfo.h>
#include <l4/kip.h>

struct bootinfo {
	long int total_size;
	char *buffer;
};

static long int
bootinfo_eof(void *handle)
{
	struct bootinfo *bootinfo = handle;
	return bootinfo->total_size;
}

static size_t
bootinfo_read(void *data, long int position, size_t count, void *handle)
{
	struct bootinfo *bootinfo = handle;
	if (position + count > bootinfo->total_size) {
		return 0;
	}
	memcpy(data, &bootinfo->buffer[position], count);
	return count;
}

static size_t
bootinfo_write(void *data, long int position, size_t count, void *handle)
{
	struct bootinfo *bootinfo = handle;
	if (position + count > bootinfo->total_size) {
		return 0;
	}
	memcpy(&bootinfo->buffer[position], data, count);
	return count;
}

static int
bootinfo_close(void *handle)
{
	struct bootinfo *bootinfo = handle;
	free(bootinfo);
	return 0;
}

/*
  Given fname, find the bootinfo module associated with it.

  Return NULL on failure 
*/
static L4_BootRec_t *
find_bootrec(const char *fname)
{
	L4_KernelInterfacePage_t *kip;
	L4_BootRec_t *rec;
	void *bootinfo;
	unsigned num_recs, objs;

	/* Iterate through bootinfo */
	kip = L4_GetKernelInterface();
	bootinfo = (void*) L4_BootInfo(kip);
	num_recs = L4_BootInfo_Entries(bootinfo);
	rec = L4_BootInfo_FirstEntry(bootinfo);

	while(num_recs > 0) {
		L4_Word_t type;
		/* find what type it is */
		type = L4_BootRec_Type(rec);
		objs = 0;
		if (type == L4_BootInfo_Module && 
		    (strcmp(L4_Module_Cmdline(rec), fname) == 0)) {
			/* Found it! */
			break;
		}
		rec = L4_BootRec_Next(rec);
		num_recs--;
	}

	if (num_recs > 0) {
		return rec;
	} else {
		return NULL;
	}
}

FILE *
fopen(const char *fname, const char *prot)
{
	L4_BootRec_t *rec;
	struct bootinfo *handle;
	FILE *newfile;


	rec = find_bootrec(fname);
	
	if (rec == NULL) {
		return NULL;
	}

	/* Note: We may want to allocate from a different pool
	   of memory to minimise this being tramped on by a user */

	newfile = malloc(sizeof(FILE));

	if (newfile == NULL) {
		return NULL;
	}

	handle = malloc(sizeof(struct bootinfo));

	if (handle == NULL) {
		free(newfile);
		return NULL;
	}

	handle->total_size = L4_Module_Size(rec);
	handle->buffer = (char*) L4_Module_Start(rec);

	newfile->handle = handle;
	newfile->write_fn = bootinfo_write;
	newfile->read_fn = bootinfo_read;
	newfile->close_fn = bootinfo_close;
	newfile->eof_fn = bootinfo_eof;
	newfile->current_pos = 0;
	newfile->buffering_mode = _IONBF;
	newfile->buffer = NULL;
	newfile->unget_pos = 0;
	newfile->eof = 0;

#ifdef THREAD_SAFE
	newfile->mutex.holder = 0;
	newfile->mutex.needed = 0;
	newfile->mutex.count = 0;
#endif

	return newfile;
}
