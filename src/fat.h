#ifndef FAT_H
#define FAT_H
#include "types.h"

// FAT boot sector signatures
#define FAT_BOOT_SIGNATURE     0xAA55
#define FAT_EBR_SIGNATURE_28   0x28
#define FAT_EBR_SIGNATURE_29   0x29

// FSInfo signatures
#define FAT_FSINFO_LEAD_SIG    0x41615252
#define FAT_FSINFO_STRUCT_SIG  0x61417272
#define FAT_FSINFO_TRAIL_SIG 0xAA550000

// FAT table values
#define FAT_FREE 0x00000000
#define FAT_EOF 0x0FFFFFF8
#define FAT_BAD 0x0FFFFFF7

struct bpb {
    u8 _[3];
    u8 OEM[8];
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 num_fats;
    u16 root_dir_entries;
    u16 total_sectors;
    u8 media_descriptor_type;
    u16 sectors_per_fat16;
    u16 sectors_per_track;
    u16 num_heads;
    u32 hidden_sectors;
    u32 large_sectors;
} __attribute__((packed));

/* NOTE: UNUSED */
struct ebr16 {
    u8 _;
};

struct ebr32 {
    u32 sectors_per_fat;
    u16 flags;
    u16 fat_version;
    u32 root_dir_cluster;
    u16 fsinfo_sector;
    u16 bkp_boot_sector;
    u8 _[12];
    u8 drive_number;
    u8 NTflags;
    u8 signature; // 0x28 or 0x29
    u32 volume_id;
    u8 label[11];
    u8 sys_id_str[8]; // always "FAT32  "
    u8 boot_code[420];
    u16 boot_signature; // 0xAA55
} __attribute__((packed));

struct boot_sector {
    struct bpb bpb;
  struct ebr32 ebr;
} __attribute__((packed));

struct FSInfo {
    u32 lead_signature; // 0x41615252
    u8 _[480];
    u32 signature; // 0x61417272
    u32 last_known_free_cluster;
    u32 free_clusters_start;
    u8 __[12];
    u32 trail_signature; // 0xAA550000
} __attribute__((packed));

// FAT directory entry attributes
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

struct fat_dir_entry {
    u8 file_name[11];
    u8 attributes;
    u8 NTflags;
    u8 creation_time_ms;
    u16 creation_time;
    u16 creation_date;
    u16 last_access_date;
    u16 first_cluster_high;
    u16 last_modification_time;
    u16 last_modification_date;
    u16 first_cluster_low;
    u32 file_size;
} __attribute__((packed));

struct lfn_extension {
    u8 entry_order;
    u8 name0[10];
    u8 attribute; // always 0x0F
    u8 entry_type;
    u8 checksum;
    u8 name1[12];
    u16 _;
    u8 name2[4];
} __attribute__((packed));

void fat_init(void);
void fat_mark_dirty(void);
void fat_flush(void); // Write FAT table(s) back to disk
void fat_flush_dirty(
    void); // Write FAT table(s) back to disk only if dirty flag set

u32 fat_read_entry(u32 cluster);
void fat_write_entry(u32 cluster, u32 val);
u32 fat_alloc_cluster(void);
u32 fat_extend_chain(u32 last_cluster);
void fat_free_chain(u32 start);

u32 cluster_to_sector(u32 cluster);

u32 fat_readdir(u32 dir_cluster, struct fat_dir_entry *out, u32 index);
u32 fat_findentry(u32 dir_cluster, const char *name, struct fat_dir_entry *out);
u32 fat_mkdir(u32 parent_cluster, const char *name);
u32 fat_rmdir(u32 parent_cluster, const char *name);
u32 fat_create(u32 parent_cluster, const char *name, u8 attr);
u32 fat_unlink(u32 parent_cluster, const char *name);

u32 fat_read(struct fat_dir_entry *file, u64 offset, u32 size, void *buf);
u32 fat_write(struct fat_dir_entry *file, u64 offset, u32 size,
              const void *buf);
u32 fat_truncate(struct fat_dir_entry *file, u32 new_size);

u32 fat_open(const char *path, struct fat_dir_entry *out);
u32 fat_stat(const char *path, struct fat_dir_entry *out);

#endif
