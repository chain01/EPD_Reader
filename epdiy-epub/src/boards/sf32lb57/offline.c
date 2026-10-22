/* Offline reader build: no Bluetooth/network worker or periodic network timer. */
#include "pan.h"
#include "wheather.h"
#include <string.h>
static weather_snapshot_t snapshot={.last_result=-RT_ENOSYS,.status="网络未启用"};
static char city[48]="nanjing";
rt_err_t pan_service_init(const char *name,rt_bool_t weather){(void)name;(void)weather;return -RT_ENOSYS;}
rt_err_t pan_service_request_connect(void){return -RT_ENOSYS;}
rt_err_t pan_service_request_weather(void){return -RT_ENOSYS;}
rt_err_t pan_service_set_enabled(rt_bool_t enabled){return enabled?-RT_ENOSYS:RT_EOK;}
rt_bool_t pan_service_is_ready(void){return RT_FALSE;}
rt_bool_t pan_service_is_bt_connected(void){return RT_FALSE;}
rt_bool_t pan_service_is_pan_connected(void){return RT_FALSE;}
const weather_snapshot_t *weather_get_snapshot(void){return &snapshot;}
const char *weather_get_city(void){return city;}
void weather_copy_string(char *dst,rt_size_t size,const char *src)
{if(dst&&size){rt_strncpy(dst,src?src:"",size-1);dst[size-1]=0;}}
rt_err_t weather_set_city(const char *value){if(!value||!*value)return -RT_EINVAL;weather_copy_string(city,sizeof(city),value);return RT_EOK;}
void weather_set_status(const char *text){weather_copy_string(snapshot.status,sizeof(snapshot.status),text);}
int weather_check_internet_access(void){return 0;}
int weather_refresh(void){return -RT_ENOSYS;}
