/*
 * gptresize.c - Extend the last GPT partition to the end of the disk.
 *
 * Usage: gptresize <block_device_or_image>
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define GPT_SIGNATURE "EFI PART"
#define SECTOR_SIZE 512U
#define PRIMARY_HEADER_LBA 1ULL

typedef struct __attribute__((packed)) {
    uint8_t signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t entries_lba;
    uint32_t num_entries;
    uint32_t entry_size;
    uint32_t entries_crc32;
} gpt_header_t;

typedef struct __attribute__((packed)) {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint8_t name[72];
} gpt_entry_t;

static uint32_t crc32_gpt(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t index = 0; index < len; index++) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }

    return crc ^ 0xFFFFFFFFU;
}

static void die_errno(const char *message)
{
    fprintf(stderr, "gptresize: %s: %s\n", message, strerror(errno));
    exit(1);
}

static void die_message(const char *message)
{
    fprintf(stderr, "gptresize: %s\n", message);
    exit(1);
}

static void read_exact(int fd, void *buf, size_t count, off_t offset)
{
    uint8_t *ptr = buf;
    size_t done = 0;

    if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
        die_errno("lseek for read");

    while (done < count) {
        ssize_t ret = read(fd, ptr + done, count - done);
        if (ret < 0)
            die_errno("read");
        if (ret == 0) {
            errno = EIO;
            die_errno("unexpected EOF");
        }
        done += (size_t)ret;
    }
}

static void write_exact(int fd, const void *buf, size_t count, off_t offset)
{
    const uint8_t *ptr = buf;
    size_t done = 0;

    if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
        die_errno("lseek for write");

    while (done < count) {
        ssize_t ret = write(fd, ptr + done, count - done);
        if (ret < 0)
            die_errno("write");
        if (ret == 0) {
            errno = EIO;
            die_errno("short write");
        }
        done += (size_t)ret;
    }
}

static uint64_t get_disk_bytes(int fd)
{
    uint64_t disk_bytes = 0;
    struct stat st;
    off_t end;

    if (ioctl(fd, BLKGETSIZE64, &disk_bytes) == 0)
        return disk_bytes;

    if (fstat(fd, &st) < 0)
        die_errno("fstat");

    if (S_ISREG(st.st_mode)) {
        end = lseek(fd, 0, SEEK_END);
        if (end < 0)
            die_errno("lseek to end");
        return (uint64_t)end;
    }

    die_errno("BLKGETSIZE64 ioctl");
    return 0;
}

static int entry_is_used(const gpt_entry_t *entry)
{
    for (size_t i = 0; i < sizeof(entry->type_guid); i++) {
        if (entry->type_guid[i] != 0)
            return 1;
    }

    return 0;
}

static uint32_t compute_header_crc(uint8_t *header_sector, uint32_t header_size)
{
    uint8_t scratch[SECTOR_SIZE];
    gpt_header_t *header;

    if (header_size < sizeof(gpt_header_t) || header_size > SECTOR_SIZE)
        die_message("invalid GPT header_size");

    memcpy(scratch, header_sector, SECTOR_SIZE);
    header = (gpt_header_t *)scratch;
    header->header_crc32 = 0;

    return crc32_gpt(scratch, header_size);
}

int main(int argc, char **argv)
{
    const char *path;
    int fd;
    uint8_t primary_header_sector[SECTOR_SIZE];
    uint8_t backup_header_sector[SECTOR_SIZE];
    gpt_header_t *primary_header;
    gpt_header_t *backup_header;
    uint8_t *entries;
    size_t entries_bytes;
    size_t entries_sectors;
    size_t entries_span_bytes;
    uint32_t entries_crc;
    uint64_t disk_bytes;
    uint64_t disk_sectors;
    uint64_t new_alternate_lba;
    uint64_t new_backup_entries_lba;
    uint64_t new_last_usable_lba;
    int last_index = -1;
    uint64_t max_last_lba = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: gptresize <block_device_or_image>\n");
        return 1;
    }

    path = argv[1];
    fd = open(path, O_RDWR | O_SYNC);
    if (fd < 0)
        die_errno("open");

    read_exact(fd, primary_header_sector, sizeof(primary_header_sector),
               PRIMARY_HEADER_LBA * SECTOR_SIZE);

    primary_header = (gpt_header_t *)primary_header_sector;
    if (memcmp(primary_header->signature, GPT_SIGNATURE, 8) != 0)
        die_message("not a valid GPT disk (missing EFI PART signature)");

    if (primary_header->header_size < sizeof(gpt_header_t) ||
        primary_header->header_size > SECTOR_SIZE)
        die_message("unsupported GPT header size");

    printf("GPT signature valid, revision=0x%x, header_size=%u\n",
           primary_header->revision, primary_header->header_size);
    printf("Current layout:\n");
    printf("  Last usable LBA: %llu\n",
           (unsigned long long)primary_header->last_usable_lba);
    printf("  Alternate LBA:  %llu\n",
           (unsigned long long)primary_header->alternate_lba);
    printf("  Entries: %u entries of %u bytes at LBA %llu\n",
           primary_header->num_entries, primary_header->entry_size,
           (unsigned long long)primary_header->entries_lba);

    if (primary_header->entry_size < sizeof(gpt_entry_t) ||
        primary_header->num_entries == 0 ||
        primary_header->num_entries > 1024)
        die_message("invalid GPT entry size or count");

    entries_bytes = (size_t)primary_header->num_entries * primary_header->entry_size;
    entries_sectors = (entries_bytes + SECTOR_SIZE - 1U) / SECTOR_SIZE;
    entries_span_bytes = entries_sectors * SECTOR_SIZE;
    printf("  Entries span: %zu sectors (%zu bytes)\n", entries_sectors, entries_bytes);

    entries = calloc(1, entries_span_bytes);
    if (!entries)
        die_errno("calloc entries");

    read_exact(fd, entries, entries_span_bytes,
               (off_t)(primary_header->entries_lba * SECTOR_SIZE));

    entries_crc = crc32_gpt(entries, entries_bytes);
    if (entries_crc != primary_header->entries_crc32) {
        fprintf(stderr,
                "gptresize: WARNING: primary entries CRC mismatch (stored=0x%08x, computed=0x%08x)\n",
                primary_header->entries_crc32, entries_crc);
    }

    for (uint32_t i = 0; i < primary_header->num_entries; i++) {
        gpt_entry_t *entry = (gpt_entry_t *)(entries + ((size_t)i * primary_header->entry_size));

        if (!entry_is_used(entry))
            continue;

        printf("  Partition %u: LBA %llu - %llu (%llu sectors)\n",
               i + 1,
               (unsigned long long)entry->first_lba,
               (unsigned long long)entry->last_lba,
               (unsigned long long)(entry->last_lba - entry->first_lba + 1ULL));

        if (entry->last_lba > max_last_lba) {
            max_last_lba = entry->last_lba;
            last_index = (int)i;
        }
    }

    if (last_index < 0)
        die_message("no GPT partitions found");

    disk_bytes = get_disk_bytes(fd);
    disk_sectors = disk_bytes / SECTOR_SIZE;
    if (disk_sectors < 34)
        die_message("disk is too small for GPT");

    printf("Disk size: %llu bytes = %llu sectors\n",
           (unsigned long long)disk_bytes,
           (unsigned long long)disk_sectors);

    new_alternate_lba = disk_sectors - 1ULL;
    new_backup_entries_lba = new_alternate_lba - entries_sectors;
    new_last_usable_lba = new_backup_entries_lba - 1ULL;

    printf("New layout:\n");
    printf("  Last usable LBA: %llu\n", (unsigned long long)new_last_usable_lba);
    printf("  Alternate LBA:  %llu\n", (unsigned long long)new_alternate_lba);
    printf("  Partition %d new end LBA: %llu\n",
           last_index + 1, (unsigned long long)new_last_usable_lba);

    if (new_last_usable_lba <= max_last_lba) {
        printf("Partition already at maximum size. Nothing to do.\n");
        free(entries);
        close(fd);
        return 0;
    }

    {
        gpt_entry_t *target = (gpt_entry_t *)(entries + ((size_t)last_index * primary_header->entry_size));
        printf("  Old size: %llu sectors -> New size: %llu sectors\n",
               (unsigned long long)(target->last_lba - target->first_lba + 1ULL),
               (unsigned long long)(new_last_usable_lba - target->first_lba + 1ULL));
        target->last_lba = new_last_usable_lba;
    }

    entries_crc = crc32_gpt(entries, entries_bytes);
    printf("  New entries CRC32: 0x%08x\n", entries_crc);

    primary_header->last_usable_lba = new_last_usable_lba;
    primary_header->alternate_lba = new_alternate_lba;
    primary_header->entries_crc32 = entries_crc;
    primary_header->header_crc32 = compute_header_crc(primary_header_sector,
                                                      primary_header->header_size);
    printf("  New primary header CRC32: 0x%08x\n", primary_header->header_crc32);

    memcpy(backup_header_sector, primary_header_sector, sizeof(backup_header_sector));
    backup_header = (gpt_header_t *)backup_header_sector;
    backup_header->my_lba = new_alternate_lba;
    backup_header->alternate_lba = PRIMARY_HEADER_LBA;
    backup_header->entries_lba = new_backup_entries_lba;
    backup_header->header_crc32 = compute_header_crc(backup_header_sector,
                                                     backup_header->header_size);
    printf("  New backup header CRC32: 0x%08x\n", backup_header->header_crc32);

    write_exact(fd, entries, entries_span_bytes,
                (off_t)(primary_header->entries_lba * SECTOR_SIZE));
    write_exact(fd, primary_header_sector, sizeof(primary_header_sector),
                PRIMARY_HEADER_LBA * SECTOR_SIZE);
    write_exact(fd, entries, entries_span_bytes,
                (off_t)(new_backup_entries_lba * SECTOR_SIZE));
    write_exact(fd, backup_header_sector, sizeof(backup_header_sector),
                (off_t)(new_alternate_lba * SECTOR_SIZE));

    if (fsync(fd) < 0)
        die_errno("fsync");

    close(fd);
    free(entries);

    printf("Done! GPT partition resized successfully.\n");
    return 0;
}
