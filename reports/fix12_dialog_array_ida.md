# F12-05 original x64 accessory order evidence

MikuMikuDanceE v9.32 x64, IDA read-only session d42983fd, 2026-09-20.

## 0x7ff7cb4772c0

```cpp
INT_PTR __fastcall sub_7FF7CB4772C0(HWND a1, int a2, int a3, HWND a4)
{
  int v4; // edx
  HWND Focus; // rbx
  HWND v7; // rax
  int v8; // ebx
  HWND v9; // rax
  HWND v10; // rax
  HWND v11; // rax
  HWND v12; // rax
  WPARAM v13; // r8
  HWND v14; // rax
  int v15; // eax
  int v16; // ebx
  WPARAM v17; // rdi
  HWND v18; // rax
  HWND v19; // rax
  WPARAM v20; // rbx
  HWND v21; // rax
  HWND v22; // rax
  __int64 v23; // r11
  __int64 v24; // rcx
  int v25; // edx
  HWND v26; // rax
  int v27; // eax
  int v28; // ebx
  WPARAM v29; // rdi
  HWND v30; // rax
  HWND v31; // rax
  WPARAM v32; // rbx
  HWND v33; // rax
  HWND v34; // rax
  __int64 v35; // r11
  __int64 v36; // rcx
  int v37; // edx
  HWND v38; // rax
  HWND v39; // rax
  int i; // r12d
  HWND v41; // rax
  HWND v42; // rax
  HWND v43; // rax
  HWND v44; // rax
  HWND v45; // rcx
  HWND v46; // rax
  HWND v47; // rax
  HWND v48; // rax
  HWND v49; // rax
  HWND v50; // rax
  HWND v51; // rax
  HWND v52; // rax
  HWND v53; // rax
  HWND v54; // rax
  int v55; // eax
  WPARAM v56; // rbx
  __int64 v57; // r12
  HWND v58; // rax
  HWND v59; // rax
  HWND v60; // rax
  void *v61; // rcx
  void *v62; // rcx
  HWND v63; // rax
  int v64; // eax
  WPARAM v65; // rbx
  HWND v66; // rax
  HWND v67; // rax
  LPARAM WindowTextLengthA; // rbx
  HWND v69; // rax
  CHAR *v70; // r9
  UINT v71; // edx
  HWND v72; // rax
  int v73; // eax
  int v74; // r12d
  int v75; // r9d
  int v76; // r11d
  HWND v77; // rax
  HWND v78; // rax
  LPARAM v79; // rbx
  HWND v80; // rax
  __int64 v81; // rcx
  int v82; // edi
  HWND DlgItem; // rax
  void *v84; // rax
  bool v85; // cc
  __int64 v86; // rcx
  HWND v87; // rax
  HWND v88; // rax
  HWND v89; // rax
  CHAR String[112]; // [rsp+40h] [rbp-C0h] BYREF
  LPARAM lParam[14]; // [rsp+B0h] [rbp-50h] BYREF

  v4 = a2 - 272; /*0x7ff7cb4772e8*/
  if ( !v4 ) /*0x7ff7cb4772f7*/
  {
    v81 = qword_7FF7CB5645F8; /*0x7ff7cb477be5*/
    v82 = 0; /*0x7ff7cb477bec*/
    if ( *(_QWORD *)(qword_7FF7CB5645F8 + 663008) ) /*0x7ff7cb477bf2*/
    {
      SetWindowPos(a1, HWND_MESSAGE|0x2LL, 0, 0, 0, 0, 3u); /*0x7ff7cb477c17*/
      v81 = qword_7FF7CB5645F8; /*0x7ff7cb477c1d*/
    }
    DlgItem = GetDlgItem(*(HWND *)(v81 + 661192), 471); /*0x7ff7cb477c30*/
    dword_7FF7CB564620 = SendMessageA(DlgItem, 0x146u, 0, 0); /*0x7ff7cb477c4d*/
    v84 = operator new(saturated_mul(dword_7FF7CB564620, 4u)); /*0x7ff7cb477c62*/
    v85 = dword_7FF7CB564620 <= 0; /*0x7ff7cb477c67*/
    v86 = qword_7FF7CB5645F8; /*0x7ff7cb477c6d*/
    *(_QWORD *)(qword_7FF7CB5645F8 + 662344) = v84; /*0x7ff7cb477c74*/
    if ( !v85 ) /*0x7ff7cb477c7b*/
    {
      while ( 1 ) /*0x7ff7cb477c8f*/
      {
        v87 = GetDlgItem(*(HWND *)(v86 + 661192), 471); /*0x7ff7cb477c8f*/
        SendMessageA(v87, 0x148u, v82, (LPARAM)String); /*0x7ff7cb477ca5*/
        v88 = GetDlgItem(a1, 628); /*0x7ff7cb477cb3*/
        SendMessageA(v88, 0x180u, 0, (LPARAM)String); /*0x7ff7cb477cc9*/
        if ( ++v82 >= dword_7FF7CB564620 ) /*0x7ff7cb477cd7*/
          break; /*0x7ff7cb477cd7*/
        v86 = qword_7FF7CB5645F8; /*0x7ff7cb477cd9*/
      }
    }
    sub_7FF7CB4A96E0(); /*0x7ff7cb477ce2*/
    sprintf_s(String, 0x64u, "%d", *(_DWORD *)(qword_7FF7CB5645F8 + 662352)); /*0x7ff7cb477d06*/
    v89 = GetDlgItem(a1, 635); /*0x7ff7cb477d14*/
    SetWindowTextA(v89, String); /*0x7ff7cb477d22*/
    v12 = GetDlgItem(a1, 628); /*0x7ff7cb477d30*/
    v13 = -1; /*0x7ff7cb477d36*/
    goto LABEL_47; /*0x7ff7cb477d36*/
  }
  if ( v4 != 1 ) /*0x7ff7cb4772ff*/
    return 0; /*0x7ff7cb4772ff*/
  if ( (_WORD)a3 != 633 ) /*0x7ff7cb47730d*/
  {
    switch ( (_WORD)a3 ) /*0x7ff7cb477400*/
    {
      case 0x276: /*0x7ff7cb477400*/
        v14 = GetDlgItem(a1, 628); /*0x7ff7cb477409*/
        v15 = SendMessageA(v14, 0x188u, 0, 0); /*0x7ff7cb47741d*/
        v16 = v15; /*0x7ff7cb477423*/
        if ( v15 >= 1 ) /*0x7ff7cb477429*/
        {
          v17 = v15; /*0x7ff7cb477437*/
          v18 = GetDlgItem(a1, 628); /*0x7ff7cb47743a*/
          SendMessageA(v18, 0x189u, v17, (LPARAM)String); /*0x7ff7cb477450*/
          v19 = GetDlgItem(a1, 628); /*0x7ff7cb47745e*/
          SendMessageA(v19, 0x182u, v17, 0); /*0x7ff7cb477472*/
          v20 = v16 - 1; /*0x7ff7cb477484*/
          v21 = GetDlgItem(a1, 628); /*0x7ff7cb477487*/
          SendMessageA(v21, 0x181u, v20, (LPARAM)String); /*0x7ff7cb47749d*/
          v22 = GetDlgItem(a1, 628); /*0x7ff7cb4774ab*/
          SendMessageA(v22, 0x186u, v20, 0); /*0x7ff7cb4774bf*/
          v23 = qword_7FF7CB5645F8; /*0x7ff7cb4774c5*/
          v24 = *(_QWORD *)(qword_7FF7CB5645F8 + 662344); /*0x7ff7cb4774cc*/
          v25 = *(_DWORD *)(v24 + 4 * v17 - 4); /*0x7ff7cb4774d6*/
          *(_DWORD *)(v24 + 4 * v17 - 4) = *(_DWORD *)(v24 + 4 * v17); /*0x7ff7cb4774da*/
          *(_DWORD *)(*(_QWORD *)(v23 + 662344) + 4 * v17) = v25; /*0x7ff7cb4774e5*/
        }
        return 0; /*0x7ff7cb4774e8*/
      case 0x277: /*0x7ff7cb477400*/
        v26 = GetDlgItem(a1, 628); /*0x7ff7cb4774fe*/
        v27 = SendMessageA(v26, 0x188u, 0, 0); /*0x7ff7cb477512*/
        v28 = v27; /*0x7ff7cb477524*/
        if ( v27 != -1 && v27 < dword_7FF7CB564620 - 1 ) /*0x7ff7cb477529*/
        {
          v29 = v27; /*0x7ff7cb477543*/
          v30 = GetDlgItem(a1, 628); /*0x7ff7cb477546*/
          SendMessageA(v30, 0x189u, v29, (LPARAM)String); /*0x7ff7cb47755c*/
          v31 = GetDlgItem(a1, 628); /*0x7ff7cb47756a*/
          SendMessageA(v31, 0x182u, v29, 0); /*0x7ff7cb47757e*/
          v32 = v28 + 1; /*0x7ff7cb477590*/
          v33 = GetDlgItem(a1, 628); /*0x7ff7cb477593*/
          SendMessageA(v33, 0x181u, v32, (LPARAM)String); /*0x7ff7cb4775a9*/
          v34 = GetDlgItem(a1, 628); /*0x7ff7cb4775b7*/
          SendMessageA(v34, 0x186u, v32, 0); /*0x7ff7cb4775cb*/
          v35 = qword_7FF7CB5645F8; /*0x7ff7cb4775d1*/
          v36 = *(_QWORD *)(qword_7FF7CB5645F8 + 662344); /*0x7ff7cb4775d8*/
          v37 = *(_DWORD *)(v36 + 4 * v29 + 4); /*0x7ff7cb4775e2*/
          *(_DWORD *)(v36 + 4 * v29 + 4) = *(_DWORD *)(v36 + 4 * v29); /*0x7ff7cb4775e6*/
          *(_DWORD *)(*(_QWORD *)(v35 + 662344) + 4 * v29) = v37; /*0x7ff7cb4775f1*/
        }
        return 0; /*0x7ff7cb4775f4*/
      case 0x278: /*0x7ff7cb477400*/
        v38 = GetDlgItem(a1, 635); /*0x7ff7cb47760a*/
        GetWindowTextA(v38, String, 256); /*0x7ff7cb47761e*/
        *(_DWORD *)(qword_7FF7CB5645F8 + 662352) = atoi(String); /*0x7ff7cb477636*/
        sub_7FF7CB4A9760(a1); /*0x7ff7cb47763f*/
        v39 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 471); /*0x7ff7cb477657*/
        SendMessageA(v39, 0x14Bu, 0, 0); /*0x7ff7cb47766b*/
        for ( i = 0; i < dword_7FF7CB564620; ++i ) /*0x7ff7cb47767c*/
        {
          v41 = GetDlgItem(a1, 628); /*0x7ff7cb47768b*/
          SendMessageA(v41, 0x189u, i, (LPARAM)String); /*0x7ff7cb4776a1*/
          v42 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 471); /*0x7ff7cb4776ba*/
          SendMessageA(v42, 0x143u, 0, (LPARAM)String); /*0x7ff7cb4776d0*/
        }
        v43 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 471); /*0x7ff7cb4776f5*/
        SendMessageA(v43, 0x14Eu, 0, 0); /*0x7ff7cb477709*/
        v44 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 434); /*0x7ff7cb477722*/
        SendMessageA(v44, 0x14Bu, 0, 0); /*0x7ff7cb477736*/
        v45 = *(HWND *)(qword_7FF7CB5645F8 + 661192); /*0x7ff7cb47774f*/
        if ( *(_BYTE *)(qword_7FF7CB5645F8 + 662416) ) /*0x7ff7cb477748*/
        {
          v46 = GetDlgItem(v45, 434); /*0x7ff7cb47775c*/
          SendMessageA(v46, 0x143u, 0, (LPARAM)"camera"); /*0x7ff7cb477774*/
          v47 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 434); /*0x7ff7cb47778d*/
          SendMessageA(v47, 0x143u, 0, (LPARAM)"light"); /*0x7ff7cb4777a5*/
          v48 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 434); /*0x7ff7cb4777be*/
          SendMessageA(v48, 0x143u, 0, (LPARAM)"s shadow"); /*0x7ff7cb4777d6*/
          v49 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 434); /*0x7ff7cb4777ef*/
          SendMessageA(v49, 0x143u, 0, (LPARAM)"grav"); /*0x7ff7cb477807*/
        }
        else
        {
          v50 = GetDlgItem(v45, 434); /*0x7ff7cb477812*/
          SendMessageW(v50, 0x143u, 0, (LPARAM)&qword_7FF7CB54B5F0); /*0x7ff7cb47782a*/
          v51 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 434); /*0x7ff7cb477843*/
          SendMessageW(v51, 0x143u, 0, (LPARAM)&qword_7FF7CB54B5F8); /*0x7ff7cb47785b*/
          v52 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 434); /*0x7ff7cb477874*/
          SendMessageW(v52, 0x143u, 0, (LPARAM)&qword_7FF7CB54BAE8); /*0x7ff7cb47788c*/
          v53 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 434); /*0x7ff7cb4778a5*/
          SendMessageW(v53, 0x143u, 0, (LPARAM)&qword_7FF7CB54BADC); /*0x7ff7cb4778bd*/
        }
        v54 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 471); /*0x7ff7cb4778d6*/
        v55 = SendMessageA(v54, 0x146u, 0, 0); /*0x7ff7cb4778ea*/
        if ( v55 > 0 ) /*0x7ff7cb4778f2*/
        {
          v56 = 0; /*0x7ff7cb4778f4*/
          v57 = (unsigned int)v55; /*0x7ff7cb4778f7*/
          do /*0x7ff7cb477962*/
          {
            v58 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 471); /*0x7ff7cb477913*/
            SendMessageA(v58, 0x148u, v56, (LPARAM)lParam); /*0x7ff7cb477928*/
            v59 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 434); /*0x7ff7cb477941*/
            SendMessageA(v59, 0x143u, 0, (LPARAM)lParam); /*0x7ff7cb477956*/
            ++v56; /*0x7ff7cb47795c*/
            --v57; /*0x7ff7cb47795f*/
          }
          while ( v57 ); /*0x7ff7cb477962*/
        }
        v60 = GetDlgItem(*(HWND *)(qword_7FF7CB5645F8 + 661192), 434); /*0x7ff7cb477977*/
        SendMessageA(v60, 0x14Eu, 0, 0); /*0x7ff7cb47798b*/
        EndDialog(a1, 1); /*0x7ff7cb477999*/
        v61 = *(void **)(qword_7FF7CB5645F8 + 662344); /*0x7ff7cb4779a6*/
        if ( v61 ) /*0x7ff7cb4779b0*/
        {
          operator delete[](v61); /*0x7ff7cb4779b6*/
          *(_QWORD *)(qword_7FF7CB5645F8 + 662344) = 0; /*0x7ff7cb4779c3*/
        }
        return 0; /*0x7ff7cb4779ca*/
      case 2: /*0x7ff7cb477400*/
        EndDialog(a1, 2); /*0x7ff7cb4779da*/
        v62 = *(void **)(qword_7FF7CB5645F8 + 662344); /*0x7ff7cb4779e7*/
        if ( v62 ) /*0x7ff7cb4779f1*/
        {
          operator delete[](v62); /*0x7ff7cb4779f7*/
          *(_QWORD *)(qword_7FF7CB5645F8 + 662344) = 0; /*0x7ff7cb477a06*/
        }
        return 0; /*0x7ff7cb477a0d*/
    }
    if ( HIWORD(a3) == 1 ) /*0x7ff7cb477a1a*/
    {
      v63 = GetDlgItem(a1, 628); /*0x7ff7cb477a25*/
      v64 = SendMessageA(v63, 0x188u, 0, 0); /*0x7ff7cb477a39*/
      if ( v64 >= 0 ) /*0x7ff7cb477a41*/
      {
        v65 = v64; /*0x7ff7cb477a4f*/
        v66 = GetDlgItem(a1, 628); /*0x7ff7cb477a52*/
        SendMessageA(v66, 0x189u, v65, (LPARAM)String); /*0x7ff7cb477a68*/
        v67 = GetDlgItem(a1, 629); /*0x7ff7cb477a76*/
        WindowTextLengthA = GetWindowTextLengthA(v67); /*0x7ff7cb477a8d*/
        v69 = GetDlgItem(a1, 629); /*0x7ff7cb477a90*/
        SendMessageA(v69, 0xB1u, 0, WindowTextLengthA); /*0x7ff7cb477aa4*/
        v12 = GetDlgItem(a1, 629); /*0x7ff7cb477ab2*/
        v70 = String; /*0x7ff7cb477ab8*/
        v13 = 0; /*0x7ff7cb477abd*/
        v71 = 194; /*0x7ff7cb477ac0*/
LABEL_48:
        SendMessageA(v12, v71, v13, (LPARAM)v70); /*0x7ff7cb477d41*/
        return 0; /*0x7ff7cb477d44*/
      }
      return 0; /*0x7ff7cb477a41*/
    }
    if ( a4 != GetDlgItem(a1, 635) || HIWORD(a3) != 768 ) /*0x7ff7cb477ae6*/
      return 0; /*0x7ff7cb477aee*/
    v72 = GetDlgItem(a1, 635); /*0x7ff7cb477afc*/
    GetWindowTextA(v72, String, 256); /*0x7ff7cb477b10*/
    v73 = atoi(String); /*0x7ff7cb477b1b*/
    v74 = v73; /*0x7ff7cb477b21*/
    if ( v73 > 0 ) /*0x7ff7cb477b26*/
    {
      v76 = dword_7FF7CB564620; /*0x7ff7cb477b30*/
      if ( v73 <= dword_7FF7CB564620 ) /*0x7ff7cb477b3a*/
        goto LABEL_38; /*0x7ff7cb477b3a*/
      v74 = dword_7FF7CB564620; /*0x7ff7cb477b3c*/
      v75 = dword_7FF7CB564620; /*0x7ff7cb477b3f*/
    }
    else
    {
      v74 = 0; /*0x7ff7cb477b28*/
      v75 = 0; /*0x7ff7cb477b2b*/
    }
    sprintf_s(String, 0x64u, "%d", v75); /*0x7ff7cb477b53*/
    v77 = GetDlgItem(a1, 635); /*0x7ff7cb477b61*/
    SetWindowTextA(v77, String); /*0x7ff7cb477b6f*/
    v78 = GetDlgItem(a1, 635); /*0x7ff7cb477b7d*/
    v79 = GetWindowTextLengthA(v78); /*0x7ff7cb477b94*/
    v80 = GetDlgItem(a1, 635); /*0x7ff7cb477b97*/
    SendMessageA(v80, 0xB1u, 0, v79); /*0x7ff7cb477bab*/
    v76 = dword_7FF7CB564620; /*0x7ff7cb477bb1*/
LABEL_38:
    if ( v74 >= v76 ) /*0x7ff7cb477bc3*/
    {
      v12 = GetDlgItem(a1, 628); /*0x7ff7cb477bd6*/
      v13 = -1; /*0x7ff7cb477bdc*/
    }
    else
    {
      v12 = GetDlgItem(a1, 628); /*0x7ff7cb477bc8*/
      v13 = v74; /*0x7ff7cb477bce*/
    }
    goto LABEL_47; /*0x7ff7cb477bd1*/
  }
  Focus = GetFocus(); /*0x7ff7cb477321*/
  if ( Focus != GetDlgItem(a1, 635) ) /*0x7ff7cb47732d*/
  {
    v7 = GetDlgItem(a1, 628); /*0x7ff7cb47733b*/
    v8 = SendMessageA(v7, 0x188u, 0, 0); /*0x7ff7cb47735d*/
    v9 = GetDlgItem(a1, 629); /*0x7ff7cb477360*/
    GetWindowTextA(v9, String, 256); /*0x7ff7cb477374*/
    if ( String[0] ) /*0x7ff7cb47737f*/
    {
      if ( v8 < dword_7FF7CB564620 && v8 >= 0 ) /*0x7ff7cb477393*/
      {
        v10 = GetDlgItem(a1, 628); /*0x7ff7cb4773a4*/
        SendMessageA(v10, 0x182u, v8, 0); /*0x7ff7cb4773b8*/
        v11 = GetDlgItem(a1, 628); /*0x7ff7cb4773c6*/
        SendMessageA(v11, 0x181u, v8, (LPARAM)String); /*0x7ff7cb4773dc*/
        v12 = GetDlgItem(a1, 628); /*0x7ff7cb4773ea*/
        v13 = v8; /*0x7ff7cb4773f0*/
LABEL_47:
        v70 = 0; /*0x7ff7cb477d39*/
        v71 = 390; /*0x7ff7cb477d3c*/
        goto LABEL_48; /*0x7ff7cb477d3c*/
      }
    }
  }
  return 0; /*0x7ff7cb477d4c*/
}
```

## 0x7ff7cb4a96e0

```cpp
BOOL sub_7FF7CB4A96E0()
{
  __int64 v0; // r10
  __int64 v1; // rbx
  int v2; // r8d
  __int64 i; // r9
  int v4; // ecx
  __int64 v5; // rdx

  v0 = dword_7FF7CB564620; /*0x7ff7cb4a96e6*/
  v1 = qword_7FF7CB5645F8; /*0x7ff7cb4a96ed*/
  v2 = 0; /*0x7ff7cb4a96f4*/
  for ( i = 0; i < v0; ++v2 ) /*0x7ff7cb4a96fd*/
  {
    v4 = 0; /*0x7ff7cb4a9700*/
    v5 = v1 + 649280; /*0x7ff7cb4a9702*/
    while ( !*(_QWORD *)v5 || *(unsigned __int8 *)(*(_QWORD *)v5 + 1197LL) != v2 ) /*0x7ff7cb4a9722*/
    {
      ++v4; /*0x7ff7cb4a9724*/
      v5 += 8; /*0x7ff7cb4a9726*/
      if ( v4 >= 255 ) /*0x7ff7cb4a9730*/
        goto LABEL_8; /*0x7ff7cb4a9730*/
    }
    *(_DWORD *)(*(_QWORD *)(v1 + 662344) + 4 * i) = v4; /*0x7ff7cb4a973b*/
LABEL_8:
    ++i; /*0x7ff7cb4a973f*/
  }
  sub_7FF7CB47F7F0(v1); /*0x7ff7cb4a974d*/
  return sub_7FF7CB440CE0(v1); /*0x7ff7cb4a9755*/
}
```

## 0x7ff7cb4a9760

```cpp
BOOL __fastcall sub_7FF7CB4A9760(HWND hDlg)
{
  __int64 v1; // rbx
  int v2; // esi
  int v3; // edi
  WPARAM v5; // rbp
  HWND DlgItem; // rax
  __int64 v7; // r11
  __int64 v8; // rdx
  char lParam[112]; // [rsp+20h] [rbp-98h] BYREF

  v1 = qword_7FF7CB5645F8; /*0x7ff7cb4a977d*/
  v2 = dword_7FF7CB564620; /*0x7ff7cb4a9788*/
  v3 = 0; /*0x7ff7cb4a9792*/
  if ( dword_7FF7CB564620 > 0 ) /*0x7ff7cb4a979d*/
  {
    v5 = 0; /*0x7ff7cb4a97a7*/
    do /*0x7ff7cb4a981f*/
    {
      *(_BYTE *)(*(_QWORD *)(v1 + 8LL * *(int *)(*(_QWORD *)(v1 + 662344) + 4 * v5) + 649280) + 1197LL) = v3; /*0x7ff7cb4a97cb*/
      DlgItem = GetDlgItem(hDlg, 628); /*0x7ff7cb4a97d2*/
      SendMessageA(DlgItem, 0x189u, v5, (LPARAM)lParam); /*0x7ff7cb4a97e8*/
      strcpy_s( /*0x7ff7cb4a9812*/
        (char *)(*(_QWORD *)(v1 + 8LL * *(int *)(*(_QWORD *)(v1 + 662344) + 4 * v5) + 649280) + 584LL),
        0x64u,
        lParam);
      ++v3; /*0x7ff7cb4a9818*/
      ++v5; /*0x7ff7cb4a981a*/
    }
    while ( v3 < v2 ); /*0x7ff7cb4a981f*/
  }
  *(_BYTE *)(v1 + 651324) = **(_BYTE **)(v1 + 662344); /*0x7ff7cb4a9833*/
  sub_7FF7CB47E510(v1); /*0x7ff7cb4a983c*/
  v7 = v1 + 649280; /*0x7ff7cb4a9859*/
  v8 = 255; /*0x7ff7cb4a9860*/
  do /*0x7ff7cb4a987b*/
  {
    if ( *(_QWORD *)v7 ) /*0x7ff7cb4a9865*/
      *(_BYTE *)(*(_QWORD *)v7 + 1212LL) = 0; /*0x7ff7cb4a986d*/
    v7 += 8; /*0x7ff7cb4a9874*/
    --v8; /*0x7ff7cb4a9878*/
  }
  while ( v8 ); /*0x7ff7cb4a987b*/
  *(_BYTE *)(*(_QWORD *)(v1 + 8LL * *(unsigned __int8 *)(v1 + 651324) + 649280) + 1212LL) = 1; /*0x7ff7cb4a988f*/
  sub_7FF7CB47F7F0(v1); /*0x7ff7cb4a9896*/
  return sub_7FF7CB440CE0(v1); /*0x7ff7cb4a98a3*/
}
```
