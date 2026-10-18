#ifndef __SPI_H__
#define __SPI_H__


#include "typedef.h"
#include "generic/ioctl.h"

#define SPI_MAX_SIZE	(0x7FFFFFFF - 1)//(2^31 - 1)//由于该宏可能赋值给有符合变量，因此使用2^31 - 1

/*enum spi_mode {
    SPI_2WIRE_MODE,
    SPI_STD_MODE,
    SPI_DUAL_MODE,
    SPI_QUAD_MODE,
};*/

#define SPI_MODE_SLAVE   	 	BIT(0)

#define SPI_UPDATE_SAMPLE_SAME 	BIT(1)
#define SPI_SCLK_H_UPH_SMPL	 	BIT(2)//CLK空闲H,上升沿更新数据，下降沿采样数据
#define SPI_SCLK_H_UPL_SMPH	 	BIT(3)//CLK空闲H,下降沿更新数据，上升沿采样数据
#define SPI_SCLK_H_UPH_SMPH	 	BIT(4)//CLK空闲H,上升沿更新数据，上升沿采样数据
#define SPI_SCLK_H_UPL_SMPL	 	BIT(5)//CLK空闲H,下降沿更新数据，下降沿采样数据
#define SPI_SCLK_L_UPH_SMPL	 	BIT(6)//CLK空闲L,上升沿更新数据，下降沿采样数据
#define SPI_SCLK_L_UPL_SMPH	 	BIT(7)//CLK空闲L,下降沿更新数据，上升沿采样数据
#define SPI_SCLK_L_UPH_SMPH	 	BIT(8)//CLK空闲L,上升沿更新数据，上升沿采样数据
#define SPI_SCLK_L_UPL_SMPL	 	BIT(9)//CLK空闲L,下降沿更新数据，下降沿采样数据

#define SPI_UNIDIR_MODE			BIT(10) //单向：只用SPI_DO一根线数据传输（一般只用于作为从机接收数据）
#define SPI_BIDIR_MODE			BIT(11) //双向：用SPI_DO/DI两根线数据传输
#define SPI_SCLK_PORT_HD		BIT(13) //SPI CLK时钟开启强电流强驱

#define IOCTL_SPI_SET_CS                    _IOW('S', 1, u8)
#define IOCTL_SPI_SEND_BYTE                 _IOW('S', 2, u8)
#define IOCTL_SPI_SEND_CMD_2BIT_MODE        _IOW('S', 3, u32)
#define IOCTL_SPI_GET_BIT_MODE              _IOW('S', 4, u32)
#define IOCTL_SPI_READ_BYTE                 _IOW('S', 5, u8)
#define IOCTL_SPI_SET_CRC                   _IOW('S', 6, u16)
#define IOCTL_SPI_READ_CRC                  _IOR('S', 7, u16)

#define IOCTL_SFC_SEND_ADDR                 _IOW('S', 8, u32)
#define IOCTL_SFC_SWITCH_TO_SPI             _IOW('S', 9, u32)
#define IOCTL_SFC_DATA_UPDATE               _IOW('S', 10, u32)
#define IOCTL_SFC_DATA_ERASE                _IOW('S', 11, u32)

#define IOCTL_SPI_SET_IRQ_CPU_ID	        _IOW('S', 12, u32)
#define IOCTL_SPI_SET_USER_INFO	  	        _IOW('S', 13, u32)
#define IOCTL_SPI_READ_DATA       	        _IOR('S', 13, u32)
#define IOCTL_SPI_FREE_DATA       	        _IOW('S', 14, u32)
#define IOCTL_SPI_SET_CB         	        _IOW('S', 15, u32)
#define IOCTL_SPI_SET_USE_SEM     	        _IOW('S', 16, u32)
#define IOCTL_SPI_SET_IRQ_FUNC 		        _IOW('S', 17, u32)

#define IOCTL_SPI_SET_BIT_MODE              _IOW('S', 18, u32)
//SPI同一组多ABC口同时使用时切换口命令，使用示例：ioctl(spi_hdl, IOCTL_SPI_SET_PORT, 0);
#define IOCTL_SPI_SET_PORT      	        _IOW('S', 19, u32)
//非阻塞方式发包
#define IOCTL_SPI_NON_BLOCK      	        _IOW('S', 20, u32)
//休眠前需要调用这个当使用非阻塞方式发包时候
#define IOCTL_SPI_WRITE_NON_BLOCK_FLUSH    	_IOW('S', 21, u32)
//获取spi配置是否阻塞发送
#define IOCTL_SPI_GET_NON_BLOCK             _IOR('S', 22, u32)
#define IOCTL_SPI_SET_THRESHOLD_SEM_WAIT    _IOW('S', 23, u32)
#define IOCTL_SPI_READ_PACKAGE_DUAL_MODE    _IOR('S', 24, u32)

struct spi_io {
    u8 cs_pin;
    u8 di_pin;
    u8 do_pin;
    u8 clk_pin;
    u8 d2_pin;
    u8 d3_pin;
};

struct spi_2bit_data {
    u32 len;
    u8 *buf;
};

struct spi_user {
    u8 *buf;
    u32 buf_size;
    u32 dma_max_cnt;
    u32 first_dma_size;
    u32 block_size;
};

struct spi_cb {
    int (*cb_func)(void *priv, u8 **data, u32 *len);
    void *cb_priv;
};

struct spi_block {
    volatile u8 *buf;
    volatile u32 buf_size;
    volatile u32 block_num;
    volatile u32 dma_max_cnt;
    volatile u32 first_dma_size;
    volatile u32 block_size;
    volatile u32 dma_size;
    volatile u32 wblock;
    volatile u32 rblock;
    volatile u32 free_size;
    volatile u8 start;
};

struct spi_regs {
    volatile u32 con;
    volatile u8  baud;
    volatile u8  reg1[3];
    volatile u8  buf;
    volatile u8  reg2[3];
    volatile u32 adr;
    volatile u32 cnt;
};

struct sfc_regs {
    volatile u32 con;
    volatile u16 baud;
    volatile u32 reserved0;
    volatile u32 base_addr;
    volatile u32 reserved1;
    volatile u32 econ;
};

struct spi_platform_data {
    u8 port;
    u8 mode;
    u8 irq;
    u32 attr;
    u32 clk;
    const struct spi_io *io;
    volatile struct spi_regs *reg;
    void (*init)(const struct spi_platform_data *);
};

struct sfc_spi_platform_data {
    u8 mode;
    u8 ro_mode;
    u32 clk;
    const struct spi_io *io;
    volatile struct spi_regs *spi_reg;
    volatile struct sfc_regs *sfc_reg;
};

struct sfc_spi_data {
    u32 addr;
    u32 len;
};

#define SPI0_PLATFORM_DATA_BEGIN(spi0_data) \
    static const struct spi_io spi0_data##_io[] = { \
        { \
            .cs_pin     = IO_PORTD_00, \
            .di_pin     = IO_PORTD_01, \
            .do_pin     = IO_PORTD_05, \
            .clk_pin    = IO_PORTD_04, \
            .d2_pin     = IO_PORTD_02, \
            .d3_pin     = IO_PORTD_03, \
        }, \
        { \
            .cs_pin     = IO_PORTC_05, \
            .di_pin     = IO_PORTC_00, \
            .do_pin     = IO_PORTC_04, \
            .clk_pin    = IO_PORTC_03, \
            .d2_pin     = IO_PORTC_01, \
            .d3_pin     = IO_PORTC_02, \
        }, \
    }; \
    static void spi0_data##_iomc_init(const struct spi_platform_data *pd) \
    { \
        JL_IOMAP->CON1 &= ~BIT(1); \
        if (pd->port == 'A') { \
           JL_IOMAP->CON0 &= ~BIT(5); \
        } else { \
           JL_IOMAP->CON0 |= BIT(5); \
        } \
    }\
    static const struct spi_platform_data spi0_data = { \
        .irq = IRQ_SPI0_IDX, \
        .io     = spi0_data##_io, \
    	.init   = spi0_data##_iomc_init, \


#define SPI0_PLATFORM_DATA_END() \
    .reg    = (volatile struct spi_regs *)&JL_SPI0->CON, \
};


#define SFC_SPI_PLATFORM_DATA_BEGIN(sfc_spi0_data) \
    static const struct spi_io sfc_spi0_io = { \
        .cs_pin     = IO_PORTD_00, \
        .di_pin     = IO_PORTD_01, \
        .do_pin     = IO_PORTD_05, \
        .clk_pin    = IO_PORTD_04, \
        .d2_pin     = IO_PORTD_02, \
        .d3_pin     = IO_PORTD_03, \
    }; \
    static const struct sfc_spi_platform_data sfc_spi0_data = { \


#define SFC_SPI_PLATFORM_DATA_END() \
    .io         =  &sfc_spi0_io, \
    .spi_reg    = (volatile struct spi_regs *)&JL_SPI0->CON, \
    .sfc_reg    = (volatile struct sfc_regs *)&JL_SFC->CON, \
};





#define SPI1_PLATFORM_DATA_BEGIN(spi1_data) \
    static const struct spi_io spi1_data##_io[] = { \
        { \
            .cs_pin     = -1, \
            .di_pin     = IO_PORTB_05, \
            .do_pin     = IO_PORTB_07, \
            .clk_pin    = IO_PORTB_06, \
            .d2_pin     = -1, \
            .d3_pin     = -1, \
        }, \
        { \
            .cs_pin     = -1, \
            .di_pin     = IO_PORTC_08, \
            .do_pin     = IO_PORTC_10, \
            .clk_pin    = IO_PORTC_09, \
            .d2_pin     = -1, \
            .d3_pin     = -1, \
        }, \
    }; \
    static void spi1_data##_iomc_init(const struct spi_platform_data *pd) \
    { \
        if (pd->port == 'A') { \
           JL_IOMAP->CON1 &= ~BIT(4); \
        } else { \
           JL_IOMAP->CON1 |= BIT(4); \
        } \
    }\
    static const struct spi_platform_data spi1_data = { \
        .irq = IRQ_SPI1_IDX, \
        .io     = spi1_data##_io, \
    	.init   = spi1_data##_iomc_init, \


#define SPI1_PLATFORM_DATA_END() \
    .reg    = (volatile struct spi_regs *)&JL_SPI1->CON, \
};



#define SPI2_PLATFORM_DATA_BEGIN(spi2_data) \
    static const struct spi_io spi2_data##_io[] = { \
        { \
            .cs_pin     = -1, \
            .di_pin     = IO_PORTH_02, \
            .do_pin     = IO_PORTH_01, \
            .clk_pin    = IO_PORTH_00, \
            .d2_pin     = -1, \
            .d3_pin     = -1, \
        }, \
        { \
            .cs_pin     = -1, \
            .di_pin     = IO_PORTG_15, \
            .do_pin     = IO_PORT_USB_DMA, \
            .clk_pin    = IO_PORT_USB_DPA, \
            .d2_pin     = -1, \
            .d3_pin     = -1, \
        }, \
        { \
            .cs_pin     = -1, \
            .di_pin     = IO_PORTA_02, \
            .do_pin     = IO_PORTA_04, \
            .clk_pin    = IO_PORTA_03, \
            .d2_pin     = -1, \
            .d3_pin     = -1, \
        }, \
    }; \
    static void spi2_data##_iomc_init(const struct spi_platform_data *pd) \
    { \
        JL_IOMAP->CON1 &= ~(BIT(16) | BIT(17)); \
        if (pd->port == 'B') { \
           JL_IOMAP->CON1 |= BIT(16); \
        } else if (pd->port == 'C') { \
           JL_IOMAP->CON1 |= BIT(17); \
        } \
    }\
    static const struct spi_platform_data spi2_data = { \
        .irq = IRQ_SPI2_IDX, \
        .io     = spi2_data##_io, \
        .init   = spi2_data##_iomc_init, \


#define SPI2_PLATFORM_DATA_END() \
    .reg    = (volatile struct spi_regs *)&JL_SPI2->CON, \
};





#define SPI3_PLATFORM_DATA_BEGIN(spi3_data) \
    static const struct spi_io spi3_data##_io[] = { \
        { \
            .cs_pin     = IO_PORTG_04, \
            .di_pin     = IO_PORTG_05, \
            .do_pin     = IO_PORTG_07, \
            .clk_pin    = IO_PORTG_03, \
            .d2_pin     = IO_PORTG_06, \
            .d3_pin     = IO_PORTG_02, \
        }, \
        { \
            .cs_pin     = IO_PORTG_11, \
            .di_pin     = IO_PORTG_12, \
            .do_pin     = IO_PORTG_14, \
            .clk_pin    = IO_PORTG_10, \
            .d2_pin     = IO_PORTG_13, \
            .d3_pin     = IO_PORTG_09, \
        }, \
    }; \
    static void spi3_data##_iomc_init(const struct spi_platform_data *pd) \
    { \
        if (pd->port == 'A') { \
           JL_IOMAP->CON0 &= ~BIT(15); \
        } else { \
           JL_IOMAP->CON0 |= BIT(15); \
        } \
    }\
    static const struct spi_platform_data spi3_data = { \
        .irq = IRQ_SPI3_IDX, \
        .io     = spi3_data##_io, \
        .init   = spi3_data##_iomc_init, \


#define SPI3_PLATFORM_DATA_END() \
    .reg    = (volatile struct spi_regs *)&JL_SPI3->CON, \
};




extern const struct device_operations spi_dev_ops;
extern const struct device_operations sfc_spi_dev_ops;


#endif

