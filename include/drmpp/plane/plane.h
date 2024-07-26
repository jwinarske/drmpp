#ifndef INCLUDE_DRMPP_PLANE_PLANE_H_
#define INCLUDE_DRMPP_PLANE_PLANE_H_

#include "drmpp.h"

namespace drmpp::plane {
    class Common {
    public:
        struct dumb_fb {
            uint32_t format;
            uint32_t width, height, stride, size;
            uint32_t handle;
            uint32_t id;
        };

        static drmModeConnector *pick_connector(int drm_fd, const drmModeRes *drm_res);

        static drmModeCrtc *pick_crtc(int drm_fd, const drmModeRes *drm_res, const drmModeConnector *connector);

        static void disable_all_crtcs_except(int drm_fd, const drmModeRes *drm_res, uint32_t crtc_id);

        static bool dumb_fb_init(Common::dumb_fb *fb, int drm_fd, uint32_t format,
                                 uint32_t width, uint32_t height);

        static void *dumb_fb_map(Common::dumb_fb const *fb, int drm_fd);

        static void dumb_fb_fill(Common::dumb_fb const *fb, int drm_fd, uint32_t color);
    };
}
#endif // INCLUDE_DRMPP_PLANE_PLANE_H_
