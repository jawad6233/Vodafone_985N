 

//Begin,[xiaoguang.xiong][2013.07.23][PR 493294]
//update driver version from v1.0 to v1.8,follow goodix's FAE shixuebo's advise.

#include "tpd.h"

#include "tpd_custom_gt9xx.h"

//#ifndef TPD_NO_GPIO
#include "cust_gpio_usage.h"
//#endif
#ifdef TPD_PROXIMITY
#include <linux/hwmsensor.h>
#include <linux/hwmsen_dev.h>
#include <linux/sensors_io.h>
#endif

#include <linux/mmprofile.h>
#include <linux/device.h>
extern struct tpd_device *tpd;

#ifdef VELOCITY_CUSTOM
extern int tpd_v_magnify_x;
extern int tpd_v_magnify_y;
#endif

static int tpd_flag = 0;
int tpd_halt = 0;
static struct task_struct *thread = NULL;
static DECLARE_WAIT_QUEUE_HEAD(waiter);
static DEFINE_MUTEX(i2c_access);

//add by xiaoguang.xiong,2013.07.13,for SYS DEBUG.
extern int tpswitch_sysfs_init(void);

//add by xiaoguang.xiong,2013.11.27,for open/short test and rawdata show.CR 562473.
extern s32 gtp_test_sysfs_init(void);
extern void gtp_test_sysfs_deinit(void);

extern s32 ctp_sysfs_init(void);
extern void ctp_sysfs_deinit(void);

#ifdef TPD_HAVE_BUTTON
static int tpd_keys_local[TPD_KEY_COUNT] = TPD_KEYS;
static int tpd_keys_dim_local[TPD_KEY_COUNT][4] = TPD_KEYS_DIM;
#endif

#if GTP_HAVE_TOUCH_KEY
const u16 touch_key_array[] = TPD_KEYS;
//#define GTP_MAX_KEY_NUM ( sizeof( touch_key_array )/sizeof( touch_key_array[0] ) )
struct touch_vitual_key_map_t
{
   int point_x;
   int point_y;
};
static struct touch_vitual_key_map_t touch_key_point_maping_array[]=GTP_KEY_MAP_ARRAY;
#endif

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
static int tpd_wb_start_local[TPD_WARP_CNT] = TPD_WARP_START;
static int tpd_wb_end_local[TPD_WARP_CNT]   = TPD_WARP_END;
#endif

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
//static int tpd_calmat_local[8]     = TPD_CALIBRATION_MATRIX;
static int tpd_def_calmat_local[8] = TPD_CALIBRATION_MATRIX;
#endif
#define CFG_GROUP_LEN(p_cfg_grp)  (sizeof(p_cfg_grp) / sizeof(p_cfg_grp[0]))
s32 gtp_send_cfg(struct i2c_client *client);
static void tpd_eint_interrupt_handler(void);
static int touch_event_handler(void *unused);
s32 gtp_i2c_read_dbl_check(struct i2c_client *client, u16 addr, u8 *rxbuf, int len);
static int tpd_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int tpd_i2c_detect(struct i2c_client *client, struct i2c_board_info *info);
static int tpd_i2c_remove(struct i2c_client *client);
static void tpd_on(void);
static void tpd_off(void);

#if GTP_CREATE_WR_NODE
extern s32 init_wr_node(struct i2c_client *);
extern void uninit_wr_node(void);
#endif



#if GTP_ESD_PROTECT
#define TPD_ESD_CHECK_CIRCLE        200     // A cycle of 2s is recommended, jiffies here: 10ms
static struct delayed_work gtp_esd_check_work;
static struct workqueue_struct *gtp_esd_check_workqueue = NULL;
static s32 gtp_init_ext_watchdog(struct i2c_client *client);
static void gtp_esd_check_func(struct work_struct *);
void gtp_esd_switch(struct i2c_client *client, s32 on);
u8 esd_running = 0;
#endif

#ifdef TPD_PROXIMITY
#define TPD_PROXIMITY_VALID_REG                   0x814E
#define TPD_PROXIMITY_ENABLE_REG                  0x8042
static u8 tpd_proximity_flag = 0;
static u8 tpd_proximity_detect = 1;//0-->close ; 1--> far away
#endif



struct i2c_client *i2c_client_point = NULL;
static const struct i2c_device_id tpd_i2c_id[] = {{"gt9xx", 0}, {}};
static unsigned short force[] = {0, 0xBA, I2C_CLIENT_END, I2C_CLIENT_END};
static const unsigned short *const forces[] = { force, NULL };
//static struct i2c_client_address_data addr_data = { .forces = forces,};
static struct i2c_board_info __initdata i2c_tpd = { I2C_BOARD_INFO("gt9xx", (0xBA >> 1))};
static struct i2c_driver tpd_i2c_driver =
{
    .probe = tpd_i2c_probe,
    .remove = tpd_i2c_remove,
    .detect = tpd_i2c_detect,
    .driver.name = "gt9xx",
    .id_table = tpd_i2c_id,
    .address_list = (const unsigned short *) forces,
};

static u8 config[GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH]
    = {GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff};

#pragma pack(1)
typedef struct
{
    u16 pid;                 //product id   //
    u16 vid;                 //version id   //
} st_tpd_info;
#pragma pack()

st_tpd_info tpd_info;
u8 int_type = 0;
u32 abs_x_max = 0;
u32 abs_y_max = 0;
u8 gtp_rawdiff_mode = 0;
u8 cfg_len = 0;
u8 grp_cfg_version = 0;
u8 fixed_config = 0;
static u8 chip_gt9xxs = 0;
/* proc file system */
s32 i2c_read_bytes(struct i2c_client *client, u16 addr, u8 *rxbuf, int len);
s32 i2c_write_bytes(struct i2c_client *client, u16 addr, u8 *txbuf, int len);
static struct proc_dir_entry *gt91xx_config_proc = NULL;



static int tpd_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
    strcpy(info->type, "mtk-tpd");
    return 0;
}

#ifdef TPD_PROXIMITY
static s32 tpd_get_ps_value(void)
{
    return tpd_proximity_detect;
}

static s32 tpd_enable_ps(s32 enable)
{
    u8  state;
    s32 ret = -1;
   // u8 buffer[3] = {0x80,0x42};
    	mutex_lock(&i2c_access);
    if (enable)
    {
//        buffer[2] = 1;
        state = 1;
        tpd_proximity_flag = 1;
        GTP_INFO("TPD proximity function to be on.");
    }
    else
    {
//        buffer[2] = 0;
        state = 0;
        //tpd_proximity_detect = 1;
        tpd_proximity_flag = 0;
        GTP_INFO("TPD proximity function to be off.");
    }

    ret = i2c_write_bytes(i2c_client_point, TPD_PROXIMITY_ENABLE_REG, &state, 1);
    //ret = gtp_i2c_write(i2c_client_point, buffer, sizeof(buffer));
    	mutex_unlock(&i2c_access);
    if (ret < 0)
    {
        GTP_ERROR("TPD %s proximity cmd failed.", state ? "enable" : "disable");
        //GTP_ERROR("TPD %s proximity cmd failed.", buffer[2] ? "enable" : "disable");
        return ret;
    }
    GTP_INFO("TPD proximity function %s success.", state ? "enable" : "disable");
    //GTP_INFO("TPD proximity function %s success.", buffer[2] ? "enable" : "disable");
    return 0;
}

s32 tpd_ps_operate(void *self, u32 command, void *buff_in, s32 size_in,
                   void *buff_out, s32 size_out, s32 *actualout)
{
    s32 err = 0;
    s32 value;
    hwm_sensor_data *sensor_data;

    switch (command)
    {
        case SENSOR_DELAY:
            if ((buff_in == NULL) || (size_in < sizeof(int)))
            {
                GTP_ERROR("Set delay parameter error!");
                err = -EINVAL;
            }

            // Do nothing
            break;

        case SENSOR_ENABLE:
            if ((buff_in == NULL) || (size_in < sizeof(int)))
            {
                GTP_ERROR("Enable sensor parameter error!");
                err = -EINVAL;
            }
            else
            {
                value = *(int *)buff_in;
                err = tpd_enable_ps(value);
            }

            break;

        case SENSOR_GET_DATA:
            if ((buff_out == NULL) || (size_out < sizeof(hwm_sensor_data)))
            {
                GTP_ERROR("Get sensor data parameter error!");
                err = -EINVAL;
            }
            else
            {
                sensor_data = (hwm_sensor_data *)buff_out;
                sensor_data->values[0] = tpd_get_ps_value();
                sensor_data->value_divide = 1;
                sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
            }

            break;

        default:
            GTP_ERROR("proxmy sensor operate function no this parameter %d!", command);
            err = -1;
            break;
    }

    return err;
}
#endif

static int gt91xx_config_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    char *ptr = page;
    char temp_data[GTP_CONFIG_MAX_LENGTH + 2] = {0};
    int i;

    ptr += sprintf(ptr, "==== GT9XX config init value====\n");

    for (i = 0 ; i < GTP_CONFIG_MAX_LENGTH ; i++)
    {
        ptr += sprintf(ptr, "0x%02X ", config[i + 2]);

        if (i % 8 == 7)
            ptr += sprintf(ptr, "\n");
    }

    ptr += sprintf(ptr, "\n");

    ptr += sprintf(ptr, "==== GT9XX config real value====\n");
    i2c_read_bytes(i2c_client_point, GTP_REG_CONFIG_DATA, temp_data, GTP_CONFIG_MAX_LENGTH);

    for (i = 0 ; i < GTP_CONFIG_MAX_LENGTH ; i++)
    {
        ptr += sprintf(ptr, "0x%02X ", temp_data[i]);

        if (i % 8 == 7)
            ptr += sprintf(ptr, "\n");
    }
    *eof = 1;
    return (ptr - page);
}

static int gt91xx_config_write_proc(struct file *file, const char *buffer, unsigned long count, void *data)
{
    s32 ret = 0;

    GTP_DEBUG("write count %ld\n", count);

    if (count > GTP_CONFIG_MAX_LENGTH)
    {
        GTP_ERROR("size not match [%d:%ld]\n", GTP_CONFIG_MAX_LENGTH, count);
        return -EFAULT;
    }

    if (copy_from_user(&config[2], buffer, count))
    {
        GTP_ERROR("copy from user fail\n");
        return -EFAULT;
    }

    ret = gtp_send_cfg(i2c_client_point);
    abs_x_max = (config[RESOLUTION_LOC + 1] << 8) + config[RESOLUTION_LOC];
    abs_y_max = (config[RESOLUTION_LOC + 3] << 8) + config[RESOLUTION_LOC + 2];
    int_type = (config[TRIGGER_LOC]) & 0x03;

    if (ret < 0)
    {
        GTP_ERROR("send config failed.");
    }

    return count;
}

int i2c_read_bytes(struct i2c_client *client, u16 addr, u8 *rxbuf, int len)
{
    u8 buffer[GTP_ADDR_LENGTH];
    u16 left = len;
    u16 offset = 0;

    struct i2c_msg msg[2] =
    {
        {
            .addr = ((client->addr &I2C_MASK_FLAG) | (I2C_ENEXT_FLAG)),
            //.addr = (client->addr &I2C_MASK_FLAG),
	    //.ext_flag = I2C_ENEXT_FLAG,
            //.addr = ((client->addr &I2C_MASK_FLAG) | (I2C_PUSHPULL_FLAG)),
            .flags = 0,
            .buf = buffer,
            .len = GTP_ADDR_LENGTH,
            .timing = I2C_MASTER_CLOCK
        },
        {
            .addr = ((client->addr &I2C_MASK_FLAG) | (I2C_ENEXT_FLAG)),
            //.addr = (client->addr &I2C_MASK_FLAG),
	    //.ext_flag = I2C_ENEXT_FLAG,
            //.addr = ((client->addr &I2C_MASK_FLAG) | (I2C_PUSHPULL_FLAG)),
            .flags = I2C_M_RD,
            .timing = I2C_MASTER_CLOCK
        },
    };

    if (rxbuf == NULL)
        return -1;

    GTP_DEBUG("i2c_read_bytes to device %02X address %04X len %d", client->addr, addr, len);

    while (left > 0)
    {
        buffer[0] = ((addr + offset) >> 8) & 0xFF;
        buffer[1] = (addr + offset) & 0xFF;

        msg[1].buf = &rxbuf[offset];

        if (left > MAX_TRANSACTION_LENGTH)
        {
            msg[1].len = MAX_TRANSACTION_LENGTH;
            left -= MAX_TRANSACTION_LENGTH;
            offset += MAX_TRANSACTION_LENGTH;
        }
        else
        {
            msg[1].len = left;
            left = 0;
        }

        if (i2c_transfer(client->adapter, &msg[0], 2) != 2)
        {
            GTP_ERROR("I2C read 0x%X length=%d failed", addr + offset, len);
            return -1;
        }
    }

    return 0;
}

s32 gtp_i2c_read(struct i2c_client *client, u8 *buf, s32 len)
{
    s32 ret = -1;
    u16 addr = (buf[0] << 8) + buf[1];

    ret = i2c_read_bytes(client, addr, &buf[2], len - 2);

    if (!ret)
    {
        return 2;
    }
    else
    {
    #if GTP_SLIDE_WAKEUP
        if (DOZE_ENABLED == doze_status)
        {
            return ret;
        }
    #endif
        gtp_reset_guitar(client, 20);
        return ret;
    }
}
s32 gtp_i2c_read_dbl_check(struct i2c_client *client, u16 addr, u8 *rxbuf, int len)
{
    u8 buf[16] = {0};
    u8 confirm_buf[16] = {0};
    u8 retry = 0;
    
    while (retry++ < 3)
    {
        memset(buf, 0xAA, 16);
        buf[0] = (u8)(addr >> 8);
        buf[1] = (u8)(addr & 0xFF);
        gtp_i2c_read(client, buf, len + 2);
        
        memset(confirm_buf, 0xAB, 16);
        confirm_buf[0] = (u8)(addr >> 8);
        confirm_buf[1] = (u8)(addr & 0xFF);
        gtp_i2c_read(client, confirm_buf, len + 2);
        
        if (!memcmp(buf, confirm_buf, len+2))
        {
            break;
        }
    }    
    if (retry < 3)
    {
        memcpy(rxbuf, confirm_buf+2, len);
        return SUCCESS;
    }
    else
    {
        GTP_ERROR("i2c read 0x%04X, %d bytes, double check failed!", addr, len);
        return FAIL;
    }
}
int i2c_write_bytes(struct i2c_client *client, u16 addr, u8 *txbuf, int len)
{
    u8 buffer[MAX_TRANSACTION_LENGTH];
    u16 left = len;
    u16 offset = 0;

    struct i2c_msg msg =
    {
        .addr = ((client->addr &I2C_MASK_FLAG) | (I2C_ENEXT_FLAG)),
        //.addr = (client->addr &I2C_MASK_FLAG),
	//.ext_flag = I2C_ENEXT_FLAG,
        //.addr = ((client->addr &I2C_MASK_FLAG) | (I2C_PUSHPULL_FLAG)),
        .flags = 0,
        .buf = buffer,
        .timing = I2C_MASTER_CLOCK,
    };


    if (txbuf == NULL)
        return -1;

    GTP_DEBUG("i2c_write_bytes to device %02X address %04X len %d", client->addr, addr, len);

    while (left > 0)
    {
        buffer[0] = ((addr + offset) >> 8) & 0xFF;
        buffer[1] = (addr + offset) & 0xFF;

        if (left > MAX_I2C_TRANSFER_SIZE)
        {
            memcpy(&buffer[GTP_ADDR_LENGTH], &txbuf[offset], MAX_I2C_TRANSFER_SIZE);
            msg.len = MAX_TRANSACTION_LENGTH;
            left -= MAX_I2C_TRANSFER_SIZE;
            offset += MAX_I2C_TRANSFER_SIZE;
        }
        else
        {
            memcpy(&buffer[GTP_ADDR_LENGTH], &txbuf[offset], left);
            msg.len = left + GTP_ADDR_LENGTH;
            left = 0;
        }

        //GTP_DEBUG("byte left %d offset %d", left, offset);
		
        if (i2c_transfer(client->adapter, &msg, 1) != 1)
        {
            GTP_ERROR("I2C write 0x%X%X length=%d failed", buffer[0], buffer[1], len);
            return -1;
        }
    }

    return 0;
}

s32 gtp_i2c_write(struct i2c_client *client, u8 *buf, s32 len)
{
    s32 ret = -1;
    u16 addr = (buf[0] << 8) + buf[1];

    ret = i2c_write_bytes(client, addr, &buf[2], len - 2);

    if (!ret)
    {
        return 1;
    }
    else
    {
    #if GTP_SLIDE_WAKEUP
        if (DOZE_ENABLED == doze_status)
        {
            return ret;
        }
    #endif
        gtp_reset_guitar(client, 20);
        return ret;
    }
}



s32 gtp_send_cfg(struct i2c_client *client)
{
    s32 ret = 1;
#if GTP_DRIVER_SEND_CFG
    s32 retry = 0;

    if (fixed_config)
    {
        GTP_INFO("Ic fixed config, no config sent!");
        return 2;
    }
    
	GTP_DEBUG("Driver Send Config");
    for (retry = 0; retry < 5; retry++)
    {

        ret = gtp_i2c_write(client, config , GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH);



        if (ret > 0)
        {
            break;
        }
    }

#endif

    return ret;
}

s32 gtp_read_version(struct i2c_client *client, u16 *version)
{
    s32 ret = -1;
    s32 i;
    u8 buf[8] = {GTP_REG_VERSION >> 8, GTP_REG_VERSION & 0xff};

    GTP_DEBUG_FUNC();

    ret = gtp_i2c_read(client, buf, sizeof(buf));

    if (ret < 0)
    {
        GTP_ERROR("GTP read version failed");
        return ret;
    }

    if (version)
    {
        *version = (buf[7] << 8) | buf[6];
    }

    tpd_info.vid = *version;
    tpd_info.pid = 0x00;

    //for gt9xx series
    for (i = 0; i < 4; i++)
    {
        if (buf[i + 2] < 0x30)break;

        tpd_info.pid |= ((buf[i + 2] - 0x30) << ((3 - i) * 4));
    }
    if (buf[5] == 0x00)
    {        
        GTP_INFO("IC VERSION: %c%c%c_%02x%02x",
             buf[2], buf[3], buf[4], buf[7], buf[6]);  
    }
    else
    {
        if (buf[5] == 'S' || buf[5] == 's')
        {
            chip_gt9xxs = 1;
        }
        GTP_INFO("IC VERSION:%c%c%c%c_%02x%02x",
             buf[2], buf[3], buf[4], buf[5], buf[7], buf[6]);
    }
    return ret;
}

extern u8 set_tpmoduleinfo(u8 val);
static s32 gtp_init_panel(struct i2c_client *client)
{
    s32 ret = 0;

#if GTP_DRIVER_SEND_CFG
    s32 i;
    u8 check_sum = 0;
    u8 opr_buf[16];
    u8 sensor_id = 0;

    u8 cfg_info_group1[] = CTP_CFG_GROUP1;
    u8 cfg_info_group2[] = CTP_CFG_GROUP2;
    u8 cfg_info_group3[] = CTP_CFG_GROUP3;
    u8 cfg_info_group4[] = CTP_CFG_GROUP4;
    u8 cfg_info_group5[] = CTP_CFG_GROUP5;
    u8 cfg_info_group6[] = CTP_CFG_GROUP6;
    u8 *send_cfg_buf[] = {cfg_info_group1, cfg_info_group2, cfg_info_group3,
                        cfg_info_group4, cfg_info_group5, cfg_info_group6};
    u8 cfg_info_len[] = { CFG_GROUP_LEN(cfg_info_group1), 
                          CFG_GROUP_LEN(cfg_info_group2),
                          CFG_GROUP_LEN(cfg_info_group3),
                          CFG_GROUP_LEN(cfg_info_group4), 
                          CFG_GROUP_LEN(cfg_info_group5),
                          CFG_GROUP_LEN(cfg_info_group6)};

    GTP_DEBUG("Config Groups\' Lengths: %d, %d, %d, %d, %d, %d", 
        cfg_info_len[0], cfg_info_len[1], cfg_info_len[2], cfg_info_len[3],
        cfg_info_len[4], cfg_info_len[5]);

    if ((!cfg_info_len[1]) && (!cfg_info_len[2]) && 
        (!cfg_info_len[3]) && (!cfg_info_len[4]) && 
        (!cfg_info_len[5]))
    {
        sensor_id = 0; 
    }
    else
    {
        ret = gtp_i2c_read_dbl_check(client, GTP_REG_SENSOR_ID, &sensor_id, 1);
        if (SUCCESS == ret)
        {
            if (sensor_id >= 0x06)
            {
                GTP_ERROR("Invalid sensor_id(0x%02X), No Config Sent!", sensor_id);
                return -1;
            }
        }
        else
        {
            GTP_ERROR("Failed to get sensor_id, No config sent!");
            return -1;
        }
    }
    GTP_DEBUG("Sensor_ID: %d", sensor_id);
    
    set_tpmoduleinfo(sensor_id);

    cfg_len = cfg_info_len[sensor_id];
    
    if (cfg_len < GTP_CONFIG_MIN_LENGTH)
    {
        GTP_ERROR("Sensor_ID(%d) matches with NULL OR INVALID CONFIG GROUP! NO Config Sent! You need to check you header file CFG_GROUP section!", sensor_id);
        return -1;
    }
    
    ret = gtp_i2c_read_dbl_check(client, GTP_REG_CONFIG_DATA, &opr_buf[0], 1);
    
    if (ret == SUCCESS)
    {
        GTP_DEBUG("CFG_GROUP%d Config Version: %d, 0x%02X; IC Config Version: %d, 0x%02X", sensor_id+1, 
                    send_cfg_buf[sensor_id][0], send_cfg_buf[sensor_id][0], opr_buf[0], opr_buf[0]);
        
        if (opr_buf[0] < 90)
        {
            grp_cfg_version = send_cfg_buf[sensor_id][0];       // backup group config version
            send_cfg_buf[sensor_id][0] = 0x00;
            fixed_config = 0;
        }
        else        // treated as fixed config, not send config
        {
            GTP_INFO("Ic fixed config with config version(%d)", opr_buf[0]);
            fixed_config = 1;
        }
    }
    else
    {
        GTP_ERROR("Failed to get ic config version!No config sent!");
        return -1;
    }
    
    memset(&config[GTP_ADDR_LENGTH], 0, GTP_CONFIG_MAX_LENGTH);
    memcpy(&config[GTP_ADDR_LENGTH], send_cfg_buf[sensor_id], cfg_len);

#if GTP_CUSTOM_CFG
    config[RESOLUTION_LOC]     = (u8)GTP_MAX_WIDTH;
    config[RESOLUTION_LOC + 1] = (u8)(GTP_MAX_WIDTH>>8);
    config[RESOLUTION_LOC + 2] = (u8)GTP_MAX_HEIGHT;
    config[RESOLUTION_LOC + 3] = (u8)(GTP_MAX_HEIGHT>>8);
    
    if (GTP_INT_TRIGGER == 0)  //RISING
    {
        config[TRIGGER_LOC] &= 0xfe; 
    }
    else if (GTP_INT_TRIGGER == 1)  //FALLING
    {
        config[TRIGGER_LOC] |= 0x01;
    }
#endif  // GTP_CUSTOM_CFG
    
    check_sum = 0;
    for (i = GTP_ADDR_LENGTH; i < cfg_len; i++)
    {
        check_sum += config[i];
    }
    config[cfg_len] = (~check_sum) + 1;
    
#else // DRIVER NOT SEND CONFIG
    cfg_len = GTP_CONFIG_MAX_LENGTH;
    ret = gtp_i2c_read(client, config, cfg_len + GTP_ADDR_LENGTH);
    if (ret < 0)
    {
        GTP_ERROR("Read Config Failed, Using DEFAULT Resolution & INT Trigger!");
        abs_x_max = GTP_MAX_WIDTH;
        abs_y_max = GTP_MAX_HEIGHT;
        int_type = GTP_INT_TRIGGER;
    }
#endif // GTP_DRIVER_SEND_CFG

    GTP_DEBUG_FUNC();
    if ((abs_x_max == 0) && (abs_y_max == 0))
    {
        abs_x_max = (config[RESOLUTION_LOC + 1] << 8) + config[RESOLUTION_LOC];
        abs_y_max = (config[RESOLUTION_LOC + 3] << 8) + config[RESOLUTION_LOC + 2];
        int_type = (config[TRIGGER_LOC]) & 0x03; 
    }
    ret = gtp_send_cfg(client);
    if (ret < 0)
    {
        GTP_ERROR("Send config error.");
    }
    GTP_DEBUG("X_MAX = %d, Y_MAX = %d, TRIGGER = 0x%02x",
        abs_x_max,abs_y_max,int_type);


    msleep(10);
    return 0;
}

static s8 gtp_i2c_test(struct i2c_client *client)
{

    u8 retry = 0;
    s8 ret = -1;
    u32 hw_info = 0;

    GTP_DEBUG_FUNC();

    while (retry++ < 5)
    {
        ret = i2c_read_bytes(client, GTP_REG_HW_INFO, (u8 *)&hw_info, sizeof(hw_info));

        if ((!ret) && (hw_info == 0x00900600))              //20121212
        {
            return ret;
        }

        GTP_ERROR("GTP_REG_HW_INFO : %08X", hw_info);
        GTP_ERROR("GTP i2c test failed time %d.", retry);
        msleep(10);
    }

    return -1;
}

void gtp_int_sync(s32 ms)
{
    GTP_DEBUG("There is pull up resisitor attached on the INT pin~!");
    GTP_GPIO_OUTPUT(GTP_INT_PORT, 0);
    msleep(ms);
    GTP_GPIO_AS_INT(GTP_INT_PORT);
}

void gtp_reset_guitar(struct i2c_client *client, s32 ms)
{
    GTP_INFO("GTP RESET!\n");
    GTP_GPIO_OUTPUT(GTP_RST_PORT, 0);
    msleep(ms);
    GTP_GPIO_OUTPUT(GTP_INT_PORT, client->addr == 0x14);
    msleep(2);
    GTP_GPIO_OUTPUT(GTP_RST_PORT, 1);
    msleep(6);
    gtp_int_sync(50);

#if GTP_ESD_PROTECT
    gtp_init_ext_watchdog(i2c_client_point);
#endif
}

static int tpd_power_on(struct i2c_client *client)
{
    int ret = 0;
    int reset_count = 0;

reset_proc:
    GTP_GPIO_OUTPUT(GTP_INT_PORT, 0);
    GTP_GPIO_OUTPUT(GTP_RST_PORT, 0);
    msleep(10);
    //power on, need confirm with SA
#ifdef TPD_POWER_SOURCE_CUSTOM
    hwPowerOn(TPD_POWER_SOURCE_CUSTOM, VOL_2800, "TP");
#else
    hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_2800, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
    hwPowerOn(TPD_POWER_SOURCE_1800, VOL_1800, "TP");
#endif    

    gtp_reset_guitar(client, 20);

    ret = gtp_i2c_test(client);

    if (ret < 0)
    {
        GTP_ERROR("I2C communication ERROR!");

        if (reset_count < TPD_MAX_RESET_COUNT)
        {
            reset_count++;
            goto reset_proc;
        }
        else
        {
            goto out; 
        }
    }


out:
    return ret;
}

#ifdef MTK_CTP_RESET_CONFIG
static int tpd_clear_config(void *unused)
{
	int ret=0, check_sum=0;
	u8 temp_data = 0, i=0;
	u8 config_1st[GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH]
		= {GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff};
	
	GTP_INFO("Clear Config Begin......");
	msleep(10000);								//wait main thread to be completed
		
	ret = i2c_read_bytes(i2c_client_point, GTP_REG_CONFIG_DATA, &temp_data, 1);
	if (ret < 0)
	{
		GTP_ERROR("GTP read config failed!");
		return -1;
	}
	
	GTP_INFO("IC config version: 0x%x; Driver config version: 0x%x",temp_data, config[GTP_ADDR_LENGTH]);
	if((temp_data<(u8)0x5A)&&(temp_data>config[GTP_ADDR_LENGTH]))
	{		
		memset(&config_1st[GTP_ADDR_LENGTH], 0, GTP_CONFIG_MAX_LENGTH);
		memcpy(&config_1st[GTP_ADDR_LENGTH], &config[GTP_ADDR_LENGTH], cfg_len);
		config_1st[GTP_ADDR_LENGTH] = 0;
		check_sum = 0;
	
		for (i = GTP_ADDR_LENGTH; i < cfg_len; i++)
		{
			check_sum += config_1st[i];
		}
	
		config_1st[cfg_len] = (~check_sum) + 1;
		ret = gtp_i2c_write(i2c_client_point, config_1st , GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH);
		if (ret < 0)
		{
				GTP_ERROR("GTP write 00 config failed!");
		}else
		{
			GTP_INFO("Force clear cfg done");
		}
	}else
	{
		GTP_INFO("No need clear cfg");
	}
	return 0;
}
#endif

static s32 tpd_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    s32 err = 0;
    s32 ret = 0;
    u16 version_info;

#if 0 //GTP_HAVE_TOUCH_KEY
    s32 idx = 0;
#endif
#ifdef TPD_PROXIMITY
    struct hwmsen_object obj_ps;
#endif

    i2c_client_point = client;
    ret = tpd_power_on(client);

    if (ret < 0)
    {
        GTP_ERROR("I2C communication ERROR!");
        goto out;
    }
	
#ifdef MTK_CTP_RESET_CONFIG
    thread = kthread_run(tpd_clear_config, 0, "mtk-tpd-clear-config");
    if (IS_ERR(thread))
    {
        err = PTR_ERR(thread);
        GTP_INFO(TPD_DEVICE " failed to create kernel thread for clearing config: %d", err);
    }
    thread = NULL;
#endif

#if GTP_AUTO_UPDATE
    ret = gup_init_update_proc(client);

    if (ret < 0)
    {
        GTP_ERROR("Create update thread error.");
        goto out;
    }

#endif

//add by xiaoguang.xiong,2013.07.13, for SYS_DEBUG.
tpswitch_sysfs_init();


#ifdef VELOCITY_CUSTOM
	tpd_v_magnify_x = TPD_VELOCITY_CUSTOM_X;
	tpd_v_magnify_y = TPD_VELOCITY_CUSTOM_Y;

#endif

    ret = gtp_read_version(client, &version_info);

    if (ret < 0)
    {
        GTP_ERROR("Read version failed.");
        goto out;
    }

    ret = gtp_init_panel(client);

    if (ret < 0)
    {
        GTP_ERROR("GTP init panel failed.");
        goto out;
    }
    // Create proc file system
    gt91xx_config_proc = create_proc_entry(GT91XX_CONFIG_PROC_FILE, 0664, NULL);

    if (gt91xx_config_proc == NULL)
    {
        GTP_ERROR("create_proc_entry %s failed", GT91XX_CONFIG_PROC_FILE);
        goto out;
    }
    else
    {
        gt91xx_config_proc->read_proc = gt91xx_config_read_proc;
        gt91xx_config_proc->write_proc = gt91xx_config_write_proc;
    }

#if GTP_CREATE_WR_NODE
    init_wr_node(client);
#endif

    thread = kthread_run(touch_event_handler, 0, TPD_DEVICE);

    if (IS_ERR(thread))
    {
        err = PTR_ERR(thread);
        GTP_INFO(TPD_DEVICE " failed to create kernel thread: %d", err);
        goto out;
    }

#if 0//GTP_HAVE_TOUCH_KEY

    for (idx = 0; idx < TPD_KEY_COUNT; idx++)
    {
        input_set_capability(tpd->dev, EV_KEY, touch_key_array[idx]);
    }

#endif
#if GTP_SLIDE_WAKEUP
    input_set_capability(tpd->dev, EV_KEY, KEY_POWER);
#endif
    /*
    #ifndef TPD_RESET_ISSUE_WORKAROUND
        mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);
        msleep(10);
    #endif
    */
#if GTP_WITH_PEN
    // pen support
    __set_bit(BTN_TOOL_PEN, tpd->dev->keybit);
    __set_bit(INPUT_PROP_DIRECT, tpd->dev->propbit);
    __set_bit(INPUT_PROP_POINTER, tpd->dev->propbit);
#endif
    // set INT mode
    //mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
    //mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
    //mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_DISABLE);
    //mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_ENABLE);	
    //mt_set_gpio_pull_select(GPIO_CTP_EINT_PIN, GPIO_PULL_UP);

    msleep(50);

    //mt_eint_set_sens(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_SENSITIVE);
    //mt_eint_set_hw_debounce(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_DEBOUNCE_DISABLE);	//no need to set, default is disable.

    if (!int_type)	//EINTF_TRIGGER
    {
        mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, EINTF_TRIGGER_RISING, tpd_eint_interrupt_handler, 1);
    }
    else
    {
        mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, EINTF_TRIGGER_FALLING, tpd_eint_interrupt_handler, 1);
    }

    mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
    /*
    #ifndef TPD_RESET_ISSUE_WORKAROUND
        mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
    #endif
    */

#ifdef TPD_PROXIMITY
    //obj_ps.self = cm3623_obj;
    obj_ps.polling = 0;         //0--interrupt mode;1--polling mode;
    obj_ps.sensor_operate = tpd_ps_operate;
    hwmsen_detach(ID_PROXIMITY);
    if ((err = hwmsen_attach(ID_PROXIMITY, &obj_ps)))
    {
        GTP_ERROR("hwmsen attach fail, return:%d.", err);
    }

#endif

#if GTP_ESD_PROTECT
    gtp_esd_switch(client, SWITCH_ON);
#endif


    tpd_load_status = 1;

    return 0;
out:
    return -1;
}

static void tpd_eint_interrupt_handler(void)
{
    TPD_DEBUG_PRINT_INT;
    tpd_flag = 1;
    wake_up_interruptible(&waiter);
}
static int tpd_i2c_remove(struct i2c_client *client)
{
#if GTP_CREATE_WR_NODE
    uninit_wr_node();
#endif

#if GTP_ESD_PROTECT
    destroy_workqueue(gtp_esd_check_workqueue);
#endif

    return 0;
}


#if GTP_ESD_PROTECT
static s32 gtp_init_ext_watchdog(struct i2c_client *client)
{
    u8 opr_buffer[4] = {0xAA, 0xAA};

    return i2c_write_bytes(client, 0x8040, opr_buffer, 2);

}

void gtp_esd_switch(struct i2c_client *client, s32 on)
{
    if (SWITCH_ON == on)     // switch on esd 
    {
        if (!esd_running)
        {
            esd_running = 1;
            GTP_INFO("Esd started");
            queue_delayed_work(gtp_esd_check_workqueue, &gtp_esd_check_work, TPD_ESD_CHECK_CIRCLE);
        }
    }
    else    // switch off esd
    {
        if (esd_running)
        {
            esd_running = 0;
            GTP_INFO("Esd cancelled");
            cancel_delayed_work_sync(&gtp_esd_check_work);
        }
    }
}
static void force_reset_guitar(void)
{
    s32 i;
    s32 ret;

    GTP_INFO("force_reset_guitar");

    //Power off TP
#ifdef TPD_POWER_SOURCE_CUSTOM
	hwPowerDown(TPD_POWER_SOURCE_CUSTOM, "TP");
#else
	hwPowerDown(MT65XX_POWER_LDO_VGP2, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerDown(TPD_POWER_SOURCE_1800, "TP");
#endif
	msleep(30);
	//Power on TP
#ifdef TPD_POWER_SOURCE_CUSTOM
	hwPowerOn(TPD_POWER_SOURCE_CUSTOM, VOL_2800, "TP");
#else
	hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_2800, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerOn(TPD_POWER_SOURCE_1800, VOL_1800, "TP");
#endif
    msleep(30);
    for (i = 0; i < 5; i++)
    {
        //Reset Guitar
        gtp_reset_guitar(i2c_client_point, 20);

        //Send config
        ret = gtp_send_cfg(i2c_client_point);

        if (ret < 0)
        {
            continue;
        }

        break;
    }

}

static void gtp_esd_check_func(struct work_struct *work)
{
    int i;
    int ret = -1;
    u8 test[4] = {0x80, 0x40};

    if (tpd_halt)
    {
        return;
    }

    for (i = 0; i < 3; i++)
    {
        ret = gtp_i2c_read(i2c_client_point, test, 4);

        GTP_DEBUG("0x8040 = 0x%02X, 0x8041 = 0x%02X", test[2], test[3]);
        if (ret < 0)
        {
            // IIC communication problem
            continue;
        }
        else 
        {
            if ((test[2] == 0xAA) || (test[3] != 0xAA))
            {
                // IC works abnormally...
                i = 3;          // jump to reset guitar
                break;
            }
            else
            {
                // IC works normally, Write 0x8040 0xAA, feed the watchdog
                test[2] = 0xAA; 
                gtp_i2c_write(i2c_client_point, test, 3);
                break;
            }
        }
    }

    if (i >= 3)
    {
        force_reset_guitar();
    }

    if (!tpd_halt)
    {
        queue_delayed_work(gtp_esd_check_workqueue, &gtp_esd_check_work, TPD_ESD_CHECK_CIRCLE);
    }

    return;
}
#endif

static void tpd_down(s32 x, s32 y, s32 size, s32 id)
{
    if ((!size) && (!id))
    {
        input_report_abs(tpd->dev, ABS_MT_PRESSURE, 100);
        input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 100);
    }
    else
    {
        input_report_abs(tpd->dev, ABS_MT_PRESSURE, size);
        input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, size);
        /* track id Start 0 */
        input_report_abs(tpd->dev, ABS_MT_TRACKING_ID, id);
    }

    input_report_key(tpd->dev, BTN_TOUCH, 1);
    input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
    input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
    input_mt_sync(tpd->dev);
    TPD_EM_PRINT(x, y, x, y, id, 1);

    MMProfileLogEx(MMP_TouchPanelEvent, MMProfileFlagPulse, 1, x+y);
#ifdef TPD_HAVE_BUTTON

    if (FACTORY_BOOT == get_boot_mode() || RECOVERY_BOOT == get_boot_mode())
    {
        tpd_button(x, y, 1);
    }

#endif
}

static void tpd_up(s32 x, s32 y, s32 id)
{
    //input_report_abs(tpd->dev, ABS_MT_PRESSURE, 0);
    input_report_key(tpd->dev, BTN_TOUCH, 0);
    //input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 0);
    input_mt_sync(tpd->dev);
    TPD_EM_PRINT(x, y, x, y, id, 0);
    MMProfileLogEx(MMP_TouchPanelEvent, MMProfileFlagPulse, 0, x+y);

#ifdef TPD_HAVE_BUTTON

    if (FACTORY_BOOT == get_boot_mode() || RECOVERY_BOOT == get_boot_mode())
    {
        tpd_button(x, y, 0);
    }

#endif
}
#if GTP_CHARGER_SWITCH
static void gtp_charger_switch(void)
{
    u32 chr_status = 0;
    u8 chr_cmd[3] = {0x80, 0x40};
    static u8 chr_pluggedin = 0;
    int ret = 0;
    
#ifdef MT6573
    chr_status = *(volatile u32 *)CHR_CON0;
    chr_status &= (1 << 13);
#else   // ( defined(MT6575) || defined(MT6577) || defined(MT6589) )
    chr_status = upmu_is_chr_det();
#endif
    
    if (chr_status)     // charger plugged in
    {
        if (!chr_pluggedin)
        {
            chr_cmd[2] = 6;
            ret = gtp_i2c_write(i2c_client_point, chr_cmd, 3);
            if (ret > 0)
            {
                GTP_INFO("Update status for Charger Plugin");
            }
            chr_pluggedin = 1;
        }
    }
    else            // charger plugged out
    {
        if (chr_pluggedin)
        {
            chr_cmd[2] = 7;
            ret = gtp_i2c_write(i2c_client_point, chr_cmd, 3);
            if (ret > 0)
            {
                GTP_INFO("Update status for Charger Plugout");
            }
            chr_pluggedin = 0;
        }
    }
}
#endif
static int touch_event_handler(void *unused)
{
    struct sched_param param = { .sched_priority = RTPM_PRIO_TPD };
    u8  end_cmd[3] = {GTP_READ_COOR_ADDR >> 8, GTP_READ_COOR_ADDR & 0xFF, 0};
    u8  point_data[2 + 1 + 8 * GTP_MAX_TOUCH + 1] = {GTP_READ_COOR_ADDR >> 8, GTP_READ_COOR_ADDR & 0xFF};
    u8  touch_num = 0;
    u8  finger = 0;
    static u8 pre_touch = 0;
    static u8 pre_key = 0;
#if GTP_WITH_PEN
    static u8 pre_pen = 0;
#endif
    u8  key_value = 0;
    u8 *coor_data = NULL;
    s32 input_x = 0;
    s32 input_y = 0;
    s32 input_w = 0;
    s32 id = 0;
    s32 i  = 0;
    s32 ret = -1;
#ifdef TPD_PROXIMITY
    s32 err = 0;
    hwm_sensor_data sensor_data;
    u8 proximity_status;
#endif
#if GTP_CHANGE_X2Y
    s32 temp;
#endif
#if GTP_SLIDE_WAKEUP
    u8 doze_buf[3] = {0x81, 0x4B};
#endif
    sched_setscheduler(current, SCHED_RR, &param);

    do
    {
        set_current_state(TASK_INTERRUPTIBLE);

        while (tpd_halt)
        {
            tpd_flag = 0;
            msleep(20);
        }

        wait_event_interruptible(waiter, tpd_flag != 0);
        tpd_flag = 0;
        TPD_DEBUG_SET_TIME;
        set_current_state(TASK_RUNNING);

    	mutex_lock(&i2c_access);

    #if GTP_CHARGER_SWITCH
        gtp_charger_switch();
    #endif

    #if GTP_SLIDE_WAKEUP
        if (DOZE_ENABLED == doze_status)
        {
            ret = gtp_i2c_read(i2c_client_point, doze_buf, 3);
            GTP_DEBUG("0x814B = 0x%02X", doze_buf[2]);
            if (ret > 0)
            {               
                if (0xAA == doze_buf[2])
                {
                    GTP_INFO("forward slide to light up the screen!");
                    doze_status = DOZE_WAKEUP;
                    input_report_key(tpd->dev, KEY_POWER, 1);
                    input_sync(tpd->dev);
                    input_report_key(tpd->dev, KEY_POWER, 0);
                    input_sync(tpd->dev);
                    // clear 0x814B
                    doze_buf[2] = 0x00;
                    gtp_i2c_write(i2c_client_point, doze_buf, 3);
                }
                else if (0xBB == doze_buf[2])
                {
                    GTP_INFO("Backward slide to light up the screen!");
                    doze_status = DOZE_WAKEUP;
                    input_report_key(tpd->dev, KEY_POWER, 1);
                    input_sync(tpd->dev);
                    input_report_key(tpd->dev, KEY_POWER, 0);
                    input_sync(tpd->dev);
                    // clear 0x814B
                    doze_buf[2] = 0x00;
                    gtp_i2c_write(i2c_client_point, doze_buf, 3);
                    
                }
                else if (0xC0 == (doze_buf[2] & 0xC0)) // double click wakeup
                {
                    GTP_INFO("double click to light up the screen!");
                    doze_status = DOZE_WAKEUP;
                    input_report_key(tpd->dev, KEY_POWER, 1);
                    input_sync(tpd->dev);
                    input_report_key(tpd->dev, KEY_POWER, 0);
                    input_sync(tpd->dev);
                    // clear 0x814B
                    doze_buf[2] = 0x00;
                    gtp_i2c_write(i2c_client_point, doze_buf, 3);
                }
                else
                {
                    gtp_enter_doze(i2c_client_point);
                }
            }
            continue;
        }
    #endif

        ret = gtp_i2c_read(i2c_client_point, point_data, 12);

        if (ret < 0)
        {
            GTP_ERROR("I2C transfer error. errno:%d ", ret);
            goto exit_work_func;
        }

        finger = point_data[GTP_ADDR_LENGTH];

        if ((finger & 0x80) == 0)
        {
            goto exit_work_func;
        }

#ifdef TPD_PROXIMITY
#if 1
 
            proximity_status = point_data[GTP_ADDR_LENGTH];
            GTP_DEBUG("REG INDEX[0x814E]:0x%02X\n", proximity_status);

            if (proximity_status & 0x60)                //proximity or large touch detect,enable hwm_sensor.
            {
                tpd_proximity_detect = 0;
                //sensor_data.values[0] = 0;
            }
            else
            {
                tpd_proximity_detect = 1;
                //sensor_data.values[0] = 1;
            }

            //get raw data
            GTP_DEBUG(" ps change\n");
            GTP_DEBUG("PROXIMITY STATUS:0x%02X\n", tpd_proximity_detect);
            //map and store data to hwm_sensor_data
            sensor_data.values[0] = tpd_get_ps_value();
            sensor_data.value_divide = 1;
            sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
            //report to the up-layer
            ret = hwmsen_get_interrupt_data(ID_PROXIMITY, &sensor_data);

            if (ret)
            {
                GTP_ERROR("Call hwmsen_get_interrupt_data fail = %d", err);
            }
	    if (!tpd_proximity_detect)
	    {
	        goto exit_work_func;
            }
#else
        if (tpd_proximity_flag == 1)
        {
            proximity_status = point_data[GTP_ADDR_LENGTH];
            GTP_DEBUG("REG INDEX[0x814E]:0x%02X\n", proximity_status);

            if (proximity_status & 0x60)                //proximity or large touch detect,enable hwm_sensor.
            {
                tpd_proximity_detect = 0;
                //sensor_data.values[0] = 0;
            }
            else
            {
                tpd_proximity_detect = 1;
                //sensor_data.values[0] = 1;
            }

            //get raw data
            GTP_DEBUG(" ps change\n");
            GTP_DEBUG("PROXIMITY STATUS:0x%02X\n", tpd_proximity_detect);
            //map and store data to hwm_sensor_data
            sensor_data.values[0] = tpd_get_ps_value();
            sensor_data.value_divide = 1;
            sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
            //report to the up-layer
            ret = hwmsen_get_interrupt_data(ID_PROXIMITY, &sensor_data);

            if (ret)
            {
                GTP_ERROR("Call hwmsen_get_interrupt_data fail = %d", err);
            }

	    if (!tpd_proximity_detect)
	    {
	        goto exit_work_func;
            }
        }
#endif
#endif

        touch_num = finger & 0x0f;

        if (touch_num > GTP_MAX_TOUCH)
        {
            goto exit_work_func;
        }

        if (touch_num > 1)
        {
            u8 buf[8 * GTP_MAX_TOUCH] = {(GTP_READ_COOR_ADDR + 10) >> 8, (GTP_READ_COOR_ADDR + 10) & 0xff};

            ret = gtp_i2c_read(i2c_client_point, buf, 2 + 8 * (touch_num - 1));
            memcpy(&point_data[12], &buf[2], 8 * (touch_num - 1));
        }	
#if GTP_HAVE_TOUCH_KEY
        key_value = point_data[3 + 8 * touch_num];

        if (key_value || pre_key)
        {
            for (i = 0; i < TPD_KEY_COUNT; i++)
            {
                //input_report_key(tpd->dev, touch_key_array[i], key_value & (0x01 << i));
		if( key_value&(0x01<<i) ) //key=1 menu ;key=2 home; key =4 back;
		{
			input_x =touch_key_point_maping_array[i].point_x;
			input_y = touch_key_point_maping_array[i].point_y;
			GTP_DEBUG("button =%d %d",input_x,input_y);
				   
			tpd_down( input_x, input_y, 0, 0);
		}
            }
			
	    if((pre_key!=0)&&(key_value ==0))
	    {
		tpd_up( 0, 0, 0);
	    }

            touch_num = 0;
            pre_touch = 0;
        }

#endif
        pre_key = key_value;

        GTP_DEBUG("pre_touch:%02x, finger:%02x.", pre_touch, finger);

        if (touch_num)
        {
            for (i = 0; i < touch_num; i++)
            {
                coor_data = &point_data[i * 8 + 3];

                id = coor_data[0];      
                input_x  = coor_data[1] | coor_data[2] << 8;
                input_y  = coor_data[3] | coor_data[4] << 8;
                input_w  = coor_data[5] | coor_data[6] << 8;

                input_x = TPD_WARP_X(abs_x_max, input_x);
                input_y = TPD_WARP_Y(abs_y_max, input_y);
                
#if GTP_CHANGE_X2Y
                temp  = input_x;
                input_x = input_y;
                input_y = temp;
#endif
            #if GTP_WITH_PEN
                if (id == 128)      // pen/stylus is activated
                {
                    GTP_DEBUG("Pen touch DOWN!");
                    input_report_key(tpd->dev, BTN_TOOL_PEN, 1);
                    pre_pen = 1;
                    id = 0;
                }
            #endif
                tpd_down(input_x, input_y, input_w, id);
            }
        }
        else
        {
            if (pre_touch)
            {
            #if GTP_WITH_PEN
                if (pre_pen)
                {   
                    GTP_DEBUG("Pen touch UP!");
                    input_report_key(tpd->dev, BTN_TOOL_PEN, 0);
                    pre_pen = 0;
                }
            #endif
                GTP_DEBUG("Touch Release!");
                tpd_up(0, 0, 0);
            }
        }

        pre_touch = touch_num;
        //input_report_key(tpd->dev, BTN_TOUCH, (touch_num || key_value));

        if (tpd != NULL && tpd->dev != NULL)
        {
            input_sync(tpd->dev);
        }

exit_work_func:

        if (!gtp_rawdiff_mode)
        {
            ret = gtp_i2c_write(i2c_client_point, end_cmd, 3);

            if (ret < 0)
            {
                GTP_INFO("I2C write end_cmd  error!");
            }
        }
    	mutex_unlock(&i2c_access);

    }
    while (!kthread_should_stop());

    return 0;
}

static int tpd_local_init(void)
{
#if GTP_ESD_PROTECT
    INIT_DELAYED_WORK(&gtp_esd_check_work, gtp_esd_check_func);
    gtp_esd_check_workqueue = create_workqueue("gtp_esd_check");
#endif
    if (i2c_add_driver(&tpd_i2c_driver) != 0)
    {
        GTP_INFO("unable to add i2c driver.");
        return -1;
    }

    if (tpd_load_status == 0) //if(tpd_load_status == 0) // disable auto load touch driver for linux3.0 porting
    {
        GTP_INFO("add error touch panel driver.");
        i2c_del_driver(&tpd_i2c_driver);
        return -1;
    }

#ifdef TPD_HAVE_BUTTON
    tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local);// initialize tpd button data
#endif

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
    TPD_DO_WARP = 1;
    memcpy(tpd_wb_start, tpd_wb_start_local, TPD_WARP_CNT * 4);
    memcpy(tpd_wb_end, tpd_wb_start_local, TPD_WARP_CNT * 4);
#endif

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
    memcpy(tpd_calmat, tpd_def_calmat_local, 8 * 4);
    memcpy(tpd_def_calmat, tpd_def_calmat_local, 8 * 4);
#endif

    // set vendor string
    tpd->dev->id.vendor = 0x00;
    tpd->dev->id.product = tpd_info.pid;
    tpd->dev->id.version = tpd_info.vid;

    GTP_INFO("end %s, %d", __FUNCTION__, __LINE__);
    tpd_type_cap = 1;

    return 0;
}

#if GTP_SLIDE_WAKEUP
static s8 gtp_enter_doze(struct i2c_client *client)
{
    s8 ret = -1;
    s8 retry = 0;
    u8 i2c_control_buf[3] = {(u8)(GTP_REG_SLEEP >> 8), (u8)GTP_REG_SLEEP, 8};

    GTP_DEBUG_FUNC();
#if GTP_DBL_CLK_WAKEUP
    i2c_control_buf[2] = 0x09;
#endif

    GTP_DEBUG("entering doze mode...");
    while(retry++ < 5)
    {
        i2c_control_buf[0] = 0x80;
        i2c_control_buf[1] = 0x46;
        ret = gtp_i2c_write(client, i2c_control_buf, 3);
        if (ret < 0)
        {
            GTP_DEBUG("failed to set doze flag into 0x8046, %d", retry);
            continue;
        }
        i2c_control_buf[0] = 0x80;
        i2c_control_buf[1] = 0x40;
        ret = gtp_i2c_write(client, i2c_control_buf, 3);
        if (ret > 0)
        {
            doze_status = DOZE_ENABLED;
            GTP_DEBUG("GTP has been working in doze mode!");
            return ret;
        }
        msleep(10);
    }
    GTP_ERROR("GTP send doze cmd failed.");
    return ret;
}

#else
static s8 gtp_enter_sleep(struct i2c_client *client)
{
    s8 ret = -1;
#if 1 //GTP_POWER_CTRL_SLEEP
    s8 retry = 0;
    u8 i2c_control_buf[3] = {(u8)(GTP_REG_SLEEP >> 8), (u8)GTP_REG_SLEEP, 5};
	
    GTP_GPIO_OUTPUT(GTP_INT_PORT, 0);
    msleep(5);

    while (retry++ < 5)
    {
        ret = gtp_i2c_write(client, i2c_control_buf, 3);

        if (ret > 0)
        {
            GTP_INFO("GTP enter sleep!");
            return ret;
        }

        msleep(10);
    }

#else

    GTP_GPIO_OUTPUT(GTP_RST_PORT, 0);
    msleep(5);
	
#ifdef TPD_POWER_SOURCE_CUSTOM
	hwPowerDown(TPD_POWER_SOURCE_CUSTOM, "TP");
#else
	hwPowerDown(MT65XX_POWER_LDO_VGP2, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerDown(TPD_POWER_SOURCE_1800, "TP");
#endif

    GTP_INFO("GTP enter sleep!");
    return 0;
	
#endif
    GTP_ERROR("GTP send sleep cmd failed.");
    return ret;
}
#endif
static s8 gtp_wakeup_sleep(struct i2c_client *client)
{
    u8 retry = 0;
    s8 ret = -1;


    GTP_INFO("GTP wakeup begin.");
#if 0//GTP_POWER_CTRL_SLEEP

    while (retry++ < 5)
    {
        ret = tpd_power_on(client);

        if (ret < 0)
        {
            GTP_ERROR("I2C Power on ERROR!");
        }

        ret = gtp_send_cfg(client);

        if (ret > 0)
        {
            GTP_DEBUG("Wakeup sleep send config success.");
            return ret;
        }
    }

#else

    while (retry++ < 10)
    {
        GTP_GPIO_OUTPUT(GTP_INT_PORT, 1);
        msleep(5);
        GTP_GPIO_OUTPUT(GTP_INT_PORT, 0);
        msleep(5);
        ret = gtp_i2c_test(client);

        if (ret >= 0)
        {
            gtp_int_sync(50);
            return ret;
        }

        gtp_reset_guitar(client, 20);
    }

#endif

    GTP_ERROR("GTP wakeup sleep failed.");
    return ret;
}
/* Function to manage low power suspend */
static void tpd_suspend(struct early_suspend *h)
{
    s32 ret = -1;
    

   // mutex_lock(&i2c_access);

#if GTP_ESD_PROTECT
    gtp_esd_switch(i2c_client_point, SWITCH_OFF);
#endif


#ifdef TPD_PROXIMITY
#if 1
	    if (!tpd_proximity_detect)
	    {
	        return;
            }
#else
    if (tpd_proximity_flag == 1)
    {
        return ;
    }
#endif
#endif
	tpd_halt = 1;
    mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
    ret = gtp_enter_sleep(i2c_client_point);
   // mutex_unlock(&i2c_access);

    if (ret < 0)
    {
        GTP_ERROR("GTP early suspend failed.");
    }

   
    msleep(58);
}

/* Function to manage power-on resume */
static void tpd_resume(struct early_suspend *h)
{
    s32 ret = -1;
#ifdef TPD_PROXIMITY
#if 1
	    if (!tpd_proximity_detect)
	    {
	       return;
            }
#else
    if (tpd_proximity_flag == 1)
    {
        return ;
    }
#endif
#endif

    ret = gtp_wakeup_sleep(i2c_client_point);

    if (ret < 0)
    {
        GTP_ERROR("GTP later resume failed.");
    }	
	
    GTP_INFO("GTP wakeup sleep.");
#if GTP_CHARGER_SWITCH
    gtp_charger_switch();
#endif
    tpd_halt = 0;
    mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);


#if GTP_ESD_PROTECT
    gtp_esd_switch(i2c_client_point, SWITCH_ON);
#endif

}


static struct tpd_driver_t tpd_device_driver =
{
    .tpd_device_name = "gt9xx",
    .tpd_local_init = tpd_local_init,
    .suspend = tpd_suspend,
    .resume = tpd_resume,
#ifdef TPD_HAVE_BUTTON
    .tpd_have_button = 1,
#else
    .tpd_have_button = 0,
#endif
	/*
	.attrs = {
		.attr = gt9xx_attrs, 
		.num  = ARRAY_SIZE(gt9xx_attrs),
	},
	*/
};

/* called when loaded into kernel */
static int __init tpd_driver_init(void)
{
    GTP_INFO("MediaTek gt91xx touch panel driver init");
#if defined(TPD_I2C_NUMBER)	
    i2c_register_board_info(TPD_I2C_NUMBER, &i2c_tpd, 1);
#else
    i2c_register_board_info(0, &i2c_tpd, 1);
#endif
    if (tpd_driver_add(&tpd_device_driver) < 0)
        GTP_INFO("add generic driver failed");

    //add by xiaoguang.xiong,2013.11.27,for open/short test and rawdata show. CR 562473.
    gtp_test_sysfs_init();

    ctp_sysfs_init();

    return 0;
}

/* should never be called */
static void __exit tpd_driver_exit(void)
{
    GTP_INFO("MediaTek gt91xx touch panel driver exit");
    //input_unregister_device(tpd->dev);
    tpd_driver_remove(&tpd_device_driver);

    //add by xiaoguang.xiong,2013.11.27,for open/short test and rawdata show. CR 562473.
    gtp_test_sysfs_deinit();

    ctp_sysfs_deinit();
}

module_init(tpd_driver_init);
module_exit(tpd_driver_exit);

