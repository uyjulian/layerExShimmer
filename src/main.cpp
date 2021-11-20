#include "ncbind.hpp"
#include "layerExBase.hpp"
#include <stdio.h>
#include <process.h>
#include "KThreadPool.h"


#define USE_SSE2		// SSE2を使う
#define MULTI_THREAD	// マルチスレッドにする


// 一ドットの型
typedef DWORD TJSPIXEL;
// 一ドットのバイト数
#define TJSPIXELSIZE (sizeof(TJSPIXEL))

/**
 * ログ出力用
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
 * 座標からバッファアドレスを求めるインライン関数
 */
inline static BYTE* bufadr(BYTE *buf, UINT x, UINT y, UINT pitch)
{
  return buf + y*pitch + x*TJSPIXELSIZE;
}

// マイナス値も含め、0からmax-1までの間に値をそろえる。オーバーフロー時はループする
#define ZERO2MAX(num, max) ((((num)%(max))+(max))%(max))
// マイナスも含め、0からmax-1までの間に値をそろえる。オーバーフロー時は0〜MAXまでの間に丸める
#define ZERO2MAX2(num, max) ((num) < 0 ? 0 : (num) >= (max) ? (max)-1 : (num))

/*
 * 座標からバッファアドレスを求めるインライン関数(座標ループ版)
 */
inline static BYTE* bufadr2(BYTE *buf, int x, int y, int width, int height, int pitch)
{
  return buf + ZERO2MAX(y,height)*pitch + ZERO2MAX(x,width)*TJSPIXELSIZE;
}
/*
 * 座標からバッファアドレスを求めるインライン関数(座標丸め版)
 */
inline static BYTE* bufadr3(BYTE *buf, int x, int y, int width, int height, int pitch)
{
  return buf + ZERO2MAX2(y,height)*pitch + ZERO2MAX2(x,width)*TJSPIXELSIZE;
}


/*
 * TJSのLayerクラスのメンバを得る
 */
inline static tTJSVariant getTJSMember(tTJSVariant instance, const wchar_t param[])
{
	iTJSDispatch2 *obj = instance.AsObjectNoAddRef();
	tTJSVariant val;
	obj->PropGet(0, param, NULL, &val, obj);
	return val;
} 

/*
 * かげろう効果用関数 shimmer() 追加
 */
class layerExShimmer : public layerExBase
{
	KThreadPool<layerExShimmer> threadPool; // スレッドプール(defでCPU数=Thread数)

#ifndef TVPMaxThreadNum
	static const tjs_int  TVPMaxThreadNum = 8;	// これdllから利用できないので定義
#endif
	// スレッド終了をEventで待つと重いから、スレッド数２くらいで頭打ちだったので
	static const tjs_int MAXTHREADNUM = TVPMaxThreadNum;

public:
	// コンストラクタ
	layerExShimmer(DispatchT obj) : layerExBase(obj)
	{
	}

	// threadedShimmer*() に渡す引数構造体
	typedef struct {
		BYTE *dstbuf; tjs_int dstwidth, dstheight, dstpitch;
		BYTE *srcbuf; tjs_int srcwidth, srcheight, srcpitch;
		BYTE *mapbuf; tjs_int mapwidth, mapheight, mappitch;
		BYTE *mskbuf; tjs_int mskwidth, mskheight, mskpitch;
		tjs_int clipx, clipy, clipw, cliph;
		float scalex, scaley;
		tjs_int mapx, mapy, mskx, msky;
	} ShimmerRect;


	// マスクのないshimmerのマルチスレッド関数
	// srcbuf の画像を mapbuf に従ってゆがませながら dstbuf に貼り付ける
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
				// マップレイヤの注目ドットの「傾き」を得る
				// 青色要素だけを使う。マップ画像は灰色だからこれでO.K.
				int gradx = *(mapp+TJSPIXELSIZE) - *(mapp-TJSPIXELSIZE);
				int grady = *(mapp+mappitch    ) - *(mapp-mappitch    );

				// 「傾き」からsrc画像中の x, y を得る
				int srcx = x + ((gradx*sx)>>16);
				int srcy = y + ((grady*sy)>>16);

				// src[xy]の範囲チェックはしない。画像は縦横ループしてるから
				// 値書き込み
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
													// xmm4 = clipx+3_clipx+2_clipx+1_clipx+0 になった
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
				// sub			ecx, 2	これは既に実施済みなので不要			// 右端・左端は処理しないので、幅は clipw-2
				sar			ecx, 2				// (clipw-2)/4。4dotごとなので
				jz			XLOOP1_4DOT_END
			XLOOP1:
					// なんとなくprefetchしとく？
					// prefetchnta	[esi+32]
					// このループの中だけ、u-OP が効くように命令の順番を考えている
					movdqu		xmm2, [esi-4]	// 効率悪いがmovdquを使用
					movdqu		xmm0, [esi+4]	// SSE2にはローテート命令ないので
					pslld		xmm0, 24		// xmm[02] の上位 24 bit を 0 クリア
					pslld		xmm2, 24
					psrld		xmm0, 24		// PMOVZX使いたかったがSSE4.1なので断念
					psrld		xmm2, 24
					psubd		xmm0, xmm2		// xmm0 = (*(xpos+1) - *(xpos-1)) = diffx
					// ここまでで xmm0 は 左右4byteの傾き(diffx)
					movd		xmm2, scalex
					cvtdq2ps	xmm0, xmm0		// 浮動小数点値に変換
					pshufd		xmm2, xmm2, 0	// scalex_scalex_scalex_scalex
					mulps		xmm0, xmm2		// *scalex
					cvtps2dq	xmm0, xmm0		// 整数に戻す これで xmm0 は (diffx*scalex)

					paddd		xmm0, xmm4		// xmm0 = x + (diffx*scalex)
					pxor		xmm2, xmm2
					pminsw		xmm0, xmm6		// pminsdにしたかったがSSE4.1だったのでpminswで
					pmaxsw		xmm0, xmm2		// 0 <= xmm0 <= srcwidth-1 になった pmaxsdにしたかったがSSE4.1だったので…

					mov			eax,  mappitch
					movdqu		xmm1, [esi+eax]	// esi+mappitch
					neg			eax
					movdqu		xmm2, [esi+eax]	// esi-mappitch
					pslld		xmm1, 24		// xmm[12] の上位 24 bit を 0 クリア
					pslld		xmm2, 24
					psrld		xmm1, 24
					psrld		xmm2, 24
					psubd		xmm1, xmm2		// xmm1 = (*(ypos+1) - *(ypos-1)) = diffy
					// ここまでで xmm1 は 上下4byteの傾き(diffy)

					movd		xmm2, scaley
					cvtdq2ps	xmm1, xmm1		// 浮動小数点値に変換
					pshufd		xmm2, xmm2, 0	// scaley_scaley_scaley_scaley
					mulps		xmm1, xmm2		// *scaley
					cvtps2dq	xmm1, xmm1		// 整数に戻す これで xmm1 は (diffy*scaley)

					paddd		xmm1, xmm5		// xmm1 = y + (diffy*scaley)
					pxor		xmm2, xmm2
					pminsw		xmm1, xmm7		// pminsdにしたかったがSSE4.1だったのでpminswで
					pmaxsw		xmm1, xmm2		// 0 <= xmm1 <= height-1 になった pmaxsdにしたかったがSSE4.1だったので…

					movd		xmm2, ebx		// ebx = srcpitch
					pslld		xmm0, 2			// x*sizeof(dot)
					pshufd		xmm2, xmm2, 0	// xmm2 = srcpitch_srcpitch_srcpitch_srcpitch
					movdqa		xmm3, xmm1
					pmullw		xmm1, xmm2		// xmm1 = (y+diffy*scaley)*srcpitchの下位16bit 
					pmulhw		xmm3, xmm2		// xmm3 = (y+diffy*scaley)*srcpitchの上位16bit
					pslld		xmm3, 16
					por			xmm1, xmm3		// (y+diffy*scaley)もsrcpitch16bit以内の正の整数なので
					// xmm1 = (y+diffy*scaley)*srcpitch
					paddd		xmm0, xmm1		// 
					// ここまでで xmm0 = (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4
					mov			eax,  srcbuf
					movd		xmm1, eax
					pshufd		xmm1, xmm1, 0
					paddd		xmm0, xmm1
					// ここまでで xmm0 = srcbuf + (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4

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
				// 右端処理が必要かどうか判断
				mov			ecx, clipw		// 右端処理を追加
				// sub			ecx, 2 これは既に実施済みなので不要
				and			ecx, 0x3		// ecx = (clipw-2)%4
				jz			XLOOP1_end

				// 右端処理が必要なので実行。上のmovdquをmovdに変更しただけ。マヂで。
				// 速度は遅いがバグが入らないことを優先
			XLOOP1_RIGHTLOOP:
					movd		xmm0, [esi+4]	// ここは4byte(1dot)のみ転送
					movd		xmm2, [esi-4]	// 
					pslld		xmm0, 24		// xmm[02] の上位 24 bit を 0 クリア
					psrld		xmm0, 24		// PMOVZX使いたかったがSSE4.1なので断念
					pslld		xmm2, 24
					psrld		xmm2, 24
					psubd		xmm0, xmm2		// xmm0 = (*(xpos+1) - *(xpos-1)) = diffx
					// ここまでで xmm0 は 左右4byteの傾き(diffx)
					cvtdq2ps	xmm0, xmm0		// 浮動小数点値に変換
					movd		xmm2, scalex
					pshufd		xmm2, xmm2, 0	// scalex_scalex_scalex_scalex
					mulps		xmm0, xmm2		// *scalex
					cvtps2dq	xmm0, xmm0		// 整数に戻す これで xmm0 は (diffx*scalex)

					paddd		xmm0, xmm4		// xmm0 = x + (diffx*scalex)
					pminsw		xmm0, xmm6		// pminsdにしたかったがSSE4.1だったのでpminswで
					pxor		xmm2, xmm2		// ↓もpmaxsdにしたかったがSSE4.1だったので…
					pmaxsw		xmm0, xmm2		// 0 <= xmm0 <= srcwidth-1 になった

					mov			eax,  mappitch
					movd		xmm1, [esi+eax]	// esi+mappitch
					neg			eax
					movd		xmm2, [esi+eax]	// esi-mappitch
					pslld		xmm1, 24		// xmm[12] の上位 24 bit を 0 クリア
					psrld		xmm1, 24
					pslld		xmm2, 24
					psrld		xmm2, 24
					psubd		xmm1, xmm2		// xmm1 = (*(ypos+1) - *(ypos-1)) = diffy
					// ここまでで xmm1 は 上下4byteの傾き(diffy)

					cvtdq2ps	xmm1, xmm1		// 浮動小数点値に変換
					movd		xmm2, scaley
					pshufd		xmm2, xmm2, 0	// scaley_scaley_scaley_scaley
					mulps		xmm1, xmm2		// *scaley
					cvtps2dq	xmm1, xmm1		// 整数に戻す これで xmm1 は (diffy*scaley)

					paddd		xmm1, xmm5		// xmm1 = y + (diffy*scaley)
					pminsw		xmm1, xmm7		// pminsdにしたかったがSSE4.1だったのでpminswで
					pxor		xmm2, xmm2		// ↓もpmaxsdにしたかったがSSE4.1だったので…
					pmaxsw		xmm1, xmm2		// 0 <= xmm1 <= height-1 になった

					pslld		xmm0, 2			// x*sizeof(dot)
					movd		xmm2, ebx		// ebx = srcpitch
					pshufd		xmm2, xmm2, 0	// xmm2 = srcpitch_srcpitch_srcpitch_srcpitch
					movdqa		xmm3, xmm1
					pmullw		xmm1, xmm2		// xmm1 = (y+diffy*scaley)*srcpitchの下位16bit 
					pmulhw		xmm3, xmm2		// xmm3 = (y+diffy*scaley)*srcpitchの上位16bit
					pslld		xmm3, 16
					por			xmm1, xmm3		// (y+diffy*scaley)もsrcpitch16bit以内の正の整数なので
					// xmm1 = (y+diffy*scaley)*srcpitch
					paddd		xmm0, xmm1		// 
					// ここまでで xmm0 = (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4
					mov			eax,  srcbuf
					movd		xmm1, eax
					pshufd		xmm1, xmm1, 0
					paddd		xmm0, xmm1
					// ここまでで xmm0 = srcbuf + (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4

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

	// マスクのあるshimmerのマルチスレッド関数
	// srcbuf の画像を mapbuf に従ってゆがませつつ mskbuf のマスクかけながら dstbuf に貼り付ける
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
//					// mask値が 0 なら、そのままコピー。この方が早い。
//					// そんかし、mask値が 0 でない領域が全画面だと、20%くらい遅くなる
//					*dstp++ = *(TJSPIXEL*)srcp;
//				} else {
				{
					// マップレイヤの注目ドットの「傾き」を得る
					// 青色要素だけを使う。マップ画像は灰色だからこれでO.K.
					int gradx = *(mapp+TJSPIXELSIZE) - *(mapp-TJSPIXELSIZE);
					int grady = *(mapp+mappitch ) - *(mapp-mappitch );

					// マスクレイヤが指定された時は、濃度に合わせて影響箇所を限定
					// 「傾き」からsrc画像中の x, y を得る
					int srcx = x + ((gradx*sx*(*mskp)/255)>>16);
					int srcy = y + ((grady*sy*(*mskp)/255)>>16);

					// src[xy]の範囲チェックはしない。画像は縦横ループしてるから
					// 値書き込み
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
													// xmm4 = 3_2_1_0 になった
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
					// このループの中だけ、u-OP が効くように命令の順番を考えている
					// なんとなくprefetchしとく？
					// prefetchnta	[edx+32]
					// prefetchnta	[esi+32]

					// ここからマスク計算
					movdqu		xmm3, [edx]
					mov			eax,  0x437f0000	// = (float)255.0
					pslld		xmm3, 24
					movd		xmm2, eax
					psrld		xmm3, 24			// blueのみ抜き出し
					pshufd		xmm2, xmm2,0 
					cvtdq2ps	xmm3, xmm3
					divps		xmm3, xmm2		// xmm3 = (*mskp)/255
					// xmm3 = maskをしばらく保存しておく

					movdqu		xmm0, [esi+4]	// SSE2にはローテート命令ないので
					movdqu		xmm2, [esi-4]	// 効率悪いがmovqdquを使用
					pslld		xmm0, 24		// xmm[01] の上位 24 bit を 0 クリア
					pslld		xmm2, 24
					psrld		xmm0, 24		// PMOVZX使いたかったがSSE4.1なので断念
					psrld		xmm2, 24
					psubd		xmm0, xmm2		// xmm0 = (*(xpos+1) - *(xpos-1)) = diffx
					// ここまでで xmm0 は 左右4byteの傾き(diffx)
					movd		xmm2, scalex
					cvtdq2ps	xmm0, xmm0		// 浮動小数点値に変換
					pshufd		xmm2, xmm2, 0	// scalex_scalex_scalex_scalex
					mulps		xmm0, xmm2		// xmm0 = diffx*scalex
						// マスク乗算処理
						mulps		xmm0, xmm3	// xmm0 = diffx*scalex*mask
					cvtps2dq	xmm0, xmm0		// 整数に戻す これで xmm0 は diffx*scalex*mask

					paddd		xmm0, xmm4		// xmm0 = x + (diffx*scalex)
					pxor		xmm2, xmm2		// ↓もpmaxsdにしたかったがSSE4.1だったので…
					pminsw		xmm0, xmm6		// pminsdにしたかったがSSE4.1だったのでpminswで
					pmaxsw		xmm0, xmm2		// 0 <= xmm0 <= srcwidth-1 になった

					mov			eax,  mappitch
					movdqu		xmm1, [esi+eax]	// ebx = +mappitch
					neg			eax
					movdqu		xmm2, [esi+eax]	// ebx = -mappitch
					pslld		xmm1, 24		// xmm[12] の上位 24 bit を 0 クリア
					pslld		xmm2, 24
					psrld		xmm1, 24
					psrld		xmm2, 24
					psubd		xmm1, xmm2		// xmm1 = (*(ypos+1) - *(ypos-1)) = diffy
					// ここまでで xmm1 は 上下4byteの傾き(diffy)
					movd		xmm2, scaley
					cvtdq2ps	xmm1, xmm1		// 浮動小数点値に変換
					pshufd		xmm2, xmm2, 0	// scaley_scaley_scaley_scaley
					mulps		xmm1, xmm2		// xmm1 = diffy*scaley
						// マスク乗算処理
						mulps		xmm1, xmm3		// xmm1 = diffy*scaley*mask
					cvtps2dq	xmm1, xmm1		// 整数に戻す これで xmm1 は diffy*scaley*mask

					paddd		xmm1, xmm5		// xmm1 = y + (diffy*scaley)
					pxor		xmm2, xmm2		// ↓もpmaxsdにしたかったがSSE4.1だったので…
					pminsw		xmm1, xmm7		// pminsdにしたかったがSSE4.1だったのでpminswで
					pmaxsw		xmm1, xmm2		// 0 <= xmm1 <= height-1 になった

					movd		xmm2, ebx		// ebx = srcpitch
					pslld		xmm0, 2			// x*sizeof(dot)
					movdqa		xmm3, xmm1
					pshufd		xmm2, xmm2, 0	// xmm2 = srcpitch_srcpitch_srcpitch_srcpitch
					pmullw		xmm1, xmm2		// xmm1 = (y+diffy*scaley)*srcpitchの下位16bit 
					pmulhw		xmm3, xmm2		// xmm3 = (y+diffy*scaley)*srcpitchの上位16bit
					pslld		xmm3, 16
					por			xmm1, xmm3		// (y+diffy*scaley)もsrcpitch16bit以内の正の整数なので
					// xmm1 = (y+diffy*scaley)*srcpitch
					paddd		xmm0, xmm1		// 
					// ここまでで xmm0 = (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4
					mov			eax,  srcbuf
					movd		xmm1, eax
					pshufd		xmm1, xmm1, 0
					paddd		xmm0, xmm1
					// ここまでで xmm0 = srcbuf + (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4

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

				// 右端処理が必要かどうか判断
				mov			ecx, clipw		// 右端処理を追加
				and			ecx, 0x3		// ecx = (clipw-2)%4
				jz			XLOOP2_end

				// 右端処理が必要なので実行。上のmovdquをmovdに変更しただけ。マヂで。
				// 速度は遅いがバグが入らないことを優先
			XLOOP2_RIGHTLOOP:
					movd		xmm3, [edx]
					pslld		xmm3, 24
					psrld		xmm3, 24			// blueのみ抜き出し
					cvtdq2ps	xmm3, xmm3
					mov			eax,  0x437f0000	// = (float)255.0
					movd		xmm2, eax
					pshufd		xmm2, xmm2,0 
					divps		xmm3, xmm2		// xmm3 = (*mskp)/255
					// xmm3 = maskをしばらく保存しておく

					movd		xmm0, [esi+4]	// SSE2にはローテート命令ないので
					movd		xmm2, [esi-4]	// 効率悪いがmovqdquを使用
					pslld		xmm0, 24		// xmm[01] の上位 24 bit を 0 クリア
					psrld		xmm0, 24		// PMOVZX使いたかったがSSE4.1なので断念
					pslld		xmm2, 24
					psrld		xmm2, 24
					psubd		xmm0, xmm2		// xmm0 = (*(xpos+1) - *(xpos-1)) = diffx
					// ここまでで xmm0 は 左右4byteの傾き(diffx)
					cvtdq2ps	xmm0, xmm0		// 浮動小数点値に変換
					movd		xmm2, scalex
					pshufd		xmm2, xmm2, 0	// scalex_scalex_scalex_scalex
					mulps		xmm0, xmm2		// xmm0 = diffx*scalex
						// マスク乗算処理
						mulps		xmm0, xmm3	// xmm0 = diffx*scalex*mask
					cvtps2dq	xmm0, xmm0		// 整数に戻す これで xmm0 は diffx*scalex*mask

					paddd		xmm0, xmm4		// xmm0 = x + (diffx*scalex)
					pminsw		xmm0, xmm6		// pminsdにしたかったがSSE4.1だったのでpminswで
					pxor		xmm2, xmm2		// ↓もpmaxsdにしたかったがSSE4.1だったので…
					pmaxsw		xmm0, xmm2		// 0 <= xmm0 <= srcwidth-1 になった

					mov			eax,  mappitch
					movdqu		xmm1, [esi+eax]	// ebx = +mappitch
					neg			eax
					movdqu		xmm2, [esi+eax]	// ebx = -mappitch
					pslld		xmm1, 24		// xmm[12] の上位 24 bit を 0 クリア
					psrld		xmm1, 24
					pslld		xmm2, 24
					psrld		xmm2, 24
					psubd		xmm1, xmm2		// xmm1 = (*(ypos+1) - *(ypos-1)) = diffy
					// ここまでで xmm1 は 上下4byteの傾き(diffy)
					cvtdq2ps	xmm1, xmm1		// 浮動小数点値に変換
					movd		xmm2, scaley
					pshufd		xmm2, xmm2, 0	// scaley_scaley_scaley_scaley
					mulps		xmm1, xmm2		// xmm1 = diffy*scaley
						// マスク乗算処理
						mulps		xmm1, xmm3		// xmm1 = diffy*scaley*mask
					cvtps2dq	xmm1, xmm1		// 整数に戻す これで xmm1 は diffy*scaley*mask

					paddd		xmm1, xmm5		// xmm1 = y + (diffy*scaley)
					pminsw		xmm1, xmm7		// pminsdにしたかったがSSE4.1だったのでpminswで
					pxor		xmm2, xmm2		// ↓もpmaxsdにしたかったがSSE4.1だったので…
					pmaxsw		xmm1, xmm2		// 0 <= xmm1 <= height-1 になった

					pslld		xmm0, 2			// x*sizeof(dot)
					movd		xmm2, ebx		// ebx = srcpitch
					pshufd		xmm2, xmm2, 0	// xmm2 = srcpitch_srcpitch_srcpitch_srcpitch
					movdqa		xmm3, xmm1
					pmullw		xmm1, xmm2		// xmm1 = (y+diffy*scaley)*srcpitchの下位16bit 
					pmulhw		xmm3, xmm2		// xmm3 = (y+diffy*scaley)*srcpitchの上位16bit
					pslld		xmm3, 16
					por			xmm1, xmm3		// (y+diffy*scaley)もsrcpitch16bit以内の正の整数なので
					// xmm1 = (y+diffy*scaley)*srcpitch
					paddd		xmm0, xmm1		// 
					// ここまでで xmm0 = (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4
					mov			eax,  srcbuf
					movd		xmm1, eax
					pshufd		xmm1, xmm1, 0
					paddd		xmm0, xmm1
					// ここまでで xmm0 = srcbuf + (y+diffy*scaley)*srcpitch + (x+diffx*scalex)*4

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
	 * shimmer: 画像にかげろう効果を与える
	 * srclayer とこのレイヤの大きさは同じでなければならない。
	 * maplayer は クリッピングウィンドウと同じまたはそれより大きいサイズでなければならない
	 * clipw/cliph は 0 の時 srclayer と同じサイズとみなされる
	 * @param srclayer 描画元レイヤ
	 * @param maplayer マップ画像レイヤ(白黒画像)
	 * @param msklayer マスク画像レイヤ(白黒画像)
	 * @param scalex   ゆがみの横方向拡大率
	 * @param scaley   ゆがみの縦方向拡大率
	 * @param clipx/clipy/clipw/cliph
	 */
	void shimmer(tTJSVariant srclayer, tTJSVariant maplayer, tTJSVariant msklayer, float scalex, float scaley, int clipx, int clipy, int clipw, int cliph) {

		// ちょっとした計算の高速化のために、整数化しておく
		const int sx = int(scalex*0x10000), sy = int(scaley*0x10000);

		tjs_int srcwidth, srcheight, srcpitch;
		tjs_int mapwidth, mapheight, mappitch;
		tjs_int mskwidth, mskheight, mskpitch;
		BYTE *srcbuf, *mapbuf, *mskbuf;
		{
			// 元レイヤ画像情報
			srcbuf    = (BYTE*)(tjs_int64)getTJSMember(srclayer, L"mainImageBuffer");
			srcwidth  = (tjs_int)getTJSMember(srclayer, L"imageWidth");
			srcheight = (tjs_int)getTJSMember(srclayer, L"imageHeight");
			srcpitch  = (tjs_int)getTJSMember(srclayer, L"mainImageBufferPitch");

			// マップレイヤ画像情報
			mapbuf    = (BYTE*)(tjs_int64)getTJSMember(maplayer, L"mainImageBuffer");
			mapwidth  = (tjs_int)getTJSMember(maplayer, L"imageWidth");
			mapheight = (tjs_int)getTJSMember(maplayer, L"imageHeight");
			mappitch  = (tjs_int)getTJSMember(maplayer, L"mainImageBufferPitch");

			// マスクレイヤ画像情報
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

		// クリッピングを処理
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

		// 画像サイズにちょっとした制限を適用
		if (clipw > mapwidth || cliph > mapheight ||
			(mskbuf != NULL && (clipw > mskwidth || cliph > mskheight)))
			return;

		// １ピクセルごとにマップ先を計算
		// x=0, x=width-1, y=0, y=height-1 の時は要特別扱い

		// まず画面の外側１ドットだけ計算。ここはマップが特殊になるから。
		{
			// 最上行一ドットのshimmerを計算(ただし左右1dotは除く)
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+1/* = initial x */, clipy, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, +0, +0, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +2, +0, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +1,  0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +1, +1, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  1,  0, mskpitch);
			int srcx, srcy;
			for (int x = clipx+1; x < clipx+clipw-2; x++) {
				if (msklayer.Type() == tvtVoid) {
					// マスクがなかった場合
					srcx = x     + (((*mapx2-*mapx1)*sx)>>16);
					srcy = clipy + (((*mapy2-*mapy1)*sy)>>16);
				} else {
					// マスクがあった場合
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
			// 最下行一ドットのshimmerを計算(ただし左右1dotは除く)
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+1, clipy+cliph-1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, +0, cliph-1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +2, cliph-1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +1, cliph-2, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +1, cliph-1, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  1, cliph-1, mskpitch);
			int srcx, srcy;
			for (int x = clipx+1; x < clipx+clipw-2; x++) {
				if (msklayer.Type() == tvtVoid) { // マスクがなかった場合
					srcx = x             + (((*mapx2-*mapx1)*sx)>>16);
					srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy)>>16);
				} else { // マスクがあった場合
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
			// 最左列一ドットのshimmerを計算(ただし上下1dotは除く)
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx, clipy+1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf,  0, +1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +1, +1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +0, +0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +0, +2, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  0,  1, mskpitch);
			int srcx, srcy;
			for	(int y = clipy+1; y < clipy+cliph-2; y++) {
				if (msklayer.Type() == tvtVoid) { // マスクがなかった場合
					srcx = clipx + (((*mapx2-*mapx1)*sx)>>16);
					srcy = y     + (((*mapy2-*mapy1)*sy)>>16);
				} else { // マスクがあった場合
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
			// 最右列一ドットのshimmerを計算(ただし上下1dotは除く)
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+clipw-1, clipy+1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, clipw-2, +1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, clipw-1, +1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, clipw-1, +0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, clipw-1, +2, mappitch);
			BYTE *mskp  = bufadr(mskbuf, clipw-1,  1, mskpitch);
			int srcx, srcy;
			for (int y = clipy+1; y < clipy+cliph-2; y++) {
				if (msklayer.Type() == tvtVoid) { // マスクがなかった場合
					srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx)>>16);
					srcy = y             + (((*mapy2-*mapy1)*sy)>>16);
				} else { // マスクがあった場合
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
			// 左上一ドットのshimmerを計算
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx, clipy, _pitch);
			BYTE *mapx1 = bufadr(mapbuf,  0, +0, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +1, +0, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +0,  0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +0, +1, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  0,  0, mskpitch);
			int srcx, srcy;
			if (msklayer.Type() == tvtVoid) { // マスクがなかった場合
				srcx = clipx + (((*mapx2-*mapx1)*sx)>>16);
				srcy = clipy + (((*mapy2-*mapy1)*sy)>>16);
			} else { // マスクがあった場合
				srcx = clipx + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
				srcy = clipy + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
			}
			*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
		}

		{
			// 右上一ドットのshimmerを計算
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+clipw-1, clipy, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, clipw-2, +0, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, clipw-1, +0, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, clipw-1,  0, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, clipw-1, +1, mappitch);
			BYTE *mskp  = bufadr(mskbuf, clipw-1,  0, mskpitch);
			int srcx, srcy;
			if (msklayer.Type() == tvtVoid) { // マスクがなかった場合
				srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx)>>16);
				srcy = clipy         + (((*mapy2-*mapy1)*sy)>>16);
			} else { // マスクがあった場合
				srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
				srcy = clipy         + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
			}
			*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
		}

		{
			// 左下一ドットのshimmerを計算
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx, clipy+cliph-1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf,  0, cliph-1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, +1, cliph-1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, +0, cliph-2, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, +0, cliph-1, mappitch);
			BYTE *mskp  = bufadr(mskbuf,  0, cliph-1, mskpitch);
			int srcx, srcy;
			if (msklayer.Type() == tvtVoid) { // マスクがなかった場合
				srcx = clipx         + (((*mapx2-*mapx1)*sx)>>16);
				srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy)>>16);
			} else { // マスクがあった場合
				srcx = clipx         + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
				srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
			}
			*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
		}

		{
			// 右下一ドットのshimmerを計算
			TJSPIXEL *dstp = (TJSPIXEL*)bufadr(_buffer, clipx+clipw-1, clipy+cliph-1, _pitch);
			BYTE *mapx1 = bufadr(mapbuf, clipw-2, cliph-1, mappitch);
			BYTE *mapx2 = bufadr(mapbuf, clipw-1, cliph-1, mappitch);
			BYTE *mapy1 = bufadr(mapbuf, clipw-1, cliph-2, mappitch);
			BYTE *mapy2 = bufadr(mapbuf, clipw-1, cliph-1, mappitch);
			BYTE *mskp  = bufadr(mskbuf, clipw-1, cliph-1, mskpitch);
			int srcx, srcy;
			if (msklayer.Type() == tvtVoid) { // マスクがなかった場合
				srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx)>>16);
				srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy)>>16);
			} else { // マスクがあった場合
				srcx = clipx+clipw-1 + (((*mapx2-*mapx1)*sx*(*mskp)/255)>>16);
				srcy = clipy+cliph-1 + (((*mapy2-*mapy1)*sy*(*mskp)/255)>>16);
			}
			*dstp = *(TJSPIXEL*)(bufadr3(srcbuf, srcx, srcy, srcwidth, srcheight, srcpitch));
		}

		clipx +=1, clipw -= 2, clipy += 1, cliph -= 2;
		if (clipw <= 0 || cliph <= 0 || clipx >= _width || clipy >= _height)
			return;
		// ここまでで、上下左右の1dotは全てshimmer済み

		// ここから、threadNum 個のスレッドを作って、threadedShimmer を実行

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
		// シングルスレッドの場合
		if (msklayer.Type() == tvtVoid) // マスクがなかった場合
			threadedShimmer((LPVOID)&defRect);
		else // マスクがあった場合
			threadedShimmerWithMask((LPVOID)&defRect);
#else
		// マルチスレッドの場合
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
			if (msklayer.Type() == tvtVoid) // マスクがなかった場合
				threadPool.run(this, &layerExShimmer::threadedShimmer, (void*)(rectAry+thread));
			else // マスクがあった場合
				threadPool.run(this, &layerExShimmer::threadedShimmerWithMask, (void*)(rectAry+thread));
		}
		// 最後は端数の高さ分を補正する必要あり
		rectAry[thread]       = defRect;
		rectAry[thread].clipy = y;
		rectAry[thread].cliph = cliph-divh*(threadNum-1);
		rectAry[thread].mapy  = 1 + divh*(threadNum-1);
		rectAry[thread].msky  = 1 + divh*(threadNum-1);
		if (msklayer.Type() == tvtVoid) // マスクがなかった場合
			threadedShimmer((LPVOID)(rectAry+thread));			// 最後のスレッドはこうした方が早い
		else // マスクがあった場合
			threadedShimmerWithMask((LPVOID)(rectAry+thread));	// 最後のスレッドはこうした方が早い
		// 全部のスレッドが終わるまで待つ
		threadPool.waitForAllThreads();
#endif
	}



	// threadedShimmerBuildMap*()に渡す構造体
	typedef struct {
		BYTE *dstbuf;  tjs_int dstwidth, dstheight, dstpitch;
		BYTE *map1buf; tjs_int map1width, map1height, map1pitch;
		BYTE *map2buf; tjs_int map2width, map2height, map2pitch;
		tjs_int map1x, map1y, map2x, map2y;
		tjs_int starty;
	} ShimmerMaps;


	// マップ画面を作成する。一枚しかマップがない場合。タイル状にそれを貼り付けるだけ
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
		int dstwidth = p->dstwidth;				// マクロらしいので変数に代入しとく
		int map1w_x4 = map1width*4;
		int map1lw   = min(map1x, p->dstwidth);						// 左側の描画幅端数
		int map1ccnt = max(0, (p->dstwidth - map1lw))/map1width;	// 中央の描画繰返回数
		int map1rw   = p->dstwidth - map1lw - map1ccnt*map1width;	// 右端の描画幅端数
//log(L"map1lw = %d, map1ccnt = %d, map1rw = %d", map1lw, map1ccnt, map1rw);
		for (int y = p->starty; y < p->dstheight; y++) {
			BYTE *dstp = bufadr(p->dstbuf, 0, y, p->dstpitch);
			BYTE *map1p = bufadr2(map1buf, 0-map1x, y-map1y, map1width, map1height, map1pitch);
			__asm {
				mov		esi, map1p
				mov		edi, dstp
					
			//BUILDMAPA_LEFT:
				// 左端端数描画
				mov		ecx, map1lw
				or		ecx, ecx
				jz		BUILDMAPA_CENTER
				sub		ecx, 4
				jb		BUILDMAPA_LEFTLOOP1_NEXT
			BUILDMAPA_LEFTLOOP1:
				// prefetchnta	[esi+32]	// まぁプリフェッチしとく
				// 最初、4dot単位で書き込み
				movdqu	xmm0, [esi]
				movdqa 	[edi],xmm0		// キャッシュを汚さない(movntdq)だと遅い
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
				// 4dot未満の書き込み
				mov		eax,  [esi]
				mov		[edi],eax
				add		esi,  4
				add		edi,  4
				dec		ecx
				jnz		BUILDMAPA_LEFTLOOP2
				sub		esi,  map1w_x4

			BUILDMAPA_CENTER:
				// 中心の描画ループ
				mov		ebx, map1ccnt;
				or		ebx, ebx
				jz		BUILDMAPA_RIGHT
			BUILDMAPA_CENTERLOOP1:
				mov		ecx, map1width
				sar		ecx, 2		// 4dot単位なので1/4する
			BUILDMAPA_CENTERLOOP2:
				prefetchnta	[esi+32]	// まぁプリフェッチしとく
				// map1wは4dot単位なのでもうそれだけしか考えない
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
				// 右端端数描画
				mov		ecx, map1rw
				or		ecx, ecx
				jz		BUILDMAPA_END
				sub		ecx, 4
				jb		BUILDMAPA_RIGHTLOOP1_NEXT
			BUILDMAPA_RIGHTLOOP1:
				prefetchnta	[esi+32]	// まぁプリフェッチしとく
				// 最初、4dot単位で書き込み
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
				// 4dot未満の書き込み
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
		

	// マップ画面を作成する。マップが二枚ある場合。それらを合成しながらタイル状に貼り付ける
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
		int dstwidth = p->dstwidth;			// マクロらしいので変数に代入しとく
		int map1w_x4 = map1width*4;
		int map2w_x4 = map2width*4;
		if (map1x%4 == 0 && map2x%4 == 0) {	// 4で割り切れる時は movdqa が使える
//log(L"map1x = %d, map1w = %d, map2x = %d, map2w = %d", map1x, map1w, map2x, map2w);
			int map1w_d4 = map1width/4, map2w_d4 = map2width/4;
			int map1lw_d4= map1x/4;			// map1左端の描画幅端数
			int map2lw_d4= map2x/4;			// map2左端の描画幅端数
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
					sub		ebx, eax	// 先に引いておく
					sub		ecx, eax
					inc		ecx
				BUILDMAPC_LOOP_EAX:
//					prefetchnta	[esi+32]
//					prefetchnta	[edx+32]
					movdqa	xmm0, [esi]
					pavgb	xmm0, [edx]	// 平均値を取る
					movntdq	[edi],xmm0	// キャッシュを汚さない。元はmovdqaだった
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
					sub		eax, ebx	// 先に引いておく
					sub		ecx, ebx
					inc		ecx
				BUILDMAPC_LOOP_EBX:
//					prefetchnta	[esi+32]
//					prefetchnta	[edx+32]
					movdqa	xmm0, [esi]
					pavgb	xmm0, [edx]	// 平均値を取る
					movntdq	[edi],xmm0	// キャッシュを汚さない。元はmovdqaだった
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
					pavgb	xmm0, [edx]	// 平均値を取る
					movntdq	[edi],xmm0	// キャッシュを汚さない。元はmovdqaだった
					add		esi,  16
					add		edx,  16
					add		edi,  16
					dec		ecx
					jnz		BUILDMAPC_LOOP_ECX
				BUILDMAPC_LOOP_END:
				}
			}
		} else {
			// 4dot 単位ではない場合
			int map1lw = min(map1x, p->dstwidth);	// map1左端の描画幅端数
			int map2lw = min(map2x, p->dstwidth);	// map2左端の描画幅端数
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
//					prefetchnta	[esi+32]	// ない方が早かった。1160:1264くらい。
//					prefetchnta	[edx+32]
					movd	xmm0, [esi]
					movd	xmm1, [edx]
					pavgb	xmm0, xmm1	// 平均値を取る
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
//					prefetchnta	[esi+32]	// ない方が早かった。1160:1264くらい。
//					prefetchnta	[edx+32]
					movd	xmm0, [esi]
					movd	xmm1, [edx]
					pavgb	xmm0, xmm1	// 平均値を取る
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
					pavgb	xmm0, xmm1		// 平均値を取る
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
	 * shimmerBuildMap:  陽炎効果用のマップ画像を作成する
	 * わかってるよこんなのが汚い方法だってのはさ！
	 * このレイヤは、shimmerするレイヤと同じサイズである必要がある
	 * maplayer1 及び maplayer2 のサイズは任意。
	 * @param maplayer1		元マップ画像レイヤ1(白黒画像)
	 * @param map1x, map1y	元マップ画像レイヤの参照位置
	 * @param maplayer2		元マップ画像レイヤ2(白黒画像)
	 * @param map2x, map2y	元マップ画像レイヤの参照位置
	 */
	void shimmerBuildMap(tTJSVariant maplayer1, tjs_int map1x, tjs_int map1y, tTJSVariant maplayer2, tjs_int map2x, tjs_int map2y) {

//log(L"width = %d", _width);
		ShimmerMaps defMap;
		defMap.dstbuf    = _buffer;
		defMap.dstwidth  = _width;
		defMap.dstheight = _height;
		defMap.dstpitch  = _pitch;

		// 元マップレイヤ1画像情報
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
			// 元マップレイヤ1画像情報
			defMap.map2buf    = (BYTE*)(tjs_int64)getTJSMember(maplayer2, L"mainImageBuffer");
			defMap.map2width  = (tjs_int)getTJSMember(maplayer2, L"imageWidth");
			defMap.map2height = (tjs_int)getTJSMember(maplayer2, L"imageHeight");
			defMap.map2pitch  = (tjs_int)getTJSMember(maplayer2, L"mainImageBufferPitch");
			defMap.map2x      = map2x;
			defMap.map2y      = map2y;
		}
		defMap.starty = 0;

//#ifndef MULTI_THREAD	// マルチスレッドにすると遅かったので、必ずシングルスレッドで処理する
#if 1
		// シングルスレッドの場合
		if (defMap.map2buf == NULL) // マスクがなかった場合
			threadedShimmerBuildMap((LPVOID)&defMap);
		else // マスクがあった場合
			threadedShimmerBuildMapWithMap2((LPVOID)&defMap);
#else
		// マルチスレッドの場合
		tjs_int threadNum = threadPool.GetNumThreads();
		tjs_int divh = defMap.dstheight/threadNum;
		ShimmerMaps mapAry[MAXTHREADNUM];
		tjs_int y, thread;
		for (thread = 0, y = 0; thread < threadNum-1; thread++, y += divh) {
			mapAry[thread]        = defMap;
			mapAry[thread].starty = y;
			if (defMap.map2buf == NULL) // マップレイヤ2がなかった場合
				threadPool.QueueRequest(this, &layerExShimmer::threadedShimmerBuildMap, (void*)(mapAry+thread));
			else // マップレイヤ2があった場合
				threadPool.QueueRequest(this, &layerExShimmer::threadedShimmerBuildMapWithMap2, (void*)(mapAry+thread));
		}
		mapAry[thread]        = defMap;
		mapAry[thread].starty = y;
		if (defMap.map2buf == NULL) // マップレイヤ2がなかった場合
			threadedShimmerBuildMap((LPVOID)(mapAry+thread));			// 最後のスレッドはこうした方が早い
		else // マップレイヤ2があった場合
			threadedShimmerBuildMapWithMap2((LPVOID)(mapAry+thread));	// 最後のスレッドはこうした方が早い
		// 全部のスレッドが終わるまで待つ
		threadPool.WaitForAllThreads();
#endif
	}
};

// ----------------------------------- クラスの登録

NCB_GET_INSTANCE_HOOK(layerExShimmer)
{
	// インスタンスゲッタ
	NCB_INSTANCE_GETTER(objthis) { // objthis を iTJSDispatch2* 型の引数とする
		ClassT* obj = GetNativeInstance(objthis);	// ネイティブインスタンスポインタ取得
		if (!obj) {
			obj = new ClassT(objthis);				// ない場合は生成する
			SetNativeInstance(objthis, obj);		// objthis に obj をネイティブインスタンスとして登録する
		}
		obj->reset();
		return obj;
	}
	// デストラクタ（実際のメソッドが呼ばれた後に呼ばれる）
	~NCB_GET_INSTANCE_HOOK_CLASS () {
	}
};


// フックつきアタッチ
NCB_ATTACH_CLASS_WITH_HOOK(layerExShimmer, Layer) {
	NCB_METHOD(shimmer);
	NCB_METHOD(shimmerBuildMap);
}
