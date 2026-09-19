# Audit 10 IDA 本次证据摘录

源文件：用户指定 MMEffect_v037x64_English/MMEffect.dll，分析会话 9e3f0f79。
摘录只作函数数据流证据，反编译器变量名/指针类型并非原源码。

## 0x18005B9E0：重置矩阵

```cpp
    sub_1800599A0(*i); /*0x18005ba46*/
    *(_DWORD *)(v3 + 240) = -1; /*0x18005ba4b*/
    *(_BYTE *)(v3 + 244) = 0; /*0x18005ba55*/
    *(_DWORD *)(v3 + 236) = 0; /*0x18005ba5c*/
    *(_QWORD *)(v3 + 300) = 0; /*0x18005ba63*/
    *(_QWORD *)(v3 + 292) = 0; /*0x18005ba6a*/
    *(_QWORD *)(v3 + 280) = 0; /*0x18005ba71*/
    *(_QWORD *)(v3 + 272) = 0; /*0x18005ba78*/
    *(_QWORD *)(v3 + 260) = 0; /*0x18005ba7f*/
    *(_QWORD *)(v3 + 252) = 0; /*0x18005ba86*/
    *(_DWORD *)(v3 + 308) = 1065353216; /*0x18005ba8d*/
    *(_DWORD *)(v3 + 288) = 1065353216; /*0x18005ba97*/
    *(_DWORD *)(v3 + 268) = 1065353216; /*0x18005baa1*/
    *(_DWORD *)(v3 + 248) = 1065353216; /*0x18005baab*/
```

## 0x180059AA0：+236 是 abs(draw order)

```cpp
void __fastcall sub_180059AA0(__int64 a1, int a2)
{
  const void *v3; // rbx
  char PmdDisp; // al
  int AcsOrder; // eax
  __int64 v6; // rdx
  _OWORD *AcsWorldMat; // rax
  __int64 v8; // rcx
  __int128 v9; // xmm1
  __int128 v10; // xmm2
  __int128 v11; // xmm3
  _BYTE v12[72]; // [rsp+20h] [rbp-48h] BYREF

  *(_DWORD *)(a1 + 240) = a2; /*0x180059aa9*/
  if ( a2 >= 0 ) /*0x180059ab1*/
  {
    if ( *(_DWORD *)(a1 + 40) == 1 ) /*0x180059ac2*/
    {
      v3 = (const void *)(a1 + 248); /*0x180059aca*/
      *(_DWORD *)(a1 + 236) = abs32(ExpGetPmdOrder((unsigned int)a2)); /*0x180059ad6*/
      *(_DWORD *)(a1 + 308) = 1065353216; /*0x180059ade*/
      *(_DWORD *)(a1 + 288) = 1065353216; /*0x180059ae5*/
      *(_DWORD *)(a1 + 268) = 1065353216; /*0x180059aec*/
      *(_DWORD *)(a1 + 248) = 1065353216; /*0x180059af3*/
      *(_QWORD *)(a1 + 300) = 0; /*0x180059af9*/
      *(_QWORD *)(a1 + 292) = 0; /*0x180059afd*/
      *(_QWORD *)(a1 + 280) = 0; /*0x180059b01*/
      *(_QWORD *)(a1 + 272) = 0; /*0x180059b05*/
      *(_QWORD *)(a1 + 260) = 0; /*0x180059b09*/
      *(_QWORD *)(a1 + 252) = 0; /*0x180059b0d*/
      PmdDisp = ExpGetPmdDisp(*(unsigned int *)(a1 + 240)); /*0x180059b17*/
    }
    else
    {
      AcsOrder = ExpGetAcsOrder((unsigned int)a2); /*0x180059b1f*/
      v6 = *(unsigned int *)(a1 + 240); /*0x180059b2f*/
      *(_DWORD *)(a1 + 236) = abs32(AcsOrder); /*0x180059b35*/
      AcsWorldMat = (_OWORD *)ExpGetAcsWorldMat(v12, v6); /*0x180059b3b*/
      v8 = *(unsigned int *)(a1 + 240); /*0x180059b41*/
      v3 = (const void *)(a1 + 248); /*0x180059b47*/
      v9 = AcsWorldMat[1]; /*0x180059b51*/
      v10 = AcsWorldMat[2]; /*0x180059b55*/
      v11 = AcsWorldMat[3]; /*0x180059b59*/
      *(_OWORD *)(a1 + 248) = *AcsWorldMat; /*0x180059b5d*/
      *(_OWORD *)(a1 + 264) = v9; /*0x180059b60*/
      *(_OWORD *)(a1 + 280) = v10; /*0x180059b64*/
      *(_OWORD *)(a1 + 296) = v11; /*0x180059b68*/
      PmdDisp = ExpGetAcsDisp(v8); /*0x180059b6c*/
    }
    *(_BYTE *)(a1 + 244) = PmdDisp; /*0x180059b82*/
    memmove((void *)(a1 + 400), v3, 0x40u); /*0x180059b88*/
  }
}
```

## 0x180057BC0：名称树选择（前半）

```cpp
  int v70; // [rsp+D8h] [rbp-9h]

  v7 = (char *)qword_1800D9BB8;
  v8 = a1;
  v63 = a1;
  v61 = a6;
  v11 = sub_180062770(Buf1);
  sub_180062620(v7 + 432, v58, v11, Buf1);
  v12 = 0;
  v13 = 0;
  if ( *(_QWORD *)v58 )
    v13 = *(_QWORD *)v58 + 40LL;
  *(_QWORD *)v58 = v13;
  v62 = 0;
  v60 = -1;
  v14 = 0;
  if ( v13 && *(_QWORD *)(v13 + 16) )
  {
    v15 = *(__int64 **)(v13 + 8);
    v16 = (__int64 *)*v15;
    if ( (__int64 *)*v15 != v15 )
    {
      do
      {
        if ( *(_DWORD *)(*(_QWORD *)(v8 + 16) + 236LL) < *(_DWORD *)(v16[4] + 236) )
          break;
        v14 = v16[4];
        if ( !*((_BYTE *)v16 + 41) )
        {
          v17 = (__int64 **)v16[2];
          if ( *((_BYTE *)v17 + 41) )
          {
            for ( i = (__int64 *)v16[1]; !*((_BYTE *)i + 41); i = (__int64 *)i[1] )
            {
              if ( v16 != (__int64 *)i[2] )
                break;
              v16 = i;
            }
            v16 = i;
          }
          else
          {
            v16 = (__int64 *)v16[2];
            for ( j = *v17; !*((_BYTE *)j + 41); j = (__int64 *)*j )
              v16 = j;
          }
        }
      }
      while ( v16 != v15 );
      if ( v14 )
      {
LABEL_64:
        v37 = v61;
        if ( !v61 )
        {
          v38 = *(_OWORD *)(v14 + 264);
          result = 16;
          v65 = *(_OWORD *)(v14 + 248);
          v66 = v38;
          v40 = *(_OWORD *)(v14 + 296);
          v67 = *(_OWORD *)(v14 + 280);
          v68 = v40;
          goto LABEL_85;
        }
        v41 = *(_QWORD *)(v8 + 16);
        if ( !*(_DWORD *)(v41 + 40) )
        {
          v42 = *((_QWORD *)v61 + 2);
          if ( *((_QWORD *)v61 + 3) < 0x10u )
            v43 = v61;
          else
            v43 = *(void **)v61;
          v44 = 14;
          if ( v42 < 0xE )
```

