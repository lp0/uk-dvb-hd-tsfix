/*
 * Copyright ©2009  Simon Arlott
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

/* CRC32 from xine-lib */
static uint32_t crc32_table[256];

static void demux_ts_build_crc32_table(void) {
	uint32_t i, j, k;

	for (i = 0; i < 256; i++) {
		k = 0;
		for (j = (i << 24) | 0x800000; j != 0x80000000; j <<= 1)
			k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
		crc32_table[i] = k;
	}
}

static uint32_t demux_ts_compute_crc32(uint8_t *data, uint32_t length) {
	uint32_t i, crc32;

	crc32 = 0xffffffff;

	for (i = 0; i < length; i++)
		crc32 = (crc32 << 8) ^ crc32_table[(crc32 >> 24) ^ data[i]];

	return crc32;
}

#define TS_LEN 188

int main(int argc, char *argv[]) {
	int fd, ret;
	off_t i;
	struct stat f_stat;
	uint8_t *data;

	if (argc != 2) {
		printf("Usage: %s <filename>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	demux_ts_build_crc32_table();

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror(argv[1]);
		exit(EXIT_FAILURE);
	}

	ret = fstat(fd, &f_stat);
	if (ret != 0) {
		perror("fstat");
		exit(EXIT_FAILURE);
	}

	data = mmap(NULL, f_stat.st_size - (f_stat.st_size % TS_LEN), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < f_stat.st_size - (f_stat.st_size % TS_LEN); i += TS_LEN) {
		unsigned int pos = 4;
		unsigned int dpos, progs;
#ifdef DEBUG
		unsigned int pid = ((data[i+1] & 0x1f) << 8) | data[i+2];
#endif
		unsigned int first = 1;
		unsigned int modify = 0;

		if (data[i+0] != 0x47) /* sync byte */
			continue;

		if (data[i+1] & 0x80) /* errors */
			continue;

		if (!(data[i+1] & 0x40)) /* not start of PES/PSI */
			continue;

		if (data[i+3] & 0xc0) /* scrambled */
			continue;

		if (!(data[i+3] & 0x10)) /* no data */
			continue;

		if (data[i+3] & 0x20) { /* skip adaptation field */
			pos += data[i+4];
			if (pos >= TS_LEN) /* overflow */
				continue;
		}

		pos += data[i+pos+0] + 1; /* skip PSI offset */
		if (pos+11 >= TS_LEN) /* overflow */
			continue;
		dpos = pos;

		if (data[i+pos+0] != 2) /* not PMT */
			continue;

		if ((data[i+pos+1] & 0x7c) != 0x30) /* invalid */
			continue;

		progs = ((data[i+pos+1] & 0xf) << 8) | data[i+pos+2];

		if (data[i+pos+6] != 0 || data[i+pos+7] != 0) /* not valid section length */
			continue;

		if ((data[i+pos+10] & 0xc) != 0) /* invalid */
			continue;

		pos += ((data[i+pos+10] & 0xf) << 8) | data[i+pos+11]; /* skip program descriptors */
		pos += 12; /* add reads so far */

		while (pos < dpos+12+progs) {
			if (pos+5 >= TS_LEN) /* overflow */
				break;

			if ((data[i+pos+1] & 0xe0) != 0xe0) /* invalid */
				break;

			if ((data[i+pos+3] & 0x30) != 0x30) /* invalid */
				break;

#ifdef DEBUG
			printf("%lu: pid %u prog %u type %u\n", (unsigned long)i, pid, ((data[i+pos+1] & 0x1f) << 8) | data[i+pos+2], data[i+pos+0]);
#endif

			/* modify first elementary PID */
			if (first) {
				if (data[i+pos+0] == 6) /* type is UNKNOWN */
					modify = pos;
			}

			pos += ((data[i+pos+3] & 0xf) << 8) | data[i+pos+4]; /* skip elementary stream descriptors */
			pos += 5; /* add reads so far */
			first = 0;
		}

		if (pos+4 >= TS_LEN) /* overflow */
			continue;

		if (modify > 0) {
			uint32_t pkt_crc = (data[i+pos+0] << 24) | (data[i+pos+1] << 16) | (data[i+pos+2] << 8) | data[i+pos+3];
			uint32_t calc_crc = demux_ts_compute_crc32(&data[i+dpos], (i+pos) - (i+dpos));
#ifdef DEBUG
			printf("%lu: pid %u pmt crc (packet %08x) (calc %08x)\n", (unsigned long)i, pid, pkt_crc, calc_crc);
#endif
			/* mplayer doesn't check the crc :( */
			data[i+modify] = 0x1b; /* set to H264 */

			/* if the crc was valid, update it */
			if (pkt_crc == calc_crc) {
				calc_crc = demux_ts_compute_crc32(&data[i+dpos], (i+pos) - (i+dpos));
#ifdef DEBUG
				printf("%lu: pid %u pmt crc (before %08x) (after %08x)\n", (unsigned long)i, pid, pkt_crc, calc_crc);
#endif
				data[i+pos+0] = (calc_crc >> 24) & 0xff;
				data[i+pos+1] = (calc_crc >> 16) & 0xff;
				data[i+pos+2] = (calc_crc >> 8) & 0xff;
				data[i+pos+3] = calc_crc & 0xff;
			}
		}
	}

	ret = msync(data, f_stat.st_size - (f_stat.st_size % TS_LEN), MS_SYNC);
	if (ret != 0) {
		perror("msync");
		exit(EXIT_FAILURE);
	}

	ret = close(fd);
	if (ret != 0) {
		perror("close");
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
