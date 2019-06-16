#define A( i, j ) a[ (i)*lda + (j) ]
#define B( i, j ) b[ (i)*ldb + (j) ]
#define abs( x ) ( (x) < 0.0 ? -(x) : (x) )

#include <stdio.h>

float compare_matrices( int m, int n, float *a, int lda, float *b, int ldb )
{
  int i, j;
  float max_diff = 0.0, diff;

  for ( j=0; j<n; j++ )
    for ( i=0; i<m; i++ ){
      diff = abs( A( i,j ) - B( i,j ) );
      max_diff = ( diff > max_diff ? diff : max_diff );
      if(max_diff > 0.5f || max_diff < -0.5f) {
        printf("\n error: i %d  j %d diff %f", i, j, max_diff);
      }
    }

  return max_diff;
}

