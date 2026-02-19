#include "fat.h"
#include "ahci.h"
#include "mem.h"
#include "panic.h"
#include "print.h"

u8 *fat_table;
u32 fat_pages;
u32 fat_sector;
struct FSInfo *fs_info;
struct boot_sector *boot_sect;
static u8 fat_dirty = 0;

// Round up to number of pages needed for n bytes
#define PAGES_FOR(n) (((n) + PAGE_SIZE - 1) / PAGE_SIZE)

void fat_init(void) {
    boot_sect = kalloc(1);
    if (!boot_sect)
        panic("OUT OF MEMORY", 0);
    ahci_read(ahci_sata_port, 0, 1, boot_sect);

    fs_info = kalloc(1); // FSInfo is one sector, fits in one page
    if (!fs_info)
        panic("OUT OF MEMORY", 0);
    ahci_read(ahci_sata_port, boot_sect->ebr.fsinfo_sector, 1, fs_info);

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
    fat_sector = boot_sect->bpb.reserved_sectors;
    ahci_read(ahci_sata_port, fat_sector, boot_sect->ebr.sectors_per_fat,
              fat_table);
}

inline void fat_mark_dirty(void) { fat_dirty = 1; }

void fat_flush(void) {
    for (u8 i = 0; i < boot_sect->bpb.num_fats; i++) {
        u64 sector = fat_sector + (i * boot_sect->ebr.sectors_per_fat);
        ahci_write(ahci_sata_port, sector, boot_sect->ebr.sectors_per_fat,
                   fat_table);
    }
    u64 sector = boot_sect->ebr.fsinfo_sector;
    ahci_write(ahci_sata_port, sector, 1, fs_info);
}

void fat_flush_dirty(void) {
    if (!fat_dirty)
        return;
    fat_flush();
    fat_dirty = 0;
}

u32 cluster_to_sector(u32 cluster) {
    u32 data_start = boot_sect->bpb.reserved_sectors
                   + (boot_sect->bpb.num_fats * boot_sect->ebr.sectors_per_fat);
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

    u8 *buf = kalloc(1);
    if (!buf)
        return -1;

    ahci_read(ahci_sata_port, cluster_to_sector(cluster),
              boot_sect->bpb.sectors_per_cluster, buf);
    struct fat_dir_entry *entries = (struct fat_dir_entry *)buf;
    if (entries[index].file_name[0] == 0x00) {
        kfree(buf, 1);
	return -1;
    }

    *out = entries[index];
    kfree(buf, 1);
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
    for (int k = 0; k < 11; k++) padded[k] = ' ';

    // Copy name part (up to 8 chars, stop at dot)
    while (name[i] && name[i] != '.' && j < 8) {
        padded[j++] = (name[i] >= 'a' && name[i] <= 'z')
                      ? name[i] - 32  // Uppercase
                      : name[i];
        i++;
    }

    // Skip to extension
    if (name[i] == '.') i++;

    // Copy extension (up to 3 chars)
    j = 8;
    while (name[i] && j < 11) {
        padded[j++] = (name[i] >= 'a' && name[i] <= 'z')
                      ? name[i] - 32
                      : name[i];
        i++;
    }

    // Compare
    for (int k = 0; k < 11; k++) {
        if (padded[k] != fat_name[k]) return 0;
    }
    return 1;
}

u32 fat_findentry(u32 dir_cluster, const char *name,
                  struct fat_dir_entry *out) {
    struct fat_dir_entry entry;

    for (u32 i = 0; fat_readdir(dir_cluster, &entry, i) == 0; i++) {
        if (entry.file_name[0] == 0xE5) continue;  // Deleted
        if (entry.attributes == FAT_ATTR_LFN) continue;  // LFN entry

        if (fat_name_match(entry.file_name, name)) {
            *out = entry;
            return 0;
        }
    }
    return -1;  // Not found
}

u32 fat_open(const char *path, struct fat_dir_entry *out) {
    u32 cluster = boot_sect->ebr.root_dir_cluster;
    struct fat_dir_entry entry;

    // Skip leading slash
    if (*path == '/') path++;

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
        if (*path == '/') path++;

        // Find in current directory
        if (fat_findentry(cluster, component, &entry) != 0) {
            return -1;  // Not found
        }

        // If more path remains, this must be a directory
        if (*path && !(entry.attributes & FAT_ATTR_DIRECTORY)) {
            return -1;  // Not a directory
        }

        // Descend into this entry
        cluster = entry_cluster(&entry);
    }

    *out = entry;
    return 0;
}

u32 fat_read(struct fat_dir_entry *file, u64 offset, u32 size, void *buf) {
    if (offset >= file->file_size) return 0;
    if (offset + size > file->file_size)
        size = file->file_size - offset;

    u32 cluster_size = boot_sect->bpb.sectors_per_cluster
                     * boot_sect->bpb.bytes_per_sector;
    u32 cluster = entry_cluster(file);
    u32 bytes_read = 0;

    // Skip to starting cluster
    while (offset >= cluster_size) {
        cluster = fat_read_entry(cluster);
        if (cluster >= FAT_EOF) return bytes_read;
        offset -= cluster_size;
    }

    // Read clusters
    u8 *cluster_buf = kalloc(PAGES_FOR(cluster_size));
    if (!cluster_buf) return -1;

    while (size > 0 && cluster < FAT_EOF) {
        ahci_read(ahci_sata_port, cluster_to_sector(cluster),
                  boot_sect->bpb.sectors_per_cluster, cluster_buf);

        // Copy relevant portion
        u32 chunk = cluster_size - offset;
        if (chunk > size) chunk = size;

        for (u32 i = 0; i < chunk; i++) {
            ((u8 *)buf)[bytes_read + i] = cluster_buf[offset + i];
        }

        bytes_read += chunk;
        size -= chunk;
        offset = 0;  // After first cluster, offset is 0

        cluster = fat_read_entry(cluster);
    }

    kfree(cluster_buf, PAGES_FOR(cluster_size));
    return bytes_read;
}

// Stub implementations - TODO
u32 fat_write(struct fat_dir_entry *file, u64 offset, u32 size,
              const void *buf) {
    (void)file; (void)offset; (void)size; (void)buf;
    return -1;  // Not implemented
}

u32 fat_truncate(struct fat_dir_entry *file, u32 new_size) {
    (void)file; (void)new_size;
    return -1;  // Not implemented
}

u32 fat_mkdir(u32 parent_cluster, const char *name) {
    (void)parent_cluster; (void)name;
    return -1;  // Not implemented
}

u32 fat_rmdir(u32 parent_cluster, const char *name) {
    (void)parent_cluster; (void)name;
    return -1;  // Not implemented
}

u32 fat_create(u32 parent_cluster, const char *name, u8 attr) {
    (void)parent_cluster; (void)name; (void)attr;
    return -1;  // Not implemented
}

u32 fat_unlink(u32 parent_cluster, const char *name) {
    (void)parent_cluster; (void)name;
    return -1;  // Not implemented
}

u32 fat_stat(const char *path, struct fat_dir_entry *out) {
    return fat_open(path, out);  // Same as open for now
}
