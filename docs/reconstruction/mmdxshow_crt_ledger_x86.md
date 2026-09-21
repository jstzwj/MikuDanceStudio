# Historical mmdxshow crt ledger x86

Archived on 2026-09-22 from `src/mmdxshow/crt_ledger.cpp`. The original analysis date is not recorded.

These are historical **32-bit x86** reconstruction notes. Their absolute addresses, object offsets, source line numbers, phase labels and coverage verdicts describe the earlier analysis, not a current x64 equivalence proof. The original binary hash was not recorded here. Claims such as "fully covered", "verbatim", and "still to be ported" below are retained as historical quotations and require fresh verification against the current source and target binary. No executable code was present in the source translation unit.

## Archived notes

```text
=============================================================================
MMDxShow.dll — Phase A: CRT / Phase-B adjudication ledger (crt_ledger.cpp)
=============================================================================
Comment-only file: nothing in here is compiled.  It records, for every
function in the original MMDxShow.dll (image base 0x10000000), whether it is
  (a) replaced by the modern C/C++ runtime / compiler-generated, or
  (b) real DirectShow code still to be ported (Phase B scope), or
  (c) already ported in Phase A (dll_main.cpp / enumerators.cpp).

Boundary verification (done by decompiling each candidate):
  * 0x10006130 is the body of __CxxFrameHandler3 (0x1000615C is a 6-byte
    jmp thunk into it).  Everything from 0x10006130 to the end of .text
    (0x10006D52) is VC8 CRT / compiler support.  There are NO real DShow
    functions above 0x100060C0 (the CEnumPins pin-list destructor).
  * Inside 0x10005000..0x100060C0 every function was decompiled and all of
    them are real DLL-support code (class factory, registry, exports,
    module locks, ole32 shim, string helpers, pin-list helpers) — no CRT
    helpers hide in that range.  (memset/memcpy/operator new/delete etc.
    live at 0x100061BA+, i.e. inside the CRT block.)

Corpus references are to translated/MMDxShow/fcn_1000XXXX.cpp.
=============================================================================

-----------------------------------------------------------------------------
[A] Phase A — implemented in src/mmdxshow/ (dll_main.cpp / enumerators.cpp)
-----------------------------------------------------------------------------
0x10001000  IsEqualGUID16 (nonzero == equal)                     dll_main.cpp
0x10001BF0  DllRegisterServer -> AMovieDllRegisterServer2(TRUE)  dll_main.cpp
0x10001C00  DllUnregisterServer -> AMovieDllRegisterServer2(FALSE) dll_main.cpp
0x10001C10  DllMain thunk -> 0x10005930                          dll_main.cpp
0x10002AD0  ~CEnumPins                                          enumerators.cpp
0x10002B40  CEnumPins::QueryInterface                           enumerators.cpp
0x10002BA0  CEnumPins::AddRef                                   enumerators.cpp
0x10002BC0  CEnumPins::Release                                  enumerators.cpp
0x10002BF0  CEnumPins::Reset                                    enumerators.cpp
0x10002C30  CEnumPins refresh (re-cache version/count/pos)      enumerators.cpp
0x10002C60  ~CEnumMediaTypes                                    enumerators.cpp
0x10002C80  CEnumMediaTypes::QueryInterface                     enumerators.cpp
0x10002CE0  CEnumMediaTypes::AddRef                             enumerators.cpp
0x10002D00  CEnumMediaTypes::Release                            enumerators.cpp
0x10002D30  CEnumMediaTypes::Reset                              enumerators.cpp
0x10003400  register/unregister via IFilterMapper (v1)          dll_main.cpp
0x10003B00  CEnumPins scalar deleting dtor                      enumerators.cpp
0x10003B20  CEnumPins::Next                                     enumerators.cpp
0x10003C20  CEnumPins::Skip                                     enumerators.cpp
0x10003C70  CEnumMediaTypes scalar deleting dtor                enumerators.cpp
0x10003C90  CEnumMediaTypes::Next                               enumerators.cpp
0x10003E10  CEnumMediaTypes::Skip                               enumerators.cpp
0x10004420  CEnumPins ctor                                      enumerators.cpp
0x100044F0  CEnumPins::Clone                                    enumerators.cpp
0x100045A0  CEnumMediaTypes ctor                                enumerators.cpp
0x10004600  CEnumMediaTypes::Clone                              enumerators.cpp
0x10004B80  InitMediaType (CMediaType default ctor body)        dll_main.cpp
0x10004D00  CopyMediaType                                       dll_main.cpp
0x10004D70  FreeMediaType                                       dll_main.cpp
0x10004DB0  ~CMediaType wrapper -> FreeMediaType                dll_main.cpp
0x10004DC0  CMediaType ctor wrapper -> InitMediaType            dll_main.cpp
0x10004F80  LockModule (InterlockedIncrement g_cModuleRef)      dll_main.cpp
0x10004FA0  UnlockModule (+ FreeLibrary ole32 shim)             dll_main.cpp
0x10004FD0  QI tail: *ppv=this, AddRef, S_OK                    dll_main.cpp
0x10005240  recursive registry key delete                       dll_main.cpp
0x10005330  write HKCR CLSID / InprocServer32 / ThreadingModel  dll_main.cpp
0x10005550  remove HKCR CLSID key                               dll_main.cpp
0x100055F0  register/unregister via IFilterMapper2              dll_main.cpp
0x10005680  per-template CLSID key loop                         dll_main.cpp
0x10005720  AMovieDllRegisterServer2                            dll_main.cpp
0x100058F0  per-template m_lpfnInit loop                        dll_main.cpp
0x10005930  DllMain body                                        dll_main.cpp
0x100058C0  CClassFactory::AddRef                               dll_main.cpp
0x100058D0  CClassFactory::LockServer                           dll_main.cpp
0x100059A0  CClassFactory::QueryInterface                       dll_main.cpp
0x10005A00  CClassFactory::CreateInstance                       dll_main.cpp
0x10005AB0  DllCanUnloadNow                                     dll_main.cpp
0x10005AD0  CClassFactory ctor                                  dll_main.cpp
0x10005B00  CClassFactory::Release                              dll_main.cpp
0x10005B30  DllGetClassObject                                   dll_main.cpp
0x10005F00  pin-list head getter (inlined into ListCopy)        enumerators.cpp
0x10005F30  pin-list init                                       enumerators.cpp
0x10005F50  pin-list free-active                                enumerators.cpp
0x10005F90  pin-list pop-front value (inlined into ListCopy)    enumerators.cpp
0x10005FB0  pin node value getter (inlined into ListFind)       enumerators.cpp
0x10005FD0  pin-list find                                       enumerators.cpp
0x10006010  pin-list push-back                                  enumerators.cpp
0x10006070  pin-list copy                                       enumerators.cpp
0x100060C0  pin-list destroy                                    enumerators.cpp

-----------------------------------------------------------------------------
[B] Phase B — real DirectShow code in 0x10001000..0x10005000 (+ helpers)
            (one-line role each; vtable targets marked with their slot)
-----------------------------------------------------------------------------
0x100010D0  DIB/format helper used by the pin's media-type builder
0x10001180  IBaseFilter/IQualityControl vtable QI stub (E_NOINTERFACE fwd)
0x100011A0  ...AddRef/Release forwarding stub (outer-unknown delegation)
0x100011C0  QueryInternalConnections stub (E_NOTIMPL)
0x100011D0  returns 0/S_OK/NULL (xor eax,eax;ret) - CBaseFilter::GetSetupData
            (primary +0x20) and the CAMThread root +0x0C/+0x10/+0x14 hooks
            "GetSetupData" slot (always NULL => Register/Unregister are
            effective no-ops) and CAMThread +0x0C/0x10/0x14 stubs
0x100011E0  IQualityControl::SetSink stub
0x100011F0  CPushPinDIBSq root+0x1C GetMediaType(CMediaType*) entry
0x10001310  CBasePin::DecideBufferSize (pin primary +0x3C)
0x10001430  CPushPinDIBSq::CheckMediaType (builds default mt, compares)
0x100014E0  CPushPinDIBSq::FillBuffer (root+0x08)
0x10001670  CPushSourceDIBSq destructor body (0x10001B50 thunk target)
0x10001710  IPin vtable +0x04 AddRef forwarding stub
0x10001730  IPushSource::SetBitmapInfo (BIH + int + fps->AvgTimePerFrame)
0x100017D0  IPushSource::GetStreamingState
0x10001800  IPushSource::StartStreaming
0x10001840  IPushSource::BeginStreaming
0x10001880  IPushSource::GetRate (writes 1.02)
0x10001900  IAMovieSetup vtable +0x04 AddRef stub
0x10001910  IPushSource vtable +0x04 AddRef stub
0x10001920  IAMovieSetup/IPushSource vtable +0x00 QI stub
0x10001930  CPushSourceDIBSq IPushSource::QueryInterface
0x10001940  CPushSourceDIBSq IPushSource::Release
0x10001950  CPushPinDIBSq scalar deleting dtor (pin primary +0x0C)
0x10001960  CPushPinDIBSq helper (streaming state machine)
0x10001A50  CPushPinDIBSq::ThreadProc (root+0x04)
0x10001A70  CPushSourceDIBSq constructor (0x78-byte object)
0x10001B50  CPushSourceDIBSq scalar deleting dtor (filter primary +0x0C)
0x10001B70  lpfnNew: new(0x78)+ctor, sets *phr — MMDxShow_NewPushSourceDIBSq
0x10001C20  CBaseFilter helper (query info / state)
0x10001CA0  IBaseFilter::FindPin
0x10001D00  CBasePin helper
0x10001D40  IPin::QueryId
0x10001DC0  CBasePin helper
0x10001E50  CSourceStream scalar deleting dtor (pin primary +0x0C)
0x10001E60  CSourceStream primary +0x0C-slot helper (Active/Inactive family)
0x10001E90  CBasePin primary helper
0x10001F20  CSource/CBaseFilter helper
0x10001FC0  CBasePin helper
0x10002090  CSource::GetPinCount (filter primary +0x18)
0x100020B0  CSource::GetPin (filter primary +0x1C; returns m_ppPins[n]+0x48)
0x10002110  CBaseFilter helper
0x100021C0  CSourceStream::ThreadProc (root+0x04)
0x100021E0  CSourceStream::CheckMediaType (primary +0x20)
0x100022B0  CBasePin::GetMediaType(int,pmt) (primary +0x34; delegates
            iPosition==0 to root+0x1C single-arg GetMediaType)
0x10002370  CBasePin::Active (primary +0x14)
0x100024B0  CBasePin::Inactive (primary +0x18)
0x10002570  CAMThread family: scalar deleting dtor (root+0x00)
0x10002630  CAMThread::DoBufferProcessingLoop (root+0x18)
0x10002740  IAMovieSetup vtable +0x08 Release stub
0x10002750  CSource scalar deleting dtor (filter primary +0x0C)
0x10002790  CSource::NonDelegatingQueryInterface (primary +0x00)
0x100028B0  CBaseFilter::StreamTime (primary +0x10)
0x100028F0  IBaseFilter::QueryFilterInfo
0x10002890  IBaseFilter::GetState stub
0x10002950  IBaseFilter::JoinFilterGraph
0x10002A60  IBaseFilter::QueryVendorInfo
0x10002A70  CSource::NonDelegatingRelease family
0x10002AB0  CSource::GetPinVersion (filter primary +0x14)
0x10002B62  EH-unwind thunk for CEnumPins (compiler, no port needed)
0x10002D50  CEnumMediaTypes-related unlock helper (calls UnlockModule)
0x10002DC0  CBasePin::NonDelegatingQueryInterface (pin primary +0x00)
0x10002E50  CBasePin::NonDelegatingAddRef    (pin primary +0x04)
0x10002E70  CBasePin::NonDelegatingRelease   (pin primary +0x08)
0x10002E90  CBasePin::SetMediaType (pin primary +0x24)
0x10002EB0  CBasePin helper (connection negotiation)
0x10002EE0  CBasePin helper
0x10002F20  IPin::ConnectedTo
0x10002F60  IPin::QueryPinInfo
0x10002FE0  IPin::QueryDirection
0x10003000  IPin::QueryAccept
0x10003030  CBasePin::GetMediaTypeVersion (pin primary +0x10)
0x10003040  CBasePin::Run (pin primary +0x1C; ret 8 stub)
0x10003050  IQualityControl::Notify
0x10003080  CPushPinDIBSq/CSourceStream IAMovieSetup+0x08-adjacent helper
0x10003090  IPin::NewSegment
0x100030D0  CBasePin::CompleteConnect (pin primary +0x30; tail-calls +0x38)
0x100030F0  CBasePin::CheckConnect (pin primary +0x28)
0x10003130  CBasePin::BreakConnect (pin primary +0x2C)
0x10003190  CBasePin::InitAllocator (pin primary +0x48)
0x100031A0  CBasePin::DecideAllocator (pin primary +0x38)
0x100032A0  CBasePin::GetDeliveryBuffer (pin primary +0x40)
0x100032E0  CBasePin::Deliver (pin primary +0x44)
0x10003310  CBasePin::DeliverEndOfStream (pin primary +0x4C)
0x10003330  CBaseFilter helper
0x10003350  CBaseFilter helper
0x10003370  S_FALSE/E_NOTIMPL stub (EndOfStream/BeginFlush/EndFlush,
            CSourceStream root+0x1C default GetMediaType(pmt))
0x10003380  CBasePin::DeliverBeginFlush (pin primary +0x50)
0x100033A0  CBasePin::DeliverEndFlush (pin primary +0x54)
0x100033C0  CBasePin::DeliverNewSegment (pin primary +0x58)
0x10003600  CSource::Stop/Pause/Run state transition support
0x10003680  IBaseFilter::SetSyncSource
0x10003700  IBaseFilter::GetSyncSource
0x100037A0  IBaseFilter::Stop
0x10003850  IBaseFilter::Pause
0x10003910  IBaseFilter::Run
0x10003A10  IAMovieSetup::Register   (uses IFilterMapper path 0x10003400)
0x10003A80  IAMovieSetup::Unregister
0x10003ED0  CBaseOutputPin helper (allocator negotiation)
0x10003F90  CMediaType partial-match wrapper (calls 0x10004BD0)
0x10004080  IPin::ReceiveConnection support
0x10004160  IPin::ReceiveConnection
0x10004220  CBaseOutputPin helper
0x100042D0  IPin::Disconnect
0x10004360  IPin::ConnectionMediaType
0x100045A0+ CEnumMediaTypes ctor (Phase A — see above)
0x100046B0  CMediaType copy-constructor-style helper (name string dup)
0x10004700  CBaseOutputPin helper
0x100047B0  IPin::Connect
0x10004970  CBaseOutputPin helper (allocator/sample creation)
0x100049B0  CBaseOutputPin helper
0x10004A10  IBaseFilter::EnumPins (news a CEnumPins)
0x10004AB0  CBaseOutputPin helper
0x10004AD0  CBasePin helper
0x10004AF0  CBasePin helper
0x10004B00  CBasePin helper
0x10004B20  CBasePin helper
0x10004BA0  CMediaType helpers (IsEqualGUID wrappers)
0x10004BD0  CMediaType partial-type match (wildcard GUID_NULL at 0x100089EC)
0x10004DD0  CMediaType comparison (used by CheckMediaType)
0x10004EF0  CBaseOutputPin helper
0x10004F10  CBaseOutputPin helper
0x10004F50  CMediaType dtor wrapper (frees via 0x10004D70)
0x10004F70  CBaseOutputPin helper
0x10005000  CMediaSample-style ctor helper (calls LockModule)
0x10005030  QI helper for Phase-B classes (IID table walk, 0x10004FD0 tail)
0x100050E0  VIH display-format fixup (biClrImportant/biSizeImage via GetBitmapSize)
0x10005140  DISPLAY DC probe (CreateDCA/GetDIBits, result ignored by ctor)
0x10005220  display-init: critsec + display-probe (ported in push_pin.cpp)
0x10005BE0  ole32 shim: GetModuleHandle/GetProcAddress "CoInitializeEx"
0x10005C10  ole32 shim entry (CoInitialize/CoUninitialize forwarding)
0x10005C50  ole32 shim helper
0x10005C90  lstrlenW-equivalent helper
0x10005CB0  string helper
0x10005D10  ole32/registry support helper
0x10005D50  IAMovieSetup/register helper
0x10005DD0  helper
0x10005E20  helper (uses CoTaskMem* / ole32)
0x10005E80  helper
0x10005EA0  helper (xref 6; critical-section wrapper)
0x10005ED0  helper (critical-section wrapper)

-----------------------------------------------------------------------------
[C] CRT / compiler range 0x10006130..0x10006D52 — replaced by modern CRT
    (each corpus fcn_XXXX file listed; nothing to port)
-----------------------------------------------------------------------------
fcn_10006130  GetBitmapSize ((w*bpp+31)>>3 & ~3) * h, negated for top-down)
              — real DShow support code, ported in push_pin.cpp.  The CRT
              boundary starts at 0x10006162 (@__security_check_cookie).
fcn_10006162  @__security_check_cookie@4               — replaced (GS, compiler)
fcn_10006171  ??_Etype_info (type_info dtor thunk)     — replaced
fcn_100061BA  memset thunk (0x100061BA, jmp [__imp_memset]) — replaced
fcn_100061C0  __allmul                                 — replaced
fcn_100061D9  "hard" (ftol helper)                     — replaced
fcn_100061F4  memcpy thunk                             — replaced
fcn_100061FA  ??2@YAPAXI@Z scalar new                  — replaced (operator new)
fcn_10006200  __alldiv                                 — replaced
fcn_100062B0  __ftol2_sse (corpus fcn_100062B9)        — replaced
fcn_100062E6  __ftol2 (corpus fcn_100062E6)            — replaced
fcn_10006360  _pre_c_init                              — replaced (CRT init)
fcn_100063AD  __CRT_INIT@12                            — replaced (CRT init)
fcn_1000654C  ___DllMainCRTStartup                     — replaced (CRT startup)
fcn_10006657  (CRT startup fragment)                   — replaced
fcn_10006662  DllEntryPoint                            — replaced (CRT entry)
fcn_10006684  _wtoi thunk                              — replaced
fcn_1000668A  _purecall                                — replaced (purecall)
fcn_10006730  __aulldiv                                — replaced
fcn_10006798  ___report_gsfailure                      — replaced (GS)
fcn_1000689C  ?__ArrayUnwind                           — replaced (EH)
fcn_100068FA  ??_M vector destructor iterator          — replaced (EH)
fcn_10006945  (CRT init fragment)                      — replaced
fcn_1000695D  _has_osfxsr_set                          — replaced (CRT init)
fcn_100069AD  __get_sse2_info                          — replaced (CRT init)
fcn_10006A0D  (CRT init fragment, no corpus file)      — replaced
fcn_10006A1A  (CRT init fragment, no corpus file)      — replaced
fcn_10006A26  __onexit                                 — replaced
fcn_10006ABC  (CRT fragment, no corpus file)           — replaced
fcn_10006AC5  _atexit                                  — replaced
fcn_10006AD7  onexit table walk (corpus fcn_10006AD7)  — replaced
fcn_10006AFB  onexit table walk (no corpus file)       — replaced
fcn_10006B20  __ValidateImageBase                      — replaced (CRT)
fcn_10006B50  __FindPESection                         — replaced (CRT)
fcn_10006B92  __IsNonwritableInCurrentImage            — replaced (CRT)
fcn_10006BFE  _initterm thunk                          — replaced
fcn_10006C04  _initterm_e thunk                        — replaced
fcn_10006C0A  _amsg_exit thunk                         — replaced
fcn_10006C10  __CppXcptFilter thunk                    — replaced
fcn_10006C18  __SEH_prolog4                            — replaced (compiler)
fcn_10006C5D  __SEH_epilog4                            — replaced (compiler)
fcn_10006C71  __except_handler4                        — replaced (compiler)
fcn_10006C94  ___security_init_cookie                  — replaced (GS)
(0x10006D28..0x10006D52: thunks to ?terminate, _type_info_dtor_internal_method,
 __clean_type_info_names_internal, _unlock, __dllonexit, _lock,
 _except_handler4_common — all replaced by the modern runtime.)

Compiler-generated EH thunks below .text proper (0x10006E00..0x1000734x and
0x10007xxx-0x10007Fxx: __unwindfunclet thunks for the destructors) are not
present in the corpus and need no port — the modern compiler regenerates
them.

-----------------------------------------------------------------------------
Summary
-----------------------------------------------------------------------------
 Phase A (ported now): 46 functions (exports + factory + registration +
                       enumerators + shared GUID/lock/mediatype/list helpers)
 Phase B (DShow code): ~110 functions in 0x10001000..0x10005000 plus the
                       0x10005000..0x100060C0 support cluster (media sample,
                       string/ole32 shim, media type compare) — all real
                       DirectShow implementation code.
 CRT (skip): 0x10006130..0x10006D52 (36+ functions/thunks) plus scattered
             compiler EH thunks — provided by MSVC/UCRT.
```

