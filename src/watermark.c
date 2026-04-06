#include "watermark.h"
#include <string.h>

#define WM_FONT_W 7
#define WM_FONT_H 12
#define WM_PAD_X  10
#define WM_PAD_Y  3

static const char wm_text[] = "Easy IRL Stream - stools.cc";

/*
 * Embedded 7x12 bitmap font.
 * Each row is one byte; bits 7-1 = 7 pixels left to right.
 * Only characters needed for wm_text are defined.
 */
struct wm_glyph {
	char ch;
	uint8_t rows[12];
};

static const struct wm_glyph wm_font[] = {
	{' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
	{'-', {0x00,0x00,0x00,0x00,0x00,0x78,0x00,0x00,0x00,0x00,0x00,0x00}},
	{'.', {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00}},
	{'E', {0x00,0x00,0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0x7C,0x00,0x00}},
	{'I', {0x00,0x00,0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x7C,0x00,0x00}},
	{'L', {0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7C,0x00,0x00}},
	{'R', {0x00,0x00,0x78,0x44,0x44,0x78,0x50,0x48,0x44,0x44,0x00,0x00}},
	{'S', {0x00,0x00,0x38,0x44,0x40,0x38,0x04,0x04,0x44,0x38,0x00,0x00}},
	{'a', {0x00,0x00,0x00,0x00,0x38,0x04,0x3C,0x44,0x44,0x3C,0x00,0x00}},
	{'c', {0x00,0x00,0x00,0x00,0x38,0x44,0x40,0x40,0x44,0x38,0x00,0x00}},
	{'e', {0x00,0x00,0x00,0x00,0x38,0x44,0x7C,0x40,0x40,0x38,0x00,0x00}},
	{'l', {0x00,0x00,0x30,0x10,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00}},
	{'m', {0x00,0x00,0x00,0x00,0x6C,0x54,0x54,0x54,0x54,0x54,0x00,0x00}},
	{'o', {0x00,0x00,0x00,0x00,0x38,0x44,0x44,0x44,0x44,0x38,0x00,0x00}},
	{'r', {0x00,0x00,0x00,0x00,0x58,0x60,0x40,0x40,0x40,0x40,0x00,0x00}},
	{'s', {0x00,0x00,0x00,0x00,0x38,0x40,0x38,0x04,0x44,0x38,0x00,0x00}},
	{'t', {0x00,0x00,0x10,0x10,0x78,0x10,0x10,0x10,0x10,0x0C,0x00,0x00}},
	{'y', {0x00,0x00,0x00,0x00,0x44,0x44,0x44,0x44,0x3C,0x04,0x78,0x00}},
};

#define WM_GLYPH_COUNT (sizeof(wm_font) / sizeof(wm_font[0]))

static const uint8_t *wm_get_glyph(char c)
{
	for (int i = 0; i < (int)WM_GLYPH_COUNT; i++)
		if (wm_font[i].ch == c)
			return wm_font[i].rows;
	return wm_font[0].rows;
}

static void stamp_y_plane(uint8_t *y, int stride, int w, int h)
{
	int text_len = (int)strlen(wm_text);
	int text_w = text_len * WM_FONT_W;
	int bar_w = text_w + WM_PAD_X * 2;
	int bar_h = WM_FONT_H + WM_PAD_Y * 2;
	int bar_x = w - bar_w;
	int bar_y = h - bar_h;

	if (bar_x < 0 || bar_y < 0)
		return;

	for (int r = bar_y; r < bar_y + bar_h && r < h; r++)
		for (int c = bar_x; c < bar_x + bar_w && c < w; c++)
			y[r * stride + c] >>= 2;

	int tx = bar_x + WM_PAD_X;
	int ty = bar_y + WM_PAD_Y;

	for (int i = 0; i < text_len; i++) {
		const uint8_t *glyph = wm_get_glyph(wm_text[i]);
		int cx = tx + i * WM_FONT_W;

		for (int r = 0; r < WM_FONT_H; r++) {
			int py = ty + r;
			if (py >= h)
				break;
			uint8_t bits = glyph[r];
			for (int c = 0; c < WM_FONT_W; c++) {
				int px = cx + c;
				if (px >= w)
					break;
				if (bits & (0x80 >> c))
					y[py * stride + px] = 220;
			}
		}
	}
}

static void neutralize_chroma_420(uint8_t *u, int u_stride,
				  uint8_t *v, int v_stride,
				  int bar_x, int bar_y,
				  int bar_w, int bar_h,
				  int w, int h)
{
	int cx0 = bar_x / 2;
	int cy0 = bar_y / 2;
	int cx1 = (bar_x + bar_w + 1) / 2;
	int cy1 = (bar_y + bar_h + 1) / 2;
	int cw = w / 2;
	int ch = h / 2;

	for (int r = cy0; r < cy1 && r < ch; r++)
		for (int c = cx0; c < cx1 && c < cw; c++) {
			u[r * u_stride + c] = 128;
			v[r * v_stride + c] = 128;
		}
}

static void neutralize_chroma_nv12(uint8_t *uv, int uv_stride,
				   int bar_x, int bar_y,
				   int bar_w, int bar_h,
				   int w, int h)
{
	int cx0 = bar_x / 2;
	int cy0 = bar_y / 2;
	int cx1 = (bar_x + bar_w + 1) / 2;
	int cy1 = (bar_y + bar_h + 1) / 2;
	int cw = w / 2;
	int ch = h / 2;

	for (int r = cy0; r < cy1 && r < ch; r++)
		for (int c = cx0; c < cx1 && c < cw; c++) {
			uv[r * uv_stride + c * 2] = 128;
			uv[r * uv_stride + c * 2 + 1] = 128;
		}
}

void watermark_draw(AVFrame *frame)
{
	int w = frame->width;
	int h = frame->height;
	enum AVPixelFormat fmt = (enum AVPixelFormat)frame->format;

	int text_len = (int)strlen(wm_text);
	int text_w = text_len * WM_FONT_W;
	int bar_w = text_w + WM_PAD_X * 2;
	int bar_h = WM_FONT_H + WM_PAD_Y * 2;
	int bar_x = w - bar_w;
	int bar_y = h - bar_h;

	if (bar_x < 0 || bar_y < 0)
		return;

	switch (fmt) {
	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVJ420P:
		stamp_y_plane(frame->data[0], frame->linesize[0], w, h);
		neutralize_chroma_420(frame->data[1], frame->linesize[1],
				      frame->data[2], frame->linesize[2],
				      bar_x, bar_y, bar_w, bar_h, w, h);
		break;

	case AV_PIX_FMT_NV12:
		stamp_y_plane(frame->data[0], frame->linesize[0], w, h);
		neutralize_chroma_nv12(frame->data[1], frame->linesize[1],
				       bar_x, bar_y, bar_w, bar_h, w, h);
		break;

	case AV_PIX_FMT_YUV422P:
	case AV_PIX_FMT_YUVJ422P:
	case AV_PIX_FMT_YUV444P:
	case AV_PIX_FMT_YUVJ444P:
		stamp_y_plane(frame->data[0], frame->linesize[0], w, h);
		break;

	default:
		break;
	}
}
