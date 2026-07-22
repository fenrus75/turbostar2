#define _GNU_SOURCE
#include "perf_catcher.h"
#include <fcntl.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define PERF_CACHE_SIZE 2048

struct perf_sample_slot {
	unsigned long ip;
	unsigned long count;
};

static char g_perf_dir[1024] = {0};
static int g_perf_fd = -1;
static void *g_mmap_base = NULL;
static size_t g_mmap_len = 0;
static struct perf_sample_slot g_perf_cache[PERF_CACHE_SIZE];

static size_t safe_strlen(const char *s)
{
	size_t len = 0;
	while (s && s[len])
		len++;
	return len;
}

static void safe_strcpy(char *dest, const char *src, int dest_size)
{
	int i = 0;
	while (src && src[i] && i < dest_size - 1) {
		dest[i] = src[i];
		i++;
	}
	dest[i] = '\0';
}

static void safe_strcat(char *dest, const char *src, int dest_size)
{
	int len = safe_strlen(dest);
	safe_strcpy(dest + len, src, dest_size - len);
}

static void safe_itoa(long val, char *buf, int buf_size)
{
	if (buf_size < 2)
		return;
	if (val == 0) {
		buf[0] = '0';
		buf[1] = '\0';
		return;
	}

	char temp[32];
	int i = 0;
	int is_neg = 0;

	if (val < 0) {
		is_neg = 1;
		val = -val;
	}

	while (val > 0 && i < 31) {
		temp[i++] = (val % 10) + '0';
		val /= 10;
	}

	if (is_neg && i < 31) {
		temp[i++] = '-';
	}

	int j = 0;
	while (i > 0 && j < buf_size - 1) {
		buf[j++] = temp[--i];
	}
	buf[j] = '\0';
}

static long sys_perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd,
				unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

void turboperf_init(void)
{
	const char *env_dir = getenv("TURBOSTAR_PERF_DIR");
	if (!env_dir || env_dir[0] == '\0') {
		return;
	}

	safe_strcpy(g_perf_dir, env_dir, sizeof(g_perf_dir));
	mkdir(g_perf_dir, 0755);

	struct perf_event_attr pe;
	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_HARDWARE;
	pe.size = sizeof(struct perf_event_attr);
	pe.config = PERF_COUNT_HW_CPU_CYCLES;
	pe.sample_freq = 1000;
	pe.freq = 1;
	pe.sample_type = PERF_SAMPLE_IP;
	pe.disabled = 0;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	int fd = (int)sys_perf_event_open(&pe, 0, -1, -1, 0);
	if (fd < 0) {
		// Fallback to software CPU clock sampling for virtualized environments/VMs/containers
		pe.type = PERF_TYPE_SOFTWARE;
		pe.config = PERF_COUNT_SW_CPU_CLOCK;
		fd = (int)sys_perf_event_open(&pe, 0, -1, -1, 0);
	}
	if (fd < 0) {
		return;
	}
	g_perf_fd = fd;

	long page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		page_size = 4096;
	}

	size_t num_pages = 256; // 1 MB ring buffer
	g_mmap_len = (1 + num_pages) * (size_t)page_size;

	g_mmap_base = mmap(NULL, g_mmap_len, PROT_READ | PROT_WRITE, MAP_SHARED, g_perf_fd, 0);
	if (g_mmap_base == MAP_FAILED) {
		close(g_perf_fd);
		g_perf_fd = -1;
		g_mmap_base = NULL;
		return;
	}

	memset(g_perf_cache, 0, sizeof(g_perf_cache));
}

void turboperf_shutdown(void)
{
	if (g_perf_dir[0] == '\0') {
		return;
	}

	pid_t pid = getpid();
	char pid_str[32];
	safe_itoa(pid, pid_str, sizeof(pid_str));

	// 1. Copy /proc/self/maps to <TURBOSTAR_PERF_DIR>/perf_maps_<pid>.txt
	char maps_out_path[1024] = {0};
	safe_strcpy(maps_out_path, g_perf_dir, sizeof(maps_out_path));
	safe_strcat(maps_out_path, "/perf_maps_", sizeof(maps_out_path));
	safe_strcat(maps_out_path, pid_str, sizeof(maps_out_path));
	safe_strcat(maps_out_path, ".txt", sizeof(maps_out_path));

	int maps_in = open("/proc/self/maps", O_RDONLY);
	if (maps_in >= 0) {
		int maps_out = open(maps_out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (maps_out >= 0) {
			char buf[1024];
			ssize_t n;
			while ((n = read(maps_in, buf, sizeof(buf))) > 0) {
				write(maps_out, buf, (size_t)n);
			}
			close(maps_out);
		}
		close(maps_in);
	}

	if (g_perf_fd < 0 || !g_mmap_base) {
		return;
	}

	// Disable PMU sampling
	ioctl(g_perf_fd, PERF_EVENT_IOC_DISABLE, 0);

	// 2. Open samples binary file <TURBOSTAR_PERF_DIR>/perf_samples_<pid>.dat
	char samples_out_path[1024] = {0};
	safe_strcpy(samples_out_path, g_perf_dir, sizeof(samples_out_path));
	safe_strcat(samples_out_path, "/perf_samples_", sizeof(samples_out_path));
	safe_strcat(samples_out_path, pid_str, sizeof(samples_out_path));
	safe_strcat(samples_out_path, ".dat", sizeof(samples_out_path));

	int samples_fd = open(samples_out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	// 3. Drain sample ring buffer into in-memory direct-mapped cache
	struct perf_event_mmap_page *header = (struct perf_event_mmap_page *)g_mmap_base;
	__sync_synchronize();
	uint64_t head = header->data_head;
	uint64_t tail = header->data_tail;
	unsigned char *data = (unsigned char *)g_mmap_base + header->data_offset;
	uint64_t data_size = header->data_size;

	if (samples_fd >= 0 && data_size > 0) {
		while (tail < head) {
			struct perf_event_header *eh = (struct perf_event_header *)(data + (tail % data_size));
			if (eh->size == 0) {
				break;
			}

			if (eh->type == PERF_RECORD_SAMPLE) {
				unsigned long ip = *(unsigned long *)((char *)eh + sizeof(struct perf_event_header));
				size_t idx = (ip ^ (ip >> 12)) & (PERF_CACHE_SIZE - 1);
				if (g_perf_cache[idx].ip == ip) {
					g_perf_cache[idx].count++;
				} else {
					if (g_perf_cache[idx].ip != 0 && g_perf_cache[idx].count > 0) {
						write(samples_fd, &g_perf_cache[idx], sizeof(struct perf_sample_slot));
					}
					g_perf_cache[idx].ip = ip;
					g_perf_cache[idx].count = 1;
				}
			}
			tail += eh->size;
		}

		// Flush remaining slots in g_perf_cache to disk
		for (size_t i = 0; i < PERF_CACHE_SIZE; ++i) {
			if (g_perf_cache[i].ip != 0 && g_perf_cache[i].count > 0) {
				write(samples_fd, &g_perf_cache[i], sizeof(struct perf_sample_slot));
			}
		}
		close(samples_fd);
	}

	munmap(g_mmap_base, g_mmap_len);
	close(g_perf_fd);
	g_perf_fd = -1;
	g_mmap_base = NULL;
}
