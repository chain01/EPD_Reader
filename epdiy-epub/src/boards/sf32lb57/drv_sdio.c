/*
 * SPDX-FileCopyrightText: 2019-2026 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board.h"
#include "drv_sdio.h"
#include "drv_config.h"
/* SF57 E0470A03 board: always-powered TF, active-low PA51 detect. */
#undef SD_INSERT_DETECT_PIN
#define SD_INSERT_DETECT_PIN 51
static uint32_t reader57_read_bytes,reader57_write_bytes,reader57_sd_errors;
#ifdef BSP_USING_SWITCH_MPI2_SDIO
    #include "drv_switch_mpi2_sdio.h"
#endif
/** @addtogroup bsp_driver Driver IO
  * @{
  */

/** @defgroup drv_sdio SDIO
  * @brief SDIO BSP driver
  * @{
  */

#ifdef BSP_USING_SD_LINE

//#define DRV_DEBUG
#define LOG_TAG             "drv.sdio"
#include <drv_log.h>

#define SDIO_USING_DMA          (1)
#define _DUMP_REG_DEBUG         (0)
#define _SDIO_DUMP_REG_NUM      (23)

enum
{
#ifdef BSP_USING_SDMMC1
    SDIO1_INDEX,
#endif
#ifdef BSP_USING_SDMMC2
    SDIO2_INDEX,
#endif
    SDIO_MAX,
};

struct rthw_sdio;

typedef rt_err_t (*sdio_dma_txconfig_cb_t)(struct rthw_sdio *sdio, rt_uint32_t *src, rt_uint32_t *dst, int size);
typedef rt_err_t (*sdio_dma_rxconfig_cb_t)(struct rthw_sdio *sdio, rt_uint32_t *src, rt_uint32_t *dst, int size);
typedef rt_uint32_t (*sdio_clk_get_cb_t)(SD_TypeDef *hw_sdio);

struct sifli_sdio_des
{
    SD_TypeDef *instance;
    sdio_dma_txconfig_cb_t txconfig;
    sdio_dma_rxconfig_cb_t rxconfig;
    sdio_clk_get_cb_t clk_get;
};

struct sdio_pkg
{
    struct rt_mmcsd_cmd *cmd;
    void *buff;
    rt_uint32_t flag;
};

struct rthw_sdio
{
    struct sifli_sdio_des des;
    const struct sifli_sdio_config *cfg;
    struct
    {
        DMA_HandleTypeDef handle_rx;
        DMA_HandleTypeDef handle_tx;
    } dma;
    /* irq state, 0: enabled, 1: disabled */
    rt_uint32_t irq_state;
    struct rt_mmcsd_host *host;
    struct rt_event event;
    struct rt_mutex mutex;
    struct sdio_pkg *pkg;
    uint32_t ahb_en;        // flag to check if ahb access enabled
    uint32_t cmd_to;        // command time out value, it should related with freq
    uint32_t part_offset;   // start offset for read/write with device interface
    uint32_t cur_freq;      // current sd frequency
    rt_uint8_t *cache_buf;
    uint32_t reg_buf[_SDIO_DUMP_REG_NUM];
#ifdef RT_USING_PM
    struct rt_device device;
#endif /* RT_USING_PM */

};

int rthw_sdio_irq_process(struct rt_mmcsd_host *host);

const static struct sifli_sdio_config sdio_config[] =
{
#ifdef BSP_USING_SDMMC1
    SDIO1_BUS_CONFIG,
#endif /* BSP_USING_SDMMC1 */
#ifdef BSP_USING_SDMMC2
    SDIO2_BUS_CONFIG,
#endif /* BSP_USING_SDMMC2 */
};

static struct rthw_sdio sdio_hw[SDIO_MAX];
#if defined(SD_INSERT_DETECT_PIN) && (SD_INSERT_DETECT_PIN != -1)
    static uint8_t sdio_card_inserted = 0; // 0: not inserted, 1: inserted
#endif /* SD_INSERT_DETECT_PIN */

#define SDIO_TX_RX_COMPLETE_TIMEOUT_LOOPS    (100000)

#define RTHW_SDIO_LOCK(_sdio)   rt_mutex_take(&_sdio->mutex, RT_WAITING_FOREVER)
#define RTHW_SDIO_UNLOCK(_sdio) rt_mutex_release(&_sdio->mutex);

ALIGN(SDIO_ALIGN_LEN)
HAL_RETM_BSS_SECT(cache_buf_pool, static rt_uint8_t cache_buf_pool[SDIO_MAX][SDIO_BUFF_SIZE]);


static rt_uint32_t sifli_sdio_clk_get(SD_TypeDef *instance)
{
    return SDIO_CLOCK_FREQ;
}

/**
  * @brief  This function get order from sdio.
  * @param  data
  * @retval sdio  order
  */
static int get_order(rt_uint32_t data)
{
    int order = 0;

    switch (data)
    {
    case 1:
        order = 0;
        break;
    case 2:
        order = 1;
        break;
    case 4:
        order = 2;
        break;
    case 8:
        order = 3;
        break;
    case 16:
        order = 4;
        break;
    case 32:
        order = 5;
        break;
    case 64:
        order = 6;
        break;
    case 128:
        order = 7;
        break;
    case 256:
        order = 8;
        break;
    case 512:
        order = 9;
        break;
    case 1024:
        order = 10;
        break;
    case 2048:
        order = 11;
        break;
    case 4096:
        order = 12;
        break;
    case 8192:
        order = 13;
        break;
    case 16384:
        order = 14;
        break;
    default :
        order = 0;
        break;
    }

    return order;
}

static void dump_sdio_reg(struct rthw_sdio *sdio)
{
    int i;
    uint32_t *reg_base;
    uint32_t *reg_buf;

    reg_base = (uint32_t *)sdio->des.instance;
    reg_buf = &sdio->reg_buf[0];

    for (i = 0; i < _SDIO_DUMP_REG_NUM; i++)
    {
        reg_buf[i] = *reg_base++;
#if _DUMP_REG_DEBUG
        rt_kprintf("%08x ", reg_buf[i]);
        if ((i + 1) % 8 == 0)
        {
            rt_kprintf("\n");
        }
        rt_kprintf("\n");
#endif
    }
}

static void recov_sdio_reg(struct rthw_sdio *sdio)
{
    int i;
    uint32_t *reg_base;
    uint32_t *reg_buf;

    reg_base = (uint32_t *)sdio->des.instance;
    reg_buf = &sdio->reg_buf[0];

    for (i = 0; i < _SDIO_DUMP_REG_NUM; i++)
    {
        *reg_base = reg_buf[i];
        // read only reg: 0x10, 0x14, 0x18, 0x1c for response
        // 0x24 for sr, 0x30 for clear sr, w1c;
        // 0x40 , 0x44, 0x48 for capbility
        // 0x54 for adma error status
#if _DUMP_REG_DEBUG
        rt_kprintf("%08x ", *reg_base);
        if ((i + 1) % 8 == 0)
        {
            rt_kprintf("\n");
        }
        rt_kprintf("\n");
#endif
        reg_base++;
    }
}

void rt_hw_sdio_timeout_handle(struct rthw_sdio *sdio)
{
    dump_sdio_reg(sdio);
    HAL_RCC_ResetModule(sdio->cfg->rcc_mod);
    rt_thread_mdelay(1);
    recov_sdio_reg(sdio);
}

void rthw_set_irq_enable_status(struct rt_mmcsd_host *host, rt_uint32_t res)
{
    struct rthw_sdio *sdio;

    RT_ASSERT(host);

    sdio = (struct rthw_sdio *)host->private_data;
    sdio->irq_state = res;
}

rt_uint32_t rthw_get_irq_enable_status(struct rt_mmcsd_host *host)
{
    struct rthw_sdio *sdio;

    RT_ASSERT(host);

    sdio = (struct rthw_sdio *)host->private_data;

    return sdio->irq_state;
}

/**
    * @brief  Wait SDIO command/data completion and process status/response.
    * @param  sdio  rthw_sdio context
    * @retval 0     Completion handled (may be success or a non-retryable error).
    *               Check cmd->err / data->err for detailed result:
    *               - RT_EOK / 0: success
    *               - -RT_ERROR / -RT_ETIMEOUT: error conditions already recorded
    * @retval 1     Request to retry (re-send) the command. Typical cases:
    *               - Response Command Index (RCI) mismatch (transient)
    *               - Command/Data timeout that is considered retryable
    *
    */
static int rthw_sdio_wait_completed(struct rthw_sdio *sdio)
{
    rt_uint32_t status = 0, rci;
    struct rt_mmcsd_cmd *cmd = sdio->pkg->cmd;
    struct rt_mmcsd_data *data = cmd->data;
    SD_TypeDef *instance = sdio->des.instance;
    if (rthw_get_irq_enable_status(sdio->host))
    {
        while (1)
        {
            status = rthw_sdio_irq_process(sdio->host);
            if (status) break;
            for (int i = 0; i < 10000; i++)
            {
                ;
            }
        }
    }
    else
    {
        if (rt_event_recv(&sdio->event, 0xffffffff, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          rt_tick_from_millisecond(500), &status) != RT_EOK)
        {
            LOG_E("wait %d completed timeout 0x%08x,arg 0x%08x\n", cmd->cmd_code, HAL_SDMMC_GET_STA(instance), cmd->arg);
            cmd->err = -RT_ETIMEOUT;
            rt_hw_sdio_timeout_handle(sdio);
            return 0;
        }
    }

    if (sdio->pkg == RT_NULL)
    {
        LOG_E("sdio->pkg NULL");
        return 0;
    }

    rci = HAL_SDMMC_GET_RCI(instance);

    if ((resp_type(cmd) == RESP_R1) || (resp_type(cmd) == RESP_R1B))
    {
        int cont = 0;
        while (rci != cmd->cmd_code)
        {
            if (cont > 50)
            {
                LOG_E("rci error rci=0x%x,cmd->cmd_code=0x%x", rci, cmd->cmd_code);
                rt_hw_sdio_timeout_handle(sdio);
                return 1;
            }
            cont ++;
            rci = HAL_SDMMC_GET_RCI(instance);
        }

    }

    //cmd->resp[0] = instance->resp1;
    //cmd->resp[1] = instance->resp2;
    //cmd->resp[2] = instance->resp3;
    //cmd->resp[3] = instance->resp4;
    HAL_SDMMC_GET_RESP(instance, (uint32_t *)cmd->resp);
    if (resp_type(cmd) == RESP_R2)
    {
        // FOR R2, it need 128 bits response, high/low words should switch.
        // least 8 bit has been removed, so need fill 8 bits at least bits
        uint32_t temp;
        // switch for [0] as highest
        temp = cmd->resp[0];
        cmd->resp[0] = cmd->resp[3];
        cmd->resp[3] = temp;
        temp = cmd->resp[1];
        cmd->resp[1] = cmd->resp[2];
        cmd->resp[2] = temp;

        // << 8
        cmd->resp[0] = (cmd->resp[0] << 8) | (cmd->resp[1] >> 24);
        cmd->resp[1] = (cmd->resp[1] << 8) | (cmd->resp[2] >> 24);
        cmd->resp[2] = (cmd->resp[2] << 8) | (cmd->resp[3] >> 24);
        cmd->resp[3] = (cmd->resp[3] << 8) ;
        LOG_D("Respones 4 words, switch order");
    }

    if (status & HW_SDIO_ERRORS)
    {
        if ((status & HW_SDIO_IT_CCRCFAIL) && (resp_type(cmd) & (RESP_R3 | RESP_R4)))
        {
            cmd->err = RT_EOK;
        }
        else
        {
            cmd->err = -RT_ERROR;
        }

        if (status & HW_SDIO_IT_CTIMEOUT)
        {
            cmd->err = -RT_ETIMEOUT;
            rt_hw_sdio_timeout_handle(sdio);
            return 1;
        }

        if ((status & HW_SDIO_IT_DCRCFAIL) && (data != NULL))
        {
            data->err = -RT_ERROR;
        }

        if ((status & HW_SDIO_IT_DTIMEOUT) && (data != NULL))
        {
            data->err = -RT_ETIMEOUT;
            rt_hw_sdio_timeout_handle(sdio);
            return 1;
        }

        if (cmd->err == RT_EOK)
        {
            LOG_D("sta0 %d:0x%08X [%08X %08X %08X %08X], tick %d\n", rci, status, cmd->resp[0], cmd->resp[1], cmd->resp[2], cmd->resp[3], rt_tick_get());
        }
        else
        {
            LOG_D("err:0x%08x, %s%s%s%s%s%s%s cmd:%d arg:0x%08x rw:%c len:%d blksize:%d, rci:%d, tick %d\n",
                  status,
                  status & HW_SDIO_IT_CCRCFAIL  ? "CCRCFAIL "    : "",
                  status & HW_SDIO_IT_DCRCFAIL  ? "DCRCFAIL "    : "",
                  status & HW_SDIO_IT_CTIMEOUT  ? "CTIMEOUT "    : "",
                  status & HW_SDIO_IT_DTIMEOUT  ? "DTIMEOUT "    : "",
                  status & HW_SDIO_IT_TXUNDERR  ? "TXUNDERR "    : "",
                  status & HW_SDIO_IT_RXOVERR   ? "RXOVERR "     : "",
                  status == 0                   ? "NULL"         : "",
                  cmd->cmd_code,
                  cmd->arg,
                  data ? (data->flags & DATA_DIR_WRITE ?  'w' : 'r') : '-',
                  data ? data->blks * data->blksize : 0,
                  data ? data->blksize : 0,
                  rci, rt_tick_get()
                 );
        }

    }
    else
    {
        cmd->err = RT_EOK;
        if (data != NULL)
            data->err = RT_EOK;
        LOG_D("sta %d:0x%08X [%08X %08X %08X %08X] tick %d\n", rci, status, cmd->resp[0], cmd->resp[1], cmd->resp[2], cmd->resp[3], rt_tick_get());
    }
    return 0;
}

/**
  * @brief  This function transfer data by dma.
  * @param  sdio  rthw_sdio
  * @param  pkg   sdio package
  * @retval None
  */
static void rthw_sdio_transfer_by_dma(struct rthw_sdio *sdio, struct sdio_pkg *pkg)
{
    struct rt_mmcsd_data *data;
    int size;
    void *buff;
    SD_TypeDef *instance;

    if ((RT_NULL == pkg) || (RT_NULL == sdio))
    {
        LOG_E("rthw_sdio_transfer_by_dma invalid args");
        return;
    }

    data = pkg->cmd->data;
    if (RT_NULL == data)
    {
        LOG_E("rthw_sdio_transfer_by_dma invalid args");
        return;
    }

    buff = pkg->buff;
    if (RT_NULL == buff)
    {
        LOG_E("rthw_sdio_transfer_by_dma invalid args");
        return;
    }
    instance = sdio->des.instance;
    size = data->blks * data->blksize;

    if (data->flags & DATA_DIR_WRITE)
    {
        mpu_dcache_clean(buff, (uint32_t)size);
        sdio->des.txconfig(sdio, (rt_uint32_t *)buff, (rt_uint32_t *)&instance->FIFO, size);
        //instance->DCTR |= HW_SDIO_DMA_ENABLE;
        // use ext-dma to replace it, sram to sdcard?
    }
    else if (data->flags & DATA_DIR_READ)
    {
        if (IS_DCACHED_RAM(buff))
            SCB_InvalidateDCache_by_Addr(buff, size);
        sdio->des.rxconfig(sdio, (rt_uint32_t *)&instance->FIFO, (rt_uint32_t *)buff, size);
        //instance->dctrl |= HW_SDIO_DMA_ENABLE | HW_SDIO_DPSM_ENABLE;
        // use ext-dma to replace, sdcard to ram?
    }
}

/**
  * @brief  This function transfer data by cpu.
  * @param  sdio  rthw_sdio
  * @param  pkg   sdio package
  * @retval None
  */
static void rthw_sdio_transfer_by_cpu(struct rthw_sdio *sdio, struct sdio_pkg *pkg)
{
    struct rt_mmcsd_data *data;
    int size;
    void *buff;
    SD_TypeDef *instance;

    if ((RT_NULL == pkg) || (RT_NULL == sdio))
    {
        LOG_E("rthw_sdio_transfer_by_cpu invalid args");
        return;
    }

    data = pkg->cmd->data;
    if (RT_NULL == data)
    {
        LOG_E("rthw_sdio_transfer_by_cpu invalid args");
        return;
    }

    buff = pkg->buff;
    if (RT_NULL == buff)
    {
        LOG_E("rthw_sdio_transfer_by_cpu invalid args");
        return;
    }
    instance = sdio->des.instance;
    size = data->blks * data->blksize;

    if (data->flags & DATA_DIR_WRITE)
    {
        //HAL_SDMMC_SET_DATA_START(instance, HW_SDIO_DPSM_ENABLE);
        HAL_SDMMC_WIRTE(instance, (uint32_t *)buff, size);
    }
    else if (data->flags & DATA_DIR_READ)
    {
        //HAL_SDMMC_SET_DATA_START(instance, HW_SDIO_DPSM_ENABLE);
        HAL_SDMMC_READ(instance, (uint32_t *)buff, size);
    }
}

/**
  * @brief  This function send command.
  * @param  sdio  rthw_sdio
  * @param  pkg   sdio package
  * @retval None
  */
static void rthw_sdio_send_command(struct rthw_sdio *sdio, struct sdio_pkg *pkg)
{
    struct rt_mmcsd_cmd *cmd = pkg->cmd;
    struct rt_mmcsd_data *data = cmd->data;
    SD_TypeDef *instance = sdio->des.instance;
    rt_uint32_t reg_cmd;
    int retry_left = 10; /* limit resend to at most 10 times */

    /* save pkg */
    sdio->pkg = pkg;

    LOG_D("CMD:%d ARG:0x%08x RES:%s%s%s%s%s%s%s%s%s rw:%c len:%d blksize:%d, tick %d",
          cmd->cmd_code,
          cmd->arg,
          resp_type(cmd) == RESP_NONE ? "NONE"  : "",
          resp_type(cmd) == RESP_R1  ? "R1"  : "",
          resp_type(cmd) == RESP_R1B ? "R1B"  : "",
          resp_type(cmd) == RESP_R2  ? "R2"  : "",
          resp_type(cmd) == RESP_R3  ? "R3"  : "",
          resp_type(cmd) == RESP_R4  ? "R4"  : "",
          resp_type(cmd) == RESP_R5  ? "R5"  : "",
          resp_type(cmd) == RESP_R6  ? "R6"  : "",
          resp_type(cmd) == RESP_R7  ? "R7"  : "",
          data ? (data->flags & DATA_DIR_WRITE ?  'w' : 'r') : '-',
          data ? data->blks * data->blksize : 0,
          data ? data->blksize : 0, rt_tick_get()
         );
#ifdef RT_USING_PM
    rt_pm_request(PM_SLEEP_MODE_IDLE);
#ifdef BSP_PM_FREQ_SCALING
    rt_pm_hw_device_start();
#endif
#endif

    // switch to normal command mode before set command
    if (sdio->ahb_en)
        HAL_SDMMC_SWITCH_NORMAL(instance);

    /* config data reg */
    if (data != RT_NULL)
    {
        rt_uint32_t dir = 0;
        rt_uint32_t size = data->blks * data->blksize;
        int res;
        rt_uint32_t wire;

        if (sdio->host->io_cfg.bus_width == MMCSD_BUS_WIDTH_8)
        {
            wire = HW_SDIO_BUSWIDE_8B;
        }
        else if (sdio->host->io_cfg.bus_width == MMCSD_BUS_WIDTH_4)
        {
            wire = HW_SDIO_BUSWIDE_4B;
        }
        else
        {
            wire = HW_SDIO_BUSWIDE_1B;
        }

        //instance->dctrl = 0;
        res = HAL_SDMMC_CLR_DATA_CTRL(instance);
        //instance->dtimer = HW_SDIO_DATATIMEOUT;
        res |= HAL_SDMMC_SET_TIMEOUT(instance, sdio->cmd_to * 5);
        //instance->dlen = size;
        res |= HAL_SDMMC_SET_DATALEN(instance, size);
        //order = get_order(data->blksize);
        dir = (data->flags & DATA_DIR_READ) ? HAL_SDMMC_DATA_CARD2HOST : HAL_SDMMC_DATA_HOST2CARD;
        //instance->dctrl = HW_SDIO_IO_ENABLE | (order << 4) | dir;
        //res |= HAL_SDMMC_SET_DATA_CTRL(instance, data->blksize, dir, HAL_SDMMC_WIRE_SINGLE, HAL_SDMMC_DATA_BLOCK_MODE);
        res |= HAL_SDMMC_SET_DATA_CTRL(instance, data->blksize, dir, wire, HAL_SDMMC_DATA_BLOCK_MODE);
        if (res != HAL_OK)
            LOG_D("HAL set error %d\n", res);
        if (data->flags & DATA_DIR_READ)
            HAL_SDMMC_SET_DATA_START(instance, HW_SDIO_DPSM_ENABLE);

#ifdef SDIO_USING_DMA
        /* transfer config */
        rthw_sdio_transfer_by_dma(sdio, pkg);
#endif
        /* wait completed */
        //rthw_sdio_wait_completed(sdio);
        LOG_D("dir: %d, start %d, length %d, blksize %d\n", dir, cmd->arg, size, data->blksize);
    }
    else
    {
        HAL_SDMMC_SET_TIMEOUT(instance, sdio->cmd_to);
    }

    /* open irq */
    rt_uint32_t mask = HAL_SDMMC_GET_IRQ_MASK(instance);
    mask |= (HW_SDIO_IT_CMDSENT | HW_SDIO_IT_CMDREND | HW_SDIO_ERRORS);
    if (data != RT_NULL)
    {
        mask |= HW_SDIO_IT_DATAEND;
    }
    if (resp_type(cmd) & (RESP_R3))
        mask &= ~(HW_SDIO_IT_CCRCFAIL);

    HAL_SDMMC_SET_IRQ_MASK(instance, mask);

    /* config cmd response */
    if (resp_type(cmd) == RESP_NONE)
        reg_cmd = 0; //HW_SDIO_RESPONSE_NO;
    else if (resp_type(cmd) == RESP_R2)
        reg_cmd = 3; //HW_SDIO_RESPONSE_LONG;
    else
        reg_cmd = 1; //HW_SDIO_RESPONSE_SHORT;

    /* send cmd */
    //instance->arg = cmd->arg;
    //instance->cmd = reg_cmd;
SET_CMD:
    HAL_SDMMC_SET_CMD(instance, cmd->cmd_code, reg_cmd, cmd->arg);

    /* wait completed */
    if (rthw_sdio_wait_completed(sdio))
    {
        if (retry_left-- > 0)
            goto SET_CMD;
        LOG_E("SDIO CMD %d retry limit(10) reached, arg=0x%08x", cmd->cmd_code, cmd->arg);
        if (cmd->err == RT_EOK)
            cmd->err = -RT_ERROR; /* mark as error if not already */
    }

    /* Waiting for data to be sent to completion */
    if ((data != RT_NULL) && (cmd->err == RT_EOK))
    {
        volatile rt_uint32_t count = SDIO_TX_RX_COMPLETE_TIMEOUT_LOOPS;
        //LOG_D("before data: 0x%08x\n",HAL_SDMMC_GET_STA(instance));
        int dma_res;
#ifndef SDIO_USING_DMA
        rthw_sdio_transfer_by_cpu(sdio, pkg);

        if (data->flags & DATA_DIR_WRITE)   // for write, start in irq, need wait data done
        {
            // open error and data end irq
            mask = HAL_SDMMC_GET_IRQ_MASK(instance);
            mask |= (HW_SDIO_IT_DATAEND | HW_SDIO_IT_CMDREND | HW_SDIO_ERRORS);
            int irq_retry_left = 10; /* limit IRQ wait resend to at most 10 times */
IRQ_MASK:
            HAL_SDMMC_SET_IRQ_MASK(instance, mask);
            if (rthw_sdio_wait_completed(sdio))
            {
                if (irq_retry_left-- > 0)
                    goto IRQ_MASK;
                LOG_E("SDIO DATA (WRITE) wait retry limit(10) reached, CMD %d arg=0x%08x", cmd->cmd_code, cmd->arg);
                if (cmd->err == RT_EOK)
                    cmd->err = -RT_ERROR;
            }
        }
#else
        if (data->flags & DATA_DIR_WRITE)
            dma_res = HAL_DMA_PollForTransfer(&sdio->dma.handle_tx, HAL_DMA_FULL_TRANSFER, 1000);
        else if (data->flags & DATA_DIR_READ)
            dma_res = HAL_DMA_PollForTransfer(&sdio->dma.handle_rx, HAL_DMA_FULL_TRANSFER, 1000);
        if (HAL_OK != dma_res)
            RT_ASSERT(0);
#endif
        //LOG_D("after data: 0x%08x\n",HAL_SDMMC_GET_STA(instance));
        while (count && (HAL_SDMMC_GET_STA(instance) & (HW_SDIO_IT_TXACT)))
        {
            count--;
        }

        if ((count == 0) || (HAL_SDMMC_GET_STA(instance) & HW_SDIO_ERRORS))
        {
            cmd->err = -RT_ERROR;
            LOG_D("count = %d, status 0x%x\n", count, HAL_SDMMC_GET_STA(instance));
        }
        //LOG_D("0x%08x\n",HAL_SDMMC_GET_STA(instance));
    }

    /* close irq, keep sdio irq ??? */
    //instance->mask = instance->mask & HW_SDIO_IT_SDIOIT ? HW_SDIO_IT_SDIOIT : 0x00;
    mask = HAL_SDMMC_GET_IRQ_MASK(instance);
    mask = mask & SD_IER_SDIO_MASK ? SD_IER_SDIO_MASK : 0x00000;

    HAL_SDMMC_SET_IRQ_MASK(instance, mask);

    //HAL_SDMMC_CLR_DATA_CTRL(instance);

    /* clear pkg */
    sdio->pkg = RT_NULL;

    // recover to AHB mode if it enabled
    if (sdio->ahb_en)
    {
        if ((data != RT_NULL) && (data->flags & DATA_DIR_WRITE))
        {
            uint32_t addr, len;
            len = data->blksize * data->blks;
            if ((sdio->host->card->flags & CARD_FLAG_SDHC) || (sdio->host->card->flags & CARD_FLAG_SDXC)) // SDHC/SDXC based on block
            {
                addr = cmd->arg * data->blksize + sdio->cfg->mem_base;
            }
            else
            {
                addr = cmd->arg + sdio->cfg->mem_base;
            }
            SCB_InvalidateDCache_by_Addr((void *)addr, len);
            LOG_I("DCache by addr 0x%x with size 0x%x\n", addr, len);
        }
        HAL_SDMMC_SWITCH_AHB(instance);
    }
#ifdef RT_USING_PM
#ifdef BSP_PM_FREQ_SCALING
    rt_pm_hw_device_stop();
#endif
    rt_pm_release(PM_SLEEP_MODE_IDLE);
#endif

    //LOG_I("set comd func done\n");
}

/**
  * @brief  This function send sdio request.
  * @param  sdio  rthw_sdio
  * @param  req   request
  * @retval None
  */
static void rthw_sdio_request(struct rt_mmcsd_host *host, struct rt_mmcsd_req *req)
{
    struct sdio_pkg pkg;
    struct rthw_sdio *sdio = host->private_data;
    struct rt_mmcsd_data *data;
#ifdef BSP_USING_SWITCH_MPI2_SDIO
    rt_switch_sdio_lock();
#endif
    RTHW_SDIO_LOCK(sdio);
    if (sdio->des.instance == hwp_sdmmc2)
        CLEAR_BIT(sdio->des.instance->CLKCR, SD_CLKCR_STOP_CLK);

    if (req->cmd != RT_NULL)
    {
        memset(&pkg, 0, sizeof(pkg));
        data = req->cmd->data;
        pkg.cmd = req->cmd;

        if (data != RT_NULL)
        {
            rt_uint32_t size = data->blks * data->blksize;

            RT_ASSERT(size <= SDIO_BUFF_SIZE);

            pkg.buff = data->buf;
            if (((rt_uint32_t)data->buf & (SDIO_ALIGN_LEN - 1)) && IS_DCACHED_RAM(data->buf))
                // replace buffer any way for SRAM buffer and aligned issue
            {
                SCB_InvalidateDCache_by_Addr(sdio->cache_buf, size);
                pkg.buff = sdio->cache_buf;
                if (data->flags & DATA_DIR_WRITE)
                {
                    memcpy(sdio->cache_buf, data->buf, size);
                }
            }
        }

        rthw_sdio_send_command(sdio, &pkg);

        if ((data != RT_NULL) && (data->flags & DATA_DIR_READ) && ((rt_uint32_t)data->buf & (SDIO_ALIGN_LEN - 1)) && IS_DCACHED_RAM(data->buf))
            //if ((data != RT_NULL) && (data->flags & DATA_DIR_READ)) // always do copy when buffer replaced.
        {
            memcpy(data->buf, sdio->cache_buf, data->blksize * data->blks);
        }
    }

    if (req->stop != RT_NULL)
    {
        memset(&pkg, 0, sizeof(pkg));
        pkg.cmd = req->stop;
        rthw_sdio_send_command(sdio, &pkg);
    }

    if (sdio->des.instance == hwp_sdmmc2) {
        if (req->cmd && (req->cmd->err || (req->cmd->data && req->cmd->data->err)))
            ++reader57_sd_errors;
        else if (req->cmd && req->cmd->data) {
            uint32_t bytes = req->cmd->data->blks * req->cmd->data->blksize;
            if (req->cmd->data->flags & DATA_DIR_WRITE) reader57_write_bytes += bytes;
            else reader57_read_bytes += bytes;
        }
        /* No SDIO module on this bus: stop only the TF clock between requests.
         * Preserve the divider, power, RCA and filesystem/open file state. */
        SET_BIT(sdio->des.instance->CLKCR, SD_CLKCR_STOP_CLK);
    }
    RTHW_SDIO_UNLOCK(sdio);

    mmcsd_req_complete(sdio->host);
#ifdef BSP_USING_SWITCH_MPI2_SDIO
    rt_switch_sdio_unlock();
#endif
}

static int rthw_sdio_set_clk(struct rthw_sdio *sdio, uint32_t clk)
{
    rt_uint32_t div, clk_src;
    SD_TypeDef *instance = sdio->des.instance;

    sdio->cur_freq = clk;

    clk_src = sdio->des.clk_get(sdio->des.instance);
    if (clk_src < 400 * 1000)
    {
        LOG_E("The clock rate is too low! rata:%d", clk_src);
        return 1;
    }

    if (clk > sdio->host->freq_max)
        clk = sdio->host->freq_max;

    if (clk > clk_src)
    {
        LOG_W("Setting rate is greater than clock source rate.");
        clk = clk_src;
    }

    if (clk != 0)
    {
        div = clk_src / clk;
    }
    else
    {
        div = 2;
    }
    if (div < 2)
    {
        div = 2;
    }

    if (clk / 10 > HAL_SDMMC_DEFAULT_TIMEOUT)
        sdio->cmd_to = clk / 10;
    else
        sdio->cmd_to = HAL_SDMMC_DEFAULT_TIMEOUT;

    LOG_D("SDIO CLK src %d, dst %d, div %d\n", clk_src, clk, div);

    HAL_SDMMC_CLK_SET(instance, div, 1);

    return 0;
}

/**
  * @brief  This function config sdio.
  * @param  host    rt_mmcsd_host
  * @param  io_cfg  rt_mmcsd_io_cfg
  * @retval None
  */
static void rthw_sdio_iocfg(struct rt_mmcsd_host *host, struct rt_mmcsd_io_cfg *io_cfg)
{
    rt_uint32_t clk = io_cfg->clock;
    struct rthw_sdio *sdio = (struct rthw_sdio *)host->private_data;
    SD_TypeDef *instance = sdio->des.instance;
#ifdef BSP_USING_SWITCH_MPI2_SDIO
    rt_switch_sdio_lock();
#endif
    LOG_D("clk:%d width:%s%s%s power:%s%s%s",
          clk,
          io_cfg->bus_width == MMCSD_BUS_WIDTH_8 ? "8" : "",
          io_cfg->bus_width == MMCSD_BUS_WIDTH_4 ? "4" : "",
          io_cfg->bus_width == MMCSD_BUS_WIDTH_1 ? "1" : "",
          io_cfg->power_mode == MMCSD_POWER_OFF ? "OFF" : "",
          io_cfg->power_mode == MMCSD_POWER_UP ? "UP" : "",
          io_cfg->power_mode == MMCSD_POWER_ON ? "ON" : ""
         );

    RTHW_SDIO_LOCK(sdio);

    // set clock divider
    rthw_sdio_set_clk(sdio, clk);

    switch (io_cfg->power_mode)
    {
    case MMCSD_POWER_OFF:
        //instance->power = HW_SDIO_POWER_OFF;
        HAL_SDMMC_POWER_SET(instance, HW_SDIO_POWER_OFF);
        break;
    case MMCSD_POWER_UP:
        //instance->power = HW_SDIO_POWER_UP;
        HAL_SDMMC_POWER_SET(instance, HW_SDIO_POWER_UP);
        break;
    case MMCSD_POWER_ON:
        //instance->power = HW_SDIO_POWER_ON;
        HAL_SDMMC_POWER_SET(instance, HW_SDIO_POWER_ON);
        break;
    default:
        LOG_W("unknown power_mode %d", io_cfg->power_mode);
        break;
    }

    RTHW_SDIO_UNLOCK(sdio);
#ifdef BSP_USING_SWITCH_MPI2_SDIO
    rt_switch_sdio_unlock();
#endif
}

/**
  * @brief  This function update sdio interrupt.
  * @param  host    rt_mmcsd_host
  * @param  enable
  * @retval None
  */
void rthw_sdio_irq_update(struct rt_mmcsd_host *host, rt_int32_t enable)
{
    struct rthw_sdio *sdio = host->private_data;
    SD_TypeDef *instance = sdio->des.instance;

    if (enable)
    {
        LOG_D("enable sdio irq\n");
        HAL_SDMMC_ENABLE_CEATA_MODE(instance, 1, 1);
        //instance->mask |= HW_SDIO_IT_SDIOIT;
        rt_uint32_t mask = HAL_SDMMC_GET_IRQ_MASK(instance);
        mask |= SD_IER_SDIO_MASK;
        HAL_SDMMC_SET_IRQ_MASK(instance, mask);
    }
    else
    {
        LOG_D("disable sdio irq\n");
        //instance->mask &= ~HW_SDIO_IT_SDIOIT;
        rt_uint32_t mask = HAL_SDMMC_GET_IRQ_MASK(instance);
        mask &= ~SD_IER_SDIO_MASK;
        HAL_SDMMC_SET_IRQ_MASK(instance, mask);
        HAL_SDMMC_DISABLE_CEATA_MODE(instance);
    }
}

/**
  * @brief  This function delect sdcard.
  * @param  host    rt_mmcsd_host
  * @retval 0x01
  */
static rt_int32_t rthw_sd_delect(struct rt_mmcsd_host *host)
{
    LOG_D("try to detect device");
    return 0x01;
}

/**
  * @brief  This function interrupt process function.
  * @param  host  rt_mmcsd_host
  * @retval None
  */
int rthw_sdio_irq_process(struct rt_mmcsd_host *host)
{
    int complete = 0;
    struct rthw_sdio *sdio = host->private_data;
    SD_TypeDef *instance = sdio->des.instance;
    rt_uint32_t intstatus = HAL_SDMMC_GET_STA(instance);

    //rt_kprintf("SD IRQ 0x%x\n",intstatus);
    if (intstatus & HW_SDIO_ERRORS)
    {
        //instance->icr = HW_SDIO_ERRORS;
        HAL_SDMMC_CLR_INT(instance, HW_SDIO_ERRORS);
        complete = 1;
    }
    else
    {
        if (intstatus & HW_SDIO_IT_CMDREND)
        {
            //instance->icr = HW_SDIO_IT_CMDREND;
            HAL_SDMMC_CLR_INT(instance, HW_SDIO_IT_CMDREND);
            //complete = 1;

            if (sdio->pkg != RT_NULL)
            {
                if (!sdio->pkg->cmd->data)
                {
                    complete = 1;
                }
                else if ((sdio->pkg->cmd->data->flags & DATA_DIR_WRITE))
                {
                    // enable data, set complete to let fifo write, and wait next irq for data done
                    HAL_SDMMC_SET_DATA_START(instance, HW_SDIO_DPSM_ENABLE);
#ifndef SDIO_USING_DMA
                    complete = 1;
#endif
                }
                else if (sdio->pkg->cmd->data->flags & DATA_DIR_READ)
                {
                    // enable data, wait data done(fill to fifo), read fifo after that.
                    //HAL_SDMMC_SET_DATA_START(instance, HW_SDIO_DPSM_ENABLE);
                    //complete = 1;
                }
            }
        }

        if (intstatus & HW_SDIO_IT_CMDSENT)
        {
            //instance->icr = HW_SDIO_IT_CMDSENT;
            HAL_SDMMC_CLR_INT(instance, HW_SDIO_IT_CMDSENT);

            if (resp_type(sdio->pkg->cmd) == RESP_NONE)
            {
                complete = 1;
            }
        }

        if (intstatus & HW_SDIO_IT_DATAEND)
        {
            //instance->icr = HW_SDIO_IT_DATAEND;
            HAL_SDMMC_CLR_INT(instance, HW_SDIO_IT_DATAEND);
            rthw_sdio_irq_update(host, 1);
            complete = 1;
        }
        if (intstatus & HW_SDIO_IT_STBITERR)
        {
            //instance->icr = HW_SDIO_IT_DATAEND;
            //rt_kprintf("HW_SDIO_IT_STBITERR 0x%x,SR=0X%x\n",intstatus,instance->DCR);
            HAL_SDMMC_CLR_INT(instance, HW_SDIO_IT_STBITERR);
            complete = 1;
        }

    }

    if ((intstatus & HW_SDIO_IT_SDIOIT) && (HAL_SDMMC_GET_IRQ_MASK(instance) & HW_SDIO_IT_SDIOIT))
    {
        //instance->icr = HW_SDIO_IT_SDIOIT;
        HAL_SDMMC_CLR_INT(instance, HW_SDIO_IT_SDIOIT);
        //sdio_irq_wakeup(host);
    }
#if 1
    if (intstatus & SD_SR_SDIO)//0x10000
    {
        HAL_SDMMC_CLR_INT(instance, SD_SR_SDIO);
        sdio_irq_wakeup(host);
    }
#endif

    if (complete)
    {
        //instance->mask &= ~HW_SDIO_ERRORS;
        rt_uint32_t mask = HAL_SDMMC_GET_IRQ_MASK(instance);
        mask &= ~HW_SDIO_ERRORS;
        HAL_SDMMC_SET_IRQ_MASK(instance, mask);
        if (rthw_get_irq_enable_status(host))
            return intstatus;
        rt_event_send(&sdio->event, intstatus);
    }
    return 0;
}

static const struct rt_mmcsd_host_ops ops =
{
    rthw_sdio_request,
    rthw_sdio_iocfg,
    rthw_sd_delect,
    rthw_sdio_irq_update,
    rthw_get_irq_enable_status,
    rthw_set_irq_enable_status,
};

/**
  * @brief  This function create mmcsd host.
  * @param  sdio  sdio
  * @retval rt_mmcsd_host
  */
struct rt_mmcsd_host *sdio_host_create(struct rthw_sdio *sdio)
{
    struct rt_mmcsd_host *host;
    const struct sifli_sdio_config *config;

    RT_ASSERT(sdio);

    config = sdio->cfg;

    host = mmcsd_alloc_host();
    if (host == RT_NULL)
    {
        LOG_E("L:%d F:%s mmcsd alloc host fail");
        return RT_NULL;
    }

    rt_event_init(&sdio->event, config->name, RT_IPC_FLAG_FIFO);
    rt_mutex_init(&sdio->mutex, config->name, RT_IPC_FLAG_FIFO);

    /* set host defautl attributes */
    rt_strncpy(host->name, config->name, sizeof(host->name) - 1);
    host->name[RT_NAME_MAX - 1] = '\0';
    host->ops = &ops;
    host->freq_min = SDIO_MIN_FREQ;
    host->freq_max = SDIO_MAX_FREQ; //SDIO_MAX_FREQ; // ??
    host->valid_ocr = 0X00FFFF80;/* The voltage range supported is 1.65v-3.6v */

    // set 1 bit only, config it when 4 bits ready
    host->flags = MMCSD_MUTBLKWRITE | MMCSD_SUP_SDIO_IRQ;

    /* Per-controller 4-bit mode control (match by instance base address) */
#ifndef SDMMC1_BUS_WIDTH_1_ONLY
    if ((uint32_t)sdio->des.instance == SDMMC1_BASE)
    {
        host->flags |= MMCSD_BUSWIDTH_4;
    }
#endif /* SDMMC1_BUS_WIDTH_1_ONLY */
#ifdef SDMMC2_BASE
#ifndef SDMMC2_BUS_WIDTH_1_ONLY
    if ((uint32_t)sdio->des.instance == SDMMC2_BASE)
    {
        host->flags |= MMCSD_BUSWIDTH_4;
    }
#endif /* SDMMC2_BUS_WIDTH_1_ONLY */
#endif /* SDMMC2_BASE */

    host->max_seg_size = SDIO_BUFF_SIZE;
    host->max_dma_segs = 1;
    host->max_blk_size = 512;
    host->max_blk_count = 512;

    /* link up host and sdio */
    sdio->host = host;
    host->private_data = sdio;

    sdio->ahb_en = 0;
    sdio->cmd_to = HAL_SDMMC_DEFAULT_TIMEOUT;
    sdio->part_offset = 0;

    rthw_sdio_irq_update(host, 1);
#if defined(SD_INSERT_DETECT_PIN) && (SD_INSERT_DETECT_PIN != -1)
    rt_pin_mode(SD_INSERT_DETECT_PIN, PIN_MODE_INPUT);
    int card_state = rt_pin_read(SD_INSERT_DETECT_PIN);
    rt_thread_mdelay(10);
    if (card_state == rt_pin_read(SD_INSERT_DETECT_PIN))
    {
        if (card_state)
        {
            rt_kprintf("[SD] no card (pin %d), skip drv_sdio\n", SD_INSERT_DETECT_PIN);
            sdio_card_inserted = 0;
            return RT_EOK;
        }
        else
        {
            sdio_card_inserted = 1;
            mmcsd_change(host);
        }
    }
#else
    mmcsd_change(host);
#endif /* SD_INSERT_DETECT_PIN */
    /* ready to change */


    return host;
}

/**
  * @brief  This function configures the DMATX.
  * @param  BufferSRC: pointer to the source buffer
  * @param  BufferSize: buffer size
  * @retval None
  */
void SD_LowLevel_DMA_TxConfig(struct rthw_sdio *sdio, uint32_t *src, uint32_t *dst, uint32_t BufferSize)
{
    static uint32_t size = 0;
    size += BufferSize * 4;
    const struct sifli_sdio_config *config;

    config = sdio->cfg;
    sdio->dma.handle_tx.Instance = config->dma_tx.Instance;
    sdio->dma.handle_tx.Init.Request             = config->dma_tx.request;
    sdio->dma.handle_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    sdio->dma.handle_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    sdio->dma.handle_tx.Init.MemInc              = DMA_MINC_ENABLE;
    sdio->dma.handle_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    sdio->dma.handle_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    sdio->dma.handle_tx.Init.Mode                = DMA_NORMAL;
    sdio->dma.handle_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    sdio->dma.handle_tx.Init.BurstSize           = 1;

    HAL_DMA_DeInit(&sdio->dma.handle_tx);
    HAL_DMA_Init(&sdio->dma.handle_tx);

    HAL_DMA_Start(&sdio->dma.handle_tx, (uint32_t)src, (uint32_t)dst, BufferSize);
}

/**
  * @brief  This function configures the DMARX.
  * @param  BufferDST: pointer to the destination buffer
  * @param  BufferSize: buffer size
  * @retval None
  */
void SD_LowLevel_DMA_RxConfig(struct rthw_sdio *sdio, uint32_t *src, uint32_t *dst, uint32_t BufferSize)
{
    const struct sifli_sdio_config *config;

    config = sdio->cfg;
    sdio->dma.handle_rx.Instance = config->dma_rx.Instance;
    sdio->dma.handle_rx.Init.Request             = config->dma_rx.request;
    sdio->dma.handle_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    sdio->dma.handle_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    sdio->dma.handle_rx.Init.MemInc              = DMA_MINC_ENABLE;
    sdio->dma.handle_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    sdio->dma.handle_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    sdio->dma.handle_rx.Init.Mode                = DMA_NORMAL;
    sdio->dma.handle_rx.Init.Priority            = DMA_PRIORITY_LOW;
    sdio->dma.handle_rx.Init.BurstSize           = 1;

    HAL_DMA_DeInit(&sdio->dma.handle_rx);
    HAL_DMA_Init(&sdio->dma.handle_rx);

    HAL_DMA_Start(&sdio->dma.handle_rx, (uint32_t)src, (uint32_t)dst, BufferSize);
}

/**
  * @brief  This function get sdio clock.
  * @param  instance: sifli sdio hardware block
  * @retval PCLK2Freq
  */
static rt_uint32_t sifli_sdio_clock_get(SD_TypeDef *instance)
{
    UNUSED(instance);
    uint32_t sclk = HAL_RCC_GetHCLKFreq(CORE_ID_HCPU);
    LOG_D("SDIO source clock %d Hz \n", sclk);
    return sclk;//48 * 1000 * 1000; //HAL_RCC_GetPCLK2Freq();
}

static rt_err_t DMA_TxConfig(struct rthw_sdio *sdio, rt_uint32_t *src, rt_uint32_t *dst, int Size)
{
    SD_LowLevel_DMA_TxConfig(sdio, (uint32_t *)src, (uint32_t *)dst, Size / 4);
    return RT_EOK;
}

static rt_err_t DMA_RxConfig(struct rthw_sdio *sdio, rt_uint32_t *src, rt_uint32_t *dst, int Size)
{
    SD_LowLevel_DMA_RxConfig(sdio, (uint32_t *)src, (uint32_t *)dst, Size / 4);
    return RT_EOK;
}

#ifdef BSP_USING_SDMMC1
void SDMMC1_IRQHandler(void)
{
    /* enter interrupt */
    rt_interrupt_enter();
    /* Process All SDIO Interrupt Sources */
    rthw_sdio_irq_process(sdio_hw[SDIO1_INDEX].host);

    /* leave interrupt */
    rt_interrupt_leave();
}
#endif /* BSP_USING_SDMMC1 */

#ifdef BSP_USING_SDMMC2
void SDMMC2_IRQHandler(void)
{
    /* enter interrupt */
    rt_interrupt_enter();
    /* Process All SDIO Interrupt Sources */
    rthw_sdio_irq_process(sdio_hw[SDIO2_INDEX].host);

    /* leave interrupt */
    rt_interrupt_leave();
}
#endif /* BSP_USING_SDMMC2 */

#ifndef SD_BOOT
    #include "dfs_fs.h"
#endif

#ifdef HPSYS_CFG_SYSCR_SDNAND
static int rt_sdio_get_offset(int part_id)
{
    struct dfs_partition part;
    int status;
    struct rt_mmcsd_req req;
    struct rt_mmcsd_data  data;
    struct rt_mmcsd_cmd   cmd;
    struct rt_mmcsd_cmd   stop;
    uint8_t *buf;
    int offset;

    memset(&req, 0, sizeof(struct rt_mmcsd_req));
    memset(&data, 0, sizeof(struct rt_mmcsd_data));
    memset(&cmd, 0, sizeof(struct rt_mmcsd_cmd));
    memset(&stop, 0, sizeof(struct rt_mmcsd_cmd));
    req.cmd = &cmd;
    req.data = &data;
    req.stop = NULL; //&stop;

    buf = rt_malloc(512);
    if (buf == NULL)
    {
        LOG_E("Malloc buf fail for SD read\n");
        return 0;
    }
    data.blks = 1;
    data.blksize = 512;
    data.buf = (rt_uint32_t *)buf;
    data.flags = DATA_DIR_READ;
    data.timeout_clks = 0;
    data.timeout_ns = 10000000;
    data.mrq = &req;
    data.stop = NULL;

    cmd.arg = 0;

    cmd.cmd_code = READ_SINGLE_BLOCK;
    req.stop = NULL;

    cmd.flags = RESP_SPI_R1 | RESP_R1 | CMD_ADTC;
    cmd.retries = 1;
    cmd.data = &data;
    cmd.mrq = &req;

    rthw_sdio_request(sdio_hw[0].host, &req);
    if (cmd.err || data.err)
    {
        rt_free(buf);
        LOG_I("SD Read error with %d, %d\n", cmd.err, data.err);
        return 0;
    }

    status = dfs_filesystem_get_partition(&part, buf, part_id);
    if (status == RT_EOK)
    {
        LOG_I("Part %d offset %d\n", part_id, part.offset);
        offset = part.offset * 512;
    }
    else
    {
        offset = 0;
    }
    rt_free(buf);

    return offset;
}

int rt_sdio_enable_ahb(uint32_t enable_sd_ahb)
{
    HAL_StatusTypeDef res;
    struct rthw_sdio *sdio = &sdio_hw[0];
    SD_TypeDef *instance = sdio->des.instance;
    struct rt_mmcsd_card *card = sdio->host->card;
    uint8_t blk_mode;
    if (card == NULL || instance == NULL)
        return -1;

    if ((card->flags & CARD_FLAG_SDHC) || (card->flags & CARD_FLAG_SDXC)) // for card larger than 2GB, use block for read/write
        blk_mode = 1;
    else
        blk_mode = 0;

    LOG_D("capacity %d KB, flag 0x%x, type %d, block flag %d\n", card->card_capacity, card->flags, card->card_type, blk_mode);

    if (enable_sd_ahb)
    {
        sdio->part_offset = rt_sdio_get_offset(0);
        if (blk_mode)   // for sdhc, sdxc, offset is block based
        {
            HAL_SDMMC_SET_CAOFFSET(instance, sdio->part_offset / 512);
        }
        else
        {
            HAL_SDMMC_SET_CAOFFSET(instance, sdio->part_offset);
        }
        HAL_SDMMC_SWITCH_AHB(instance);

        HAL_SDMMC_SELECT_VERSION(instance,   blk_mode);

        HAL_SDMMC_ENABLE_AHB_MAP(instance, 1);

        HAL_SDMMC_VOID_FIFO(instance, 0);    // stop sd clock when fifo under flow, it should not work for AHB

        //HAL_SDMMC_CACHE_TO_EN(instance, 0);  // for some special card, their latency too large, close timeout function

        sdio->ahb_en = 1;
    }
    else
    {

        HAL_SDMMC_SWITCH_NORMAL(instance);

        //HAL_SDMMC_SELECT_VERSION(instance,   blk_mode);

        HAL_SDMMC_ENABLE_AHB_MAP(instance, 0);

        HAL_SDMMC_VOID_FIFO(instance, 1);

        sdio->ahb_en = 0;
    }

    return 0;
}
#endif /* HPSYS_CFG_SYSCR_SDNAND */

#ifdef RT_USING_PM

static int rt_sdio_freq_chg(const struct rt_device *device, uint8_t mode)
{
    struct rthw_sdio *sdio = device->user_data;
    rt_uint32_t hclk;
    rt_uint32_t sdio_clk;

    hclk = HAL_RCC_GetHCLKFreq(CORE_ID_HCPU);

    HAL_SDMMC_CLK_SET((SD_TypeDef *)sdio->des.instance, 1, 0);
    HAL_Delay(1);
    sdio_clk = hclk >= 240000000 ? SDIO_MAX_FREQ : 24000000;
    rthw_sdio_set_clk(sdio, sdio_clk);

    LOG_I("change to freq:%dkHz", sdio_clk / 1000);

    return 0;
}

static const struct rt_device_pm_ops sdio_pm_op =
{
    .suspend = NULL,
    .resume = NULL,
    .frequency_change = rt_sdio_freq_chg,
};

#ifdef PM_STANDBY_ENABLE

static void rthw_sdio_resume_hw(struct rthw_sdio *sdio)
{
    if (!sdio || !sdio->cfg || !sdio->host)
        return;

    /* the controller module clock was gated when HPSYS powered down */
    HAL_RCC_EnableModule(sdio->cfg->rcc_mod);

    /* re-init controller registers + re-enable its interrupt */
    HAL_SDMMC_INIT(sdio->des.instance);
    HAL_NVIC_SetPriority(sdio->cfg->irqn, 2, 0);
    HAL_NVIC_EnableIRQ(sdio->cfg->irqn);

    /* HAL_SDMMC_INIT leaves the controller at the default low speed; restore the
     * working clock/power so it matches the still-powered card */
    if (sdio->host->io_cfg.clock != 0)
        rthw_sdio_set_clk(sdio, sdio->host->io_cfg.clock);
    HAL_SDMMC_POWER_SET(sdio->des.instance, HW_SDIO_POWER_ON);
}

static rt_err_t rt_sdio_control(struct rt_device *dev, int cmd, void *args)
{
    rt_err_t result = RT_EOK;
    uint8_t mode = (uint8_t)((uint32_t)args);
    struct rthw_sdio *sdio;

    sdio = dev->user_data;
    switch (cmd)
    {
    case RT_DEVICE_CTRL_RESUME:
    {

        if (PM_SLEEP_MODE_STANDBY == mode)
        {
            rthw_sdio_resume_hw(sdio);
        }
        else
        {
            //TODO: is it needed?
            rthw_sdio_irq_update(sdio->host, 1);
            /* ready to change */
            mmcsd_change(sdio->host);
        }
        break;
    }
    case RT_DEVICE_CTRL_SUSPEND:
    {

        HAL_SDMMC_CLK_SET(sdio->des.instance, 1, 0);

        break;
    }
    default:
    {
        break;
    }
    }
    return result;
}
#endif /* PM_STANDBY_ENABLE */

#ifdef RT_USING_DEVICE_OPS
static const rt_device_ops sdio_device_ops =
{
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
#ifdef PM_STANDBY_ENABLE
    rt_sdio_control,
#else
    NULL,
#endif /* PM_STANDBY_ENABLE */
};
#endif

static void rt_sdio_register_rt_device(uint32_t idx)
{
    rt_err_t err = RT_EOK;
    rt_device_t device;
    char devname[8] = {0};

    RT_ASSERT(idx < SDIO_MAX)

    device = &sdio_hw[idx].device;

    device->type        = RT_Device_Class_Miscellaneous;
    device->rx_indicate = RT_NULL;
    device->tx_complete = RT_NULL;

#ifdef RT_USING_DEVICE_OPS
    device->ops         = &sdio_device_ops;
#else
    device->init        = RT_NULL;
    device->open        = RT_NULL;
    device->close       = RT_NULL;
    device->read        = RT_NULL;
    device->write       = RT_NULL;
#ifdef PM_STANDBY_ENABLE
    device->control     = rt_sdio_control;
#else
    device->control     = RT_NULL;
#endif /* PM_STANDBY_ENABLE */
#endif
    device->user_data = (void *)&sdio_hw[idx];

    rt_snprintf(devname, 8, "sdio%d",  idx);
    err = rt_device_register(device, devname, RT_DEVICE_FLAG_RDONLY | RT_DEVICE_FLAG_STANDALONE);
    RT_ASSERT(RT_EOK == err);
}

#endif  /* RT_USING_PM */

static int sdiox_init(uint32_t idx)
{
    struct sifli_sdio_des *des;
    uint32_t time_out = 3000;
    const struct sifli_sdio_config *config;
    struct rthw_sdio *sdio;

    RT_ASSERT(idx < SDIO_MAX);

    config = &sdio_config[idx];
    sdio = &sdio_hw[idx];
    sdio->cfg = config;
    sdio->cache_buf = &cache_buf_pool[idx][0];

    HAL_RCC_EnableModule(config->rcc_mod);

    des = &sdio->des;
    des->clk_get = sifli_sdio_clock_get;
    des->instance = (SD_TypeDef *)config->Instance;
    des->rxconfig = DMA_RxConfig;
    des->txconfig = DMA_TxConfig;

    HAL_SDMMC_INIT(des->instance);

    if (RT_NULL == sdio_host_create(sdio))
    {
        LOG_E("sdio_host create fail");
        return -1;
    }

    HAL_NVIC_SetPriority(config->irqn, 2, 0);
    HAL_NVIC_EnableIRQ(config->irqn);
    //HAL_SD_MspInit(&hsd);
#ifdef SDIO_USING_DMA
    LOG_I("SDIO USING DMA MODE !\n");
#else
    LOG_I("SDIO USING POLLING MODE !\n");
#endif

#if defined(SD_INSERT_DETECT_PIN) && (SD_INSERT_DETECT_PIN != -1)
    if (!sdio_card_inserted)
        time_out = 1;
#endif

#ifdef RT_USING_PM
    rt_sdio_register_rt_device(idx);
    rt_pm_request(PM_SLEEP_MODE_IDLE);
    rt_pm_hw_device_start();
#endif /* RT_USING_PM */

    while (time_out--)
    {
        rt_thread_mdelay(1);
        uint8_t mmcsd_get_stat(void);
        if (mmcsd_get_stat()) break;
    }

#ifdef RT_USING_PM
    rt_pm_release(PM_SLEEP_MODE_IDLE);
    rt_pm_hw_device_stop();
#endif /* RT_USING_PM */

    return 0;

}

int rt_hw_sdio_init(void)
{
    uint32_t i;
    rt_uint8_t priority = RT_MMCSD_THREAD_PREORITY;

    for (i = 0; i < SDIO_MAX; i++)
    {
        sdiox_init(i);
    }

    if (mmcsd_get_thread())
    {
        rt_thread_control(mmcsd_get_thread(), RT_THREAD_CTRL_CHANGE_PRIORITY, &priority);
    }

    return 0;
}
INIT_DEVICE_EXPORT(rt_hw_sdio_init);

#ifdef RT_USING_PM
static int sdio_pm_device_register(void)
{
    uint32_t i;

    for (i = 0; i < SDIO_MAX; i++)
    {
        rt_pm_device_register(&sdio_hw[i].device, &sdio_pm_op);
    }
}
INIT_ENV_EXPORT(sdio_pm_device_register);
#endif /* RT_USING_PM */

//#define DRV_SDIO_TEST
#ifdef DRV_SDIO_TEST
int cmd_sdcard(int argc, char *argv[])
{
    if (strcmp(argv[1], "-ahb") == 0)
    {
        rt_device_t dev = rt_device_find("sd0");    // make sure sd card exist
        {
            uint32_t ahb = atoi(argv[2]);
            int res = rt_sdio_enable_ahb(ahb);
            rt_kprintf("Enable SD AHB (%d) with res %d\n", ahb, res);
        }
        return 0;
    }
    if ((strcmp(argv[1], "-r") && strcmp(argv[1], "-w")) || (argc < 4))
    {
        LOG_I("Invalid parameter\n");
        LOG_I("-r for read, -w for write; with addr length (and value if needed)\n");
        return 2;
    }
    rt_uint32_t *buf = (rt_uint32_t *)rt_malloc(1024 * 4);
    if (buf == NULL)
    {
        LOG_I("Malloc 4KB fail\n");
        return 3;
    }
    memset(buf, 0, 4096);
    rt_uint32_t addr = atoi(argv[2]);
    rt_uint32_t len = atoi(argv[3]);
    rt_uint32_t value = 0;
    if (argc >= 5)
        value = atoi(argv[4]);
    if (len > 4096)
    {
        LOG_I("lenght too large, change to 4KB\n");
        len = 4096;
    }
    else if (len < 512)
    {
        LOG_I("lenght too small, change to 512\n");
        len = 512;
    }

    rt_device_t dev = rt_device_find("sd0");    // get block device
    if (dev)
    {
        if (rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK)
        {
            LOG_I("Open device sd0 fail\n");
            return 1;
        }
        if ((dev->read == NULL) || (dev->write == NULL))
        {
            LOG_I("SD0 device read/write function empty!\n");
            return 1;
        }

        int i, res, blk;
        blk = len >> 9;
        if (strcmp(argv[1], "-r") == 0)
        {
            res = rt_device_read(dev, addr, (void *)buf, blk);
            if (res > 0)
            {
                LOG_I("Read Data :\n");
                for (i = 0; i < len / 4; i++)
                {
                    LOG_RAW(" 0x%08x ", *(buf + i));
                    if ((i & 7) == 7)
                        LOG_RAW("\n");
                }
            }
            else
            {
                LOG_I("read data fail %d\n", res);
            }
        }
        if (strcmp(argv[1], "-w") == 0)
        {
            // initial write data
            if (value == 0)
            {
                for (i = 0; i < len / 4; i++)
                    *(buf + i) = (i << 16) | (i + 1);
            }
            else
            {
                for (i = 0; i < len / 4; i++)
                    *(buf + i) = value;
            }
            res = rt_device_write(dev, addr, buf, blk);
            if (res > 0)
                LOG_I("write done %d\n", res);
            else
                LOG_I("write fail %d\n", res);

        }

        rt_device_close(dev);
    }
    else
    {
        LOG_I("find device sd0 fail\n");
    }

    rt_free(buf);

    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_sdcard, __cmd_sdcard, Test hw sdcard);

#endif  // DRV_SDIO_TEST

#endif

/// @} drv_sdio
/// @} bsp_driver
/// @} file


#ifdef RT_USING_FINSH
static int sd57(int argc,char **argv)
{
    (void)argc;(void)argv;
    rt_kprintf("TF57_STATS read=%u write=%u errors=%u CLKCR=%08x stopped=%u\n",
        reader57_read_bytes,reader57_write_bytes,reader57_sd_errors,hwp_sdmmc2->CLKCR,
        !!(hwp_sdmmc2->CLKCR & SD_CLKCR_STOP_CLK));
    return 0;
}
MSH_CMD_EXPORT(sd57, TF read/write and idle clock status);
#endif
