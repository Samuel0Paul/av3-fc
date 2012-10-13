#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crescent.h"

static int handle_msg1(struct msg1 *m)
{
	printf("\n\n01: ");
	switch (m->nav_mode & 0x7f)
	{
	case 0:		printf("No fix     "); break;
	case 1:		printf("2D fix     "); break;
	case 2:		printf("3D fix     "); break;
	case 3:		printf("2D+diff    "); break;
	case 4:		printf("3D+diff    "); break;
	case 5:		printf("RTK search "); break;
	case 6:		printf("3D+diff+RTK"); break;
	default:	printf("BAD NAV MODE: %d", m->nav_mode);
	}
	if (m->nav_mode & 0x80)
		printf(" (manual)");
	printf(" %u SATs,", m->num_sats);
	printf(" %0.5lf LAT, %0.5lf LON, %02.3lfm ALT,", m->latitude, m->longitude, m->height);
	printf(" %0.3f vN, %0.3f vE, %0.3f vZ\n\n", m->v_north, m->v_east, m->v_up);
	return 0;
}

static int handle_msg99(struct msg99 *m)
{
	// we care about overall fix, and each satellite's SNR
	printf("\n\n99: ");
	int i, sats = 0;
	switch (m->nav_mode & 0x7)
	{
	case 0:		printf("Inv fix"); break;
	case 1:		printf("No fix "); break;
	case 2:		printf("2D fix "); break;
	case 3:		printf("3D fix "); break;
	default:	printf("BAD NAV MODE: %d", m->nav_mode);
	}
	for (i=0; i<12; i++)
		if ((m->c[i].status & STATUS99_CHANNEL_RESET) == 0)
			sats++;
	printf(" - %02d sats", sats);

	struct chan99 *c;
	for (c = m->c; c < m->c+12; c++) {
		if (c->status & STATUS99_CHANNEL_RESET) continue;
		printf(" / %3u:%3.0lf", c->sat,
			40960.0 * c->cli_no / 80000.0);
	}
	printf("\n\n");
	return 0;
}

static int handle_packet(struct msg *m)
{
	switch (m->type)
	{
	case 1:
		if (m->len == sizeof(struct msg1))
			return handle_msg1(&m->m1);
		fprintf(stderr, "bad length %d != %lu\n", m->len, sizeof(struct msg1));
		return -3;
	case 99:
		if (m->len == sizeof(struct msg99))
			return handle_msg99(&m->m99);
		fprintf(stderr, "bad length %d != %lu\n", m->len, sizeof(struct msg99));
		return -3;
	default:
		// fprintf(stderr, "unhandled packet type: %d length %d\n", m->type, m->len);
		fprintf(stderr, "%03d ", m->type);
		return -4;
	}
}

static uint16_t sum(uint8_t *packet, int len)
{
	uint16_t s = 0;
	while (len--)
		s += *packet++;
	return s;
}

static void *memstr(const void *m, const char *s, size_t len)
{
	size_t l = strlen(s);
	void *p;
	while ((p = memchr(m, *s, len)) != NULL)
	{
		len -= p-m + 1;
		if (len <= l)
			return NULL;
		if (memcmp(p, s, l) == 0)
			return p;
		m = p + 1;
	}
	return p;
}

int get_packet(FILE *gps, struct msg *m)
{
	static char buf[8];
	char *p;
	uint16_t checksum;

	// sync stream to "$BIN"
	do {
		fread(buf, 1, 8, gps);
		p = memchr(buf, '$', 8);
		if (p == NULL)
			continue;
		if (p > buf) {
			memmove(buf, p, 8-(p-buf));
			fread(buf + 8-(p-buf), 1, p-buf, gps);
		}
	} while (memcmp(buf, "$BIN", 4) != 0);
	memcpy(m, buf, 8);

	if (m->type > 99 || m->len > 304) {
		fprintf(stderr, "bad header\n");
		return -1;
	}

	fread(&m->raw, 1, m->len, gps);
	// check whether packet was truncated
	if ((p = memstr(m->raw, "$BIN", m->len)) != NULL) {
		// TODO: resync on found header
		fprintf(stderr, "packet truncated: got %ld wanted %d\n", p-m->raw, m->len);
		return -2;
	}
	fread(&checksum, 2, 1, gps);
	fread(buf, 1, 2, gps);			// \r\n

	if (sum(m->raw, m->len) != checksum) {
		fprintf(stderr, "bad checksum %u (expected %u)\n", sum(m->raw, m->len), checksum);
		return -3;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s gps-device\n", argv[0]);
		exit(1);
	}

	FILE *gps = fopen(argv[1], "r");
	if (gps == NULL) {
		fprintf(stderr, "can't open %s\n", argv[1]);
		exit(1);
	}
#if 0
	// turn on binary packet 99
	char buf[16];
	fprintf(gps, "$JBIN,99,1\r\n");	fflush(gps);
	fread(buf, 1, 2, gps);
	if (memcmp(buf, "$>", 2) != 0) {
		fprintf(stderr, "bad response from device, starting with '%c%c'\n", buf[0], buf[1]);
		exit(2);
	}
#endif

	// suck packets forever
	while (1) {
		struct msg m;

		if (get_packet(gps, &m) < 0)
			continue;

		handle_packet(&m);
	}
	return 0;
}