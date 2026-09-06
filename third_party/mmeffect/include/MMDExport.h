///////////////////////////////////////////////////////////////////////////
//   MMDExport.h  Ver.0.08 (2013/06/30)  -  reconstruction header
//     MikuMikuDance export functions header.
//     Import library: MMDExport.lib (built for MikuMikuDance.exe x64).
//
//     Based on the official MMDExport.h shipped in MMD_Plugin_Project_Template
//     (comments converted to ASCII; dllimport branch enabled for plugin use).
//
//     ABI note: D3DMATERIAL9/D3DMATRIX are returned by value; on x64 MSVC this
//     compiles to a hidden return-buffer pointer (sret), exactly matching the
//     host implementation verified by the MMD_9.31 analysis kit
//     (analysis/sdk/mmd_931_exports.hpp, PHASE3_MME_ABI.md).
///////////////////////////////////////////////////////////////////////////
#ifndef __MMDEXPORT_H__
#define __MMDEXPORT_H__

#include <d3d9.h>

/////////////// _EXPORT definition ///////////////
// (plugin side) imports from MikuMikuDance.exe
#define _EXPORT __declspec(dllimport)
//#define _EXPORT __declspec(dllexport)   // (MMD main body side)

///////////// Export functions (37) ////////////////
#ifdef __cplusplus
extern "C" {
#endif

	_EXPORT	float			ExpGetFrameTime();				// frame time (sec), frame 0 = 0 sec
	_EXPORT	int				ExpGetPmdNum();					// number of PMD models
	_EXPORT	char*			ExpGetPmdFilename(int);			// PMD model file name (full path)		arg: 0..GetPmdNum-1
	_EXPORT	int				ExpGetPmdOrder(int);			// PMD model draw order (see note 1)	arg: 0..GetPmdNum-1
	_EXPORT	int				ExpGetPmdMatNum(int);			// material count of PMD model			arg: 0..GetPmdNum-1
	_EXPORT	D3DMATERIAL9	ExpGetPmdMaterial(int,int);		// material of PMD model				args: model, material
	_EXPORT	int				ExpGetPmdBoneNum(int);			// bone count of PMD model				arg: 0..GetPmdNum-1
	_EXPORT	char*			ExpGetPmdBoneName(int,int);		// bone name of PMD model				args: model, bone
	_EXPORT	D3DMATRIX		ExpGetPmdBoneWorldMat(int,int);	// bone world matrix of PMD model (2)	args: model, bone
	_EXPORT	int				ExpGetPmdMorphNum(int);			// morph (skin) count of PMD model		arg: 0..GetPmdNum-1
	_EXPORT	char*			ExpGetPmdMorphName(int,int);	// morph name of PMD model				args: model, morph
	_EXPORT	float			ExpGetPmdMorphValue(int,int);	// morph value of PMD model				args: model, morph
	_EXPORT	bool			ExpGetPmdDisp(int);				// visibility of PMD model (true: shown)arg: 0..GetPmdNum-1
	_EXPORT	void*			ExpGetPmdID(int);				// PMD model ID (object pointer; x64 宿主以完整指针返回，官方头的 int 截断高位)	arg: 0..GetPmdNum-1

	_EXPORT	int				ExpGetAcsNum();					// number of accessories
	_EXPORT	int				ExpGetPreAcsNum();				// accessories drawn before models
	_EXPORT	char*			ExpGetAcsFilename(int);			// accessory file name (full path)		arg: 0..GetAcsNum-1
	_EXPORT	int				ExpGetAcsOrder(int);			// accessory draw order (see note 1)	arg: 0..GetAcsNum-1
	_EXPORT	D3DMATRIX		ExpGetAcsWorldMat(int);			// accessory world matrix (2)			arg: 0..GetAcsNum-1
	_EXPORT	float			ExpGetAcsX(int);				// accessory X (panel X)				arg: 0..GetAcsNum-1
	_EXPORT	float			ExpGetAcsY(int);				// accessory Y (panel Y)				arg: 0..GetAcsNum-1
	_EXPORT	float			ExpGetAcsZ(int);				// accessory Z (panel Z)				arg: 0..GetAcsNum-1
	_EXPORT	float			ExpGetAcsRx(int);				// accessory rotation X (panel Rx)		arg: 0..GetAcsNum-1
	_EXPORT	float			ExpGetAcsRy(int);				// accessory rotation Y (panel Ry)		arg: 0..GetAcsNum-1
	_EXPORT	float			ExpGetAcsRz(int);				// accessory rotation Z (panel Rz)		arg: 0..GetAcsNum-1
	_EXPORT	float			ExpGetAcsSi(int);				// accessory scale (panel Si)			arg: 0..GetAcsNum-1
	_EXPORT	float			ExpGetAcsTr(int);				// accessory transparency (panel Tr)	arg: 0..GetAcsNum-1
	_EXPORT	bool			ExpGetAcsDisp(int);				// visibility of accessory (true: shown)arg: 0..GetAcsNum-1
	_EXPORT	void*			ExpGetAcsID(int);				// accessory ID (object pointer; 同上)					arg: 0..GetAcsNum-1
	_EXPORT	int				ExpGetAcsMatNum(int);			// material count of accessory			arg: 0..GetAcsNum-1
	_EXPORT	D3DMATERIAL9	ExpGetAcsMaterial(int,int);		// material of accessory				args: accessory, material

	_EXPORT	int				ExpGetCurrentObject();			// object currently processed (1)
	_EXPORT	int				ExpGetCurrentMaterial();		// material currently processed (3)
	_EXPORT	int				ExpGetCurrentTechnic();			// technique currently processed
														// (0:other 1:normal draw (self-shadow OFF)
														//  2:normal draw (self-shadow ON)
														//  3:shadow (non-self-shadow)
														//  4:edge 5:self-shadow Z-buffer plot)
	_EXPORT	void			ExpSetRenderRepeatCount(int);	// repeat rendering n times (4)
	_EXPORT	int				ExpGetRenderRepeatCount();		// rendering repeat count (4)
	_EXPORT	bool			ExpGetEnglishMode();			// true when in English mode

#ifdef __cplusplus
}
#endif

/*
Note 1 - draw order:
MMD draws A. accessories before models, B. models, C. accessories after models.
ExpGetPmdOrder / ExpGetAcsOrder / ExpGetCurrentObject return the combined A+B+C
sequence number; A values are returned negative.

Note 2 - world matrices:
The MMD camera moves only by distance in 3D space while objects move/rotate, so
the exported world matrix is the value BEFORE the camera position/rotation is
applied. It differs from D3DDevice->GetTransform(D3DTS_WORLD).
ExpGetPmdBoneWorldMat already includes the model world matrix when an accessory
follows a model.

Note 3 - ExpGetCurrentMaterial:
Also returns values during shadow/edge/self-shadow Z-plot processing. Those
passes render with a single material, but the return value is the material
number that would apply during normal drawing.

Note 4 - render repeat:
ExpSetRenderRepeatCount sets the host's internal ExRepeat counter. One frame
roughly runs: clear surfaces -> BeginScene -> self-shadow Z-plot ->
while(ExRepeat>0){ ExRepeat--; model/accessory rendering } -> capture/overlay
-> EndScene -> AVI -> bone/physics -> vertex updates -> Present -> device-loss
recovery. MME uses this to re-run the object passes per post-effect.
*/

#endif	// __MMDEXPORT_H__
