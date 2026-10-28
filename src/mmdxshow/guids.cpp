// =============================================================================
// MMDxShow.dll — interface-IID instantiation.  The modern SDK declares these
// in <strmif.h> but routes the storage through __uuidof/uuid.lib; the port
// references the plain C symbols, so define them here once, pinned to the
// exact original .rdata values:
//   IID_IBaseFilter  0x10008600  {56A86895-0AD4-11CE-B03A-0020AF0BA770}
//   IID_IMediaFilter 0x10008610  {56A86899-0AD4-11CE-B03A-0020AF0BA770}
//   IID_IPersist     0x10008A1C  {0000010C-0000-0000-C000-000000000046}
//   IID_IEnumPins    0x10008630  {56A86892-0AD4-11CE-B03A-0020AF0BA770}
// =============================================================================
#include <windows.h>
#include <objbase.h>
#include <strmif.h>

extern "C" {
const IID IID_IBaseFilter = {
    0x56a86895, 0xad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const IID IID_IMediaFilter = {
    0x56a86899, 0xad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const IID IID_IPersist = {
    0x0000010c, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID IID_IEnumPins = {
    0x56a86892, 0xad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const IID IID_IPin = {0x56a86891, 0xad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
}
