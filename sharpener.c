#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <math.h>

static void die(char *reason, ...)
{
	va_list args;

	va_start(args, reason);
	vfprintf(stderr, reason, args);
	va_end(args);

	exit(1);
}

/*Protected allocation*/
void *palloc(size_t members, size_t element)
{
	void *ret;

	ret = calloc(members, element);
	if (!ret)
		die("Failed to allocate memory.\n");

	return ret;
}

#define KILO 1024
typedef unsigned char uchar;

/*protected realloc*/
void *prealloc(void *p, size_t members, size_t element)
{
	p = realloc(p, members*element);
	if (!p)
		die("Failed to reallocate memory");

	return p;
}

uchar* ReadLine(FILE *f)
{
	int c;
	long len, i;
	uchar *line;

	len  = KILO*4; /*4 kilobytes seems like a reasonable starting size*/
	line = palloc(len, sizeof(uchar));
	for (i = 0; (c = fgetc(f)) != EOF; i++) {
		if (i >= len) {
			len += (4*KILO);
			line = prealloc(line, len, sizeof(uchar));
		}

		if (c != '\n')
			line[i] = c;
		else {
			line[i] = '\0';
			break;
		}
	}

	/*Clip the line length to exactly what is required*/
	line = prealloc(line, i+1, sizeof(uchar)); /*+1 since we count from 0*/

	return line;
}

/*Y'CbCr*/
typedef struct {
	FILE *f;

	uint16_t Width;
	uint16_t Height;

	/*For the fractions*/
	uint32_t FPS[2];
	uint32_t Aspect[2];

	/*Unsigned to ensure C casting quirks become security problems*/
	char Interlacing;
	char ColourSpace[16];

	uchar *Y;
	uchar *Cb;
	uchar *Cr;

	/*Y4M metadata removed*/
}Y4M_t;

bool Y4M_GetFrame(Y4M_t h)
{
	int c;
	uint32_t i;
	long PlaneSize;
	uchar* plane[3]; /*3 color planes: luma, chroma blue, chroma red*/

	/*skips the first six characters*/
	for (i = 0; i < 6; i++) {
		c = getc(h.f);
		if (c == EOF)
			return 0;
	}

	PlaneSize = h.Width*h.Height;

	plane[0] = h.Y;
	plane[1] = h.Cb;
	plane[2] = h.Cr;
	/*Chroma 444 read only*/
	for (i = 0; i < 3; i++) {
		if (!fread(plane[i], sizeof(uchar), PlaneSize, h.f))
			return 0;
	}

	return 1;
}

uint32_t LoadNum(uchar *str, uint32_t *pos)
{
	uchar c;
	uchar *tmp;
	uint32_t i;
	uint32_t ret;

	tmp = palloc(64, sizeof(uchar));

	ret = 0;
	for (i = 0; (c = str[i]) >= '0' && c <= '9'; i++)
		tmp[i] = c;

	*pos = i+1; /*+1 since it counts zero as 1*/
	ret = atoi((char*)tmp);
	free(tmp);

	return ret;
}

Y4M_t Y4M_ReadHeader(FILE *in)
{
	Y4M_t ret;
	uchar *line;
	uchar *param;
	uchar  c1, c2;
	uint32_t i, j, len, rint, paramSize, lineSize, PlaneSize;

	paramSize = 128;
	/*The memory is allocated like this to crash
	  instead of overwriting stack space*/
	param = palloc(paramSize, sizeof(uchar));

	line = ReadLine(in);
	if (!line)
		die("Failed to read line\n");

	bzero(&ret, sizeof(Y4M_t));
	lineSize = strlen((char*)line);
	for (i = 0; i < lineSize && (c1 = line[i]) != '\0';) {
		bzero(param, paramSize*sizeof(uchar));
		for (j = 0; (c2 = line[i+j]) != ' ' && c2 != '\0'; j++)
			param[j] = line[i+j];
		i += j+1; /*The +1 skips a space in line*/

		/*replaces the space after a parameter to make it a null
		  terminated string*/
		param[j] = '\0';

		switch (param[0]) {
		case ('Y'):
			/*YUV4MPEG2 ID bytes*/
			break;
		case ('W'): /*Width and Height respectively*/
		case ('H'):
			rint = LoadNum(param+1, &len);
			if (param[0] == 'W')
				ret.Width  = rint;
			else
				ret.Height = rint;
			break;
		case ('A'):/*Aspect ratio and framerate respectively*/
		case ('F'):
			rint = LoadNum(param+1, &len);
			if (param[0] == 'A')
				ret.Aspect[0] = rint;
			else
				ret.FPS[0]    = rint;

			/*The magic +1 skips the colon in the fraction*/
			rint = LoadNum(param+len+1, &len);
			if (param[0] == 'A')
				ret.Aspect[1] = rint;
			else
				ret.FPS[1]    = rint;
			break;
		case ('I'):/*Interlacing*/
			if (param[1] != (uchar)'p')
				die("Interlacing: %s\n", param);
			break;
		case ('C'):/*Colour space or luma, chroma blue, chroma red*/
			if (strncmp((char*)(param+1), "444", 3))
				die("Only 4:4:4 chroma supported\n");
			strncpy((char*)ret.ColourSpace, "444", 4);
			break;
		case ('X'):/*Comment set by encoder*/
			/*YUV4MPEG2 comment*/
			break;
		default:
			/*This means the file is corrupt or some obscure
			 offshoot of the YUV4MPEG format*/
			die("Unknown parameter: %s\n", param);
		}
	}

	ret.f = in;
	if (!ret.Width || !ret.Height)
		die("Width: %d\tHeight: %d\n", ret.Width, ret.Height);

	PlaneSize = ret.Width * ret.Height;
	ret.Y  = palloc(PlaneSize, sizeof(uchar));
	ret.Cb = palloc(PlaneSize, sizeof(uchar));
	ret.Cr = palloc(PlaneSize, sizeof(uchar));

	free(line);
	free(param);
	return ret;
}

void Y4M_free(Y4M_t h)
{
	free(h.Y);
	free(h.Cb);
	free(h.Cr);
}

bool Y4M_WriteHeader(Y4M_t h)
{
	/*Ip because only progressive interlacing is supported*/
	if (fprintf(h.f, "YUV4MPEG2 W%d H%d F%d:%d A%d:%d C%s Ip X%s%c",
	    h.Width, h.Height, h.FPS[0], h.FPS[1], h.Aspect[0],
	    h.Aspect[1], h.ColourSpace, "Y4MLIB", 0x0A) < 0)
		return 0;

	return 1;
}

const size_t FrameHeaderSize = 6;
const uchar FrameHeader[] = {'F', 'R', 'A', 'M', 'E', 0x0A};

bool Y4M_PutFrame(Y4M_t h)
{
	uint32_t i;
	uchar* plane[3]; /*Three colour planes: luma, chroma blue, chroma red*/
	uint32_t PlaneSize;

	if (!fwrite(FrameHeader, FrameHeaderSize, sizeof(uchar), h.f))
		return 0;

	PlaneSize = h.Width*h.Height;

	plane[0] = h.Y;
	plane[1] = h.Cb;
	plane[2] = h.Cr;

	for (i = 0; i < 3; i++) {
		if (!fwrite(plane[i], sizeof(uchar), PlaneSize, h.f))
			return 0;
	}

	return 1;
}

#ifndef TESTING

/*I wonder if this will change anything*/
#include <pthread.h>

#include <math.h>

#define MINV 0.f
#define MAXV 255.f

const float mval = 0.125;

uchar getxy(Y4M_t h, uchar* plane, uint32_t x, uint32_t y)
{
	return plane[(y*h.Width)+x];
}

void putxy(Y4M_t h, uchar *plane, uint32_t x, uint32_t y, uchar c)
{
	plane[(y*h.Width)+x] = c;
}

/* The algorithm for reference:*/
float algorithm(float v, float* others, uint32_t len)
{
	uint32_t i;
	float max, min, ret, n, avg, range, mod;

	max = MINV;
	min = MAXV;
	for (i = 0, avg = 0.f; i < len; i++) {
		n = others[i];
		if (n < min)
			min = n;
		else if (n > max)
			max = n;

		avg += n;
	}

	avg   /= (float)i;
	range  = (max-min) / MAXV;

	mod = mval * range;
	if (v > avg) {
		mod += 1.f;
		ret  = mod * v;
		if (ret > max)
			ret = max;
	} else if (v < avg) {
		mod = 1.f - mod;
		ret = mod * v;
		if (ret < min)
			ret = min;
	} else
		ret = v;

	return ret;
}

#define OTHERSLEN 8

void ProcessPlane(Y4M_t h, uchar* plane, uchar *target)
{
	int32_t len;
	float v, *others /*others[OTHERSLEN]*/;
	uint32_t x, y, lw, lh, lwm1, lhm1;

	lw   = h.Width;
	lh   = h.Height;
	lwm1 = lw-1;
	lhm1 = lh-1;

	others = palloc(OTHERSLEN, sizeof(float));

	for (y = 0; y < h.Height; y++)
		for (x = 0; x < h.Width; x++) {
			len = 0;

			/*---
			  -+-
			  ---*/
			v = getxy(h, plane, x, y);

			if (x) {
				/*---
				  ++-
				  ---
				 */
				others[len++] = getxy(h, plane, x-1, y);
				if (x < lwm1) {
					/*---
					  +++
					  ---*/
					others[len++] = getxy(h, plane, x+1, y  );
					if (y && y < lhm1) {
						/*+--
						  +++
						  ---*/
						others[len++] = getxy(h, plane, x-1, y-1);
						/*+-+
						  +++
						  ---*/
						others[len++] = getxy(h, plane, x+1, y-1);
					}
				}
			}
			if (y) {
				/*+++
				  +++
				  ---*/
				others[len++] = getxy(h, plane, x  , y-1);
				if (y < lhm1) {
					/*+++
					  +++
					  -+-*/
					others[len++] = getxy(h, plane, x  , y+1);
					if (x && x < lwm1) {
						/*+++
						  +++
						  ++-*/
						others[len++] = getxy(h, plane, x-1, y+1);
						/*+++
						  +++
						  +++*/
						others[len++] = getxy(h, plane, x+1, y+1);
					}
				}
			}

			v = algorithm(v, others, len);
			putxy(h, target, x, y, roundf(v));
		}

	free(others);
}

typedef struct {
	Y4M_t h;
	uchar *input;
	uchar *output;
}args;

void *process_call(void *param)
{
	args *in;

	in = (args*)param;
	ProcessPlane(in->h, in->input, in->output);
	return NULL;
}

args argpack(Y4M_t h, uchar *input, uchar *output)
{
	args ret;

	ret.h      = h;
	ret.input  = input;
	ret.output = output;

	return ret;
}

pthread_t callThread(Y4M_t h, uchar *input, uchar *output, args *params)
{
	pthread_t ret;
	int threadRet;

	*params = argpack(h, input, output);

	threadRet = pthread_create(&ret, NULL, process_call, (void*)params);
	if (threadRet)
		die("pthread_create failed\n");

	return ret;
}

int main(int argc, char *argv[])
{
	int i;
	FILE *in, *out;
	uchar* plane[3];
	Y4M_t Yin, Yout;
	uint32_t PlaneSize;
	pthread_t threadY, threadCb, threadCr;

	args params[3];

	in  = NULL;
	out = NULL;
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-i")) {
			if (!strcmp(argv[i+1], "-"))
				in = stdin;
			else {
				in = fopen(argv[i+1], "rb");
				if (!in)
					die("Failed to open: %s\n", argv[i+1]);
			}

			i++;
		} else if (!strcmp(argv[i], "-o")) {
			if (!strcmp(argv[i+1], "-"))
				out = stdout;
			else {
				out = fopen(argv[i+1], "wb");
				if (!out)
					die("Failed to open: %s\n", argv[i+1]);
			}

			i++;
		}
	}

	if (!in || !out)
		die("Usage: ./%s -i input -o output\n", argv[0]);

	Yin    = Y4M_ReadHeader(in);
	Yout   = Yin;
	Yout.f = out;

	plane[0] = Yout.Y;
	plane[1] = Yout.Cb;
	plane[2] = Yout.Cr;

	PlaneSize = Yout.Width * Yout.Height;
	for (i = 0; i < 3; i++)
		plane[i] = palloc(PlaneSize, sizeof(uchar));

	Yout.Y  = plane[0];
	Yout.Cb = plane[1];
	Yout.Cr = plane[2];

/*	if ((Yin.Width % 4) || (Yin.Height % 4)) {
		printf("Video dimensions must be a multiple of 4\n");
		goto end;
	}
*/

	Y4M_WriteHeader(Yout);

	for (i = 0;;i++) {
		if (!Y4M_GetFrame(Yin))
			break;

		threadY  = callThread(Yin, Yin.Y,  Yout.Y,  &params[0]);
		threadCb = callThread(Yin, Yin.Cb, Yout.Cb, &params[1]);
		threadCr = callThread(Yin, Yin.Cr, Yout.Cr, &params[2]);

		pthread_join(threadY,  NULL);
		pthread_join(threadCb, NULL);
		pthread_join(threadCr, NULL);

		if (!Y4M_PutFrame(Yout))
			break;
	}

/*	end:*/
		Y4M_free(Yin);
		Y4M_free(Yout);
		fclose(in);
		fclose(out);
		return 0;
}
#endif




