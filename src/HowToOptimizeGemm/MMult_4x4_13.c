#ifdef __ARM_NEON
#include <arm_neon.h>
#else
#error("arm neon not supported")
#endif

#include <assert.h>
#include <stdlib.h>

/* Block sizes */
#define mc 128 
#define kc 128 

/* Create macros so that the matrices are stored in row-major order */

#define A(i,j) a[ (i)*lda + (j) ]
#define B(i,j) b[ (i)*ldb + (j) ]
#define C(i,j) c[ (i)*ldc + (j) ]

#define min(i, j) ((i) < (j) ? (i): (j))

#define GEMM_N (8)  // GEMM_R
#define GEMM_M (8)  // GEMM_P
#define GEMM_K (8)  // GEMM_Q
#define GEMM_UNROLL (4)
#define KERNEL_4x4  kernel_4x4_v2

/* Routine for computing C = A * B + C */
void packB_4(int k, int n, float* from, int ldb, float* to);
void packA_4(int m, int k, float* from, int lda, float* to);
void kernel_4x4_v2(int m, int n, int k, 
        float* sa, float* sb, float* sc, int ldc);

float* fastMalloc(int size){
    void* ptr = 0;
    int iRet = posix_memalign(&ptr, 64, size * sizeof(float));
    assert(0 == iRet);
    return ptr;
}

/* Suppose that m%4==0 and n%4==0 and k%4==0, avoiding process boundary !! */
void MY_MMult(int m, int n, int k, float * restrict a, int lda,
                                   float * restrict b, int ldb,
                                   float * restrict c, int ldc )
{
    float* restrict sa = fastMalloc(m * k);
    float* restrict sb = fastMalloc(k * n);

    int ms, mms, ns, ks;
    int min_m, min_mm, min_n, min_k;
    for (ms = 0; ms < m; ms += GEMM_M) {
        min_m = m - ms;
        if (min_m > GEMM_M) {
            min_m = GEMM_M;
        }

        for (ks = 0; ks < k; ks += min_k){
            min_k = k - ks;
            if (min_k >= (GEMM_K << 1)) {
                min_k = GEMM_K;
            } else if (min_k > GEMM_K) {
                min_k = (min_k / 2 + GEMM_UNROLL - 1) & ~(GEMM_UNROLL - 1);
            }

            // first packB
            min_n = n;
            if (n >= GEMM_N * 2) {
                min_n = GEMM_N;
            } else if(n > GEMM_N) {
                min_n = (min_n / 2 + GEMM_UNROLL - 1) & ~(GEMM_UNROLL - 1);
            }
            packB_4(min_k, min_n, b + ks * ldb, ldb, sb);

            // micro kernel, split A Block to smaller Panel
            for (mms = ms; mms < ms + min_m; mms += min_mm) {
                min_mm = (ms + min_m) - mms;
                if (min_mm >= 3 * GEMM_UNROLL) {
                    min_mm = 3 * GEMM_UNROLL;
                } else if(min_mm >= 2 * GEMM_UNROLL) {
                    min_mm = 2 * GEMM_UNROLL;
                } else if(min_mm > GEMM_UNROLL) {
                    min_mm = GEMM_UNROLL;
                }

                // coninueous packA
                packA_4(min_mm, min_k, a + mms * lda + ks, lda, sa + min_k * (mms - ms));

                KERNEL_4x4(min_mm, min_n, min_k,
                    sa,
                    sb + min_k * (mms - ms),
                    c + mms * ldc, ldc);
            }

            // the first B Block has been packed, proc the others 
            for (ns = min_n; ns < n; ns += min_n) {
                min_n = n - ns;
                if (min_n >= GEMM_N * 2) {
                    min_n = GEMM_N; 
                } else if(min_n > GEMM_N) {
                    min_n = (min_n / 2 + GEMM_UNROLL - 1) & ~(GEMM_UNROLL - 1);
                }

                packB_4(min_k, min_n, b + ns + ldb * ks, ldb, sb);
                KERNEL_4x4(min_m, min_n, min_k,
                    sa,
                    sb,
                    c + ms * ldc + ns, ldc);
            }
        }
    }

    free(sa);
    free(sb);
}

/**

float* a: A
float* b: (B)T
float* c: C

C = A * (B)T

A1 A2 A3    B1 B4 B7
A4 A5 A6  x B2 B5 B8 => C1 C4 C7 C2 C5 C8 C3 C6 C9 (packed)
A7 A8 A9    B3 B4 B9

1st. calculate C1
2st. calculate C4
3st. calculate C7
...
9st. calculate C9

the output has packed.

 */

// TODO
void kernel_4x4_v2(int m, int n, int k,
    float* sa, float * sb, float* restrict c, int ldc) {
    assert(m > 0 && n > 0 && k > 0);
    assert(m % 4 == 0 && n % 4 == 0 && k % 4 == 0);

    float *restrict a = sa, *restrict b = sb;
    int i, j, l;
    for(i = 0; i < m; i += 4) {
        for(j = 0; j < n; j += 4) {
            __builtin_prefetch(b, 0, 3);
            __builtin_prefetch(a, 0, 3);

            float32x4_t v24 = {0};
            float32x4_t v25 = {0};
            float32x4_t v26 = {0};
            float32x4_t v27 = {0};
           
            for(l = 0; l < k; l += 4) {
                float32x4_t v0 = vld1q_f32(b);
                float32x4_t v16 = vld1q_f32(a);

                v24 = vmlaq_laneq_f32(v24, v0, v16, 0);
                v25 = vmlaq_laneq_f32(v25, v0, v16, 1);
                v26 = vmlaq_laneq_f32(v26, v0, v16, 2);
                v27 = vmlaq_laneq_f32(v27, v0, v16, 3);

                float32x4_t v1 = vld1q_f32(b + 4);
                float32x4_t v17 = vld1q_f32(a + 4);

                v24 = vmlaq_laneq_f32(v24, v1, v17, 0);
                v25 = vmlaq_laneq_f32(v25, v1, v17, 1);
                v26 = vmlaq_laneq_f32(v26, v1, v17, 2);
                v27 = vmlaq_laneq_f32(v27, v1, v17, 3);

                float32x4_t v2 = vld1q_f32(b + 8);
                float32x4_t v18 = vld1q_f32(a + 8);

                v24 = vmlaq_laneq_f32(v24, v2, v18, 0);
                v25 = vmlaq_laneq_f32(v25, v2, v18, 1);
                v26 = vmlaq_laneq_f32(v26, v2, v18, 2);
                v27 = vmlaq_laneq_f32(v27, v2, v18, 3);

                float32x4_t v3 = vld1q_f32(b + 12);
                float32x4_t v19 = vld1q_f32(a + 12);

                v24 = vmlaq_laneq_f32(v24, v3, v19, 0);
                v25 = vmlaq_laneq_f32(v25, v3, v19, 1);
                v26 = vmlaq_laneq_f32(v26, v3, v19, 2);
                v27 = vmlaq_laneq_f32(v27, v3, v19, 3);

                __builtin_prefetch(b+16, 0, 3);
                __builtin_prefetch(a+16, 0, 3);

                b += 16;
                a += 16;
            } // endl
            
            v24 = vaddq_f32(vld1q_f32(c), v24);
            v25 = vaddq_f32(vld1q_f32(c + ldc), v25);
            v26 = vaddq_f32(vld1q_f32(c + 2*ldc), v26);
            v27 = vaddq_f32(vld1q_f32(c + 3*ldc), v27);

            vst1q_f32(c, v24);
            vst1q_f32(c + ldc, v25);
            vst1q_f32(c + 2 * ldc, v26);
            vst1q_f32(c + 3 * ldc, v27);

            c += 4;
            a -= 4*k;
        } // endj
        c += ldc*3;
        a += 4*k;
        b = sb;
    }// endi
}

/*
            b += 4*k;
            c += 4;
            a -= 4*k;
        }//end i

        c += m*3;
        b = sb;
        a1 = a + 4*k;
        a += k*4;
    }//end j
    */

void packA_4(int m, int k, float* from, int lda, float* to) {
    assert( k != 0 && m != 0 && k % 4 == 0 && m % 4 == 0);
    int i, j;

    float *a_offset, *a_offset1, *a_offset2, *a_offset3, *a_offset4;
    float *b_offset;
    float  ctemp1,  ctemp2,  ctemp3,  ctemp4;
    float  ctemp5,  ctemp6,  ctemp7,  ctemp8;
    float  ctemp9, ctemp10, ctemp11, ctemp12;
    float ctemp13, ctemp14, ctemp15, ctemp16;

    a_offset = from;
    b_offset = to;

    j = (m >> 2);
    do{
        a_offset1  = a_offset;
        a_offset2  = a_offset1 + lda;
        a_offset3  = a_offset2 + lda;
        a_offset4  = a_offset3 + lda;
        a_offset += 4 * lda;

        i = (k >> 2);
        do{
            ctemp1  = *(a_offset1 + 0);
            ctemp2  = *(a_offset1 + 1);
            ctemp3  = *(a_offset1 + 2);
            ctemp4  = *(a_offset1 + 3);

            ctemp5  = *(a_offset2 + 0);
            ctemp6  = *(a_offset2 + 1);
            ctemp7  = *(a_offset2 + 2);
            ctemp8  = *(a_offset2 + 3);

            ctemp9  = *(a_offset3 + 0);
            ctemp10 = *(a_offset3 + 1);
            ctemp11 = *(a_offset3 + 2);
            ctemp12 = *(a_offset3 + 3);

            ctemp13 = *(a_offset4 + 0);
            ctemp14 = *(a_offset4 + 1);
            ctemp15 = *(a_offset4 + 2);
            ctemp16 = *(a_offset4 + 3);

            *(b_offset +  0) = ctemp1;
            *(b_offset +  1) = ctemp5;
            *(b_offset +  2) = ctemp9;
            *(b_offset +  3) = ctemp13;

            *(b_offset +  4) = ctemp2;
            *(b_offset +  5) = ctemp6;
            *(b_offset +  6) = ctemp10;
            *(b_offset +  7) = ctemp14;

            *(b_offset +  8) = ctemp3;
            *(b_offset +  9) = ctemp7;
            *(b_offset + 10) = ctemp11;
            *(b_offset + 11) = ctemp15;

            *(b_offset + 12) = ctemp4;
            *(b_offset + 13) = ctemp8;
            *(b_offset + 14) = ctemp12;
            *(b_offset + 15) = ctemp16;

            a_offset1 += 4;
            a_offset2 += 4;
            a_offset3 += 4;
            a_offset4 += 4;

            b_offset += 16;
            i --;
        }while(i > 0);
        j --;
    }while(j > 0);
}

/* suppose that k and n is mutiple of 4 */
void packB_4(int k, int n, float* from, int ldb, float* to) {
    assert( k != 0 && n != 0 && k % 4 == 0 && n % 4 == 0);

    int i, j;

    float *a_offset, *a_offset1, *a_offset2, *a_offset3, *a_offset4;
    float *b_offset, *b_offset1;
    float  ctemp1,  ctemp2,  ctemp3,  ctemp4;
    float  ctemp5,  ctemp6,  ctemp7,  ctemp8;
    float  ctemp9, ctemp10, ctemp11, ctemp12;
    float ctemp13, ctemp14, ctemp15, ctemp16;
    a_offset   = from;
    b_offset   = to;

    j = (k >> 2);
    do{
        a_offset1  = a_offset;
        a_offset2  = a_offset1 + ldb;
        a_offset3  = a_offset2 + ldb;
        a_offset4  = a_offset3 + ldb;
        a_offset  += 4 * ldb;

        b_offset1  = b_offset;
        b_offset  += 16;

        i = (n >> 2);
        do{
            ctemp1  = *(a_offset1 + 0);
            ctemp2  = *(a_offset1 + 1);
            ctemp3  = *(a_offset1 + 2);
            ctemp4  = *(a_offset1 + 3);

            ctemp5  = *(a_offset2 + 0);
            ctemp6  = *(a_offset2 + 1);
            ctemp7  = *(a_offset2 + 2);
            ctemp8  = *(a_offset2 + 3);

            ctemp9  = *(a_offset3 + 0);
            ctemp10 = *(a_offset3 + 1);
            ctemp11 = *(a_offset3 + 2);
            ctemp12 = *(a_offset3 + 3);

            ctemp13 = *(a_offset4 + 0);
            ctemp14 = *(a_offset4 + 1);
            ctemp15 = *(a_offset4 + 2);
            ctemp16 = *(a_offset4 + 3);

            a_offset1 += 4;
            a_offset2 += 4;
            a_offset3 += 4;
            a_offset4 += 4;

            *(b_offset1 +  0) = ctemp1;
            *(b_offset1 +  1) = ctemp2;
            *(b_offset1 +  2) = ctemp3;
            *(b_offset1 +  3) = ctemp4;

            *(b_offset1 +  4) = ctemp5;
            *(b_offset1 +  5) = ctemp6;
            *(b_offset1 +  6) = ctemp7;
            *(b_offset1 +  7) = ctemp8;

            *(b_offset1 +  8) = ctemp9;
            *(b_offset1 +  9) = ctemp10;
            *(b_offset1 + 10) = ctemp11;
            *(b_offset1 + 11) = ctemp12;

            *(b_offset1 + 12) = ctemp13;
            *(b_offset1 + 13) = ctemp14;
            *(b_offset1 + 14) = ctemp15;
            *(b_offset1 + 15) = ctemp16;

            b_offset1 += n * 4;
            i --;
        }while(i > 0);
        j --;
    }while(j > 0);
}
