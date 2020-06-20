/*
  zip_algorithm_zstd.c -- zstd (de)compression routines
  Copyright (C) 2017-2019 Dieter Baron and Thomas Klausner

  This file is part of libzip, a library to manipulate ZIP archives.
  The authors can be contacted at <libzip@nih.at>

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.
  3. The names of the authors may not be used to endorse or promote
     products derived from this software without specific prior
     written permission.

  THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS
  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "zipint.h"

#include <zstd.h>
#include <limits.h>
#include <stdlib.h>

struct ctx {
    zip_error_t *error;
    bool compress;
    zip_uint32_t compression_flags;
    bool end_of_input;
    ZSTD_DStream *zdstream;
    ZSTD_CStream *zcstream;
    ZSTD_outBuffer out;
    ZSTD_inBuffer in;
};


static void *
allocate(bool compress, int compression_flags, zip_error_t *error, zip_uint16_t method) {
    struct ctx *ctx;

    if (compression_flags < 0) {
	zip_error_set(error, ZIP_ER_INVAL, 0);
	return NULL;
    }

    if ((ctx = (struct ctx *)malloc(sizeof(*ctx))) == NULL) {
	zip_error_set(error, ZIP_ER_MEMORY, 0);
	return NULL;
    }

    ctx->error = error;
    ctx->compress = compress;
    ctx->compression_flags = (zip_uint32_t)compression_flags;
    ctx->end_of_input = false;

    return ctx;
}


static void *
compress_allocate(zip_uint16_t method, int compression_flags, zip_error_t *error) {
    return allocate(true, compression_flags, error, method);
}


static void *
decompress_allocate(zip_uint16_t method, int compression_flags, zip_error_t *error) {
    return allocate(false, compression_flags, error, method);
}


static void
deallocate(void *ud) {
    struct ctx *ctx = (struct ctx *)ud;
    free(ctx);
}


static zip_uint16_t
general_purpose_bit_flags(void *ud) {
    /* struct ctx *ctx = (struct ctx *)ud; */
    return 0;
}

static int
map_error(size_t ret) {
    return 0;
}


static bool
start(void *ud) {
    struct ctx *ctx = (struct ctx *)ud;
    ctx->in.src = NULL;
    ctx->in.pos = 0;
    ctx->in.size = 0;
    ctx->out.dst = NULL;
    ctx->out.pos = 0;
    ctx->out.size = 0;
    if (ctx->compress) {
	ctx->zcstream = ZSTD_createCStream();
    }
    else {
	ctx->zdstream = ZSTD_createDStream();
    }

    return true;
}


static bool
end(void *ud) {
    struct ctx *ctx = (struct ctx *)ud;
    if (ctx->compress) {
	ZSTD_freeCStream(ctx->zcstream);
	ctx->zcstream = NULL;
    }
    else {
	ZSTD_freeDStream(ctx->zdstream);
	ctx->zdstream = NULL;
    }

    return true;
}


static bool
input(void *ud, zip_uint8_t *data, zip_uint64_t length) {
    struct ctx *ctx = (struct ctx *)ud;
    if (ctx->in.pos != 0 && ctx->in.pos != ctx->in.size) {
	zip_error_set(ctx->error, ZIP_ER_INVAL, 0);
	return false;
    }
    ctx->in.src = (const void *)data;
    ctx->in.size = length;
    ctx->in.pos = 0;
    return true;
}


static void
end_of_input(void *ud) {
    struct ctx *ctx = (struct ctx *)ud;

    ctx->end_of_input = true;
}


static zip_compression_status_t
process(void *ud, zip_uint8_t *data, zip_uint64_t *length) {
    struct ctx *ctx = (struct ctx *)ud;

    size_t ret;

    ctx->out.dst = data;
    ctx->out.pos = 0;
    ctx->out.size = ZIP_MIN(UINT_MAX, *length);

    if (ctx->compress) {
	ret = ZSTD_compressStream(ctx->zcstream, &ctx->out, &ctx->in);
    }
    else {
	ret = ZSTD_decompressStream(ctx->zdstream, &ctx->out, &ctx->in);
    }
    if (ZSTD_isError(ret)) {
	zip_error_set(ctx->error, ZIP_ER_ZSTD, (int)ret);
	return ZIP_COMPRESSION_ERROR;
    }
    *length = ctx->out.pos;
    if (ctx->out.pos == 0) {
	return ZIP_COMPRESSION_NEED_DATA;
    }
    if (ctx->in.size == 0) {
	return ZIP_COMPRESSION_END;
    }
    return ZIP_COMPRESSION_OK;
}

/* clang-format off */

zip_compression_algorithm_t zip_algorithm_zstd_compress = {
    compress_allocate,
    deallocate,
    general_purpose_bit_flags,
    20,
    start,
    end,
    input,
    end_of_input,
    process
};


zip_compression_algorithm_t zip_algorithm_zstd_decompress = {
    decompress_allocate,
    deallocate,
    general_purpose_bit_flags,
    20,
    start,
    end,
    input,
    end_of_input,
    process
};

/* clang-format on */