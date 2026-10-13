/* The zero word that ends the C++ exception frame table (.eh_frame): see scripts/eh_frame_end.py,
   which links this last. */
    .section .eh_frame,"a",@progbits
    .4byte 0
