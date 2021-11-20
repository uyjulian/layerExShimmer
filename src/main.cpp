#include "ncbind.hpp"
#include "layerExBase.hpp"
#include <stdio.h>
#include <process.h>
#include "KThreadPool.h"


#define USE_SSE2		// SSE2���g��
#define MULTI_THREAD	// �}���`�X���b�h�ɂ���


// ��h�b�g�̌^
typedef DWORD TJSPIXEL;
// ��h�b�g�̃o�C�g��
#define TJSPIXELSIZE (sizeof(TJSPIXEL))

/**
 * ���O�o�͗p
 */
#if 1
static void log(const tjs_char *format, ...)
{
#if 0
	return;
#else
	va_list args;
	va_start(args, format);
	tjs_char msg[1024];
	_vsnwprintf_s(msg, 1024, format, args);
	TVPAddLog(msg);
	va_end(args);
#endif
}
#endif


/*
 * ���W����o�b�t�@�A�h���X�����߂�C�����C���֐�
 */
inline static BYTE* bufadr(BYTE *buf, UINT x, UINT y, UINT pitch)
{
  return buf + y*pitch + x*TJSPIXELSIZE;
}

// �}�C�i�X�l���܂߁A0����max-1�܂ł̊Ԃɒl�����낦��B�I�[�o�[�t���[���̓��[�v����
#define ZERO2MAX(num, max) ((((num)%(max))+(max))%(max))
// �}�C�i�X���܂߁A0����max-1�܂ł̊Ԃɒl�����낦��B�I�[�o�[�t���[����0�`MAX�܂ł̊ԂɊۂ߂�
#define ZERO2MAX2(num, max) ((num) < 0 ? 0 : (num) >= (max) ? (max)-1 : (num))

/*
 * ���W����o�b�t�@�A�h���X�����߂�C�����C���֐�(���W���[�v��)
 */
inline static BYTE* bufadr2(BYTE *buf, int x, int y, int width, int height, int pitch)
{
  return buf + ZERO2MAX(y,height)*pitch + ZERO2MAX(x,width)*TJSPIXELSIZE;
}
/*
 * ���W����o�b�t�@�A�h���X�����߂�C�����C���֐�(���W�ۂߔ�)
 */
inline static BYTE* bufadr3(BYTE *buf, int x, int y, int width, int height, int pitch)
{
  return buf + ZERO2MAX2(y,height)*pitch + ZERO2MAX2(x,width)*TJSPIXELSIZE;
}


/*
 * TJS��Layer�N���X�̃����o�𓾂�
 */
inline static tTJSVariant getTJSMember(tTJSVariant instance, const wchar_t param[])
{
	iTJSDispatch2 *obj = instance.AsObjectNoAddRef();
	tTJSVariant val;
	obj->PropGet(0, param, NULL, &val, obj);
	return val;
} 

/*
 * �����낤���ʗp�֐� shimmer() �ǉ�
 */
class layerExShimmer : public layerExBase
{
	KThreadPool<layerExShimmer> threadPool; // �X���b�h�v�[��(def��CPU��=Thread��)

#ifndef TVPMaxThreadNum
	static const tjs_int  TVPMaxThreadNum = 8;	// ����dll���痘�p�ł��Ȃ��̂Œ�`
#endif
	// �X���b�h�I����Event�ő҂Əd������A�X���b�h���Q���炢�œ��ł��������̂�
	static const tjs_int MAXTHREADNUM = TVPMaxThreadNum;

public:
	// �R���X�g���N�^
	layerExShimmer(DispatchT obj) : layerExBase(obj)
	{
	}

	// threadedShimmer*() �ɓn�������\����
	typedef struct {
		BYTE *dstbuf; tjs_int dstwidth, dstheight, dstpitch;
		BYTE *srcbuf; tjs_int srcwidth, srcheight, srcpitch;
		BYTE *mapbuf; tjs_int mapwidth, mapheight, mappitch;
		BYTE *mskbuf; tjs_int mskwidth, mskheight, mskpitch;
		tjs_int clipx, clipy, clipw, cliph;
		float scalex, scaley;
		tjs_int mapx, mapy, mskx, msky;
	} ShimmerRect;


	// �}�X�N�̂Ȃ�shimmer�̃}���`�X���b�h�֐�
	// srcbuf �̉摜�� mapbuf �ɏ]���Ă䂪�܂��Ȃ��� dstbuf �ɓ\��t����
	void threadedShimmer(LPVOID params)
	{
		tjs_int dstwidth, dstheight, dstpitch;
		tjs_int srcwidth, srcheight, srcpitch;
		tjs_int mapwidth, mapheight, mappitch;
		tjs_int clipx, clipy, clipw, cliph;
		BYTE *dstbuf, *srcbuf, *mapbuf;
		float scalex, scaley;

		ShimmerRect *p = (ShimmerRect*)params;
		dstbuf = p->dstbuf, dstwidth = p->dstwidth, dstheight = p->dstheight, dstpitch = p->dstpitch;
		srcbuf = p->srcbuf, srcwidth = p->srcwidth, srcheight = p->srcheight, srcpitch = p->srcpitch;
		mapbuf = p->mapbuf, mapwidth = p->mapwidth, mapheight = p->mapheight, mappitch = p->mappitch;
		clipx = p->clipx, clipy = p->clipy, clipw = p->clipw, cliph = p->cliph;
		scalex = p->scalex, scaley = p->scaley;
//log(L"dstbuf = 0x%04x, dstwidth = %d, dstheight = %d, dstpitch = %d", dstbuf, dstwidth, dstheight, dstpitch);
//log(L"srcbuf = 0x%04x, srcwidth = %d, srcheight = %d, srcpitch = %d", srcbuf, srcwidth, srcheight, srcpitch);
//log(L"mapbuf = 0x%04x, mapwidth = %d, mapheight = %d, mappitch = %d", mapbuf, mapwidth, mapheight, mappitch);
//log(L"mskbuf = 0x%04x, mskwidth = %d, mskheight = %d, mskpitch = %d", mskbuf, mskwidth, mskheight, mskpitch);
//log(L"clipx = %d, clipw = %d, clipy = %d, cliph = %d", clipx, clipw, clipy, cliph);
		for (int y = clipy; y < clipy+cliph; y++) {
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(dstbuf, clipx, y, dstpitch);
			BYTE *mapp = bufadr(mapbuf, p->mapx, p->mapy+y-clipy, mappitch);
#ifndef USE_SSE2
			const int sx = int(scalex*0x10000), sy = int(scaley*0x10000);
			for (int x = clipx; x < clipx+clipw; x++) {
				// �}�b�v���C���̒��ڃh�b�g�́u�X���v�𓾂�
				// �F�v�f�������g���B�}�b�v�摜�͊D�F�����炱���O.K.
				int gradx = *(mapp+TJSPIXELSIZE) - *(mapp-TJSPIXELSIZE);
				int grady = *(mapp+mappitch    ) - *(mapp-mappitch    );

				// �u�X���v����src�摜���� x, y �𓾂�
				int srcx = x + ((gradx*sx)>>16);
				int srcy = y + ((grady*sy)>>16);

				// src[xy]�͈̔̓`�F�b�N�͂��Ȃ��B�摜�͏c�����[�v���Ă邩��
				// �l��������
				*dstp++ = *(TJSPIXEL*)(bufadr2(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
				mapp += TJSPIXELSIZE;
			}
#else
			__asm {
				mov			eax,  clipx
				movd		xmm0, eax
				movss		xmm4, xmm0
				pshufd		xmm4, xmm4, 0x39	// PACK(0 3 2 1) = 4byte rotate right 
				inc			eax
				movd		xmm0, eax
				movss		xmm4, xmm0
				pshufd		xmm4, xmm4, 0x39	// PACK(0 3 2 1) = 4byte rotate right 
				inc			eax
				movd		xmm0, eax
				movss		xmm4, xmm0
				pshufd		xmm4, xmm4, 0x39	// PACK(0 3 2 1) = 4byte rotate right 
				inc			eax
				movd		xmm0, eax
				movss		xmm4, xmm0
				pshufd		xmm4, xmm4, 0x39	// PACK(0 3 2 1) = 4byte rotate right 
													// xmm4 = clipx+3_clipx+2_clipx+1_clipx+0 �ɂȂ���
				mov			eax,  y
				movd		xmm5, eax
				pshufd		xmm5, xmm5, 0		// xmm5 = y+clipy_y+clipy_y+clipy_y+clipy

				mov			eax, srcwidth
				dec			eax
				movd		xmm6, eax
				pshufd		xmm6, xmm6, 0		// xmm6 = srcwidth-1_srcwidth-1_srcwidth-1_srcwidth-1

				mov			eax, srcheight
				dec			eax
				movd		xmm7, eax
				pshufd		xmm7, xmm7, 0		// xmm7 = srcheight-1_srcheight-1_srcheight-1_srcheight-1

				mov			edi, dstp
				mov			esi, mapp
				mov			ebx, srcpitch
				// for (int x = clipx; x < clipw; x+=4 /*++*/) {
				mov			ecx, clipw
				// sub			ecx, 2	����͊��Ɏ��{�ς݂Ȃ̂ŕs�v			// �E�[�E���[�͏������Ȃ��̂ŁA���� clipw-2
				sar			ecx, 2				// (clipw-2)/4�B4dot���ƂȂ̂�
				jz			XLOOP1_4DOT_END
			XLOOP1:
					// �Ȃ�ƂȂ�prefetch���Ƃ��H
					// prefetchnta	[esi+32]
					// ���̃��[�v�̒������Au-OP �������悤�ɖ��߂̏��Ԃ��l���Ă���
					movdqu		xmm2, [esi-4]	// ����������movdqu���g�p
					movdqu		xmm0, [esi+4]	// SSE2�ɂ̓��[�e�[�g���߂Ȃ��̂�
					pslld		xmm0, 24		// xmm[02] �̏�� 24 bit �� 0 �N���A
					pslld		xmm2, 24
					psrld		xmm0, 24		// PMOVZX�g������������SSE4.1�Ȃ̂Œf�O
					psrld		xmm2, 24
					psubd		xmm0, xmm2		// xmm0 = (*(xpos+1) - *(xpos-1)) = diffx
					// �����܂ł� xmm0 �� ���E4byte�̌X��(diffx)
					movd		xmm2, scalex
					cvtdq2ps	xmm0, xmm0		// ���������_�l�ɕϊ�
					pshufd		xmm2, xmm2, 0	// scalex_scalex_scalex_scalex
					mulps		xmm0, xmm2		// *scalex
					cvtps2dq	xmm0, xmm0		// �����ɖ߂� ����� xmm0 �� (diffx*scalex)

					paddd		xmm0, xmm4		// xmm0 = x + (diffx*scalex)
					pxor		xmm2, xmm2
					pminsw		xmm0, xmm6		// pminsd�ɂ�����������SSE4.1�������̂�pminsw��
					pmaxsw		xmm0, xmm2		// 0 <= xmm0 <= srcwidth-1 �ɂȂ��� pmaxsd�ɂ�����������SSE4.1�������̂Łc

					mov			eax,  mappitch
					movdqu		xmm1, [esi+eax]	// esi+mappitch
					neg			eax
					movdqu		xmm2, [esi+eax]	// esi-mappitch
					pslld		xmm1, 24		// xmm[12] �̏�� 24 bit �� 0 �N���A
					pslld		xmm2, 24
					psrld		xmm1, 24
					psrld		xmm2, 24
					psubd		xmm1, xmm2		// xmm1 = (*(ypos+1) - *(ypos-1)) = diffy
					// �����܂ł� xmm1 �� �㉺4byte�̌X��(diffy)

					movd		xmm2, scaley
					cvtdq2ps	xmm1, xmm1		// ���������_�l�ɕϊ�
					pshufd		xmm2, xmm2, 0	// scaley_scaley_scaley_scaley
					mulps		xmm1, xmm2		// *scaley
					cvtps2dq	xmm1, xmm1		// �����ɖ߂� ����� xmm1 �� (diffy*scaley)

					paddd		xmm1, xmm5		// xmm1 = y + (diffy*scaley)
					pxor		xmm2, xmm2
					pminsw		xmm1, xmm7		// pminsd�ɂ�����������SSE4.1�������̂�pminsw��
					pmaxsw		xmm1, xmm2		// 0 <= xmm1 <= height-1 �ɂȂ��� pmaxsd�ɂ�����������SSE4.1�������̂Łc

					movd		xmm2, ebx		// ebx = srcpitch
					pslld		xmm0, 2			// x*sizeof(dot)
					pshufd		xmm2, xmm2, 0	// xmm2 = srcpitch_srcpitch_srcpitch_srcpitch
					movdqa		xmm3, xmm1
					pmullw		xmm1, xmm2		// xmm1 = (y+diffy*scaley)*srcpitch�̉���16bit 
					pmulhw		xmm3, xmm2		// xmm3 = (y+diffy*scaley)*srcpitch�̏��16bit
					pslld		xmm3, 16
					por			xmm1, xmm3		// (y+diffy*scaley)��srcpitch16bit�ȓ��̐��̐����Ȃ̂�
					// xmm1 = (y+diffy*scaley)*srcpitch
					paddd		xmm0, xmm1		// 
					// �����܂ł� xmm0 = (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4
					mov			eax,  srcbuf
					movd		xmm1, eax
					pshufd		xmm1, xmm1, 0
					paddd		xmm0, xmm1
					// �����܂ł� xmm0 = srcbuf + (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4

					// *p = *src
					movd		eax,  xmm0
					mov			eax, [eax]
					pshufd		xmm0, xmm0, 0x39	// PACK(0,3,2,1) = 4byte rotate
					mov			[edi], eax
					movd		eax,  xmm0
					mov			eax, [eax]
					pshufd		xmm0, xmm0, 0x39	// PACK(0,3,2,1) = 4byte rotate
					mov			[edi+4], eax
					movd		eax,  xmm0
					mov			eax, [eax]
					pshufd		xmm0, xmm0, 0x39	// PACK(0,3,2,1) = 4byte rotate
					mov			[edi+8], eax
					movd		eax,  xmm0
					mov			eax, [eax]
					mov			[edi+12], eax

					mov			eax, 4
					add			edi, 16				// p += 4dot*sizeof(dot)
					movd		xmm0, eax
					add			esi, 16				// mapp += 4dot*sizeof(dot)
					pshufd		xmm0, xmm0, 0
					paddd		xmm4, xmm0			// x += 4
					dec			ecx
					jnz			XLOOP1
			XLOOP1_4DOT_END:
				// �E�[�������K�v���ǂ������f
				mov			ecx, clipw		// �E�[������ǉ�
				// sub			ecx, 2 ����͊��Ɏ��{�ς݂Ȃ̂ŕs�v
				and			ecx, 0x3		// ecx = (clipw-2)%4
				jz			XLOOP1_end

				// �E�[�������K�v�Ȃ̂Ŏ��s�B���movdqu��movd�ɕύX���������B�}�a�ŁB
				// ���x�͒x�����o�O������Ȃ����Ƃ�D��
			XLOOP1_RIGHTLOOP:
					movd		xmm0, [esi+4]	// ������4byte(1dot)�̂ݓ]��
					movd		xmm2, [esi-4]	// 
					pslld		xmm0, 24		// xmm[02] �̏�� 24 bit �� 0 �N���A
					psrld		xmm0, 24		// PMOVZX�g������������SSE4.1�Ȃ̂Œf�O
					pslld		xmm2, 24
					psrld		xmm2, 24
					psubd		xmm0, xmm2		// xmm0 = (*(xpos+1) - *(xpos-1)) = diffx
					// �����܂ł� xmm0 �� ���E4byte�̌X��(diffx)
					cvtdq2ps	xmm0, xmm0		// ���������_�l�ɕϊ�
					movd		xmm2, scalex
					pshufd		xmm2, xmm2, 0	// scalex_scalex_scalex_scalex
					mulps		xmm0, xmm2		// *scalex
					cvtps2dq	xmm0, xmm0		// �����ɖ߂� ����� xmm0 �� (diffx*scalex)

					paddd		xmm0, xmm4		// xmm0 = x + (diffx*scalex)
					pminsw		xmm0, xmm6		// pminsd�ɂ�����������SSE4.1�������̂�pminsw��
					pxor		xmm2, xmm2		// ����pmaxsd�ɂ�����������SSE4.1�������̂Łc
					pmaxsw		xmm0, xmm2		// 0 <= xmm0 <= srcwidth-1 �ɂȂ���

					mov			eax,  mappitch
					movd		xmm1, [esi+eax]	// esi+mappitch
					neg			eax
					movd		xmm2, [esi+eax]	// esi-mappitch
					pslld		xmm1, 24		// xmm[12] �̏�� 24 bit �� 0 �N���A
					psrld		xmm1, 24
					pslld		xmm2, 24
					psrld		xmm2, 24
					psubd		xmm1, xmm2		// xmm1 = (*(ypos+1) - *(ypos-1)) = diffy
					// �����܂ł� xmm1 �� �㉺4byte�̌X��(diffy)

					cvtdq2ps	xmm1, xmm1		// ���������_�l�ɕϊ�
					movd		xmm2, scaley
					pshufd		xmm2, xmm2, 0	// scaley_scaley_scaley_scaley
					mulps		xmm1, xmm2		// *scaley
					cvtps2dq	xmm1, xmm1		// �����ɖ߂� ����� xmm1 �� (diffy*scaley)

					paddd		xmm1, xmm5		// xmm1 = y + (diffy*scaley)
					pminsw		xmm1, xmm7		// pminsd�ɂ�����������SSE4.1�������̂�pminsw��
					pxor		xmm2, xmm2		// ����pmaxsd�ɂ�����������SSE4.1�������̂Łc
					pmaxsw		xmm1, xmm2		// 0 <= xmm1 <= height-1 �ɂȂ���

					pslld		xmm0, 2			// x*sizeof(dot)
					movd		xmm2, ebx		// ebx = srcpitch
					pshufd		xmm2, xmm2, 0	// xmm2 = srcpitch_srcpitch_srcpitch_srcpitch
					movdqa		xmm3, xmm1
					pmullw		xmm1, xmm2		// xmm1 = (y+diffy*scaley)*srcpitch�̉���16bit 
					pmulhw		xmm3, xmm2		// xmm3 = (y+diffy*scaley)*srcpitch�̏��16bit
					pslld		xmm3, 16
					por			xmm1, xmm3		// (y+diffy*scaley)��srcpitch16bit�ȓ��̐��̐����Ȃ̂�
					// xmm1 = (y+diffy*scaley)*srcpitch
					paddd		xmm0, xmm1		// 
					// �����܂ł� xmm0 = (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4
					mov			eax,  srcbuf
					movd		xmm1, eax
					pshufd		xmm1, xmm1, 0
					paddd		xmm0, xmm1
					// �����܂ł� xmm0 = srcbuf + (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4

					// *p = *src
					movd		eax,  xmm0
					mov			eax, [eax]
					mov			[edi], eax

					add			edi, 4				// p += 1dot*sizeof(dot)
					add			esi, 4				// mapp += 1dot*sizeof(dot)
					mov			eax, 1
					movd		xmm0, eax
					pshufd		xmm0, xmm0, 0
					paddd		xmm4, xmm0			// x += 1
					dec			ecx
					jnz			XLOOP1_RIGHTLOOP
			XLOOP1_end:
			}
#endif
		}
	}

	// �}�X�N�̂���shimmer�̃}���`�X���b�h�֐�
	// srcbuf �̉摜�� mapbuf �ɏ]���Ă䂪�܂��� mskbuf �̃}�X�N�����Ȃ��� dstbuf �ɓ\��t����
	void threadedShimmerWithMask(LPVOID params)
	{
		tjs_int dstwidth, dstheight, dstpitch;
		tjs_int srcwidth, srcheight, srcpitch;
		tjs_int mapwidth, mapheight, mappitch;
		tjs_int mskwidth, mskheight, mskpitch;
		tjs_int clipx, clipy, clipw, cliph;
		BYTE *dstbuf, *srcbuf, *mapbuf, *mskbuf;
		float scalex, scaley;

		ShimmerRect *p = (ShimmerRect*)params;
		dstbuf = p->dstbuf, dstwidth = p->dstwidth, dstheight = p->dstheight, dstpitch = p->dstpitch;
		srcbuf = p->srcbuf, srcwidth = p->srcwidth, srcheight = p->srcheight, srcpitch = p->srcpitch;
		mapbuf = p->mapbuf, mapwidth = p->mapwidth, mapheight = p->mapheight, mappitch = p->mappitch;
		mskbuf = p->mskbuf, mskwidth = p->mskwidth, mskheight = p->mskheight, mskpitch = p->mskpitch;
		clipx = p->clipx, clipy = p->clipy, clipw = p->clipw, cliph = p->cliph;
		scalex = p->scalex, scaley = p->scaley;
//log(L"dstbuf = 0x%04x, dstwidth = %d, dstheight = %d, dstpitch = %d", dstbuf, dstwidth, dstheight, dstpitch);
//log(L"srcbuf = 0x%04x, srcwidth = %d, srcheight = %d, srcpitch = %d", srcbuf, srcwidth, srcheight, srcpitch);
//log(L"mapbuf = 0x%04x, mapwidth = %d, mapheight = %d, mappitch = %d", mapbuf, mapwidth, mapheight, mappitch);
//log(L"mskbuf = 0x%04x, mskwidth = %d, mskheight = %d, mskpitch = %d", mskbuf, mskwidth, mskheight, mskpitch);
//log(L"clipx = %d, clipw = %d, clipy = %d, cliph = %d", clipx, clipw, clipy, cliph);
		for (int y = clipy; y < clipy+cliph; y++) {
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(dstbuf, clipx, y, dstpitch);
			BYTE *mapp = bufadr(mapbuf, p->mapx, p->mapy+y-clipy, mappitch);
			BYTE *mskp = bufadr(mskbuf, p->mskx, p->msky+y-clipy, mskpitch);
#ifndef USE_SSE2
			const int sx = int(scalex*0x10000), sy = int(scaley*0x10000);
			for (int x = clipx; x < clipx+clipw; x++) {
//				if (*mskp == 0) {
//					// mask�l�� 0 �Ȃ�A���̂܂܃R�s�[�B���̕��������B
//					// ���񂩂��Amask�l�� 0 �łȂ��̈悪�S��ʂ��ƁA20%���炢�x���Ȃ�
//					*dstp++ = *(TJSPIXEL*)srcp;
//				} else {
				{
					// �}�b�v���C���̒��ڃh�b�g�́u�X���v�𓾂�
					// �F�v�f�������g���B�}�b�v�摜�͊D�F�����炱���O.K.
					int gradx = *(mapp+TJSPIXELSIZE) - *(mapp-TJSPIXELSIZE);
					int grady = *(mapp+mappitch ) - *(mapp-mappitch );

					// �}�X�N���C�����w�肳�ꂽ���́A�Z�x�ɍ��킹�ĉe���ӏ�������
					// �u�X���v����src�摜���� x, y �𓾂�
					int srcx = x + ((gradx*sx*(*mskp)/255)>>16);
					int srcy = y + ((grady*sy*(*mskp)/255)>>16);

					// src[xy]�͈̔̓`�F�b�N�͂��Ȃ��B�摜�͏c�����[�v���Ă邩��
					// �l��������
					*dstp++ = *(TJSPIXEL*)(bufadr2(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
				}
				mapp += TJSPIXELSIZE;
				mskp += TJSPIXELSIZE;
			}
#else
			__asm {
				mov			eax,  clipx
				movd		xmm0, eax
				movss		xmm4, xmm0
				pshufd		xmm4, xmm4, 0x39	// PACK(0 3 2 1) = 4byte rotate right 
				inc			eax
				movd		xmm0, eax
				movss		xmm4, xmm0
				pshufd		xmm4, xmm4, 0x39	// PACK(0 3 2 1) = 4byte rotate right 
				inc			eax
				movd		xmm0, eax
				movss		xmm4, xmm0
				pshufd		xmm4, xmm4, 0x39	// PACK(0 3 2 1) = 4byte rotate right
				inc			eax
				movd		xmm0, eax
				movss		xmm4, xmm0
				pshufd		xmm4, xmm4, 0x39	// PACK(0 3 2 1) = 4byte rotate right 
													// xmm4 = 3_2_1_0 �ɂȂ���
				mov			eax, y
				movd		xmm5, eax
				pshufd		xmm5, xmm5, 0		// xmm5 = y+clipy_y+clipy_y+clipy_y+clipy

				mov			eax, srcwidth
				dec			eax
				movd		xmm6, eax
				pshufd		xmm6, xmm6, 0		// xmm6 = srcwidth-1_srcwidth-1_srcwidth-1_srcwidth-1

				mov			eax, srcheight
				dec			eax
				movd		xmm7, eax
				pshufd		xmm7, xmm7, 0		// xmm7 = srcheight-1_srcheight-1_srcheight-1_srcheight-1

				mov			edi, dstp
				mov			esi, mapp
				mov			ebx, srcpitch
				mov			edx, mskp
				// for (int x = clipx+1; x < clipw+clipw-1; x+=4 /*++*/) {
				mov			ecx, clipw
				sar			ecx, 2	// ecx = (ecx-2)/4
			XLOOP2:
					// ���̃��[�v�̒������Au-OP �������悤�ɖ��߂̏��Ԃ��l���Ă���
					// �Ȃ�ƂȂ�prefetch���Ƃ��H
					// prefetchnta	[edx+32]
					// prefetchnta	[esi+32]

					// ��������}�X�N�v�Z
					movdqu		xmm3, [edx]
					mov			eax,  0x437f0000	// = (float)255.0
					pslld		xmm3, 24
					movd		xmm2, eax
					psrld		xmm3, 24			// blue�̂ݔ����o��
					pshufd		xmm2, xmm2,0 
					cvtdq2ps	xmm3, xmm3
					divps		xmm3, xmm2		// xmm3 = (*mskp)/255
					// xmm3 = mask�����΂炭�ۑ����Ă���

					movdqu		xmm0, [esi+4]	// SSE2�ɂ̓��[�e�[�g���߂Ȃ��̂�
					movdqu		xmm2, [esi-4]	// ����������movqdqu���g�p
					pslld		xmm0, 24		// xmm[01] �̏�� 24 bit �� 0 �N���A
					pslld		xmm2, 24
					psrld		xmm0, 24		// PMOVZX�g������������SSE4.1�Ȃ̂Œf�O
					psrld		xmm2, 24
					psubd		xmm0, xmm2		// xmm0 = (*(xpos+1) - *(xpos-1)) = diffx
					// �����܂ł� xmm0 �� ���E4byte�̌X��(diffx)
					movd		xmm2, scalex
					cvtdq2ps	xmm0, xmm0		// ���������_�l�ɕϊ�
					pshufd		xmm2, xmm2, 0	// scalex_scalex_scalex_scalex
					mulps		xmm0, xmm2		// xmm0 = diffx*scalex
						// �}�X�N��Z����
						mulps		xmm0, xmm3	// xmm0 = diffx*scalex*mask
					cvtps2dq	xmm0, xmm0		// �����ɖ߂� ����� xmm0 �� diffx*scalex*mask

					paddd		xmm0, xmm4		// xmm0 = x + (diffx*scalex)
					pxor		xmm2, xmm2		// ����pmaxsd�ɂ�����������SSE4.1�������̂Łc
					pminsw		xmm0, xmm6		// pminsd�ɂ�����������SSE4.1�������̂�pminsw��
					pmaxsw		xmm0, xmm2		// 0 <= xmm0 <= srcwidth-1 �ɂȂ���

					mov			eax,  mappitch
					movdqu		xmm1, [esi+eax]	// ebx = +mappitch
					neg			eax
					movdqu		xmm2, [esi+eax]	// ebx = -mappitch
					pslld		xmm1, 24		// xmm[12] �̏�� 24 bit �� 0 �N���A
					pslld		xmm2, 24
					psrld		xmm1, 24
					psrld		xmm2, 24
					psubd		xmm1, xmm2		// xmm1 = (*(ypos+1) - *(ypos-1)) = diffy
					// �����܂ł� xmm1 �� �㉺4byte�̌X��(diffy)
					movd		xmm2, scaley
					cvtdq2ps	xmm1, xmm1		// ���������_�l�ɕϊ�
					pshufd		xmm2, xmm2, 0	// scaley_scaley_scaley_scaley
					mulps		xmm1, xmm2		// xmm1 = diffy*scaley
						// �}�X�N��Z����
						mulps		xmm1, xmm3		// xmm1 = diffy*scaley*mask
					cvtps2dq	xmm1, xmm1		// �����ɖ߂� ����� xmm1 �� diffy*scaley*mask

					paddd		xmm1, xmm5		// xmm1 = y + (diffy*scaley)
					pxor		xmm2, xmm2		// ����pmaxsd�ɂ�����������SSE4.1�������̂Łc
					pminsw		xmm1, xmm7		// pminsd�ɂ�����������SSE4.1�������̂�pminsw��
					pmaxsw		xmm1, xmm2		// 0 <= xmm1 <= height-1 �ɂȂ���

					movd		xmm2, ebx		// ebx = srcpitch
					pslld		xmm0, 2			// x*sizeof(dot)
					movdqa		xmm3, xmm1
					pshufd		xmm2, xmm2, 0	// xmm2 = srcpitch_srcpitch_srcpitch_srcpitch
					pmullw		xmm1, xmm2		// xmm1 = (y+diffy*scaley)*srcpitch�̉���16bit 
					pmulhw		xmm3, xmm2		// xmm3 = (y+diffy*scaley)*srcpitch�̏��16bit
					pslld		xmm3, 16
					por			xmm1, xmm3		// (y+diffy*scaley)��srcpitch16bit�ȓ��̐��̐����Ȃ̂�
					// xmm1 = (y+diffy*scaley)*srcpitch
					paddd		xmm0, xmm1		// 
					// �����܂ł� xmm0 = (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4
					mov			eax,  srcbuf
					movd		xmm1, eax
					pshufd		xmm1, xmm1, 0
					paddd		xmm0, xmm1
					// �����܂ł� xmm0 = srcbuf + (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4

					// *p = *src
					movd		eax,  xmm0
					mov			eax,  [eax]
					pshufd		xmm0, xmm0, 0x39	// PACK(0,3,2,1) = 4byte rotate
					mov			[edi],eax
					movd		eax,  xmm0
					mov			eax,  [eax]
					pshufd		xmm0, xmm0, 0x39	// PACK(0,3,2,1) = 4byte rotate
					mov			[edi+4], eax
					movd		eax,  xmm0
					mov			eax,  [eax]
					pshufd		xmm0, xmm0, 0x39	// PACK(0,3,2,1) = 4byte rotate
					mov			[edi+8], eax
					movd		eax,  xmm0
					mov			eax,  [eax]
					mov			[edi+12], eax

					mov			eax, 4
					add			edi, 16				// p += 4dot*sizeof(dot)
					movd		xmm0, eax
					add			esi, 16				// mapp += 4dot*sizeof(dot)
					pshufd		xmm0, xmm0, 0
					add			edx, 16				// mskp += 4dot*sizeof(dot)
					paddd		xmm4, xmm0			// x += 4
					dec			ecx
					jnz			XLOOP2

				// �E�[�������K�v���ǂ������f
				mov			ecx, clipw		// �E�[������ǉ�
				and			ecx, 0x3		// ecx = (clipw-2)%4
				jz			XLOOP2_end

				// �E�[�������K�v�Ȃ̂Ŏ��s�B���movdqu��movd�ɕύX���������B�}�a�ŁB
				// ���x�͒x�����o�O������Ȃ����Ƃ�D��
			XLOOP2_RIGHTLOOP:
					movd		xmm3, [edx]
					pslld		xmm3, 24
					psrld		xmm3, 24			// blue�̂ݔ����o��
					cvtdq2ps	xmm3, xmm3
					mov			eax,  0x437f0000	// = (float)255.0
					movd		xmm2, eax
					pshufd		xmm2, xmm2,0 
					divps		xmm3, xmm2		// xmm3 = (*mskp)/255
					// xmm3 = mask�����΂炭�ۑ����Ă���

					movd		xmm0, [esi+4]	// SSE2�ɂ̓��[�e�[�g���߂Ȃ��̂�
					movd		xmm2, [esi-4]	// ����������movqdqu���g�p
					pslld		xmm0, 24		// xmm[01] �̏�� 24 bit �� 0 �N���A
					psrld		xmm0, 24		// PMOVZX�g������������SSE4.1�Ȃ̂Œf�O
					pslld		xmm2, 24
					psrld		xmm2, 24
					psubd		xmm0, xmm2		// xmm0 = (*(xpos+1) - *(xpos-1)) = diffx
					// �����܂ł� xmm0 �� ���E4byte�̌X��(diffx)
					cvtdq2ps	xmm0, xmm0		// ���������_�l�ɕϊ�
					movd		xmm2, scalex
					pshufd		xmm2, xmm2, 0	// scalex_scalex_scalex_scalex
					mulps		xmm0, xmm2		// xmm0 = diffx*scalex
						// �}�X�N��Z����
						mulps		xmm0, xmm3	// xmm0 = diffx*scalex*mask
					cvtps2dq	xmm0, xmm0		// �����ɖ߂� ����� xmm0 �� diffx*scalex*mask

					paddd		xmm0, xmm4		// xmm0 = x + (diffx*scalex)
					pminsw		xmm0, xmm6		// pminsd�ɂ�����������SSE4.1�������̂�pminsw��
					pxor		xmm2, xmm2		// ����pmaxsd�ɂ�����������SSE4.1�������̂Łc
					pmaxsw		xmm0, xmm2		// 0 <= xmm0 <= srcwidth-1 �ɂȂ���

					mov			eax,  mappitch
					movdqu		xmm1, [esi+eax]	// ebx = +mappitch
					neg			eax
					movdqu		xmm2, [esi+eax]	// ebx = -mappitch
					pslld		xmm1, 24		// xmm[12] �̏�� 24 bit �� 0 �N���A
					psrld		xmm1, 24
					pslld		xmm2, 24
					psrld		xmm2, 24
					psubd		xmm1, xmm2		// xmm1 = (*(ypos+1) - *(ypos-1)) = diffy
					// �����܂ł� xmm1 �� �㉺4byte�̌X��(diffy)
					cvtdq2ps	xmm1, xmm1		// ���������_�l�ɕϊ�
					movd		xmm2, scaley
					pshufd		xmm2, xmm2, 0	// scaley_scaley_scaley_scaley
					mulps		xmm1, xmm2		// xmm1 = diffy*scaley
						// �}�X�N��Z����
						mulps		xmm1, xmm3		// xmm1 = diffy*scaley*mask
					cvtps2dq	xmm1, xmm1		// �����ɖ߂� ����� xmm1 �� diffy*scaley*mask

					paddd		xmm1, xmm5		// xmm1 = y + (diffy*scaley)
					pminsw		xmm1, xmm7		// pminsd�ɂ�����������SSE4.1�������̂�pminsw��
					pxor		xmm2, xmm2		// ����pmaxsd�ɂ�����������SSE4.1�������̂Łc
					pmaxsw		xmm1, xmm2		// 0 <= xmm1 <= height-1 �ɂȂ���

					pslld		xmm0, 2			// x*sizeof(dot)
					movd		xmm2, ebx		// ebx = srcpitch
					pshufd		xmm2, xmm2, 0	// xmm2 = srcpitch_srcpitch_srcpitch_srcpitch
					movdqa		xmm3, xmm1
					pmullw		xmm1, xmm2		// xmm1 = (y+diffy*scaley)*srcpitch�̉���16bit 
					pmulhw		xmm3, xmm2		// xmm3 = (y+diffy*scaley)*srcpitch�̏��16bit
					pslld		xmm3, 16
					por			xmm1, xmm3		// (y+diffy*scaley)��srcpitch16bit�ȓ��̐��̐����Ȃ̂�
					// xmm1 = (y+diffy*scaley)*srcpitch
					paddd		xmm0, xmm1		// 
					// �����܂ł� xmm0 = (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4
					mov			eax,  srcbuf
					movd		xmm1, eax
					pshufd		xmm1, xmm1, 0
					paddd		xmm0, xmm1
					// �����܂ł� xmm0 = srcbuf + (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4

					// *p = *src
					movd		eax,  xmm0
					mov			eax,  [eax]
					mov			[edi],eax

					add			edi, 4				// p += 4dot*sizeof(dot)
					add			esi, 4				// mapp += 4dot*sizeof(dot)
					add			edx, 4				// mskp += 4dot*sizeof(dot)
					mov			eax, 1
					movd		xmm0, eax
					pshufd		xmm0, xmm0, 0
					paddd		xmm4, xmm0			// x += 1
					dec			ecx
					jnz			XLOOP2_RIGHTLOOP
			XLOOP2_end:
			}
#endif
		}
	}
	
	/*
	 * shimmer: �摜�ɂ����낤���ʂ�^����
	 * srclayer �Ƃ��̃��C���̑傫���͓����łȂ���΂Ȃ�Ȃ��B
	 * maplayer �� �N���b�s���O�E�B���h�E�Ɠ����܂��͂�����傫���T�C�Y�łȂ���΂Ȃ�Ȃ�
	 * clipw/cliph �� 0 �̎� srclayer �Ɠ����T�C�Y�Ƃ݂Ȃ����
	 * @param srclayer �`�挳���C��
	 * @param maplayer �}�b�v�摜���C��(�����摜)
	 * @param msklayer �}�X�N�摜���C��(�����摜)
	 * @param scalex   �䂪�݂̉������g�嗦
	 * @param scaley   �䂪�݂̏c�����g�嗦
	 * @param clipx/clipy/clipw/cliph
	 */
	void shimmer(tTJSVariant srclayer, tTJSVariant maplayer, tTJSVariant msklayer, float scalex, float scaley, int clipx, int clipy, int clipw, int cliph) {

		// ������Ƃ����v�Z�̍������̂��߂ɁA���������Ă���
		const int sx = int(scalex*0x10000), sy = int(scaley*0x10000);

		tjs_int srcwidth, srcheight, srcpitch;
		tjs_int mapwidth, mapheight, mappitch;
		tjs_int mskwidth, mskheight, mskpitch;
		BYTE *srcbuf, *mapbuf, *mskbuf;
		{
			// �����C���摜���
			srcbuf    = (BYTE*)(tjs_int64)getTJSMember(srclayer, L"mainImageBuffer");
			srcwidth  = (tjs_int)getTJSMember(srclayer, L"imageWidth");
			srcheight = (tjs_int)getTJSMember(srclayer, L"imageHeight");
			srcpitch  = (tjs_int)getTJSMember(srclayer, L"mainImageBufferPitch");

			// �}�b�v���C���摜���
			mapbuf    = (BYTE*)(tjs_int64)getTJSMember(maplayer, L"mainImageBuffer");
			mapwidth  = (tjs_int)getTJSMember(maplayer, L"imageWidth");
			mapheight = (tjs_int)getTJSMember(maplayer, L"imageHeight");
			mappitch  = (tjs_int)getTJSMember(maplayer, L"mainImageBufferPitch");

			// �}�X�N���C���摜���
			if (msklayer.Type() == tvtVoid) {
				mskbuf = NULL;
				mskwidth = mskheight = mskpitch = 0;
			} else {
				mskbuf    = (BYTE*)(tjs_int64)getTJSMember(msklayer, L"mainImageBuffer");
				mskwidth  = (tjs_int)getTJSMember(msklayer, L"imageWidth");
				mskheight = (tjs_int)getTJSMember(msklayer, L"imageHeight");
				mskpitch  = (tjs_int)getTJSMember(msklayer, L"mainImageBufferPitch");
			}
		}

		// �N���b�s���O������
		if (clipw == 0)
			clipw = srcwidth;
		if (cliph == 0)
			cliph = srcheight;
		if (clipx < 0)
			clipw += clipx, clipx = 0;
		if (clipy < 0)
			cliph += clipy, clipy = 0;
		if (clipw <= 0 || cliph <= 0 || clipx >= _width || clipy >= _height)
			return;
		if (clipx+clipw > _width)
			clipw = _width - clipx;
		if (clipy+cliph > _height)
			cliph = _height - clipy;

		// �摜�T�C�Y�ɂ�����Ƃ���������K�p
		if (clipw > mapwidth || cliph > mapheight ||
			(mskbuf != NULL && (clipw > mskwidth || cliph > mskheight)))
			return;

		// �P�s�N�Z�����ƂɃ}�b�v����v�Z
		// x=0, x=width-1, y=0, y=height-1 �̎��͗v���ʈ���

		// �܂���ʂ̊O���P�h�b�g�����v�Z�B�����̓}�b�v������ɂȂ邩��B
		{
			// �ŏ�s��h�b�g��shimmer���v�Z(���������E1dot�͏���)
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+1/* = initial x */, clipy, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, +0, +0, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +2, +0, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +1,  0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +1, +1, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  1,  0, mskpitch);
			int srcx, srcy;
			for (int x = clipx+1; x < clipx+clipw-2; x++) {
				if (msklayer.Type() == tvtVoid) {
					// �}�X�N���Ȃ������ꍇ
					srcx = x     + (((*mapx2-*mapx1)*sx)>>16);
					srcy = clipy + (((*mapy2-*mapy1)*sy)>>16);
				} else {
					// �}�X�N���������ꍇ
					srcx = x     + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
					srcy = clipy + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
				}
				*dstp++ = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
				mapx1 += TJSPIXELSIZE, mapx2 += TJSPIXELSIZE;
				mapy1 += TJSPIXELSIZE, mapy2 += TJSPIXELSIZE;
				mskp  += TJSPIXELSIZE;
			}
		}

		{
			// �ŉ��s��h�b�g��shimmer���v�Z(���������E1dot�͏���)
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+1, clipy+cliph-1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, +0, cliph-1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +2, cliph-1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +1, cliph-2, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +1, cliph-1, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  1, cliph-1, mskpitch);
			int srcx, srcy;
			for (int x = clipx+1; x < clipx+clipw-2; x++) {
				if (msklayer.Type() == tvtVoid) { // �}�X�N���Ȃ������ꍇ
					srcx = x             + (((*mapx2-*mapx1)*sx)>>16);
					srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy)>>16);
				} else { // �}�X�N���������ꍇ
					srcx = x             + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
					srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
				}
				*dstp++ = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
				mapx1 += TJSPIXELSIZE, mapx2 += TJSPIXELSIZE;
				mapy1 += TJSPIXELSIZE, mapy2 += TJSPIXELSIZE;
				mskp  += TJSPIXELSIZE;
			}
		}

		{
			// �ō����h�b�g��shimmer���v�Z(�������㉺1dot�͏���)
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx, clipy+1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf,  0, +1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +1, +1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +0, +0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +0, +2, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  0,  1, mskpitch);
			int srcx, srcy;
			for	(int y = clipy+1; y < clipy+cliph-2; y++) {
				if (msklayer.Type() == tvtVoid) { // �}�X�N���Ȃ������ꍇ
					srcx = clipx + (((*mapx2-*mapx1)*sx)>>16);
					srcy = y     + (((*mapy2-*mapy1)*sy)>>16);
				} else { // �}�X�N���������ꍇ
					srcx = clipx + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
					srcy = y     + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
				}
				*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
				dstp  += _pitch/sizeof(*dstp);
				mapx1 += mappitch, mapx2 += mappitch;
				mapy1 += mappitch, mapy2 += mappitch;
				mskp  += mskpitch;
			}
		}

		{
			// �ŉE���h�b�g��shimmer���v�Z(�������㉺1dot�͏���)
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+clipw-1, clipy+1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, clipw-2, +1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, clipw-1, +1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, clipw-1, +0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, clipw-1, +2, mappitch);
			BYTE *mskp  = bufadr(mskbuf, clipw-1,  1, mskpitch);
			int srcx, srcy;
			for (int y = clipy+1; y < clipy+cliph-2; y++) {
				if (msklayer.Type() == tvtVoid) { // �}�X�N���Ȃ������ꍇ
					srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx)>>16);
					srcy = y             + (((*mapy2-*mapy1)*sy)>>16);
				} else { // �}�X�N���������ꍇ
					srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
					srcy = y             + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
				}
				*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
				dstp  += _pitch/sizeof(*dstp);
				mapx1 += mappitch, mapx2 += mappitch;
				mapy1 += mappitch, mapy2 += mappitch;
				mskp  += mskpitch;
			}
		}

		{
			// �����h�b�g��shimmer���v�Z
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx, clipy, _pitch);
			BYTE *mapx1 = bufadr(mapbuf,  0, +0, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +1, +0, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +0,  0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +0, +1, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  0,  0, mskpitch);
			int srcx, srcy;
			if (msklayer.Type() == tvtVoid) { // �}�X�N���Ȃ������ꍇ
				srcx = clipx + (((*mapx2-*mapx1)*sx)>>16);
				srcy = clipy + (((*mapy2-*mapy1)*sy)>>16);
			} else { // �}�X�N���������ꍇ
				srcx = clipx + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
				srcy = clipy + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
			}
			*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
		}

		{
			// �E���h�b�g��shimmer���v�Z
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+clipw-1, clipy, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, clipw-2, +0, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, clipw-1, +0, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, clipw-1,  0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, clipw-1, +1, mappitch);
			BYTE *mskp  = bufadr(mskbuf, clipw-1,  0, mskpitch);
			int srcx, srcy;
			if (msklayer.Type() == tvtVoid) { // �}�X�N���Ȃ������ꍇ
				srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx)>>16);
				srcy = clipy         + (((*mapy2-*mapy1)*sy)>>16);
			} else { // �}�X�N���������ꍇ
				srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
				srcy = clipy         + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
			}
			*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
		}

		{
			// ������h�b�g��shimmer���v�Z
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx, clipy+cliph-1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf,  0, cliph-1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +1, cliph-1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +0, cliph-2, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +0, cliph-1, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  0, cliph-1, mskpitch);
			int srcx, srcy;
			if (msklayer.Type() == tvtVoid) { // �}�X�N���Ȃ������ꍇ
				srcx = clipx         + (((*mapx2-*mapx1)*sx)>>16);
				srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy)>>16);
			} else { // �}�X�N���������ꍇ
				srcx = clipx         + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
				srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
			}
			*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
		}

		{
			// �E����h�b�g��shimmer���v�Z
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+clipw-1, clipy+cliph-1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, clipw-2, cliph-1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, clipw-1, cliph-1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, clipw-1, cliph-2, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, clipw-1, cliph-1, mappitch);
			BYTE *mskp  = bufadr(mskbuf, clipw-1, cliph-1, mskpitch);
			int srcx, srcy;
			if (msklayer.Type() == tvtVoid) { // �}�X�N���Ȃ������ꍇ
				srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx)>>16);
				srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy)>>16);
			} else { // �}�X�N���������ꍇ
				srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
				srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
			}
			*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
		}

		clipx +=1, clipw -= 2, clipy += 1, cliph -= 2;
		if (clipw <= 0 || cliph <= 0 || clipx >= _width || clipy >= _height)
			return;
		// �����܂łŁA�㉺���E��1dot�͑S��shimmer�ς�

		// ��������AthreadNum �̃X���b�h������āAthreadedShimmer �����s

		ShimmerRect defRect = {
			/*.dstbuf =*/ _buffer,/*.dstwidth =*/ _width,   /*.dstheight =*/ _height,   /*.dstpitch =*/ _pitch,
			/*.srcbuf =*/ srcbuf, /*.srcwidth =*/ srcwidth, /*.srcheight =*/ srcheight, /*.srcpitch =*/ srcpitch,
			/*.mapbuf =*/ mapbuf, /*.mapwidth =*/ mapwidth, /*.mapheight =*/ mapheight, /*.mappitch =*/ mappitch,
			/*.mskbuf =*/ mskbuf, /*.mskwidth =*/ mskwidth, /*.mskheight =*/ mskheight, /*.mskpitch =*/ mskpitch,
			/*.clipx =*/ clipx, /*.clipy =*/ clipy, /*.clipw =*/ clipw, /*.cliph =*/cliph,
			/*.scalex =*/ scalex, /*.scaley =*/ scaley,
			/*.mapx =*/ 1, /*.mapy =*/ 1, /*.mskx = */ 1, /*.msky = */ 1
		};
#ifndef MULTI_THREAD
		// �V���O���X���b�h�̏ꍇ
		if (msklayer.Type() == tvtVoid) // �}�X�N���Ȃ������ꍇ
			threadedShimmer((LPVOID)&defRect);
		else // �}�X�N���������ꍇ
			threadedShimmerWithMask((LPVOID)&defRect);
#else
		// �}���`�X���b�h�̏ꍇ
		tjs_int threadNum = threadPool.getThreadNum();
		tjs_int divh = cliph/threadNum;
		ShimmerRect rectAry[MAXTHREADNUM];
		tjs_int y, thread;
		for (thread = 0, y = clipy; thread < threadNum-1; thread++, y += divh) {
			rectAry[thread]       = defRect;
			rectAry[thread].clipy = y;
			rectAry[thread].cliph = divh;
			rectAry[thread].mapy  = 1 + divh*thread;
			rectAry[thread].msky  = 1 + divh*thread;
			if (msklayer.Type() == tvtVoid) // �}�X�N���Ȃ������ꍇ
				threadPool.run(this, &layerExShimmer::threadedShimmer, (void*)(rectAry+thread));
			else // �}�X�N���������ꍇ
				threadPool.run(this, &layerExShimmer::threadedShimmerWithMask, (void*)(rectAry+thread));
		}
		// �Ō�͒[���̍�������␳����K�v����
		rectAry[thread]       = defRect;
		rectAry[thread].clipy = y;
		rectAry[thread].cliph = cliph-divh*(threadNum-1);
		rectAry[thread].mapy  = 1 + divh*(threadNum-1);
		rectAry[thread].msky  = 1 + divh*(threadNum-1);
		if (msklayer.Type() == tvtVoid) // �}�X�N���Ȃ������ꍇ
			threadedShimmer((LPVOID)(rectAry+thread));			// �Ō�̃X���b�h�͂���������������
		else // �}�X�N���������ꍇ
			threadedShimmerWithMask((LPVOID)(rectAry+thread));	// �Ō�̃X���b�h�͂���������������
		// �S���̃X���b�h���I���܂ő҂�
		threadPool.waitForAllThreads();
#endif
	}



	// threadedShimmerBuildMap*()�ɓn���\����
	typedef struct {
		BYTE *dstbuf;  tjs_int dstwidth, dstheight, dstpitch;
		BYTE *map1buf; tjs_int map1width, map1height, map1pitch;
		BYTE *map2buf; tjs_int map2width, map2height, map2pitch;
		tjs_int map1x, map1y, map2x, map2y;
		tjs_int starty;
	} ShimmerMaps;


	// �}�b�v��ʂ��쐬����B�ꖇ�����}�b�v���Ȃ��ꍇ�B�^�C����ɂ����\��t���邾��
	void threadedShimmerBuildMap(LPVOID params)
	{
		BYTE    *map1buf;
		tjs_int map1width, map1height, map1pitch;
		tjs_int map1x, map1y;

		ShimmerMaps *p = (ShimmerMaps*)params;
		map1buf   = p->map1buf;
		map1width = p->map1width, map1height = p->map1height, map1pitch = p->map1pitch;
		map1x     = ZERO2MAX(p->map1x,           p->map1width);
		map1y     = ZERO2MAX(p->map1y+p->starty, p->map1height);
#ifndef USE_SSE2
		for (int y = p->starty; y < p->dstheight; y++) {
			BYTE *dstp = bufadr(p->dstbuf, 0, y, p->dstpitch);
			for	(int x = 0; x < p->dstwidth; x++) {
				BYTE col = *bufadr2(map1buf, x-map1x, y-map1y, map1width, map1height, map1pitch);
				*dstp = *bufadr2(map1buf, x-map1x, y-map1y, map1width, map1height, map1pitch);
				dstp += TJSPIXELSIZE;
			}
		}
#else
		int dstwidth = p->dstwidth;				// �}�N���炵���̂ŕϐ��ɑ�����Ƃ�
		int map1w_x4 = map1width*4;
		int map1lw   = min(map1x, p->dstwidth);						// �����̕`�敝�[��
		int map1ccnt = max(0, (p->dstwidth - map1lw))/map1width;	// �����̕`��J�ԉ�
		int map1rw   = p->dstwidth - map1lw - map1ccnt*map1width;	// �E�[�̕`�敝�[��
//log(L"map1lw = %d, map1ccnt = %d, map1rw = %d", map1lw, map1ccnt, map1rw);
		for (int y = p->starty; y < p->dstheight; y++) {
			BYTE *dstp = bufadr(p->dstbuf, 0, y, p->dstpitch);
			BYTE *map1p = bufadr2(map1buf, 0-map1x, y-map1y, map1width, map1height, map1pitch);
			__asm {
				mov		esi, map1p
				mov		edi, dstp
					
			//BUILDMAPA_LEFT:
				// ���[�[���`��
				mov		ecx, map1lw
				or		ecx, ecx
				jz		BUILDMAPA_CENTER
				sub		ecx, 4
				jb		BUILDMAPA_LEFTLOOP1_NEXT
			BUILDMAPA_LEFTLOOP1:
				// prefetchnta	[esi+32]	// �܂��v���t�F�b�`���Ƃ�
				// �ŏ��A4dot�P�ʂŏ�������
				movdqu	xmm0, [esi]
				movdqa 	[edi],xmm0		// �L���b�V���������Ȃ�(movntdq)���ƒx��
				add		esi,  16
				add		edi,  16
				sub		ecx,  4
				ja		BUILDMAPA_LEFTLOOP1
			BUILDMAPA_LEFTLOOP1_NEXT:
				add		ecx,  4
				jnz		BUILDMAPA_LEFTLOOP2
				sub		esi,  map1w_x4
				jmp		BUILDMAPA_CENTER
			BUILDMAPA_LEFTLOOP2:
				// 4dot�����̏�������
				mov		eax,  [esi]
				mov		[edi],eax
				add		esi,  4
				add		edi,  4
				dec		ecx
				jnz		BUILDMAPA_LEFTLOOP2
				sub		esi,  map1w_x4

			BUILDMAPA_CENTER:
				// ���S�̕`�惋�[�v
				mov		ebx, map1ccnt;
				or		ebx, ebx
				jz		BUILDMAPA_RIGHT
			BUILDMAPA_CENTERLOOP1:
				mov		ecx, map1width
				sar		ecx, 2		// 4dot�P�ʂȂ̂�1/4����
			BUILDMAPA_CENTERLOOP2:
				prefetchnta	[esi+32]	// �܂��v���t�F�b�`���Ƃ�
				// map1w��4dot�P�ʂȂ̂ł������ꂾ�������l���Ȃ�
				movdqu	xmm0, [esi]
				movdqu	[edi],xmm0
				add		esi,  16
				add		edi,  16
				dec		ecx
				jnz		BUILDMAPA_CENTERLOOP2
				sub		esi,  map1w_x4
				dec		ebx
				jnz		BUILDMAPA_CENTERLOOP1

			BUILDMAPA_RIGHT:
				// �E�[�[���`��
				mov		ecx, map1rw
				or		ecx, ecx
				jz		BUILDMAPA_END
				sub		ecx, 4
				jb		BUILDMAPA_RIGHTLOOP1_NEXT
			BUILDMAPA_RIGHTLOOP1:
				prefetchnta	[esi+32]	// �܂��v���t�F�b�`���Ƃ�
				// �ŏ��A4dot�P�ʂŏ�������
				movdqu	xmm0, [esi]
				movdqu	[edi],xmm0
				add		esi,  16
				add		edi,  16
				sub		ecx,  4
				ja		BUILDMAPA_RIGHTLOOP1
			BUILDMAPA_RIGHTLOOP1_NEXT:
				add		ecx,  4
				jz		BUILDMAPA_END
			BUILDMAPA_RIGHTLOOP2:
				// 4dot�����̏�������
				mov		eax,  [esi]
				mov		[edi],eax
				add		esi,  4
				add		edi,  4
				dec		ecx
				jnz		BUILDMAPA_RIGHTLOOP2
			BUILDMAPA_END:
			}
		}
#endif
	}
		

	// �}�b�v��ʂ��쐬����B�}�b�v���񖇂���ꍇ�B�������������Ȃ���^�C����ɓ\��t����
	void threadedShimmerBuildMapWithMap2(LPVOID params)
	{
		BYTE    *map1buf, *map2buf;
		tjs_int map1width, map1height, map1pitch;
		tjs_int map2width, map2height, map2pitch;
		tjs_int map1x, map1y, map2x, map2y;

		ShimmerMaps *p = (ShimmerMaps*)params;
		map1buf   = p->map1buf;
		map1width = p->map1width, map1height = p->map1height, map1pitch = p->map1pitch;
		map1x     = ZERO2MAX(p->map1x,           p->map1width);
		map1y     = ZERO2MAX(p->map1y+p->starty, p->map1height);
		map2buf   = p->map2buf;
		map2width = p->map2width, map2height = p->map2height, map2pitch = p->map2pitch;
		map2x     = ZERO2MAX(p->map2x,           p->map2width);
		map2y     = ZERO2MAX(p->map2y+p->starty, p->map2height);
#ifndef USE_SSE2
		for (int y = p->starty; y < p->dstheight; y++) {
			BYTE *dstp = bufadr(p->dstbuf, 0, y, p->dstpitch);
			for	(int x = 0; x < p->dstwidth; x++) {
				BYTE *map1p = bufadr2(map1buf, x-map1x, y-map1y, map1width, map1height, map1pitch);
				BYTE *map2p = bufadr2(map2buf, x-map2x, y-map2y, map2width, map2height, map2pitch);
				BYTE col = (*map1p + *map2p)/2;
				*dstp++ = col;
				*dstp++ = col;
				*dstp++ = col;
				*dstp++ = 255;
//				*dstp = (*map1p + *map2p)/2;
//				dstp += TJSPIXELSIZE;
			}
		}
#else
		int dstwidth = p->dstwidth;			// �}�N���炵���̂ŕϐ��ɑ�����Ƃ�
		int map1w_x4 = map1width*4;
		int map2w_x4 = map2width*4;
		if (map1x%4 == 0 && map2x%4 == 0) {	// 4�Ŋ���؂�鎞�� movdqa ���g����
//log(L"map1x = %d, map1w = %d, map2x = %d, map2w = %d", map1x, map1w, map2x, map2w);
			int map1w_d4 = map1width/4, map2w_d4 = map2width/4;
			int map1lw_d4= map1x/4;			// map1���[�̕`�敝�[��
			int map2lw_d4= map2x/4;			// map2���[�̕`�敝�[��
			if (map1lw_d4 == 0)
				map1lw_d4 = map1w_d4;
			if (map2lw_d4 == 0)
				map2lw_d4 = map2w_d4;
//log(L"_buffer = 0x%08x", _buffer);
//log(L"map1lw_d4 = %d, map2lw_d4 = %d", map1lw_d4, map2lw_d4);
			for (int y = p->starty; y < p->dstheight; y++) {
				BYTE *dstp  = bufadr(p->dstbuf, 0, y, p->dstpitch);
				BYTE *map1p = bufadr2(map1buf, 0-map1x, y-map1y, map1width, map1height, map1pitch);
				BYTE *map2p = bufadr2(map2buf, 0-map2x, y-map2y, map2width, map2height, map2pitch);
//log(L"p = 0x%08x", p);
//log(L"map1p = 0x%08x, map2p = 0x%08x", map1p, map2p);
//log(L"map1ptop = 0x%08x, map2ptop = 0x%08x",
//	bufadr2(map1buf, 0, y-map1y, map1w, map1h, map1pitch),
//	bufadr2(map2buf, 0, y-map2y, map2w, map2h, map2pitch));
				__asm {
					mov		esi, map1p
					mov		edx, map2p
					mov		edi, dstp
					mov		ecx, dstwidth
					sar		ecx, 2		// dstwidth/4
					mov		eax, map1lw_d4	// eax = map1 loop count/4
					mov		ebx, map2lw_d4	// ebx = map2 loop count/4
				BUILDMAPC_LOOP:
					cmp		ecx, ebx
					ja		BUILDMAPC_EAX_EBX_ARE_SMALLER
					cmp		ecx, eax
					jbe		BUILDMAPC_ECX_IS_SMALLEST
				BUILDMAPC_EAX_IS_SMALLEST:
					sub		ebx, eax	// ��Ɉ����Ă���
					sub		ecx, eax
					inc		ecx
				BUILDMAPC_LOOP_EAX:
//					prefetchnta	[esi+32]
//					prefetchnta	[edx+32]
					movdqa	xmm0, [esi]
					pavgb	xmm0, [edx]	// ���ϒl�����
					movntdq	[edi],xmm0	// �L���b�V���������Ȃ��B����movdqa������
					add		esi,  16
					add		edx,  16
					add		edi,  16
					dec		eax
					jnz		BUILDMAPC_LOOP_EAX
					mov		eax,  map1w_d4
					sub		esi,  map1w_x4
					or		ebx,  ebx
					jnz		BUILDMAPC_LOOP_EAX_NEXT
					mov		ebx,  map2w_d4
					sub		edx,  map2w_x4
				BUILDMAPC_LOOP_EAX_NEXT:
					dec		ecx
					jnz		BUILDMAPC_LOOP
					jmp		BUILDMAPC_LOOP_END

				BUILDMAPC_EAX_EBX_ARE_SMALLER:
					cmp		ebx, eax
					ja		BUILDMAPC_EAX_IS_SMALLEST
				// BUILDMAPC_EBX_IS_SMALLEST:
					sub		eax, ebx	// ��Ɉ����Ă���
					sub		ecx, ebx
					inc		ecx
				BUILDMAPC_LOOP_EBX:
//					prefetchnta	[esi+32]
//					prefetchnta	[edx+32]
					movdqa	xmm0, [esi]
					pavgb	xmm0, [edx]	// ���ϒl�����
					movntdq	[edi],xmm0	// �L���b�V���������Ȃ��B����movdqa������
					add		esi,  16
					add		edx,  16
					add		edi,  16
					dec		ebx
					jnz		BUILDMAPC_LOOP_EBX
					mov		ebx,  map2w_d4
					sub		edx,  map2w_x4
					or		eax,  eax
					jnz		BUILDMAPC_LOOP_EBX_NEXT
					mov		eax,  map1w_d4
					sub		esi,  map1w_x4
				BUILDMAPC_LOOP_EBX_NEXT:
					dec		ecx
					jnz		BUILDMAPC_LOOP
					jmp		BUILDMAPC_LOOP_END

				BUILDMAPC_ECX_IS_SMALLEST:
					// sub		eax, ecx
					// sub		ebx, ecx
				BUILDMAPC_LOOP_ECX:
//					prefetchnta	[esi+32]
//					prefetchnta	[edx+32]
					movdqa	xmm0, [esi]
					pavgb	xmm0, [edx]	// ���ϒl�����
					movntdq	[edi],xmm0	// �L���b�V���������Ȃ��B����movdqa������
					add		esi,  16
					add		edx,  16
					add		edi,  16
					dec		ecx
					jnz		BUILDMAPC_LOOP_ECX
				BUILDMAPC_LOOP_END:
				}
			}
		} else {
			// 4dot �P�ʂł͂Ȃ��ꍇ
			int map1lw = min(map1x, p->dstwidth);	// map1���[�̕`�敝�[��
			int map2lw = min(map2x, p->dstwidth);	// map2���[�̕`�敝�[��
			if (map1lw == 0)
				map1lw = map1width;
			if (map2lw == 0)
				map2lw = map2width;
			for (int y = p->starty; y < p->dstheight; y++) {
				BYTE *dstp  = bufadr(p->dstbuf, 0, y, p->dstpitch);
				BYTE *map1p = bufadr2(map1buf, 0-map1x, y-map1y, map1width, map1height, map1pitch);
				BYTE *map2p = bufadr2(map2buf, 0-map2x, y-map2y, map2width, map2height, map2pitch);
				__asm {
					mov		esi, map1p
					mov		edx, map2p
					mov		edi, dstp
					mov		ecx, dstwidth
					mov		eax, map1lw		// eax = map1 loop count
					mov		ebx, map2lw		// ebx = map2 loop count
				BUILDMAPB_LOOP:
					cmp		ecx, ebx
					ja		BUILDMAPB_EAX_EBX_ARE_SMALLER
					cmp		ecx, eax
					jbe		BUILDMAPB_ECX_IS_SMALLEST
				BUILDMAPB_EAX_IS_SMALLEST:
					sub		ebx, eax
					sub		ecx, eax
					inc		ecx
				BUILDMAPB_LOOP_EAX:
//					prefetchnta	[esi+32]	// �Ȃ��������������B1160:1264���炢�B
//					prefetchnta	[edx+32]
					movd	xmm0, [esi]
					movd	xmm1, [edx]
					pavgb	xmm0, xmm1	// ���ϒl�����
					movd	[edi],xmm0
					add		esi,  4
					add		edx,  4
					add		edi,  4
					dec		eax
					jnz		BUILDMAPB_LOOP_EAX
					mov		eax,  map1width
					sub		esi,  map1w_x4
					or		ebx,  ebx
					jnz		BUILDMAPB_LOOP_EAX_NEXT
					mov		ebx,  map2width
					sub		edx,  map2w_x4
				BUILDMAPB_LOOP_EAX_NEXT:
					dec		ecx
					jnz		BUILDMAPB_LOOP
					jmp		BUILDMAPB_LOOP_END

				BUILDMAPB_EAX_EBX_ARE_SMALLER:
					cmp		ebx, eax
					ja		BUILDMAPB_EAX_IS_SMALLEST
				// BUILDMAPB_EBX_IS_SMALLEST:
					sub		eax, ebx
					sub		ecx, ebx
					inc		ecx
				BUILDMAPB_LOOP_EBX:
//					prefetchnta	[esi+32]	// �Ȃ��������������B1160:1264���炢�B
//					prefetchnta	[edx+32]
					movd	xmm0, [esi]
					movd	xmm1, [edx]
					pavgb	xmm0, xmm1	// ���ϒl�����
					movd	xmm1, [edx]
					movd	[edi],xmm0
					add		esi,  4
					add		edx,  4
					add		edi,  4
					dec		ebx
					jnz		BUILDMAPB_LOOP_EBX
					mov		ebx,  map2width
					sub		edx,  map2w_x4
					or		eax, eax
					jnz		BUILDMAPB_LOOP_EBX_NEXT
					mov		eax,  map1width
					sub		esi,  map1w_x4
				BUILDMAPB_LOOP_EBX_NEXT:
					dec		ecx
					jnz		BUILDMAPB_LOOP
					jmp		BUILDMAPB_LOOP_END

				BUILDMAPB_ECX_IS_SMALLEST:
					// sub		eax, ecx
					// sub		ebx, ecx
				BUILDMAPB_LOOP_ECX:
//					prefetchnta	[esi+32]
//					prefetchnta	[edx+32]
					movd	xmm0, [esi]
					movd	xmm1, [edx]
					pavgb	xmm0, xmm1		// ���ϒl�����
					movd	[edi],xmm0
					add		esi,  4
					add		edx,  4
					add		edi,  4
					dec		ecx
					jnz		BUILDMAPB_LOOP_ECX
				BUILDMAPB_LOOP_END:
				}
			}
		}
#endif
	}


	/*
	 * shimmerBuildMap:  �z�����ʗp�̃}�b�v�摜���쐬����
	 * �킩���Ă�悱��Ȃ̂��������@�����Ă̂͂��I
	 * ���̃��C���́Ashimmer���郌�C���Ɠ����T�C�Y�ł���K�v������
	 * maplayer1 �y�� maplayer2 �̃T�C�Y�͔C�ӁB
	 * @param maplayer1		���}�b�v�摜���C��1(�����摜)
	 * @param map1x, map1y	���}�b�v�摜���C���̎Q�ƈʒu
	 * @param maplayer2		���}�b�v�摜���C��2(�����摜)
	 * @param map2x, map2y	���}�b�v�摜���C���̎Q�ƈʒu
	 */
	void shimmerBuildMap(tTJSVariant maplayer1, tjs_int map1x, tjs_int map1y, tTJSVariant maplayer2, tjs_int map2x, tjs_int map2y) {

//log(L"width = %d", _width);
		ShimmerMaps defMap;
		defMap.dstbuf    = _buffer;
		defMap.dstwidth  = _width;
		defMap.dstheight = _height;
		defMap.dstpitch  = _pitch;

		// ���}�b�v���C��1�摜���
		{
			defMap.map1buf    = (BYTE*)(tjs_int64)getTJSMember(maplayer1, L"mainImageBuffer");
			defMap.map1width  = (tjs_int)getTJSMember(maplayer1, L"imageWidth");
			defMap.map1height = (tjs_int)getTJSMember(maplayer1, L"imageHeight");
			defMap.map1pitch  = (tjs_int)getTJSMember(maplayer1, L"mainImageBufferPitch");
			defMap.map1x      = map1x;
			defMap.map1y      = map1y;
		}

		if (maplayer2.Type() == tvtVoid) {
			defMap.map2buf = NULL;
			defMap.map2width = defMap.map2height = defMap.map2pitch = defMap.map2x = defMap.map2y = 0;
		} else {
			// ���}�b�v���C��1�摜���
			defMap.map2buf    = (BYTE*)(tjs_int64)getTJSMember(maplayer2, L"mainImageBuffer");
			defMap.map2width  = (tjs_int)getTJSMember(maplayer2, L"imageWidth");
			defMap.map2height = (tjs_int)getTJSMember(maplayer2, L"imageHeight");
			defMap.map2pitch  = (tjs_int)getTJSMember(maplayer2, L"mainImageBufferPitch");
			defMap.map2x      = map2x;
			defMap.map2y      = map2y;
		}
		defMap.starty = 0;

//#ifndef MULTI_THREAD	// �}���`�X���b�h�ɂ���ƒx�������̂ŁA�K���V���O���X���b�h�ŏ�������
#if 1
		// �V���O���X���b�h�̏ꍇ
		if (defMap.map2buf == NULL) // �}�X�N���Ȃ������ꍇ
			threadedShimmerBuildMap((LPVOID)&defMap);
		else // �}�X�N���������ꍇ
			threadedShimmerBuildMapWithMap2((LPVOID)&defMap);
#else
		// �}���`�X���b�h�̏ꍇ
		tjs_int threadNum = threadPool.GetNumThreads();
		tjs_int divh = defMap.dstheight/threadNum;
		ShimmerMaps mapAry[MAXTHREADNUM];
		tjs_int y, thread;
		for (thread = 0, y = 0; thread < threadNum-1; thread++, y += divh) {
			mapAry[thread]        = defMap;
			mapAry[thread].starty = y;
			if (defMap.map2buf == NULL) // �}�b�v���C��2���Ȃ������ꍇ
				threadPool.QueueRequest(this, &layerExShimmer::threadedShimmerBuildMap, (void*)(mapAry+thread));
			else // �}�b�v���C��2���������ꍇ
				threadPool.QueueRequest(this, &layerExShimmer::threadedShimmerBuildMapWithMap2, (void*)(mapAry+thread));
		}
		mapAry[thread]        = defMap;
		mapAry[thread].starty = y;
		if (defMap.map2buf == NULL) // �}�b�v���C��2���Ȃ������ꍇ
			threadedShimmerBuildMap((LPVOID)(mapAry+thread));			// �Ō�̃X���b�h�͂���������������
		else // �}�b�v���C��2���������ꍇ
			threadedShimmerBuildMapWithMap2((LPVOID)(mapAry+thread));	// �Ō�̃X���b�h�͂���������������
		// �S���̃X���b�h���I���܂ő҂�
		threadPool.WaitForAllThreads();
#endif
	}
};

// ----------------------------------- �N���X�̓o�^

NCB_GET_INSTANCE_HOOK(layerExShimmer)
{
	// �C���X�^���X�Q�b�^
	NCB_INSTANCE_GETTER(objthis) { // objthis �� iTJSDispatch2* �^�̈����Ƃ���
		ClassT* obj = GetNativeInstance(objthis);	// �l�C�e�B�u�C���X�^���X�|�C���^�擾
		if (!obj) {
			obj = new ClassT(objthis);				// �Ȃ��ꍇ�͐�������
			SetNativeInstance(objthis, obj);		// objthis �� obj ���l�C�e�B�u�C���X�^���X�Ƃ��ēo�^����
		}
		obj->reset();
		return obj;
	}
	// �f�X�g���N�^�i���ۂ̃��\�b�h���Ă΂ꂽ��ɌĂ΂��j
	~NCB_GET_INSTANCE_HOOK_CLASS () {
	}
};


// �t�b�N���A�^�b�`
NCB_ATTACH_CLASS_WITH_HOOK(layerExShimmer, Layer) {
	NCB_METHOD(shimmer);
	NCB_METHOD(shimmerBuildMap);
}
