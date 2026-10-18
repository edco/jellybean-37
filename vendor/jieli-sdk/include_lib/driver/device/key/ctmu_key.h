#ifndef DEVICE_CTMU_KEY_H
#define DEVICE_CTMU_KEY_H

#include "generic/typedef.h"
#include "touch_key.h"

/* =========== ctmu API ============= */
//ctmu 初始化
int ctmu_touch_key_init(const struct touch_key_platform_data *ctmu_touch_key_data);

//获取plcnt按键状态
u8 ctmu_touch_key_get_value(void);

#endif

