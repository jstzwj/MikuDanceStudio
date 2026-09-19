void __fastcall sub_7FF7CB4C8D50(__int64 a1)
{
  int v2; // esi
  __int64 v3; // rdi
  void *v4; // rcx
  void *v5; // rcx
  void *v6; // rcx
  int v7; // esi
  __int64 v8; // rdi
  void *v9; // rcx
  void *v10; // rcx
  void *v11; // rcx
  __int64 v12; // rdi
  __int64 v13; // rsi
  void *v14; // rcx
  void *v15; // rcx
  void *v16; // rcx
  void *v17; // rcx
  void *v18; // rcx
  __int64 i; // rdi
  void *v20; // rcx
  __int64 j; // rdi
  void *v22; // rcx
  void *v23; // rcx
  void *v24; // rcx
  void *v25; // rcx
  void *v26; // rcx
  void *v27; // rcx
  void *v28; // rcx
  void *v29; // rcx
  void *v30; // rcx
  unsigned __int16 k; // si
  __int64 v32; // rdi
  void *v33; // rcx
  void *v34; // rcx
  void *v35; // rcx
  void *v36; // rcx
  void *v37; // rcx
  void *v38; // rcx
  void *v39; // rcx
  void *v40; // rcx
  void *v41; // rcx
  void *v42; // rcx
  void *v43; // rcx
  void *v44; // rcx
  void *v45; // rcx
  void *v46; // rcx
  void *v47; // rcx
  void *v48; // rcx
  void *v49; // rcx
  void *v50; // rcx
  void *v51; // rcx
  void *v52; // rcx
  void *v53; // rcx
  void *v54; // rcx
  void *v55; // rcx
  unsigned __int16 m; // di
  __int64 v57; // rsi
  void *v58; // rcx
  void *v59; // rcx
  void *v60; // rcx
  void *v61; // rcx
  void *v62; // rcx
  void *v63; // rcx
  int v64; // esi
  __int64 v65; // rdi
  void *v66; // rcx
  void *v67; // rcx
  void *v68; // rcx
  void *v69; // rcx
  void *v70; // rcx
  void *v71; // rcx
  void *v72; // rcx
  void *v73; // rcx
  void *v74; // rcx
  __int64 v75; // rcx
  __int64 v76; // rcx
  __int64 v77; // rcx
  void *v78; // rcx
  void *v79; // rcx
  void *v80; // rcx
  void *v81; // rcx

  v2 = 0; /*0x7ff7cb4c8d69*/
  if ( *(int *)(a1 + 13692) > 0 ) /*0x7ff7cb4c8d71*/
  {
    v3 = 0; /*0x7ff7cb4c8d73*/
    do /*0x7ff7cb4c8de1*/
    {
      sub_7FF7CB4260E0(*(_QWORD *)(a1 + 112), *(_DWORD *)(v3 + *(_QWORD *)(a1 + 13680) + 144)); /*0x7ff7cb4c8d87*/
      v4 = *(void **)(v3 + *(_QWORD *)(a1 + 13680) + 24); /*0x7ff7cb4c8d93*/
      if ( v4 ) /*0x7ff7cb4c8d9b*/
      {
        operator delete[](v4); /*0x7ff7cb4c8d9d*/
        *(_QWORD *)(v3 + *(_QWORD *)(a1 + 13680) + 24) = 0; /*0x7ff7cb4c8daa*/
      }
      v5 = *(void **)(v3 + *(_QWORD *)(a1 + 13680) + 32); /*0x7ff7cb4c8db6*/
      if ( v5 ) /*0x7ff7cb4c8dbe*/
      {
        operator delete[](v5); /*0x7ff7cb4c8dc0*/
        *(_QWORD *)(v3 + *(_QWORD *)(a1 + 13680) + 32) = 0; /*0x7ff7cb4c8dcd*/
      }
      ++v2; /*0x7ff7cb4c8dd2*/
      v3 += 152; /*0x7ff7cb4c8dd4*/
    }
    while ( v2 < *(_DWORD *)(a1 + 13692) ); /*0x7ff7cb4c8de1*/
  }
  v6 = *(void **)(a1 + 13680); /*0x7ff7cb4c8de3*/
  if ( v6 ) /*0x7ff7cb4c8ded*/
  {
    operator delete[](v6); /*0x7ff7cb4c8def*/
    *(_QWORD *)(a1 + 13680) = 0; /*0x7ff7cb4c8df5*/
  }
  v7 = 0; /*0x7ff7cb4c8dfc*/
  if ( *(int *)(a1 + 13688) > 0 ) /*0x7ff7cb4c8e04*/
  {
    v8 = 0; /*0x7ff7cb4c8e06*/
    do /*0x7ff7cb4c8e79*/
    {
      sub_7FF7CB426760(*(_QWORD *)(a1 + 112), *(_DWORD *)(v8 + *(_QWORD *)(a1 + 13672) + 96)); /*0x7ff7cb4c8e1f*/
      v9 = *(void **)(v8 + *(_QWORD *)(a1 + 13672) + 24); /*0x7ff7cb4c8e2b*/
      if ( v9 ) /*0x7ff7cb4c8e33*/
      {
        operator delete[](v9); /*0x7ff7cb4c8e35*/
        *(_QWORD *)(v8 + *(_QWORD *)(a1 + 13672) + 24) = 0; /*0x7ff7cb4c8e42*/
      }
      v10 = *(void **)(v8 + *(_QWORD *)(a1 + 13672) + 32); /*0x7ff7cb4c8e4e*/
      if ( v10 ) /*0x7ff7cb4c8e56*/
      {
        operator delete[](v10); /*0x7ff7cb4c8e58*/
        *(_QWORD *)(v8 + *(_QWORD *)(a1 + 13672) + 32) = 0; /*0x7ff7cb4c8e65*/
      }
      ++v7; /*0x7ff7cb4c8e6a*/
      v8 += 192; /*0x7ff7cb4c8e6c*/
    }
    while ( v7 < *(_DWORD *)(a1 + 13688) ); /*0x7ff7cb4c8e79*/
  }
  v11 = *(void **)(a1 + 13672); /*0x7ff7cb4c8e7b*/
  if ( v11 ) /*0x7ff7cb4c8e85*/
  {
    operator delete[](v11); /*0x7ff7cb4c8e87*/
    *(_QWORD *)(a1 + 13672) = 0; /*0x7ff7cb4c8e8d*/
  }
  v12 = a1 + 10168; /*0x7ff7cb4c8e94*/
  v13 = 30; /*0x7ff7cb4c8e9b*/
  do /*0x7ff7cb4c8efd*/
  {
    v14 = *(void **)(v12 + 8); /*0x7ff7cb4c8ea0*/
    if ( v14 ) /*0x7ff7cb4c8ea7*/
    {
      operator delete[](v14); /*0x7ff7cb4c8ea9*/
      *(_QWORD *)(v12 + 8) = 0; /*0x7ff7cb4c8eaf*/
    }
    if ( *(_QWORD *)v12 ) /*0x7ff7cb4c8eb3*/
    {
      operator delete[](*(void **)v12); /*0x7ff7cb4c8ebb*/
      *(_QWORD *)v12 = 0; /*0x7ff7cb4c8ec1*/
    }
    v15 = *(void **)(v12 + 1208); /*0x7ff7cb4c8ec4*/
    if ( v15 ) /*0x7ff7cb4c8ece*/
    {
      operator delete[](v15); /*0x7ff7cb4c8ed0*/
      *(_QWORD *)(v12 + 1208) = 0; /*0x7ff7cb4c8ed6*/
    }
    v16 = *(void **)(v12 + 1200); /*0x7ff7cb4c8edd*/
    if ( v16 ) /*0x7ff7cb4c8ee7*/
    {
      operator delete[](v16); /*0x7ff7cb4c8ee9*/
      *(_QWORD *)(v12 + 1200) = 0; /*0x7ff7cb4c8eef*/
    }
    v12 += 40; /*0x7ff7cb4c8ef6*/
    --v13; /*0x7ff7cb4c8efa*/
  }
  while ( v13 ); /*0x7ff7cb4c8efd*/
  v17 = *(void **)(a1 + 12624); /*0x7ff7cb4c8eff*/
  if ( v17 ) /*0x7ff7cb4c8f09*/
  {
    operator delete[](v17); /*0x7ff7cb4c8f0b*/
    *(_QWORD *)(a1 + 12624) = 0; /*0x7ff7cb4c8f11*/
  }
  v18 = *(void **)(a1 + 12616); /*0x7ff7cb4c8f18*/
  if ( v18 ) /*0x7ff7cb4c8f22*/
  {
    operator delete[](v18); /*0x7ff7cb4c8f24*/
    *(_QWORD *)(a1 + 12616) = 0; /*0x7ff7cb4c8f2a*/
  }
  if ( *(_QWORD *)(a1 + 10144) ) /*0x7ff7cb4c8f31*/
  {
    for ( i = 0; i < 40000; i += 40 ) /*0x7ff7cb4c8f3a*/
    {
      v20 = *(void **)(i + *(_QWORD *)(a1 + 10144) + 16); /*0x7ff7cb4c8f47*/
      if ( v20 ) /*0x7ff7cb4c8f4f*/
      {
        operator delete[](v20); /*0x7ff7cb4c8f51*/
        *(_QWORD *)(i + *(_QWORD *)(a1 + 10144) + 16) = 0; /*0x7ff7cb4c8f5e*/
      }
    }
    for ( j = 0; j < 40000; j += 40 ) /*0x7ff7cb4c8f70*/
    {
      v22 = *(void **)(j + *(_QWORD *)(a1 + 10144) + 32); /*0x7ff7cb4c8f87*/
      if ( v22 ) /*0x7ff7cb4c8f8f*/
      {
        operator delete[](v22); /*0x7ff7cb4c8f91*/
        *(_QWORD *)(j + *(_QWORD *)(a1 + 10144) + 32) = 0; /*0x7ff7cb4c8f9e*/
      }
    }
  }
  v23 = *(void **)(a1 + 10144); /*0x7ff7cb4c8fb0*/
  if ( v23 ) /*0x7ff7cb4c8fba*/
  {
    operator delete[](v23); /*0x7ff7cb4c8fbc*/
    *(_QWORD *)(a1 + 10144) = 0; /*0x7ff7cb4c8fc2*/
  }
  v24 = *(void **)(a1 + 10136); /*0x7ff7cb4c8fc9*/
  if ( v24 ) /*0x7ff7cb4c8fd3*/
  {
    operator delete[](v24); /*0x7ff7cb4c8fd5*/
    *(_QWORD *)(a1 + 10136) = 0; /*0x7ff7cb4c8fdb*/
  }
  v25 = *(void **)(a1 + 10128); /*0x7ff7cb4c8fe2*/
  if ( v25 ) /*0x7ff7cb4c8fec*/
  {
    operator delete[](v25); /*0x7ff7cb4c8fee*/
    *(_QWORD *)(a1 + 10128) = 0; /*0x7ff7cb4c8ff4*/
  }
  v26 = *(void **)(a1 + 12576); /*0x7ff7cb4c8ffb*/
  if ( v26 ) /*0x7ff7cb4c9005*/
  {
    operator delete[](v26); /*0x7ff7cb4c9007*/
    *(_QWORD *)(a1 + 12576) = 0; /*0x7ff7cb4c900d*/
  }
  v27 = *(void **)(a1 + 12584); /*0x7ff7cb4c9014*/
  if ( v27 ) /*0x7ff7cb4c901e*/
  {
    operator delete[](v27); /*0x7ff7cb4c9020*/
    *(_QWORD *)(a1 + 12584) = 0; /*0x7ff7cb4c9026*/
  }
  v28 = *(void **)(a1 + 10112); /*0x7ff7cb4c902d*/
  if ( v28 ) /*0x7ff7cb4c9037*/
  {
    operator delete[](v28); /*0x7ff7cb4c9039*/
    *(_QWORD *)(a1 + 10112) = 0; /*0x7ff7cb4c903f*/
  }
  v29 = *(void **)(a1 + 10096); /*0x7ff7cb4c9046*/
  if ( v29 ) /*0x7ff7cb4c9050*/
  {
    operator delete[](v29); /*0x7ff7cb4c9052*/
    *(_QWORD *)(a1 + 10096) = 0; /*0x7ff7cb4c9058*/
  }
  v30 = *(void **)(a1 + 10120); /*0x7ff7cb4c905f*/
  if ( v30 ) /*0x7ff7cb4c9069*/
  {
    operator delete[](v30); /*0x7ff7cb4c906b*/
    *(_QWORD *)(a1 + 10120) = 0; /*0x7ff7cb4c9071*/
  }
  if ( *(_QWORD *)(a1 + 10072) ) /*0x7ff7cb4c9078*/
  {
    for ( k = 0; k < *(int *)(a1 + 12556); ++k ) /*0x7ff7cb4c908e*/
    {
      v32 = 192LL * k; /*0x7ff7cb4c90ae*/
      v33 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 40); /*0x7ff7cb4c90b2*/
      if ( v33 ) /*0x7ff7cb4c90ba*/
      {
        operator delete[](v33); /*0x7ff7cb4c90bc*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 40) = 0; /*0x7ff7cb4c90c9*/
      }
      v34 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 48); /*0x7ff7cb4c90d5*/
      if ( v34 ) /*0x7ff7cb4c90dd*/
      {
        operator delete[](v34); /*0x7ff7cb4c90df*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 48) = 0; /*0x7ff7cb4c90ec*/
      }
      v35 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 104); /*0x7ff7cb4c90f8*/
      if ( v35 ) /*0x7ff7cb4c9100*/
      {
        operator delete[](v35); /*0x7ff7cb4c9102*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 104) = 0; /*0x7ff7cb4c910f*/
      }
      v36 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 128); /*0x7ff7cb4c911b*/
      if ( v36 ) /*0x7ff7cb4c9126*/
      {
        operator delete[](v36); /*0x7ff7cb4c9128*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 128) = 0; /*0x7ff7cb4c9135*/
      }
      v37 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 136); /*0x7ff7cb4c9144*/
      if ( v37 ) /*0x7ff7cb4c914f*/
      {
        operator delete[](v37); /*0x7ff7cb4c9151*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 136) = 0; /*0x7ff7cb4c915e*/
      }
      v38 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 144); /*0x7ff7cb4c916d*/
      if ( v38 ) /*0x7ff7cb4c9178*/
      {
        operator delete[](v38); /*0x7ff7cb4c917a*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 144) = 0; /*0x7ff7cb4c9187*/
      }
      v39 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 152); /*0x7ff7cb4c9196*/
      if ( v39 ) /*0x7ff7cb4c91a1*/
      {
        operator delete[](v39); /*0x7ff7cb4c91a3*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 152) = 0; /*0x7ff7cb4c91b0*/
      }
      v40 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 160); /*0x7ff7cb4c91bf*/
      if ( v40 ) /*0x7ff7cb4c91ca*/
      {
        operator delete[](v40); /*0x7ff7cb4c91cc*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 160) = 0; /*0x7ff7cb4c91d9*/
      }
      v41 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 112); /*0x7ff7cb4c91e8*/
      if ( v41 ) /*0x7ff7cb4c91f0*/
      {
        operator delete[](v41); /*0x7ff7cb4c91f2*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 112) = 0; /*0x7ff7cb4c91ff*/
      }
      v42 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 120); /*0x7ff7cb4c920b*/
      if ( v42 ) /*0x7ff7cb4c9213*/
      {
        operator delete[](v42); /*0x7ff7cb4c9215*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 120) = 0; /*0x7ff7cb4c9222*/
      }
      v43 = *(void **)(*(_QWORD *)(a1 + 10072) + v32 + 168); /*0x7ff7cb4c922e*/
      if ( v43 ) /*0x7ff7cb4c9239*/
      {
        operator delete[](v43); /*0x7ff7cb4c923b*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10072) + v32 + 168) = 0; /*0x7ff7cb4c9248*/
      }
    }
  }
  v44 = *(void **)(a1 + 8800); /*0x7ff7cb4c9262*/
  if ( v44 ) /*0x7ff7cb4c926c*/
  {
    operator delete[](v44); /*0x7ff7cb4c926e*/
    *(_QWORD *)(a1 + 8800) = 0; /*0x7ff7cb4c9274*/
  }
  v45 = *(void **)(a1 + 8824); /*0x7ff7cb4c927b*/
  if ( v45 ) /*0x7ff7cb4c9285*/
  {
    operator delete[](v45); /*0x7ff7cb4c9287*/
    *(_QWORD *)(a1 + 8824) = 0; /*0x7ff7cb4c928d*/
  }
  v46 = *(void **)(a1 + 8832); /*0x7ff7cb4c9294*/
  if ( v46 ) /*0x7ff7cb4c929e*/
  {
    operator delete[](v46); /*0x7ff7cb4c92a0*/
    *(_QWORD *)(a1 + 8832) = 0; /*0x7ff7cb4c92a6*/
  }
  v47 = *(void **)(a1 + 8840); /*0x7ff7cb4c92ad*/
  if ( v47 ) /*0x7ff7cb4c92b7*/
  {
    operator delete[](v47); /*0x7ff7cb4c92b9*/
    *(_QWORD *)(a1 + 8840) = 0; /*0x7ff7cb4c92bf*/
  }
  v48 = *(void **)(a1 + 8848); /*0x7ff7cb4c92c6*/
  if ( v48 ) /*0x7ff7cb4c92d0*/
  {
    operator delete[](v48); /*0x7ff7cb4c92d2*/
    *(_QWORD *)(a1 + 8848) = 0; /*0x7ff7cb4c92d8*/
  }
  v49 = *(void **)(a1 + 8856); /*0x7ff7cb4c92df*/
  if ( v49 ) /*0x7ff7cb4c92e9*/
  {
    operator delete[](v49); /*0x7ff7cb4c92eb*/
    *(_QWORD *)(a1 + 8856) = 0; /*0x7ff7cb4c92f1*/
  }
  v50 = *(void **)(a1 + 8808); /*0x7ff7cb4c92f8*/
  if ( v50 ) /*0x7ff7cb4c9302*/
  {
    operator delete[](v50); /*0x7ff7cb4c9304*/
    *(_QWORD *)(a1 + 8808) = 0; /*0x7ff7cb4c930a*/
  }
  v51 = *(void **)(a1 + 8816); /*0x7ff7cb4c9311*/
  if ( v51 ) /*0x7ff7cb4c931b*/
  {
    operator delete[](v51); /*0x7ff7cb4c931d*/
    *(_QWORD *)(a1 + 8816) = 0; /*0x7ff7cb4c9323*/
  }
  v52 = *(void **)(a1 + 8864); /*0x7ff7cb4c932a*/
  if ( v52 ) /*0x7ff7cb4c9334*/
  {
    operator delete[](v52); /*0x7ff7cb4c9336*/
    *(_QWORD *)(a1 + 8864) = 0; /*0x7ff7cb4c933c*/
  }
  v53 = *(void **)(a1 + 8872); /*0x7ff7cb4c9343*/
  if ( v53 ) /*0x7ff7cb4c934d*/
  {
    operator delete[](v53); /*0x7ff7cb4c934f*/
    *(_QWORD *)(a1 + 8872) = 0; /*0x7ff7cb4c9355*/
  }
  v54 = *(void **)(a1 + 8880); /*0x7ff7cb4c935c*/
  if ( v54 ) /*0x7ff7cb4c9366*/
  {
    operator delete[](v54); /*0x7ff7cb4c9368*/
    *(_QWORD *)(a1 + 8880) = 0; /*0x7ff7cb4c936e*/
  }
  v55 = *(void **)(a1 + 10072); /*0x7ff7cb4c9375*/
  if ( v55 ) /*0x7ff7cb4c937f*/
  {
    operator delete[](v55); /*0x7ff7cb4c9381*/
    *(_QWORD *)(a1 + 10072) = 0; /*0x7ff7cb4c9387*/
  }
  if ( *(_QWORD *)(a1 + 10064) ) /*0x7ff7cb4c938e*/
  {
    for ( m = 0; m < *(int *)(a1 + 12564); ++m ) /*0x7ff7cb4c93a0*/
    {
      v57 = 32LL * m; /*0x7ff7cb4c93ac*/
      v58 = *(void **)(*(_QWORD *)(a1 + 10064) + v57 + 16); /*0x7ff7cb4c93b0*/
      if ( v58 ) /*0x7ff7cb4c93b8*/
      {
        operator delete[](v58); /*0x7ff7cb4c93ba*/
        *(_QWORD *)(*(_QWORD *)(a1 + 10064) + v57 + 16) = 0; /*0x7ff7cb4c93c7*/
      }
    }
  }
  v59 = *(void **)(a1 + 10064); /*0x7ff7cb4c93da*/
  if ( v59 ) /*0x7ff7cb4c93e4*/
  {
    operator delete[](v59); /*0x7ff7cb4c93e6*/
    *(_QWORD *)(a1 + 10064) = 0; /*0x7ff7cb4c93ec*/
  }
  v60 = *(void **)(a1 + 88); /*0x7ff7cb4c93f3*/
  if ( v60 ) /*0x7ff7cb4c93fa*/
  {
    operator delete[](v60); /*0x7ff7cb4c93fc*/
    *(_QWORD *)(a1 + 88) = 0; /*0x7ff7cb4c9402*/
  }
  v61 = *(void **)(a1 + 96); /*0x7ff7cb4c9406*/
  if ( v61 ) /*0x7ff7cb4c940d*/
  {
    operator delete[](v61); /*0x7ff7cb4c940f*/
    *(_QWORD *)(a1 + 96) = 0; /*0x7ff7cb4c9415*/
  }
  v62 = *(void **)(a1 + 72); /*0x7ff7cb4c9419*/
  if ( v62 ) /*0x7ff7cb4c9420*/
  {
    operator delete[](v62); /*0x7ff7cb4c9422*/
    *(_QWORD *)(a1 + 72) = 0; /*0x7ff7cb4c9428*/
  }
  v63 = *(void **)(a1 + 80); /*0x7ff7cb4c942c*/
  if ( v63 ) /*0x7ff7cb4c9433*/
  {
    operator delete[](v63); /*0x7ff7cb4c9435*/
    *(_QWORD *)(a1 + 80) = 0; /*0x7ff7cb4c943b*/
  }
  v64 = 0; /*0x7ff7cb4c943f*/
  if ( *(int *)(a1 + 12560) > 0 ) /*0x7ff7cb4c9447*/
  {
    v65 = 0; /*0x7ff7cb4c944d*/
    do /*0x7ff7cb4c94ce*/
    {
      v66 = *(void **)(v65 + *(_QWORD *)(a1 + 10056) + 40); /*0x7ff7cb4c9457*/
      if ( v66 ) /*0x7ff7cb4c945f*/
      {
        operator delete[](v66); /*0x7ff7cb4c9461*/
        *(_QWORD *)(v65 + *(_QWORD *)(a1 + 10056) + 40) = 0; /*0x7ff7cb4c946e*/
      }
      v67 = *(void **)(v65 + *(_QWORD *)(a1 + 10056) + 48); /*0x7ff7cb4c947a*/
      if ( v67 ) /*0x7ff7cb4c9482*/
      {
        operator delete[](v67); /*0x7ff7cb4c9484*/
        *(_QWORD *)(v65 + *(_QWORD *)(a1 + 10056) + 48) = 0; /*0x7ff7cb4c9491*/
      }
      v68 = *(void **)(v65 + *(_QWORD *)(a1 + 10056) + 576); /*0x7ff7cb4c949d*/
      if ( v68 ) /*0x7ff7cb4c94a8*/
      {
        operator delete[](v68); /*0x7ff7cb4c94aa*/
        *(_QWORD *)(v65 + *(_QWORD *)(a1 + 10056) + 576) = 0; /*0x7ff7cb4c94b7*/
      }
      ++v64; /*0x7ff7cb4c94bf*/
      v65 += 624; /*0x7ff7cb4c94c1*/
    }
    while ( v64 < *(_DWORD *)(a1 + 12560) ); /*0x7ff7cb4c94ce*/
  }
  v69 = *(void **)(a1 + 10056); /*0x7ff7cb4c94d0*/
  if ( v69 ) /*0x7ff7cb4c94da*/
  {
    operator delete[](v69); /*0x7ff7cb4c94dc*/
    *(_QWORD *)(a1 + 10056) = 0; /*0x7ff7cb4c94e2*/
  }
  v70 = *(void **)(a1 + 64); /*0x7ff7cb4c94e9*/
  if ( v70 ) /*0x7ff7cb4c94f0*/
  {
    operator delete[](v70); /*0x7ff7cb4c94f2*/
    *(_QWORD *)(a1 + 64) = 0; /*0x7ff7cb4c94f8*/
  }
  v71 = *(void **)(a1 + 48); /*0x7ff7cb4c94fc*/
  if ( v71 ) /*0x7ff7cb4c9503*/
  {
    operator delete[](v71); /*0x7ff7cb4c9505*/
    *(_QWORD *)(a1 + 48) = 0; /*0x7ff7cb4c950b*/
  }
  v72 = *(void **)(a1 + 10080); /*0x7ff7cb4c950f*/
  if ( v72 ) /*0x7ff7cb4c9519*/
  {
    operator delete[](v72); /*0x7ff7cb4c951b*/
    *(_QWORD *)(a1 + 10080) = 0; /*0x7ff7cb4c9521*/
  }
  v73 = *(void **)(a1 + 10088); /*0x7ff7cb4c9528*/
  if ( v73 ) /*0x7ff7cb4c9532*/
  {
    operator delete[](v73); /*0x7ff7cb4c9534*/
    *(_QWORD *)(a1 + 10088) = 0; /*0x7ff7cb4c953a*/
  }
  v74 = *(void **)(a1 + 615536); /*0x7ff7cb4c9541*/
  if ( v74 ) /*0x7ff7cb4c954b*/
  {
    operator delete[](v74); /*0x7ff7cb4c954d*/
    *(_QWORD *)(a1 + 615536) = 0; /*0x7ff7cb4c9553*/
  }
  v75 = *(_QWORD *)(a1 + 24); /*0x7ff7cb4c955a*/
  if ( v75 ) /*0x7ff7cb4c9561*/
  {
    (*(void (__fastcall **)(__int64))(*(_QWORD *)v75 + 16LL))(v75); /*0x7ff7cb4c9566*/
    *(_QWORD *)(a1 + 24) = 0; /*0x7ff7cb4c9569*/
  }
  v76 = *(_QWORD *)(a1 + 16); /*0x7ff7cb4c956d*/
  if ( v76 ) /*0x7ff7cb4c9574*/
  {
    (*(void (__fastcall **)(__int64))(*(_QWORD *)v76 + 16LL))(v76); /*0x7ff7cb4c9579*/
    *(_QWORD *)(a1 + 16) = 0; /*0x7ff7cb4c957c*/
  }
  v77 = *(_QWORD *)(a1 + 32); /*0x7ff7cb4c9580*/
  if ( v77 ) /*0x7ff7cb4c9587*/
  {
    (*(void (__fastcall **)(__int64))(*(_QWORD *)v77 + 16LL))(v77); /*0x7ff7cb4c958c*/
    *(_QWORD *)(a1 + 32) = 0; /*0x7ff7cb4c958f*/
  }
  v78 = *(void **)(a1 + 9512); /*0x7ff7cb4c9593*/
  if ( v78 ) /*0x7ff7cb4c959d*/
  {
    operator delete[](v78); /*0x7ff7cb4c959f*/
    *(_QWORD *)(a1 + 9512) = 0; /*0x7ff7cb4c95a5*/
  }
  v79 = *(void **)(a1 + 9520); /*0x7ff7cb4c95ac*/
  if ( v79 ) /*0x7ff7cb4c95b6*/
  {
    operator delete[](v79); /*0x7ff7cb4c95b8*/
    *(_QWORD *)(a1 + 9520) = 0; /*0x7ff7cb4c95be*/
  }
  v80 = *(void **)(a1 + 9528); /*0x7ff7cb4c95c5*/
  if ( v80 ) /*0x7ff7cb4c95cf*/
  {
    operator delete[](v80); /*0x7ff7cb4c95d1*/
    *(_QWORD *)(a1 + 9528) = 0; /*0x7ff7cb4c95d7*/
  }
  v81 = *(void **)(a1 + 9536); /*0x7ff7cb4c95de*/
  if ( v81 ) /*0x7ff7cb4c95e8*/
  {
    operator delete[](v81); /*0x7ff7cb4c95ea*/
    *(_QWORD *)(a1 + 9536) = 0; /*0x7ff7cb4c95f0*/
  }
}
