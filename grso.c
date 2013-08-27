/* hash.c     January 2011
 *
 * Groestl-512 implementation with inline assembly containing mmx and
 * sse instructions. Optimized for Opteron.
 * Authors: Krystian Matusiewicz and Soeren S. Thomsen
 *
 * This code is placed in the public domain
 */

#include "grso.h"
#include "grso-asm.h"
#include "grsotab.h"

#define DECL_GRS

#define GRS_I \
do { \
  int i; \
  /* set initial value */ \
  for (i = 0; i < grsoCOLS-1; i++) sts_grs.grsstate[i] = 0; \
  sts_grs.grsstate[grsoCOLS-1] = grsoU64BIG((u64)(8*grsoDIGESTSIZE)); \
 \
  /* set other variables */ \
  sts_grs.grsbuf_ptr = 0; \
  sts_grs.grsblock_counter = 0; \
} while (0); \

//int grsoUpdate(grsoState* ctx,
//   const unsigned char* in,
#define GRS_U \
do { \
    unsigned char* in = hash; \
  unsigned long long index = 0; \
 \
  /* if the buffer contains data that has not yet been digested, first \
     add data to buffer until full */ \
  if (sts_grs.grsbuf_ptr) { \
    while (sts_grs.grsbuf_ptr < grsoSIZE && index < 64) { \
      sts_grs.grsbuffer[(int)sts_grs.grsbuf_ptr++] = in[index++]; \
    } \
    if (sts_grs.grsbuf_ptr < grsoSIZE) continue; \
 \
    /* digest buffer */ \
    sts_grs.grsbuf_ptr = 0; \
    grsoTransform(&sts_grs, sts_grs.grsbuffer, grsoSIZE); \
  } \
 \
  /* digest bulk of message */ \
  grsoTransform(&sts_grs, in+index, 64-index); \
  index += ((64-index)/grsoSIZE)*grsoSIZE; \
 \
  /* store remaining data in buffer */ \
  while (index < 64) { \
    sts_grs.grsbuffer[(int)sts_grs.grsbuf_ptr++] = in[index++]; \
  } \
 \
} while (0);

//int grsoFinal(grsoState* ctx,
//unsigned char* out) {
#define GRS_C \
do { \
    char *out = hash; \
  int i, j = 0; \
  unsigned char *s = (unsigned char*)sts_grs.grsstate; \
 \
  sts_grs.grsbuffer[sts_grs.grsbuf_ptr++] = 0x80; \
 \
  /* pad with '0'-bits */ \
  if (sts_grs.grsbuf_ptr > grsoSIZE-grsoLENGTHFIELDLEN) { \
    /* padding requires two blocks */ \
    while (sts_grs.grsbuf_ptr < grsoSIZE) { \
      sts_grs.grsbuffer[sts_grs.grsbuf_ptr++] = 0; \
    } \
    /* digest first padding block */ \
    grsoTransform(&sts_grs, sts_grs.grsbuffer, grsoSIZE); \
    sts_grs.grsbuf_ptr = 0; \
  } \
  while (sts_grs.grsbuf_ptr < grsoSIZE-grsoLENGTHFIELDLEN) { \
    sts_grs.grsbuffer[sts_grs.grsbuf_ptr++] = 0; \
  } \
 \
  /* length padding */ \
  sts_grs.grsblock_counter++; \
  sts_grs.grsbuf_ptr = grsoSIZE; \
  while (sts_grs.grsbuf_ptr > grsoSIZE-grsoLENGTHFIELDLEN) { \
    sts_grs.grsbuffer[--sts_grs.grsbuf_ptr] = (unsigned char)sts_grs.grsblock_counter; \
    sts_grs.grsblock_counter >>= 8; \
  } \
 \
  /* digest final padding block */ \
  grsoTransform(&sts_grs, sts_grs.grsbuffer, grsoSIZE); \
  /* perform output transformation */ \
  grsoOutputTransformation(&sts_grs); \
 \
  /* store hash result in output */ \
  for (i = grsoSIZE-grsoDIGESTSIZE; i < grsoSIZE; i++,j++) { \
    out[j] = s[i]; \
  } \
 \
  /* zeroise relevant variables and deallocate memory */ \
  for (i = 0; i < grsoCOLS; i++) { \
    sts_grs.grsstate[i] = 0; \
  } \
  for (i = 0; i < grsoSIZE; i++) { \
    sts_grs.grsbuffer[i] = 0; \
  } \
} while (0); 
 
/* digest up to len bytes of input (full blocks only) */
void grsoTransform(grsoState *ctx, 
	       const unsigned char *in, 
	       unsigned long long len) {
  u64 y[grsoCOLS+2] __attribute__ ((aligned (16)));
  u64 z[grsoCOLS+2] __attribute__ ((aligned (16)));
  u64 *m, *h = (u64*)ctx->grsstate;
  int i;
  
  /* increment block counter */
  ctx->grsblock_counter += len/grsoSIZE;
  
  asm volatile ("emms");
  /* digest message, one block at a time */
  for (; len >= grsoSIZE; len -= grsoSIZE, in += grsoSIZE) {
    m = (u64*)in;
    for (i = 0; i < grsoCOLS; i++) {
      y[i] = m[i];
      z[i] = m[i] ^ h[i];
    }

    grsoQ1024ASM(y);
    grsoP1024ASM(z);

    /* h' == h + Q(m) + P(h+m) */
    for (i = 0; i < grsoCOLS; i++) {
      h[i] ^= z[i] ^ y[i];
    }
  }
  asm volatile ("emms");
}

/* given state h, do h <- P(h)+h */
void grsoOutputTransformation(grsoState *ctx) {
  u64 z[grsoCOLS] __attribute__ ((aligned (16)));
  int j;

  for (j = 0; j < grsoCOLS; j++) {
    z[j] = ctx->grsstate[j];
  }
  asm volatile ("emms");
  grsoP1024ASM(z);
  asm volatile ("emms");
  for (j = 0; j < grsoCOLS; j++) {
    ctx->grsstate[j] ^= z[j];
  }
}

/* initialise context */
int grsoInit(grsoState* ctx) {
  int i;
  /* set initial value */
  for (i = 0; i < grsoCOLS-1; i++) ctx->grsstate[i] = 0;
  ctx->grsstate[grsoCOLS-1] = grsoU64BIG((u64)(8*grsoDIGESTSIZE));

  /* set other variables */
  ctx->grsbuf_ptr = 0;
  ctx->grsblock_counter = 0;

  return 0;
}

/* update state with databitlen bits of input */
int grsoUpdate(grsoState* ctx,
	   const unsigned char* in,
	   unsigned long long len) {
  unsigned long long index = 0;

  /* if the buffer contains data that has not yet been digested, first
     add data to buffer until full */
  if (ctx->grsbuf_ptr) {
    while (ctx->grsbuf_ptr < grsoSIZE && index < len) {
      ctx->grsbuffer[(int)ctx->grsbuf_ptr++] = in[index++];
    }
    if (ctx->grsbuf_ptr < grsoSIZE) return 0;

    /* digest buffer */
    ctx->grsbuf_ptr = 0;
    grsoTransform(ctx, ctx->grsbuffer, grsoSIZE);
  }

  /* digest bulk of message */
  grsoTransform(ctx, in+index, len-index);
  index += ((len-index)/grsoSIZE)*grsoSIZE;

  /* store remaining data in buffer */
  while (index < len) {
    ctx->grsbuffer[(int)ctx->grsbuf_ptr++] = in[index++];
  }

  return 0;
}

/* finalise: process remaining data (including padding), perform
   output transformation, and write hash result to 'output' */
int grsoFinal(grsoState* ctx,
	  unsigned char* out) {
  int i, j = 0;
  unsigned char *s = (unsigned char*)ctx->grsstate;

  ctx->grsbuffer[ctx->grsbuf_ptr++] = 0x80;

  /* pad with '0'-bits */
  if (ctx->grsbuf_ptr > grsoSIZE-grsoLENGTHFIELDLEN) {
    /* padding requires two blocks */
    while (ctx->grsbuf_ptr < grsoSIZE) {
      ctx->grsbuffer[ctx->grsbuf_ptr++] = 0;
    }
    /* digest first padding block */
    grsoTransform(ctx, ctx->grsbuffer, grsoSIZE);
    ctx->grsbuf_ptr = 0;
  }
  while (ctx->grsbuf_ptr < grsoSIZE-grsoLENGTHFIELDLEN) {
    ctx->grsbuffer[ctx->grsbuf_ptr++] = 0;
  }

  /* length padding */
  ctx->grsblock_counter++;
  ctx->grsbuf_ptr = grsoSIZE;
  while (ctx->grsbuf_ptr > grsoSIZE-grsoLENGTHFIELDLEN) {
    ctx->grsbuffer[--ctx->grsbuf_ptr] = (unsigned char)ctx->grsblock_counter;
    ctx->grsblock_counter >>= 8;
  }

  /* digest final padding block */
  grsoTransform(ctx, ctx->grsbuffer, grsoSIZE);
  /* perform output transformation */
  grsoOutputTransformation(ctx);

  /* store hash result in output */
  for (i = grsoSIZE-grsoDIGESTSIZE; i < grsoSIZE; i++,j++) {
    out[j] = s[i];
  }

  /* zeroise relevant variables and deallocate memory */
  for (i = 0; i < grsoCOLS; i++) {
    ctx->grsstate[i] = 0;
  }
  for (i = 0; i < grsoSIZE; i++) {
    ctx->grsbuffer[i] = 0;
  }

  return 0;
}

/* update state with databitlen bits of input */
int grsoUpdateq(grsoState* ctx, const unsigned char* in)
{
  unsigned long long index = 0;

  /* if the buffer contains data that has not yet been digested, first
     add data to buffer until full */
  if (ctx->grsbuf_ptr) {
    while (ctx->grsbuf_ptr < grsoSIZE && index < 64) {
      ctx->grsbuffer[(int)ctx->grsbuf_ptr++] = in[index++];
    }
    if (ctx->grsbuf_ptr < grsoSIZE) return 0;

    /* digest buffer */
    ctx->grsbuf_ptr = 0;
    grsoTransform(ctx, ctx->grsbuffer, grsoSIZE);
  }

  /* digest bulk of message */
  grsoTransform(ctx, in+index, 64-index);
  index += ((64-index)/grsoSIZE)*grsoSIZE;

  /* store remaining data in buffer */
  while (index < 64) {
    ctx->grsbuffer[(int)ctx->grsbuf_ptr++] = in[index++];
  }

  return 0;
}

/* finalise: process remaining data (including padding), perform
   output transformation, and write hash result to 'output' */
int grsoFinalq(grsoState* ctx,
	  unsigned char* out) {
  int i, j = 0;
  unsigned char *s = (unsigned char*)ctx->grsstate;

  ctx->grsbuffer[ctx->grsbuf_ptr++] = 0x80;

  /* pad with '0'-bits */
  if (ctx->grsbuf_ptr > grsoSIZE-grsoLENGTHFIELDLEN) {
    /* padding requires two blocks */
    while (ctx->grsbuf_ptr < grsoSIZE) {
      ctx->grsbuffer[ctx->grsbuf_ptr++] = 0;
    }
    /* digest first padding block */
    grsoTransform(ctx, ctx->grsbuffer, grsoSIZE);
    ctx->grsbuf_ptr = 0;
  }
  while (ctx->grsbuf_ptr < grsoSIZE-grsoLENGTHFIELDLEN) {
    ctx->grsbuffer[ctx->grsbuf_ptr++] = 0;
  }

  /* length padding */
  ctx->grsblock_counter++;
  ctx->grsbuf_ptr = grsoSIZE;
  while (ctx->grsbuf_ptr > grsoSIZE-grsoLENGTHFIELDLEN) {
    ctx->grsbuffer[--ctx->grsbuf_ptr] = (unsigned char)ctx->grsblock_counter;
    ctx->grsblock_counter >>= 8;
  }

  /* digest final padding block */
  grsoTransform(ctx, ctx->grsbuffer, grsoSIZE);
  /* perform output transformation */
  grsoOutputTransformation(ctx);

  /* store hash result in output */
  for (i = grsoSIZE-grsoDIGESTSIZE; i < grsoSIZE; i++,j++) {
    out[j] = s[i];
  }

  /* zeroise relevant variables and deallocate memory */
  for (i = 0; i < grsoCOLS; i++) {
    ctx->grsstate[i] = 0;
  }
  for (i = 0; i < grsoSIZE; i++) {
    ctx->grsbuffer[i] = 0;
  }

  return 0;
}
int grsohash(unsigned char *out,
		const unsigned char *in,
		unsigned long long len) {
  int ret;
  grsoState ctx;

  /* initialise */
  if ((ret = grsoInit(&ctx)) < 0)
    return ret;

  /* process message */
  if ((ret = grsoUpdate(&ctx, in, len)) < 0)
    return ret;

  /* finalise */
  ret = grsoFinal(&ctx, out);

  return ret;
}

