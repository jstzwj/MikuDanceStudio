// =============================================================================
// MMDxShow.dll — Phase A: CEnumPins and CEnumMediaTypes (enumerators.cpp)
// =============================================================================
// Faithful ports of:
//   CEnumPins        vtable 0x1000854C — ctor 0x10004420, QI 0x10002B40,
//                    AddRef 0x10002BA0, Release 0x10002BC0, Next 0x10003B20,
//                    Skip 0x10003C20, Reset 0x10002BF0, Clone 0x100044F0,
//                    ~CEnumPins 0x10002AD0, deleting dtor 0x10003B00,
//                    version refresh 0x10002C30
//   CEnumMediaTypes  vtable 0x10008570 — ctor 0x100045A0, QI 0x10002C80,
//                    AddRef 0x10002CE0, Release 0x10002D00, Next 0x10003C90,
//                    Skip 0x10003E10, Reset 0x10002D30, Clone 0x10004600,
//                    ~CEnumMediaTypes 0x10002C60, deleting dtor 0x10003C70
//   pin keep-alive list (CEnumPins +0x18):
//                    init      0x10005F30   free-active 0x10005F50
//                    node new  0x10006010   copy        0x10006070
//                    destroy   0x100060C0   find        0x10005FD0
//                    head      0x10005F00   value       0x10005FB0
//                    popfront  0x10005F90
// =============================================================================

#include "mmdxshow.hpp"
#include <new>

// IID_IEnumPins (.rdata 0x10008630) is the standard
// {56A86892-0AD4-11CE-B03A-0020AF0BA770}; CEnumMediaTypes uses the
// ActiveMovie-1.0 IID (MMDXSHOW_IID_IEnumMediaTypes, 0x10008620) declared in
// mmdxshow.hpp and instantiated in dll_main.cpp.

// =============================================================================
// Pin keep-alive / dedup list (private to CEnumPins)
// =============================================================================

// 0x10005F30 — {0,0,0,10,0,0}
void CEnumPins::ListInit(PinList* pList) {
    pList->pHead = NULL;
    pList->pTail = NULL;
    pList->nCount = 0;
    pList->nCapacity = 10;
    pList->nFree = 0;
    pList->pFree = NULL;
}

// 0x10006010 — append, reusing a node from the free list when possible
CEnumPins::PinNode* CEnumPins::ListPushBack(PinList* pList, CBasePin* pPin) {
    PinNode* pNode = pList->pFree;
    if (pNode) {
        pList->pFree = pNode->pNext;
        --pList->nFree;
    } else {
        pNode = new (std::nothrow) PinNode();
        if (pNode == NULL)
            return NULL; // 0x1000602f: allocation failure
    }
    pNode->pPin = pPin;
    pNode->pNext = NULL;
    pNode->pPrev = pList->pTail;
    if (pList->pTail) {
        pList->pTail->pNext = pNode;
        ++pList->nCount;
    } else {
        ++pList->nCount;
        pList->pHead = pNode;
    }
    pList->pTail = pNode;
    return pNode;
}

// 0x10005F50 — free the active nodes only (Reset / full clear)
void CEnumPins::ListFreeActive(PinList* pList) {
    PinNode* pNode = pList->pHead;
    while (pNode) {
        PinNode* pNext = pNode->pNext;
        delete pNode;
        pNode = pNext;
    }
    pList->nCount = 0;
    pList->pTail = NULL;
    pList->pHead = NULL;
}

// 0x100060C0 — free active nodes AND the free-node list (destructor)
void CEnumPins::ListDestroy(PinList* pList) {
    ListFreeActive(pList);
    PinNode* pNode = pList->pFree;
    while (pNode) {
        PinNode* pNext = pNode->pNext;
        delete pNode;
        pNode = pNext;
    }
}

// 0x10006070 — copy every value of pSrc into pList
void CEnumPins::ListCopy(PinList* pList, const PinList* pSrc) {
    PinNode* pNode = pSrc->pHead; // 0x10005F00
    while (pNode) {
        CBasePin* pPin = pNode->pPin; // 0x10005F90 pop-front value
        pNode = pNode->pNext;
        if (ListPushBack(pList, pPin) == NULL)
            break; // allocation failure
    }
}

// 0x10005FD0 — return the node whose value equals pPin, or NULL
CEnumPins::PinNode* CEnumPins::ListFind(const PinList* pList, CBasePin* pPin) {
    for (PinNode* pNode = pList->pHead; pNode; pNode = pNode->pNext)
        if (pNode->pPin == pPin)
            return pNode;
    return NULL;
}

// =============================================================================
// CEnumPins
// =============================================================================

// 0x10004420
CEnumPins::CEnumPins(CBaseFilter* pFilter, CEnumPins* pEnum) {
    // (vtable store is implicit)
    m_Position = 0;
    m_PinCount = 0;
    m_pFilter = pFilter;
    m_cRef = 1;
    ListInit(&m_Pins);

    // (*(m_pFilter+0x0C vtable +0x04))(m_pFilter+0x0C)  ==  IBaseFilter::AddRef
    static_cast<IBaseFilter*>(m_pFilter)->AddRef();

    if (pEnum) {
        m_Position = pEnum->m_Position;
        m_PinCount = pEnum->m_PinCount;
        m_PinVersion = pEnum->m_PinVersion;
        ListCopy(&m_Pins, &pEnum->m_Pins);
    } else {
        m_PinVersion = m_pFilter->GetPinVersion(); // primary +0x14 (0x10002AB0)
        m_PinCount = m_pFilter->GetPinCount();     // primary +0x18 (0x10002090)
    }
}

// 0x10002AD0
CEnumPins::~CEnumPins() {
    static_cast<IBaseFilter*>(m_pFilter)->Release(); // IBaseFilter::Release
    ListDestroy(&m_Pins);                            // 0x100060C0
}

// 0x10002B40
STDMETHODIMP CEnumPins::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == NULL)
        return E_POINTER;
    if (MMDxShow_IsEqualGUID16(&riid, &IID_IEnumPins) ||
        MMDxShow_IsEqualGUID16(&riid, &IID_IUnknown))
        return MMDxShow_ReturnSelf(this, ppv); // 0x10004FD0
    *ppv = NULL;
    return E_NOINTERFACE;
}

// 0x10002BA0
STDMETHODIMP_(ULONG) CEnumPins::AddRef() {
    return (ULONG)InterlockedIncrement(&m_cRef);
}

// 0x10002BC0 — the last Release goes through vtable +0x1C (deleting dtor)
STDMETHODIMP_(ULONG) CEnumPins::Release() {
    const LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0) {
        delete this;
        return 0;
    }
    return (ULONG)cRef;
}

// 0x10003B20
STDMETHODIMP CEnumPins::Next(ULONG cPins, IPin** ppPins, ULONG* pcFetched) {
    if (ppPins == NULL)
        return E_POINTER;
    if (pcFetched)
        *pcFetched = 0;
    else if (cPins > 1)
        return E_INVALIDARG;

    ULONG nFetched = 0;
    if (m_pFilter->GetPinVersion() != m_PinVersion) // primary +0x14
        RefreshPinState();                          // 0x10002C30

    LONG cWanted = m_PinCount - m_Position;
    if (cWanted >= (LONG)cPins)
        cWanted = (LONG)cPins;
    if (cWanted == 0)
        return S_FALSE;

    while (cWanted) {
        const LONG nPin = m_Position;
        if (m_PinCount == nPin)
            break;
        ++m_Position;
        CBasePin* pPin = m_pFilter->GetPin(nPin); // primary +0x1C (0x100020B0)
        if (pPin == NULL)
            return VFW_E_ENUM_OUT_OF_SYNC;     // 0x80040203
        if (ListFind(&m_Pins, pPin) == NULL) { // 0x10005FD0
            // *ppPins = (IPin*)(pPin+0x0C); IPin::AddRef
            *ppPins = static_cast<IPin*>(pPin);
            static_cast<IPin*>(pPin)->AddRef();
            ++nFetched;
            ++ppPins;
            ListPushBack(&m_Pins, pPin); // 0x10006010
            --cWanted;
        }
    }

    if (pcFetched)
        *pcFetched = nFetched;
    return (cPins != nFetched) ? S_FALSE : S_OK;
}

// 0x10003C20
STDMETHODIMP CEnumPins::Skip(ULONG cPins) {
    if (m_pFilter->GetPinVersion() != m_PinVersion)
        return VFW_E_ENUM_OUT_OF_SYNC;
    const LONG nCurrent = m_Position;
    if (cPins > (ULONG)(m_PinCount - nCurrent))
        return S_FALSE;
    m_Position = (LONG)cPins + nCurrent;
    return S_OK;
}

// 0x10002BF0
STDMETHODIMP CEnumPins::Reset() {
    m_PinVersion = m_pFilter->GetPinVersion(); // primary +0x14
    m_PinCount = m_pFilter->GetPinCount();     // primary +0x18
    m_Position = 0;
    ListFreeActive(&m_Pins); // 0x10005F50
    return S_OK;
}

// 0x10002C30 — re-cache version/count after an out-of-sync Next (internal)
void CEnumPins::RefreshPinState() {
    m_PinVersion = m_pFilter->GetPinVersion();
    m_PinCount = m_pFilter->GetPinCount();
    m_Position = 0;
}

// 0x100044F0
STDMETHODIMP CEnumPins::Clone(IEnumPins** ppEnum) {
    if (ppEnum == NULL)
        return E_POINTER;
    if (m_pFilter->GetPinVersion() != m_PinVersion) {
        *ppEnum = NULL;
        return VFW_E_ENUM_OUT_OF_SYNC;
    }
    CEnumPins* pNew = new (std::nothrow) CEnumPins(m_pFilter, this); // new(0x30) / x64 new(0x48)
    *ppEnum = pNew;
    if (pNew == NULL)
        return E_OUTOFMEMORY;
    return S_OK;
}

// =============================================================================
// CEnumMediaTypes
// =============================================================================

// 0x100045A0
CEnumMediaTypes::CEnumMediaTypes(CBasePin* pPin, CEnumMediaTypes* pEnum) {
    m_pPin = pPin;
    m_Position = 0;
    m_cRef = 1;
    // (*(m_pPin+0x0C vtable +0x04))(m_pPin+0x0C)  ==  IPin::AddRef
    static_cast<IPin*>(m_pPin)->AddRef();
    if (pEnum) {
        m_Position = pEnum->m_Position;
        m_Version = pEnum->m_Version;
    } else {
        m_Version = m_pPin->GetMediaTypeVersion(); // primary +0x10 (0x10003030)
    }
}

// 0x10002C60
CEnumMediaTypes::~CEnumMediaTypes() {
    static_cast<IPin*>(m_pPin)->Release(); // IPin::Release
}

// 0x10002C80 — accepts the ActiveMovie-1.0 IID_IEnumMediaTypes (0x10008620)
STDMETHODIMP CEnumMediaTypes::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == NULL)
        return E_POINTER;
    if (MMDxShow_IsEqualGUID16(&riid, &MMDXSHOW_IID_IEnumMediaTypes) ||
        MMDxShow_IsEqualGUID16(&riid, &IID_IUnknown))
        return MMDxShow_ReturnSelf(this, ppv);
    *ppv = NULL;
    return E_NOINTERFACE;
}

// 0x10002CE0
STDMETHODIMP_(ULONG) CEnumMediaTypes::AddRef() {
    return (ULONG)InterlockedIncrement(&m_cRef);
}

// 0x10002D00
STDMETHODIMP_(ULONG) CEnumMediaTypes::Release() {
    const LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0) {
        delete this;
        return 0;
    }
    return (ULONG)cRef;
}

// 0x10003C90
STDMETHODIMP CEnumMediaTypes::Next(ULONG cMediaTypes, AM_MEDIA_TYPE** ppMediaTypes,
                                   ULONG* pcFetched) {
    if (ppMediaTypes == NULL)
        return E_POINTER;
    if (m_pPin->GetMediaTypeVersion() != m_Version) // primary +0x10
        return VFW_E_ENUM_OUT_OF_SYNC;
    if (pcFetched)
        *pcFetched = 0;
    else if (cMediaTypes > 1)
        return E_INVALIDARG;

    ULONG nFetched = 0;
    while (cMediaTypes) {
        AM_MEDIA_TYPE mt; // 0x10004DC0
        MMDxShow_InitMediaType(&mt);
        const LONG nType = m_Position;
        ++m_Position;
        if (m_pPin->GetMediaType(nType, &mt) != 0) { // primary +0x34 (0x100022B0)
            MMDxShow_FreeMediaType(&mt);             // loop-exit path
            break;
        }
        AM_MEDIA_TYPE* pmt = (AM_MEDIA_TYPE*)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
        *ppMediaTypes = pmt;
        if (pmt == NULL) {
            MMDxShow_FreeMediaType(&mt); // 0x10003dc4
            break;
        }
        ++ppMediaTypes;
        ++nFetched;
        memcpy(pmt, &mt, sizeof(AM_MEDIA_TYPE));
        // detach the source so its destructor frees nothing
        mt.pUnk = NULL;
        mt.cbFormat = 0;
        mt.pbFormat = NULL;
        --cMediaTypes;
        MMDxShow_FreeMediaType(&mt); // 0x10004DB0 (no-op now)
    }

    if (pcFetched)
        *pcFetched = nFetched;
    return cMediaTypes ? S_FALSE : S_OK;
}

// 0x10003E10
STDMETHODIMP CEnumMediaTypes::Skip(ULONG cMediaTypes) {
    if (cMediaTypes == 0)
        return S_OK;
    if (m_pPin->GetMediaTypeVersion() != m_Version)
        return VFW_E_ENUM_OUT_OF_SYNC;

    m_Position += (LONG)cMediaTypes;

    AM_MEDIA_TYPE mt;
    MMDxShow_InitMediaType(&mt);
    const BOOL bPastEnd = (m_pPin->GetMediaType(m_Position - 1, &mt) != 0);
    MMDxShow_FreeMediaType(&mt);
    return bPastEnd ? S_FALSE : S_OK;
}

// 0x10002D30
STDMETHODIMP CEnumMediaTypes::Reset() {
    m_Position = 0;
    m_Version = m_pPin->GetMediaTypeVersion();
    return S_OK;
}

// 0x10004600
STDMETHODIMP CEnumMediaTypes::Clone(IEnumMediaTypes** ppEnum) {
    if (ppEnum == NULL)
        return E_POINTER;
    if (m_pPin->GetMediaTypeVersion() != m_Version) {
        *ppEnum = NULL;
        return VFW_E_ENUM_OUT_OF_SYNC;
    }
    CEnumMediaTypes* pNew =
        new (std::nothrow) CEnumMediaTypes(m_pPin, this); // new(0x14) / x64 new(0x20)
    *ppEnum = pNew;
    if (pNew == NULL)
        return E_OUTOFMEMORY;
    return S_OK;
}
