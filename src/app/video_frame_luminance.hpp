#pragma once

#include "qr_adapter.hpp"

#include <QVideoFrame>

namespace aqrt::app {

[[nodiscard]] qr::QrRasterImage video_frame_to_luminance(QVideoFrame frame);
[[nodiscard]] qr::RgbImage video_frame_to_rgb(QVideoFrame frame);

} // namespace aqrt::app
