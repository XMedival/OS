#include "fat.h"
#include "blk.h"
#include "mem.h"
#include "panic.h"
#include "print.h"

u8 *fat_table;
u32 fat_pages;
u32 fat_sector;
struct FSInfo *fs_info;
struct boot_sector *boot_sect;
static u8 fat_dirty = 0;
static u64 partition_lba = 0;          // Starting LBA of the FAT partition
static struct blk_device *fat_blk_dev; // Block device used for all I/O

// Round up to number of pages needed for n bytes
#define PAGES_FOR(n) (((n) + PAGE_SIZE - 1) / PAGE_SIZE)

// Helper to compare GUIDs
static int guid_match(const u8 *a, const u8 *b) {
    for (int i = 0; i < 16; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

// Check if partition type is FAT
static int is_fat_partition(u8 type) {
    return type == MBR_TYPE_FAT32_LBA || type == MBR_TYPE_FAT32 ||
           type == MBR_TYPE_FAT16_LBA || type == MBR_TYPE_FAT16;
}

// Find FAT partition in MBR, returns starting LBA or 0 if not found
static u64 find_mbr_partition(void) {
    u8 *buf = kalloc(1);
    if (!buf)
        return 0;

    blk_read(fat_blk_dev, 0, 1, buf);
    struct mbr *mbr = (struct mbr *)buf;

    if (mbr->signature != MBR_SIGNATURE) {
        kfree(buf, 1);
        return 0;
    }

    u64 fat_lba = 0;
    for (int i = 0; i < 4; i++) {
        struct mbr_partition *p = &mbr->partitions[i];
        if (is_fat_partition(p->partition_type)) {
            fat_lba = p->start_lba;
            break;
        }
    }

    kfree(buf, 1);
    return fat_lba;
}

// Find ESP partition in GPT, returns starting LBA or 0 if not found
static u64 find_gpt_partition(void) {
    u8 *buf = kalloc(1);
    if (!buf)
        return 0;

    // Read LBA 1 (GPT header)
    blk_read(fat_blk_dev, 1, 1, buf);
    struct gpt_header *gpt = (struct gpt_header *)buf;

    if (gpt->signature != GPT_SIGNATURE) {
        kfree(buf, 1);
        return 0;
    }

    u64 entries_lba = gpt->partition_entries_lba;
    u32 num_entries = gpt->num_partition_entries;
    u32 entry_size = gpt->partition_entry_size;

    kfree(buf, 1);

    // Read partition entries (typically at LBA 2)
    // Entries are 128 bytes each, 4 per sector
    u32 entries_per_sector = 512 / entry_size;
    u32 sectors_needed =
        (num_entries + entries_per_sector - 1) / entries_per_sector;

    buf = kalloc(PAGES_FOR(sectors_needed * 512));
    if (!buf)
        return 0;

    blk_read(fat_blk_dev, entries_lba, sectors_needed, buf);

    u64 esp_lba = 0;
    for (u32 i = 0; i < num_entries; i++) {
        struct gpt_entry *entry = (struct gpt_entry *)(buf + i * entry_size);

        // Check for null entry (type GUID all zeros)
        int is_null = 1;
        for (int j = 0; j < 16; j++) {
            if (entry->type_guid[j] != 0) {
                is_null = 0;
                break;
            }
        }
        if (is_null)
            continue;

        if (guid_match(entry->type_guid, ESP_GUID)) {
            esp_lba = entry->starting_lba;
            break;
        }
    }

    kfree(buf, PAGES_FOR(sectors_needed * 512));
    return esp_lba;
}

// Find FAT partition, tries GPT first, then MBR
static u64 find_fat_partition(void) {
    u64 lba = find_gpt_partition();
    if (lba != 0)
        return lba;

    return find_mbr_partition();
}

void fat_init(struct blk_device *dev) {
    fat_blk_dev = dev;
    // Find FAT partition (try GPT first, then MBR)
    partition_lba = find_fat_partition();
    if (partition_lba == 0) {
        puts("FAT: No partition found, assuming sector 0\r\n");
    }

    boot_sect = kalloc(1);
    if (!boot_sect)
        panic("OUT OF MEMORY", 0);
    blk_read(fat_blk_dev, partition_lba, 1, boot_sect);

    // Verify FAT boot signature
    if (boot_sect->ebr.boot_signature != FAT_BOOT_SIGNATURE) {
        panic("Invalid FAT boot sector", 0);
    }

    fs_info = kalloc(1); // FSInfo is one sector, fits in one page
    if (!fs_info)
        panic("OUT OF MEMORY", 0);
    blk_read(fat_blk_dev, partition_lba + boot_sect->ebr.fsinfo_sector, 1,
             fs_info);

    // Calculate pages needed for FAT table
    u32 fat_size_bytes =
        boot_sect->ebr.sectors_per_fat * boot_sect->bpb.bytes_per_sector;
    fat_pages = PAGES_FOR(fat_size_bytes);
    if (fat_pages == 0)
        fat_pages = 1;

    fat_table = kalloc(fat_pages);
    if (!fat_table)
        panic("OUT OF MEMORY", 0);

    // Read FAT table
    fat_sector = partition_lba + boot_sect->bpb.reserved_sectors;
    blk_read(fat_blk_dev, fat_sector, boot_sect->ebr.sectors_per_fat,
             fat_table);
    puts("Hello from fat_init()\r\n");

    puts("[ OK ] FAT initialized\r\n");
}

inline void fat_mark_dirty(void) { fat_dirty = 1; }

void fat_flush(void) {
    for (u8 i = 0; i < boot_sect->bpb.num_fats; i++) {
        u64 sector = fat_sector + (i * boot_sect->ebr.sectors_per_fat);
        blk_write(fat_blk_dev, sector, boot_sect->ebr.sectors_per_fat,
                  fat_table);
    }
    u64 sector = partition_lba + boot_sect->ebr.fsinfo_sector;
    blk_write(fat_blk_dev, sector, 1, fs_info);
}

void fat_flush_dirty(void) {
    if (!fat_dirty)
        return;
    fat_flush();
    fat_dirty = 0;
}

u32 cluster_to_sector(u32 cluster) {
    u32 data_start = partition_lba + boot_sect->bpb.reserved_sectors +
                     (boot_sect->bpb.num_fats * boot_sect->ebr.sectors_per_fat);
    return data_start + (cluster - 2) * boot_sect->bpb.sectors_per_cluster;
}

u32 fat_read_entry(u32 cluster) {
    u32 *fat = (u32 *)fat_table;
    return fat[cluster] & 0x0FFFFFFF;
}

void fat_write_entry(u32 cluster, u32 val) {
    u32 *fat = (u32 *)fat_table;
    fat[cluster] = val;
}

u32 fat_alloc_cluster(void) {
    u32 *fat = (u32 *)fat_table;

    u32 start = fs_info->free_clusters_start;
    if (start < 2)
        start = 2;

    u32 total = boot_sect->ebr.sectors_per_fat * 128;

    for (u32 i = start; i < total; i++) {
        if ((fat[i] & 0x0FFFFFFF) == FAT_FREE) {
            fat[i] = FAT_EOF;
            fat_mark_dirty();

            fs_info->free_clusters_start = i + 1;
            if (fs_info->last_known_free_cluster > 0)
                fs_info->last_known_free_cluster--;
            return i;
        }
    }

    for (u32 i = 2; i < start; i++) {
        if ((fat[i] & 0x0FFFFFFF) == FAT_FREE) {
            fat[i] = FAT_EOF;
            fat_mark_dirty();
            fs_info->free_clusters_start = i + 1;
            if (fs_info->last_known_free_cluster > 0)
                fs_info->last_known_free_cluster--;
            return i;
        }
    }
    return 0;
}

u32 fat_extend_chain(u32 last_cluster) {
    u32 new = fat_alloc_cluster();
    if (!new)
        return -1;
    fat_write_entry(last_cluster, new);
    return new;
}

void fat_free_chain(u32 start) {
    u32 *fat = (u32 *)fat_table;

    while (start >= 2 && start < FAT_EOF) {
        u32 next = fat[start] & 0x0FFFFFFF;
        fat[start] = FAT_FREE;
        start = next;
    }
    fat_mark_dirty();
}

u32 fat_readdir(u32 dir_cluster, struct fat_dir_entry *out, u32 index) {
    u32 entries_per_cluster =
        (boot_sect->bpb.sectors_per_cluster * boot_sect->bpb.bytes_per_sector) /
        32;
    u32 cluster = dir_cluster;

    while (index >= entries_per_cluster) {
        cluster = fat_read_entry(cluster);
        if (cluster >= FAT_EOF)
            return -1;
        index -= entries_per_cluster;
    }

    u32 sector = cluster_to_sector(cluster);

    u32 cluster_size =
        boot_sect->bpb.sectors_per_cluster * boot_sect->bpb.bytes_per_sector;
    u32 pages = PAGES_FOR(cluster_size);
    u8 *buf = kalloc(pages);
    if (!buf)
        return -1;

    blk_read(fat_blk_dev, sector, boot_sect->bpb.sectors_per_cluster, buf);
    struct fat_dir_entry *entries = (struct fat_dir_entry *)buf;
    if (entries[index].file_name[0] == 0x00) {
        kfree(buf, pages);
        return -1;
    }

    *out = entries[index];
    kfree(buf, pages);
    return 0;
}

// Helper: get cluster number from directory entry
static inline u32 entry_cluster(struct fat_dir_entry *e) {
    return ((u32)e->first_cluster_high << 16) | e->first_cluster_low;
}

// Helper: compare name against 8.3 padded FAT name
static int fat_name_match(const u8 *fat_name, const char *name) {
    char padded[11];
    int i = 0, j = 0;

    // Fill with spaces
    for (int k = 0; k < 11; k++)
        padded[k] = ' ';

    // Copy name part (up to 8 chars, stop at dot)
    while (name[i] && name[i] != '.' && j < 8) {
        padded[j++] = (name[i] >= 'a' && name[i] <= 'z')
                          ? name[i] - 32 // Uppercase
                          : name[i];
        i++;
    }

    // Skip to extension
    if (name[i] == '.')
        i++;

    // Copy extension (up to 3 chars)
    j = 8;
    while (name[i] && j < 11) {
        padded[j++] =
            (name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i];
        i++;
    }

    // Compare
    for (int k = 0; k < 11; k++) {
        if (padded[k] != fat_name[k])
            return 0;
    }
    return 1;
}

// Extract characters from LFN entry into buffer
// Returns number of characters extracted (up to 13)
static int lfn_extract_chars(struct lfn_extension *lfn, char *buf, int max) {
    int pos = 0;

    // First 5 chars from name0 (10 bytes, UTF-16LE)
    for (int i = 0; i < 10 && pos < max; i += 2) {
        u16 c = lfn->name0[i] | (lfn->name0[i + 1] << 8);
        if (c == 0 || c == 0xFFFF)
            return pos;
        buf[pos++] = (char)c; // ASCII only for now
    }

    // Next 6 chars from name1 (12 bytes, UTF-16LE)
    for (int i = 0; i < 12 && pos < max; i += 2) {
        u16 c = lfn->name1[i] | (lfn->name1[i + 1] << 8);
        if (c == 0 || c == 0xFFFF)
            return pos;
        buf[pos++] = (char)c;
    }

    // Last 2 chars from name2 (4 bytes, UTF-16LE)
    for (int i = 0; i < 4 && pos < max; i += 2) {
        u16 c = lfn->name2[i] | (lfn->name2[i + 1] << 8);
        if (c == 0 || c == 0xFFFF)
            return pos;
        buf[pos++] = (char)c;
    }

    return pos;
}

// Case-insensitive string compare
static int strcasecmp_fat(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb)
            return 1;
        a++;
        b++;
    }
    return *a != *b;
}

u32 fat_findentry(u32 dir_cluster, const char *name,
                  struct fat_dir_entry *out) {
    struct fat_dir_entry entry;

    // LFN buffer: max 255 chars + null
    char lfn_name[256];
    int lfn_len = 0;
    int collecting_lfn = 0;

    // Temporary storage for LFN entries (max 20 entries for 255 chars)
    struct lfn_extension lfn_entries[20];
    int lfn_count = 0;

    for (u32 i = 0; fat_readdir(dir_cluster, &entry, i) == 0; i++) {
        if (entry.file_name[0] == 0xE5) {
            // Deleted entry - reset LFN collection
            collecting_lfn = 0;
            lfn_count = 0;
            continue;
        }

        if (entry.attributes == FAT_ATTR_LFN) {
            // LFN entry - collect it
            struct lfn_extension *lfn = (struct lfn_extension *)&entry;
            u8 seq = lfn->entry_order & 0x3F; // Sequence number (1-20)

            if (lfn->entry_order & 0x40) {
                // Last LFN entry (first in sequence on disk)
                collecting_lfn = 1;
                lfn_count = seq;
                if (lfn_count > 20)
                    lfn_count = 20;
            }

            if (collecting_lfn && seq > 0 && seq <= 20) {
                lfn_entries[seq - 1] = *lfn;
            }
            continue;
        }

        // Regular entry - reconstruct LFN if we collected entries
        lfn_len = 0;
        if (collecting_lfn && lfn_count > 0) {
            // Reconstruct long filename from collected entries
            for (int j = 0; j < lfn_count && lfn_len < 255; j++) {
                lfn_len += lfn_extract_chars(&lfn_entries[j],
                                             lfn_name + lfn_len, 255 - lfn_len);
            }
            lfn_name[lfn_len] = '\0';
        }

        // Reset LFN collection
        collecting_lfn = 0;
        lfn_count = 0;

        // Check LFN match first
        if (lfn_len > 0 && strcasecmp_fat(lfn_name, name) == 0) {
            *out = entry;
            return 0;
        }

        // Fall back to 8.3 name match
        if (fat_name_match(entry.file_name, name)) {
            *out = entry;
            return 0;
        }
    }
    return -1; // Not found
}

u32 fat_open(const char *path, struct fat_dir_entry *out) {
    u32 cluster = boot_sect->ebr.root_dir_cluster;
    struct fat_dir_entry entry;

    // Skip leading slash
    if (*path == '/')
        path++;

    // Empty path = root directory
    if (*path == '\0') {
        out->attributes = FAT_ATTR_DIRECTORY;
        out->first_cluster_high = (cluster >> 16) & 0xFFFF;
        out->first_cluster_low = cluster & 0xFFFF;
        out->file_size = 0;
        return 0;
    }

    while (*path) {
        // Extract next path component
        char component[13];
        int i = 0;
        while (*path && *path != '/' && i < 12) {
            component[i++] = *path++;
        }
        component[i] = '\0';

        // Skip trailing slash
        if (*path == '/')
            path++;

        // Find in current directory
        if (fat_findentry(cluster, component, &entry) != 0) {
            return -1; // Not found
        }

        // If more path remains, this must be a directory
        if (*path && !(entry.attributes & FAT_ATTR_DIRECTORY)) {
            return -1; // Not a directory
        }

        // Descend into this entry
        cluster = entry_cluster(&entry);
    }

    *out = entry;
    return 0;
}

u32 fat_read(struct fat_dir_entry *file, u64 offset, u32 size, void *buf) {
    if (offset >= file->file_size)
        return 0;
    if (offset + size > file->file_size)
        size = file->file_size - offset;

    u32 cluster_size =
        boot_sect->bpb.sectors_per_cluster * boot_sect->bpb.bytes_per_sector;
    u32 cluster = entry_cluster(file);
    u32 bytes_read = 0;

    // Skip to starting cluster
    while (offset >= cluster_size) {
        cluster = fat_read_entry(cluster);
        if (cluster >= FAT_EOF)
            return bytes_read;
        offset -= cluster_size;
    }

    // Read clusters
    u8 *cluster_buf = kalloc(PAGES_FOR(cluster_size));
    if (!cluster_buf)
        return -1;

    while (size > 0 && cluster < FAT_EOF) {
        blk_read(fat_blk_dev, cluster_to_sector(cluster),
                 boot_sect->bpb.sectors_per_cluster, cluster_buf);

        // Copy relevant portion
        u32 chunk = cluster_size - offset;
        if (chunk > size)
            chunk = size;

        for (u32 i = 0; i < chunk; i++) {
            ((u8 *)buf)[bytes_read + i] = cluster_buf[offset + i];
        }

        bytes_read += chunk;
        size -= chunk;
        offset = 0; // After first cluster, offset is 0

        cluster = fat_read_entry(cluster);
    }

    kfree(cluster_buf, PAGES_FOR(cluster_size));
    return bytes_read;
}

// Stub implementations - TODO
u32 fat_write(struct fat_dir_entry *file, u64 offset, u32 size,
              const void *buf) {
    (void)file;
    (void)offset;
    (void)size;
    (void)buf;
    return -1; // Not implemented
}

u32 fat_truncate(struct fat_dir_entry *file, u32 new_size) {
    (void)file;
    (void)new_size;
    return -1; // Not implemented
}

u32 fat_mkdir(u32 parent_cluster, const char *name) {
    (void)parent_cluster;
    (void)name;
    return -1; // Not implemented
}

u32 fat_rmdir(u32 parent_cluster, const char *name) {
    (void)parent_cluster;
    (void)name;
    return -1; // Not implemented
}

u32 fat_create(u32 parent_cluster, const char *name, u8 attr) {
    (void)parent_cluster;
    (void)name;
    (void)attr;
    return -1; // Not implemented
}

u32 fat_unlink(u32 parent_cluster, const char *name) {
    (void)parent_cluster;
    (void)name;
    return -1; // Not implemented
}

u32 fat_stat(const char *path, struct fat_dir_entry *out) {
    return fat_open(path, out); // Same as open for now
}
