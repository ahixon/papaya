#include <pawpaw.h>

void _Exit (int status) {
    pawpaw_suicide ();

    /* ensure this never returns */
    for (;;) { }
}
