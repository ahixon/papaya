#!/bin/bash -e

# @LICENSE(NICTA_CORE)

# Check usage.
if [ $# -lt 3 ]; then
    echo "Usage: $0 <output file> <symbol name> <input files> [...]"
    echo
    echo "Generate an '.o' file that contains the symbol 'symbol name'"
    echo "that contains binary data of a CPIO file containing the input"
    echo "files."
    echo
    exit 1
fi
OUTPUT_FILE=$1
SYMBOL=$2
shift 2

# Determine ".o" file to generate.
case "$PLAT" in
    "nslu2")
        FORMAT=elf32-bigarm
        ;;
    "integratorcp"|"imx31"|"imx6"|"omap3"|"am335x"|"omap4"|\
    "exynos4"|"exynos5"| "realview"| "pxa255")
        FORMAT=elf32-littlearm
        ;;
    "pc99")
        FORMAT=elf32-i386
        ;;
    *)
        echo "$0: Unknown platform \"$PLAT\""
        exit 1
        ;;
esac

# Create working directory.
# Warning: mktemp functions differently on Linux and OSX.
TEMP_DIR=`mktemp -d -t seL4XXXX`
cleanup() {
    rm -rf ${TEMP_DIR}
}
trap cleanup EXIT

# Generate an archive of the input images.
mkdir -p "${TEMP_DIR}/cpio"
for i in $@; do
    cp -f $i "${TEMP_DIR}/cpio"
done
pushd "${TEMP_DIR}/cpio" >/dev/null
ARCHIVE="${TEMP_DIR}/archive.cpio"
ls | cpio -o -H newc > ${ARCHIVE} 2>/dev/null
popd > /dev/null

# Generate a linker script.
LINK_SCRIPT="${TEMP_DIR}/linkscript.ld"
echo "SECTIONS { ._archive_cpio : { ${SYMBOL} = . ; *(.*) ; ${SYMBOL}_end = . ; } }" \
        > ${LINK_SCRIPT}

# Generate an output object file.
${TOOLPREFIX}ld -T ${LINK_SCRIPT} \
        --oformat ${FORMAT} -r -b binary ${ARCHIVE} \
        -o ${OUTPUT_FILE}

# Done
exit 0
