#!/usr/bin/env bash
set -euo pipefail

xad_dir=${1:-reference_repos/XADMaster}

if [ ! -d "$xad_dir" ]; then
    echo "missing XADMaster directory: $xad_dir" >&2
    exit 1
fi

objc_runtime_flags="-DGNUSTEP -DGNUSTEP_BASE_LIBRARY=1 -DGNUSTEP_RUNTIME=1 -D_NONFRAGILE_ABI=1 -D_NATIVE_OBJC_EXCEPTIONS -fexceptions -fobjc-exceptions -fobjc-runtime=gnustep-2.2 -fblocks -fconstant-string-class=NSConstantString -I. -I/usr/lib/gcc/x86_64-linux-gnu/11/include"
libs="-Wl,--no-whole-archive -lgnustep-base -lz -lbz2 -licuuc -lobjc -lm"
ldflags="-Wl,--whole-archive -fexceptions -fobjc-runtime=gnustep-2.2 -fblocks"

library_c_files="BWT.c LZSS.c LZW.c StuffItXEnglishDictionary.c RARAudioDecoder.c RARBug.c RARVirtualMachine.c Crypto/aes_modes.c Crypto/aescrypt.c Crypto/aeskey.c Crypto/aestab.c Crypto/des.c Crypto/hmac_sha1.c Crypto/hmac_sha256.c Crypto/md5.c Crypto/pbkdf2_hmac_sha256.c Crypto/sha.c libxad/all.c libxad/clients.c libxad/unix/emulation.c libxad/unix/init.c lzma/LzmaDec.c lzma/BraIA64.c lzma/Bra86.c lzma/Bra.c lzma/Lzma2Dec.c PPMd/Context.c PPMd/RangeCoder.c PPMd/SubAllocatorBrimstone.c PPMd/SubAllocatorVariantG.c PPMd/SubAllocatorVariantH.c PPMd/SubAllocatorVariantI.c PPMd/VariantG.c PPMd/VariantH.c PPMd/VariantI.c WinZipJPEG/ArithmeticDecoder.c WinZipJPEG/Decompressor.c WinZipJPEG/JPEG.c wavpack/common_utils.c wavpack/decorr_utils.c wavpack/entropy_utils.c wavpack/open_legacy.c wavpack/open_utils.c wavpack/read_words.c wavpack/tags.c wavpack/unpack.c wavpack/unpack_floats.c wavpack/unpack_seek.c wavpack/unpack_utils.c"

make -C "$xad_dir" -f Makefile.linux \
    -o ../UniversalDetector/libUniversalDetector.a \
    OBJCC=clang \
    CC=clang \
    CXX=clang++ \
    LD=clang++ \
    GNUSTEP_OPTS="$objc_runtime_flags" \
    ALL_LDFLAGS="$ldflags" \
    LIBS="$libs" \
    LIBRARY_C_FILES="$library_c_files" \
    lsar unar
