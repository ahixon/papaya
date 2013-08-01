/* @LICENSE(NICTA_CORE) */

ENTRY(_start)

SECTIONS
{
    .text :
    {
        *(.text.start)
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(16);
        _archive_start = .;
        *(.archive_ar)
        _archive_end = .;
    }

    .data :
    {
        *(.data)
        . = ALIGN(8);
        _bootstack_bottom = .;
        . = . + 4K;
        . = ALIGN(16K);
        _bootstack_top = .;
        _boot_pd = .;
        . = . + 16K;
    }
    _end = .;
}
