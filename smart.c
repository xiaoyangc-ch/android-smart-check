/* Minimal SMART reader for Android/ARM — SATA via libata (HDIO_DRIVE_CMD)
 * or SG_IO ATA passthrough (SAT). Build:
 *   zig cc -target arm-linux-musleabihf -O2 smart.c -o smart_arm
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/hdreg.h>
#include <dirent.h>

#define SG_IO 0x2285

typedef struct sg_io_hdr {
    int interface_id;
    int dxfer_direction;
    unsigned char cmd_len;
    unsigned char mx_sb_len;
    unsigned short iovec_count;
    unsigned int dxfer_len;
    void *dxferp;
    unsigned char *cmdp;
    void *sbp;
    unsigned int timeout;
    unsigned int flags;
    int pack_id;
    void *usr_ptr;
    unsigned char status;
    unsigned char masked_status;
    unsigned char msg_status;
    unsigned char sb_len_wr;
    unsigned short host_status;
    unsigned short driver_status;
    int resid;
    unsigned int duration;
    unsigned int info;
} sg_io_hdr_t;

#define SG_DXFER_FROM_DEV -3

/* ---- method 1: HDIO_DRIVE_CMD on block device (SATA/libata direct) ---- */
static int smart_hdio(const char *dev, unsigned char feature, unsigned char *buf)
{
    unsigned char args[4 + 512];
    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) { fprintf(stderr, "无法打开块设备 %s: %s\n", dev, strerror(errno)); return -1; }

    memset(args, 0, sizeof(args));
    args[0] = 0xB0;  /* SMART command */
    args[1] = 0x01;  /* lba low  */
    args[2] = feature;
    args[3] = 0x01;  /* sector count */

    if (ioctl(fd, HDIO_DRIVE_CMD, args) < 0) {
        fprintf(stderr, "HDIO读取SMART失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    memcpy(buf, args + 4, 512);
    close(fd);
    return 0;
}

/* ---- method 2/3: SG_IO ATA passthrough on /dev/sg0 ---- */
static int sg_ata_v(const char *dev, unsigned char feature, unsigned char *buf,
                    int variant, int with_tdir, int quiet);

static int sg_ata(const char *dev, unsigned char feature, unsigned char *buf, int use12)
{
    return sg_ata_v(dev, feature, buf, use12, 0, 0);
}

static int sg_ata_v(const char *dev, unsigned char feature, unsigned char *buf,
                    int variant, int with_tdir, int quiet)
{
    unsigned char cdb[16] = {0};
    unsigned char sense[32] = {0};
    sg_io_hdr_t io;
    int cdb_len;

    /* feature == 0xEC 表示 IDENTIFY DEVICE 命令(不带 SMART 签名) */
    int is_identify = (feature == 0xEC);
    unsigned char ata_cmd = is_identify ? 0xEC : 0xB0;
    unsigned char lba_mid = is_identify ? 0x00 : 0x4F;
    unsigned char lba_hi  = is_identify ? 0x00 : 0xC2;

    if (variant == 2) {
        /* SFF-8070i ATA PASS-THROUGH (12) — libata layout */
        cdb[0] = 0xA1;
        cdb[1] = 0x08;      /* protocol 4 = PIO Data-In */
        cdb[2] = 0x00;
        cdb[3] = feature;
        cdb[4] = 0x01;      /* sector count */
        cdb[5] = 0x00;      /* LBA low */
        cdb[6] = lba_mid;
        cdb[7] = lba_hi;
        cdb[8] = 0xA0;      /* device */
        cdb[9] = ata_cmd;
        cdb_len = 12;
    } else if (variant == 3) {
        /* SAT ATA PASS-THROUGH (12) — USB 桥接常用布局 */
        cdb[0] = 0xA1;
        cdb[1] = 0x08;      /* protocol 4 = PIO Data-In */
        cdb[2] = 0xA0;      /* T_LENGTH=2 (sector count), CK_COND */
        cdb[3] = feature;   /* FEATURES(7:0) */
        cdb[4] = 0x00;      /* FEATURES(15:8) */
        cdb[5] = 0x00;      /* FEATURES(23:16) */
        cdb[6] = 0x01;      /* sector count */
        cdb[7] = 0x00;      /* LBA low */
        cdb[8] = lba_mid;
        cdb[9] = lba_hi;
        cdb[10] = 0xA0;     /* device */
        cdb[11] = ata_cmd;
        cdb_len = 12;
    } else {
        /* SAT ATA PASS-THROUGH (16) — no T_DIR (libata rejects it) */
        cdb[0] = 0x85;
        cdb[1] = with_tdir ? 0x88 : 0x08;  /* protocol 4 = PIO Data-In */
        cdb[2] = 0xA0;      /* T_LENGTH=2 (sector count), CK_COND */
        cdb[3] = feature;
        cdb[6] = 0x01;      /* sector count */
        cdb[8] = lba_mid;
        cdb[9] = lba_hi;
        cdb[14] = 0xA0;     /* device */
        cdb[15] = ata_cmd;
        cdb_len = 16;
    }

    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) { fprintf(stderr, "无法打开SG设备 %s: %s\n", dev, strerror(errno)); return -1; }

    memset(&io, 0, sizeof(io));
    memset(buf, 0, 512);
    io.interface_id = 'S';
    io.dxfer_direction = SG_DXFER_FROM_DEV;
    io.cmd_len = cdb_len;
    io.mx_sb_len = sizeof(sense);
    io.dxfer_len = 512;
    io.dxferp = buf;
    io.cmdp = cdb;
    io.sbp = sense;
    io.timeout = 20000;

    if (ioctl(fd, SG_IO, &io) < 0) {
        fprintf(stderr, "SG_IO调用失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    if (io.driver_status & 0x08) {
        if (!quiet) {
            int asc = (io.sb_len_wr >= 13) ? sense[12] : 0;
            int ascq = (io.sb_len_wr >= 14) ? sense[13] : 0;
            fprintf(stderr, "SCSI错误: key=0x%x asc=0x%x ascq=0x%x\n",
                    sense[2] & 0x0f, asc, ascq);
        }
        close(fd);
        return -2;
    }
    if (io.host_status || io.status) {
        fprintf(stderr, "host_status=0x%x status=0x%x\n", io.host_status, io.status);
        close(fd);
        return -3;
    }
    close(fd);
    return 0;
}

/* 通过 sysfs 关联: 找到 blockdev(/dev/block/sdX) 对应的 /dev/sgN, 失败返回 -1。
   /sys/block/sdX/device 与 /sys/class/scsi_generic/sgN/device 规范化后指向同一个
   scsi_device 目录, 据此建立块设备与 SG 设备的一一对应, 避免多盘数据张冠李戴 */
static int find_sg_for_block(const char *blockdev)
{
    char linkpath[64], resolved_block[512], resolved_sg[512];
    const char *base;
    DIR *d;
    struct dirent *e;

    base = strrchr(blockdev, '/');
    base = base ? base + 1 : blockdev;
    snprintf(linkpath, sizeof(linkpath), "/sys/block/%s/device", base);
    if (!realpath(linkpath, resolved_block)) return -1;

    d = opendir("/sys/class/scsi_generic");
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (strncmp(e->d_name, "sg", 2) != 0) continue;
        snprintf(linkpath, sizeof(linkpath), "/sys/class/scsi_generic/%s/device", e->d_name);
        if (!realpath(linkpath, resolved_sg)) continue;
        if (strcmp(resolved_block, resolved_sg) != 0) continue;
        {
            int idx = atoi(e->d_name + 2);
            closedir(d);
            return idx;
        }
    }
    closedir(d);
    return -1;
}

/* 通过 SG 设备发 ATA IDENTIFY DEVICE (0xEC), 解析到 hd_driveid。
   优先发往 sysfs 关联的 sgN; 关联失败回退旧的 sg0..sg15 扫描。
   注意: 型号字符串保持原始字节序返回, 字节交换统一在打印处做 */
static int sg_identify(const char *blockdev, struct hd_driveid *hid)
{
    char sgpath[32];
    unsigned char buf[512];
    int n, first, v;

    first = find_sg_for_block(blockdev);
    for (n = -1; n < 16; n++) {
        if (n == first) continue; /* first=-1 时自动跳过关联轮 */
        if (n < 0) snprintf(sgpath, sizeof(sgpath), "/dev/sg%d", first);
        else snprintf(sgpath, sizeof(sgpath), "/dev/sg%d", n);
        for (v = 0; v < 4; v++) {
            /* 复用 sg_ata_v 发 IDENTIFY(0xEC), 试 4 种 CDB 变体 */
            if (sg_ata_v(sgpath, 0xEC, buf, v, 0, 1) != 0) continue;
            /* 有效 IDENTIFY 数据: 型号区域非零 */
            if (buf[54] == 0 && buf[55] == 0) continue;
            if (hid) memcpy(hid, buf, sizeof(*hid));
            return 0;
        }
    }
    return -1;
}

/* 通过 SG 设备读取 SMART(USB 桥接盘): 优先 sysfs 关联的 sgN,
   关联失败回退扫描 sg0..sg15, 每个设备尝试 4 种 CDB 变体 */
static int read_smart_sg(const char *blockdev, unsigned char *data, unsigned char *thresh)
{
    char sgpath[32];
    int n, first, r;

    first = find_sg_for_block(blockdev);
    for (n = -1; n < 16; n++) {
        if (n == first) continue;
        if (n < 0) snprintf(sgpath, sizeof(sgpath), "/dev/sg%d", first);
        else snprintf(sgpath, sizeof(sgpath), "/dev/sg%d", n);
        r = sg_ata_v(sgpath, 0xD0, data, 0, 0, 1);
        if (r != 0) r = sg_ata_v(sgpath, 0xD0, data, 0, 1, 1);
        if (r != 0) r = sg_ata_v(sgpath, 0xD0, data, 2, 0, 1);
        if (r != 0) r = sg_ata_v(sgpath, 0xD0, data, 3, 0, 1);
        if (r == 0) {
            sg_ata_v(sgpath, 0xD1, thresh, 3, 0, 1);
            return 0;
        }
    }
    return -1;
}

static int read_smart(const char *blockdev, unsigned char *data, unsigned char *thresh)
{
    int r;

    /* try HDIO_DRIVE_CMD first (native SATA path) */
    r = smart_hdio(blockdev, 0xD0, data);
    if (r == 0) {
        /* 阈值读取失败必须清零, 避免栈上垃圾值参与健康判定 */
        if (smart_hdio(blockdev, 0xD1, thresh) != 0) memset(thresh, 0, 512);
        return 0;
    }
    fprintf(stderr, "块设备直读失败, 尝试SG接口...\n");
    return read_smart_sg(blockdev, data, thresh);
}

static const char *attr_name(int id)
{
    switch (id) {
    case 1:  return "底层数据读取错误率";
    case 2:  return "吞吐性能";
    case 3:  return "主轴起转时间";
    case 4:  return "启停次数";
    case 5:  return "重映射扇区数";
    case 7:  return "寻道错误率";
    case 8:  return "寻道时间性能";
    case 9:  return "通电时间(小时)";
    case 10: return "起转重试次数";
    case 11: return "校准重试次数";
    case 12: return "电源循环次数";
    case 184: return "端到端错误";
    case 187: return "报告不可纠正错误";
    case 188: return "命令超时";
    case 189: return "磁头飞行高度异常";
    case 190: return "气流温度";
    case 191: return "冲击感应错误率";
    case 192: return "断电磁头缩回次数";
    case 193: return "磁头加载循环次数";
    case 194: return "硬盘温度";
    case 195: return "硬件ECC恢复次数";
    case 196: return "重映射事件次数";
    case 197: return "当前待映射扇区";
    case 198: return "离线不可纠正扇区";
    case 199: return "数据线CRC错误次数";
    case 200: return "多区域错误率";
    case 240: return "磁头飞行时间(小时)";
    case 241: return "累计写入量(LBA)";
    case 242: return "累计读取量(LBA)";
    default:  return "未知属性";
    }
}

/* swap bytes of a string word-wise (ATA string convention) */
static void ata_string(unsigned char *dst, const unsigned char *src, int words)
{
    int i;
    for (i = 0; i < words; i++) {
        dst[i * 2] = src[i * 2 + 1];
        dst[i * 2 + 1] = src[i * 2];
    }
    dst[words * 2] = 0;
}

static void trim_ascii(char *s)
{
    int start = 0;
    int end = (int)strlen(s);
    while (end > 0 && (s[end - 1] == ' ' || (unsigned char)s[end - 1] < 32)) end--;
    while (start < end && (s[start] == ' ' || (unsigned char)s[start] < 32)) start++;
    memmove(s, s + start, end - start);
    s[end - start] = 0;
}

/* 检测单块盘并输出报告段, 返回 0=成功 1=跳过 2=有异常 */
static int check_one_disk(const char *blockdev, int *disk_idx)
{
    unsigned char data[512], thresh[512];
    int i, n = 0, fail = 0;
    memset(data, 0, sizeof(data));
    memset(thresh, 0, sizeof(thresh));

    /* IDENTIFY DEVICE via HDIO_GET_IDENTITY */
    {
        int fd = open(blockdev, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            fprintf(stderr, "无法打开设备 %s: %s\n", blockdev, strerror(errno));
            return 1;
        }
        {
            struct hd_driveid hid;
            memset(&hid, 0, sizeof(hid));
            if (ioctl(fd, HDIO_GET_IDENTITY, &hid) != 0) {
                /* USB 桥接盘不支持 HDIO, 走 SG_IO IDENTIFY */
                if (sg_identify(blockdev, &hid) != 0) {
                    fprintf(stderr, "设备 %s 不是ATA/SATA硬盘或无法读取: %s\n",
                            blockdev, strerror(errno));
                    close(fd);
                    return 1;
                }
            }
            {
                char s[64], model[41];
                unsigned long long cap = (unsigned long long)hid.lba_capacity_2;
                /* 型号与序列号/固件统一在此处做 ATA 字节交换(两条读取路径一致) */
                ata_string((unsigned char *)model, (unsigned char *)hid.model, 20);
                trim_ascii(model);
                ata_string((unsigned char *)s, (unsigned char *)hid.serial_no, 10);
                trim_ascii(s);
                (*disk_idx)++;
                printf("========== 硬盘 %d ==========\n", *disk_idx);
                printf("设备路径: %s\n", blockdev);
                printf("硬盘型号: %s\n", model);
                printf("序列号:   %s\n", s);
                ata_string((unsigned char *)s, (unsigned char *)hid.fw_rev, 4);
                trim_ascii(s);
                printf("固件版本: %s\n", s);
                printf("LBA48扇区: %llu  容量 %.2f GiB (%.3f TB)\n",
                       cap,
                       (double)cap * 512.0 / 1073741824.0,
                       (double)cap * 512.0 / 1000000000000.0);
            }
            close(fd);
        }
    }

    if (read_smart(blockdev, data, thresh) != 0) {
        fprintf(stderr, "无法读取SMART数据, 设备: %s\n", blockdev);
        return 1;
    }

    printf("SMART 数据结构版本: %u\n",
           (unsigned)(data[0] | (data[1] << 8)));

    printf("----- SMART 健康属性 -----\n");
    for (i = 0; i < 30; i++) {
        unsigned char *a = data + 2 + i * 12;
        int id, cur, worst, thr, j;
        uint64_t raw;
        char raw_s[32];

        if (a[0] == 0) continue;
        id = a[0];
        cur = a[3];
        worst = a[4];
        /* 阈值表按属性 ID 配对(不依赖表项槽位顺序), 找不到按 0 处理 */
        thr = 0;
        for (j = 0; j < 30; j++) {
            if (thresh[2 + j * 12] == id) {
                thr = thresh[2 + j * 12 + 1];
                break;
            }
        }
        raw = (uint64_t)a[5] | ((uint64_t)a[6] << 8) | ((uint64_t)a[7] << 16)
            | ((uint64_t)a[8] << 24) | ((uint64_t)a[9] << 32) | ((uint64_t)a[10] << 40);

        if (id == 190 || id == 194) {
            int lo = (int)(raw & 0xFF);
            snprintf(raw_s, sizeof(raw_s), "%llu (%d°C)",
                     (unsigned long long)raw, lo);
        } else if (id == 9 && raw > 0xFFFFFFFFULL) {
            /* 希捷DM系列 ID9 raw = (通电小时 << 32) | 低32位杂项, 取高32位 */
            snprintf(raw_s, sizeof(raw_s), "%llu",
                     (unsigned long long)(raw >> 32));
        } else {
            snprintf(raw_s, sizeof(raw_s), "%llu", (unsigned long long)raw);
        }

        const char *st = "正常";
        if (thr && cur <= thr) {
            st = (a[1] & 0x01) ? "故障" : "警告";
            fail++;
        } else if ((id == 197 || id == 198) && raw > 0) {
            /* 待映射/离线不可纠正扇区: 有记录即为坏道 */
            st = "故障";
            fail++;
        } else if ((id == 5 || id == 187) && raw > 0) {
            /* 重映射/不可纠正错误有历史记录 */
            st = "警告";
            fail++;
        } else if (id == 188 && raw > 100) {
            /* 命令超时大量累积: 电路/磁头问题 */
            st = "警告";
            fail++;
        } else if (id == 199 && raw > 1000) {
            /* CRC错误大量累积: 接口/线缆/电路问题 */
            st = "警告";
            fail++;
        } else if (id == 188 && raw > 0) {
            st = "注意";
        } else if (id == 199 && raw > 0) {
            st = "注意";
        } else if ((id == 190 || id == 194) && (int)(raw & 0xFF) > 45) {
            st = "注意";
        } else if (a[1] & 0x01) {
            st = "预警";
        }
        printf("  ID %-3d %-20s 当前%-3d 最差%-3d 阈值%-3d 原始值%-14s [%s]\n",
               id, attr_name(id), cur, worst, thr, raw_s, st);
        n++;
    }
    printf("\n共解析 %d 项属性。\n", n);
    if (fail > 0) {
        printf("综合结论: *** 硬盘存在异常 *** (%d 项异常，建议备份数据并尽快更换)\n", fail);
        return 2;
    }
    printf("综合结论: 硬盘健康 (所有属性正常)\n");
    printf("  (注意: 标记为[注意]的属性如CRC错误, 建议检查数据线/接口)\n\n");
    return 0;
}

int main(int argc, char **argv)
{
    int total = 0, unhealthy = 0, checked = 0;
    int i, miss = 0;

    if (argc > 1) {
        /* 指定设备模式: smart_arm /dev/block/sdX */
        printf("\xEF\xBB\xBF"); /* UTF-8 BOM, 与扫描模式编码一致 */
        int r = check_one_disk(argv[1], &checked);
        return (r == 2) ? 2 : (r == 0 ? 0 : 1);
    }

    /* 自动扫描模式: 遍历 /dev/block/sd[a-z] */
    printf("\xEF\xBB\xBF"); /* UTF-8 BOM, 报告文件记事本兼容 */
    printf("正在扫描设备上的硬盘...\n\n");
    for (i = 0; i < 26; i++) {
        char path[32];
        int r;
        snprintf(path, sizeof(path), "/dev/block/sd%c", 'a' + i);
        if (access(path, F_OK) != 0) {
            /* sd 字母允许缺号(热插拔残留), 连续 3 个不存在视为扫描结束 */
            if (++miss >= 3) break;
            continue;
        }
        miss = 0;
        r = check_one_disk(path, &checked);
        if (r == 0) total++;
        if (r == 2) { total++; unhealthy++; }
    }

    {
        /* 统计 SATA 端口数, 有端口没识别到盘时提示 */
        int ports = 0;
        DIR *d = opendir("/sys/class/ata_link");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (e->d_name[0] == 'l') ports++;
            }
            closedir(d);
        }
        if (ports > total) {
            printf("提示: 检测到 %d 个SATA端口, 但只有 %d 块盘被识别, %d 个端口无盘或盘已损坏不识别\n",
                   ports, total, ports - total);
        }
    }
    printf("================================================\n");
    printf("扫描完成: 共发现 %d 块硬盘\n", total);
    if (unhealthy > 0) {
        printf("总体结论: *** 有 %d 块硬盘存在异常 ***\n", unhealthy);
        return 2;
    }
    if (total > 0) printf("总体结论: 所有硬盘健康\n");
    else printf("总体结论: 未发现可检测的硬盘\n");
    return unhealthy > 0 ? 2 : 0;
}
