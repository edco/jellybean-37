
#ifndef __CAMERA_EFFECT_H_
#define __CAMERA_EFFECT_H_

typedef enum {
    EM_FLIP_LR,//左右对称
    EM_QUADRATE,//正方形
    EM_QUADRATE_UNIFROM,
    EM_VERTICAL_STRIPE,
    EM_DOWN_SAMP,
    EM_REFLECTION_P,
    EM_FOUR_CORNER,
    EM_ROTATE_180,
    EM_END,
} effect_mode_t;


typedef enum {
    DST_AS_DST,
    SRC_AS_DST,
} effect_dst_indicator_t;


typedef enum {
    RESULOTION_320,
    RESULOTION_720,
    RESULOTION_272,
    RESULOTION_600,
} resulotion_t;


typedef struct {
    int w;
    int h;
    int wh_mode;
    int mode_full;
    u8 *buffer;
} effect_t;


struct camera_effect_config {
    u8 enable;

    effect_t eff_t;
    effect_mode_t mode;
    resulotion_t resulotion;
    effect_dst_indicator_t indicator;
    u8 *dst_buf;
    void *fb;
};


int effect_init(effect_t *e, int wh_mode, int width, int height, effect_mode_t *indicator);

void process_effect(effect_t *e, unsigned char *src, unsigned char *dst, effect_mode_t *mode, effect_dst_indicator_t *indicator);

int effect_free(effect_t *e);



void camera_effect_init(struct camera_effect_config *cam_eff_cfg);

void camera_effect_start(struct camera_effect_config *cam_eff_cfg);

void camera_effect_stop(struct camera_effect_config *cam_eff_cfg);

void camera_effect_switch_mode(struct camera_effect_config *cam_eff_cfg, int mode);

void camera_effect_uninit(struct camera_effect_config *cam_eff_cfg);

#endif










