#ifndef __SFC_NORFLASH_API_H__
#define __SFC_NORFLASH_API_H__

#include "typedef.h"
#include "asm/cpu.h"
#include "device/device.h"

int norflash_init(const struct dev_node *node, void *arg);
int norflash_open(const char *name, struct device **device, void *arg);
int norflash_read(struct device *device, void *buf, u32 len, u32 offset);
int norflash_origin_read(u8 *buf, u32 offset, u32 len);
int norflash_write(struct device *device, void *buf, u32 len, u32 offset);
int norflash_erase_zone(struct device *device, u32 addr, u32 len);
int norflash_ioctl(struct device *device, u32 cmd, u32 arg);
int norflash_write_protect(u32 cmd);
u32 flash_addr2cpu_addr(u32 offset);
u32 cpu_addr2flash_addr(u32 offset);
u8 *get_norflash_uuid(void);
void norflash_set_spi_con(u32 arg);
int norflash_protect_resume(void);
int norflash_protect_suspend(void);
void norflash_enter_spi_code(void);
void norflash_exit_spi_code(void);
void norflash_spi_cs(char cs);
void norflash_spi_write_byte(unsigned char data);
u8 norflash_spi_read_byte(void);
int norflash_wait_busy(void);
int norflash_read_otp_general(u8 cmd, u32 addr, u8 *buf, u32 len);
void MEMCPY_FLASH(void *buf, void *read_addr, u32 size);

/*
 * 用于flash otp擦除
 */
int norflash_eraser_otp(void);

/*
 * 用于flash otp写
 * 参数buf 写入数据buffer
 * 参数len 写入长度，必须以zone_len字节对齐
 * 参数zone_len 为otp单次写入长度
 * 返回值小于0 写入失败, 等于0 写入成功
 */
int norflash_write_otp(u8 *buf, const int len, const int zone_len);

/*
 * 用于flash otp读
 * 参数buf 读取数据buffer
 * 参数len 读取长度
 * 参数zone_len 为otp单次读取长度
 * 返回值小于0 读取数据失败, 等于0 读取数据成功
 */
int norflash_read_otp(u8 *buf, const int len, const int zone_len);

#endif
