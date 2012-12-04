/* Utility tool to convert pgmping recorded latency files into gnuplot
 * source files for drawing a heatmap.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

const char header[] =
	"set title \"PGM Latency Heat Map\"\n"
	"unset key\n"
	"set tic scale 0\n"
	"\n"
	"# Colour runs from white to green\n"
	"set palette rgbformula -6,2,-3\n"
	"set cbrange [0:25]\n"
	"set cblabel \"Density\"\n"
	"unset cbtics\n"
	"\n"
	"set xrange [-0.5:%u.5]\n"
	"set yrange [-0.5:%u.5]\n"
	"\n"
	"set view map\n"
	"plot '-' using 1:2:3 with image\n";
const char footer[] =
	"e\n";

const unsigned yrange_max = 2*1000;
const unsigned yrange_ivl = 10;

static
int
key_compare (
	const void*	p1,
	const void*	p2
	)
{
	const uint32_t i = *((uint32_t*)p1);
	const uint32_t j = *((uint32_t*)p2);

	if (i > j)
		return 1;
	if (i < j)
		return -1;
	return 0;
}

int main (void) {
	const char src[] = "/tmp/heat.dmp";
	FILE* fh;
	uint32_t slice_count, alloc_count = 0;
	uint32_t* words;
	unsigned slice = 0, xrange_max = 0, density_max = 0;

	if (NULL == (fh = fopen (src, "r"))) {
		perror ("fopen");
		return EXIT_FAILURE;
	}

/* find time period of dump */
	while (1) {
		if (1 != fread (&slice_count, sizeof (slice_count), 1, fh)) {
			break;
		}
		slice_count = ntohl (slice_count);
		if (0 == alloc_count) {
			alloc_count = slice_count;
			words = malloc (alloc_count * sizeof (uint32_t) * 2);
		} else if (slice_count > alloc_count) {
			alloc_count = slice_count;
			words = realloc (words, alloc_count * sizeof (uint32_t) * 2);
		}
		if (slice_count != fread (words, sizeof (uint32_t) * 2, slice_count, fh)) {
			perror ("words");
			goto abort;
		}
		for (int_fast32_t i = slice_count - 1; i >= 0; i--) {
			const unsigned density = ntohl (words[(2*i)+1]);
			if (density > density_max)
				density_max = density;
		}
		xrange_max++;
	}
	rewind (fh);

	printf (header, xrange_max - 1, yrange_max - yrange_ivl);

	while (1) {
		if (1 != fread (&slice_count, sizeof (slice_count), 1, fh)) {
			break;
		}
		slice_count = ntohl (slice_count);
		if (slice_count != fread (words, sizeof (uint32_t) * 2, slice_count, fh)) {
			perror ("words");
			goto abort;
		}
		for (int_fast32_t i = slice_count - 1; i >= 0; i--) {
			words[(2*i)+0] = ntohl (words[(2*i)+0]);
			words[(2*i)+1] = ((ntohl (words[(2*i)+1]) * 25) + (density_max - 1)) / density_max;
		}
		qsort ((void*)words, slice_count, sizeof (uint32_t) * 2, key_compare);
		for (int_fast32_t i = 0, j = 0; i < slice_count; i++) {
			while ((j * yrange_ivl) < words[(2*i)+0]) {
				if ((j * yrange_ivl) >= yrange_max)
					goto end_slice;
				printf ("%u %u 0\n",
					(unsigned)slice,
					(unsigned)(j * yrange_ivl));
				j++;
			}
			if ((j * yrange_ivl) >= yrange_max)
				goto end_slice;
			printf ("%u %u %u\n",
				(unsigned)slice,
				(unsigned)(j * yrange_ivl),
				(unsigned)words[(2*i)+1]);
			j++;
		}
		for (int_fast32_t j = words[(2*(slice_count-1))+0] + yrange_ivl; j < yrange_max; j += yrange_ivl) {
			printf ("%u %u 0\n",
				(unsigned)slice,
				(unsigned)(j));
		}
end_slice:
		putchar ('\n');
		slice++;
	}

	puts (footer);

abort:
	fclose (fh);
	return EXIT_SUCCESS;
}
